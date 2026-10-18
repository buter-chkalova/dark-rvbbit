// VM-exit dispatch. APIC events use the virtualized path where
// supported, or the timer fallback otherwise.

#include "hv.h"

static VOID HvAdvanceRip(ULONG bytes) {
    ULONG64 rip = 0;
    __vmx_vmread(VMCS_GUEST_RIP, &rip);
    rip += bytes;
    __vmx_vmwrite(VMCS_GUEST_RIP, rip);
}

static VOID HvInjectException(ULONG vector, BOOLEAN hasErr, ULONG errCode) {
    ULONG64 info = 0x80000000ULL | (3ULL << 8) | (vector & 0xFF);
    if (hasErr) info |= (1ULL << 11);
    __vmx_vmwrite(VMCS_CTRL_VM_ENTRY_INTR_INFO, info);
    if (hasErr) __vmx_vmwrite(VMCS_CTRL_VM_ENTRY_EXC_ERRCODE, errCode);
}

VOID HvHandleCpuid(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx) {
    UNREFERENCED_PARAMETER(ctx);
    int out[4];
    __cpuidex(out, (int)(ULONG)regs->Rax, (int)(ULONG)regs->Rcx);

    if ((ULONG)regs->Rax == 0x00000001) {
        out[2] &= ~CPUID_HYPERVISOR_BIT;
        out[2] &= ~CPUID_VMX_BIT;
    }
    if ((ULONG)regs->Rax == 0x40000000) {
        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
    }

    regs->Rax = (ULONG64)(ULONG)out[0];
    regs->Rbx = (ULONG64)(ULONG)out[1];
    regs->Rcx = (ULONG64)(ULONG)out[2];
    regs->Rdx = (ULONG64)(ULONG)out[3];
}

VOID HvHandleMsrRead(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx) {
    ULONG msr = (ULONG)regs->Rcx;
    ULONG64 value = 0;

    // With APICv enabled, these timer reads normally use the virtual APIC state.
    if (!ctx->Caps.HasApicv &&
        (msr == IA32_X2APIC_LVT_TIMER    ||
         msr == IA32_X2APIC_TIMER_INITIAL ||
         msr == IA32_X2APIC_TIMER_CURRENT ||
         msr == IA32_X2APIC_TIMER_DIVIDE)) {
        HvApicTimerReadMsr(ctx, msr, &value);
    } else if (!HvMsrFakeRead(msr, &value)) {
        value = __readmsr(msr);
    }

    regs->Rax = (ULONG)(value & 0xFFFFFFFF);
    regs->Rdx = (ULONG)(value >> 32);
}

VOID HvHandleMsrWrite(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx) {
    ULONG msr = (ULONG)regs->Rcx;
    ULONG64 value = (regs->Rdx << 32) | (regs->Rax & 0xFFFFFFFF);

    if (!ctx->Caps.HasApicv &&
        (msr == IA32_X2APIC_LVT_TIMER    ||
         msr == IA32_X2APIC_TIMER_INITIAL ||
         msr == IA32_X2APIC_TIMER_CURRENT ||
         msr == IA32_X2APIC_TIMER_DIVIDE)) {
        HvApicTimerWriteMsr(ctx, msr, value);
        return;
    }

    // swallow feature_control write so guest can't disable vmx
    if (msr == IA32_FEATURE_CONTROL) return;
    __writemsr(msr, value);
}

// vmfunc covert channel. guest executes vmfunc with eax=opcode.
// vmfunc is illegal outside vmcs, so it #ud's. we catch via exception
// bitmap and check exit_qualification bit 0 to distinguish from
// ordinary #ud. then dispatch on eax.
VOID HvHandleVmfuncUd(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual) {
    if (!(qual & UD_QUAL_VMFUNC)) {
        HvInjectException(EXCEPTION_UD, FALSE, 0);
        return;
    }

    ULONG op = (ULONG)regs->Rax;
    ULONG64 a1 = regs->Rcx, a2 = regs->Rdx;

    switch (op) {
    case HV_OP_PEEK_HIDDEN: {
        PVOID mapped = MmMapIoSpace((PHYSICAL_ADDRESS){ .QuadPart = a1 & ~0xFFFULL },
                                    PAGE_SIZE, MmNonCached);
        if (mapped) {
            regs->Rax = *(ULONG64*)((PUCHAR)mapped + (a1 & 0xFFF));
            MmUnmapIoSpace(mapped, PAGE_SIZE);
        } else regs->Rax = 0;
        HvAdvanceRip(3);    // vmfunc = 0F 01 D4 = 3 bytes
        return;
    }
    case HV_OP_UNHIDE_RANGE:
        HvEptUnhideRange(ctx, a1, a2);
        regs->Rax = 0;
        HvAdvanceRip(3);
        return;
    case HV_OP_HIDE_RANGE:
        HvEptHideRange(ctx, a1, a2);
        regs->Rax = 0;
        HvAdvanceRip(3);
        return;
    case HV_OP_SET_TSC_FOG:
        ctx->TscFogStrength = (ULONG)a1;
        regs->Rax = 0;
        HvAdvanceRip(3);
        return;
    case HV_OP_QUERY_STATE:
        regs->Rax = ((ULONG64)ctx->HiddenCount << 32) | (ctx->Launched ? 1 : 0);
        HvAdvanceRip(3);
        return;
    default:
        break;
    }

    HvInjectException(EXCEPTION_UD, FALSE, 0);
}

// Current EPT-violation behavior distinguishes instruction fetches
// from data accesses. Neither case has been validated here.
VOID HvHandleEptViolation(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual) {
    UNREFERENCED_PARAMETER(regs);
    ULONG64 guestPhys = 0;
    __vmx_vmread(VMCS_RO_GUEST_PHYS_ADDR, &guestPhys);

    if (!HvEptIsHidden(ctx, guestPhys)) {
        HvInjectException(EXCEPTION_GP, TRUE, 0);
        return;
    }

    if (qual & EPT_QUAL_FETCH) {
        ULONG64 len = 0;
        __vmx_vmread(VMCS_RO_VM_EXIT_INSTR_LEN, &len);
        if (len == 0) len = 1;
        HvAdvanceRip((ULONG)len);
        return;
    }

    HvInjectException(EXCEPTION_GP, TRUE, 0);
}

VOID HvHandlePreemptionTimer(PHV_CPU_CONTEXT_EX ctx) {
    HvTscTick(ctx);

    if (ctx->Caps.HasApicv) {
        HvApicvTimerTick(ctx);
    } else {
        HvApicTimerTick(ctx);
    }

    __vmx_vmwrite(VMCS_CTRL_PREEMPTION_TIMER, 0x100000);
}

VOID HvVmExitDispatch(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx) {
    ULONG64 exitReason = 0;
    __vmx_vmread(VMCS_RO_VM_EXIT_REASON, &exitReason);

    ULONG reason = (ULONG)(exitReason & 0xFFFF);
    if (exitReason >> 31) return;   // VM-entry failure requires separate error handling

    ULONG64 qual = 0;
    __vmx_vmread(VMCS_RO_EXIT_QUALIFICATION, &qual);

    switch (reason) {
    case EXIT_REASON_EXCEPTION_NMI: {
        ULONG64 info = 0;
        __vmx_vmread(VMCS_RO_VM_EXIT_INTR_INFO, &info);
        ULONG vec = (ULONG)(info & 0xFF);
        if (vec == EXCEPTION_UD)
            HvHandleVmfuncUd(regs, ctx, qual);
        else
            HvInjectException(vec, FALSE, 0);
        break;
    }
    case EXIT_REASON_CPUID:
        HvHandleCpuid(regs, ctx);
        HvAdvanceRip(2);        // 0F A2
        break;
    case EXIT_REASON_RDMSR:
        HvHandleMsrRead(regs, ctx);
        HvAdvanceRip(2);        // 0F 32
        break;
    case EXIT_REASON_WRMSR:
        HvHandleMsrWrite(regs, ctx);
        HvAdvanceRip(2);        // 0F 30
        break;
    case EXIT_REASON_MOV_DR: {
        ULONG64 len = 0;
        __vmx_vmread(VMCS_RO_VM_EXIT_INSTR_LEN, &len);
        if (len == 0) len = 2;
        HvAdvanceRip((ULONG)len);
        break;
    }
    case EXIT_REASON_PAUSE:
        HvAdvanceRip(2);        // F3 90
        break;
    case EXIT_REASON_EPT_VIOLATION:
        HvHandleEptViolation(regs, ctx, qual);
        break;
    case EXIT_REASON_PREEMPTION_TIMER:
        HvHandlePreemptionTimer(ctx);
        break;
    case EXIT_REASON_APIC_ACCESS:
        HvHandleApicAccess(regs, ctx, qual);
        break;
    case EXIT_REASON_APIC_WRITE:
        HvHandleApicWrite(regs, ctx, qual);
        break;
    case EXIT_REASON_EOI:
        HvHandleEoi(regs, ctx, qual);
        break;
    case EXIT_REASON_SELF_IPI:
        HvHandleSelfIpi(regs, ctx, qual);
        break;
    default:
        break;
    }
}