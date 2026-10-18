// VMCS control fields and initial guest/host state.
// The requested controls are checked against the CPU capability MSRs.

#include "hv.h"

static ULONG64 HvReadGdtEntry(ULONG64 gdtBase, USHORT sel) {
    return *(PULONG64)(gdtBase + (sel & ~7));
}

static VOID HvParseSegment(ULONG64 entry, PULONG64 base, PULONG limit, PULONG ar) {
    *limit = (ULONG)((entry & 0xFFFF) | (((entry >> 48) & 0xF) << 16));
    *base  = ((entry >> 16) & 0xFFFF)
           | (((entry >> 32) & 0xFF) << 16)
           | (((entry >> 56) & 0xFF) << 24);
    ULONG access = (ULONG)((entry >> 40) & 0xFF);
    ULONG flags  = (ULONG)((entry >> 48) & 0xF);
    *ar = access | (flags << 12);
}

// capability msr: low 32 = must be 1, high 32 = may be 1.
// value = (desired | low) & high
static VOID HvWriteControl(ULONG64 msr, ULONG64 field, ULONG64 desired) {
    ULONG64 cap   = __readmsr(msr);
    ULONG64 low   = cap & 0xFFFFFFFF;
    ULONG64 high  = (cap >> 32) & 0xFFFFFFFF;
    ULONG64 value = ((desired & 0xFFFFFFFF) | low) & high;
    __vmx_vmwrite(field, value);
}

static VOID HvProbeCaps(PHV_CAPS c) {
    RtlZeroMemory(c, sizeof(*c));
    ULONG64 prim1 = __readmsr(IA32_VMX_PROCBASED_CTLS);
    ULONG64 prim2 = __readmsr(IA32_VMX_PROCBASED_CTLS2);
    ULONG64 pin   = __readmsr(IA32_VMX_PINBASED_CTLS);

    ULONG64 a1 = (prim1 >> 32) & 0xFFFFFFFF;
    ULONG64 a2 = (prim2 >> 32) & 0xFFFFFFFF;
    ULONG64 ap = (pin   >> 32) & 0xFFFFFFFF;

    c->HasPle             = (a2 & CPU_BASED2_PAUSE_LOOP_EXITING) != 0;
    c->HasTscScaling      = (a2 & CPU_BASED2_USE_TSC_SCALING) != 0;
    c->HasMovDr           = (a1 & CPU_BASED_MOV_DR_EXITING) != 0;
    c->HasVirtualNmi      = (ap & PIN_BASED_VIRTUAL_NMI) != 0;
    c->HasVpid            = (a2 & CPU_BASED2_ENABLE_VPID) != 0;

    c->HasApicv           = ((a1 & CPU_BASED_TPR_SHADOW) != 0) &&
                            ((a2 & CPU_BASED2_VIRTUALIZE_APIC) != 0);
    c->HasApicRegVirt     = (a2 & CPU_BASED2_APIC_REG_VIRT) != 0;
    c->HasVirtIntDelivery = (a2 & CPU_BASED2_VIRTUAL_INT_DELIVERY) != 0;
    c->HasX2ApicVirt      = (a2 & CPU_BASED2_VIRTUALIZE_X2APIC) != 0;
    c->HasPostedIntr      = (ap & PIN_BASED_PROCESS_POSTED_INTR) != 0;
}

NTSTATUS HvSetupVmcs(PHV_CPU_CONTEXT_EX ctx) {
    HvProbeCaps(&ctx->Caps);

    ULONG64 pinD  = PIN_BASED_ACTIVATE_PREEMPT_TMR;
    ULONG64 prim1 = CPU_BASED_ACTIVATE_SECONDARY
                  | CPU_BASED_USE_MSR_BITMAPS
                  | CPU_BASED_USE_TSC_OFFSETTING;
    ULONG64 prim2 = CPU_BASED2_ENABLE_EPT;

    if (ctx->Caps.HasVirtualNmi) pinD  |= PIN_BASED_VIRTUAL_NMI;
    if (ctx->Caps.HasPle)        prim2 |= CPU_BASED2_PAUSE_LOOP_EXITING;
    if (ctx->Caps.HasTscScaling) prim2 |= CPU_BASED2_USE_TSC_SCALING;
    if (ctx->Caps.HasMovDr)      prim1 |= CPU_BASED_MOV_DR_EXITING;
    if (ctx->Caps.HasVpid)       prim2 |= CPU_BASED2_ENABLE_VPID;

    if (ctx->Caps.HasApicv) {
        prim1 |= CPU_BASED_TPR_SHADOW;
        prim2 |= CPU_BASED2_VIRTUALIZE_APIC;
    }
    if (ctx->Caps.HasApicRegVirt)    prim2 |= CPU_BASED2_APIC_REG_VIRT;
    if (ctx->Caps.HasVirtIntDelivery) prim2 |= CPU_BASED2_VIRTUAL_INT_DELIVERY;
    if (ctx->Caps.HasX2ApicVirt)     prim2 |= CPU_BASED2_VIRTUALIZE_X2APIC;
    if (ctx->Caps.HasPostedIntr && ctx->PostedIntrDesc)
        pinD |= PIN_BASED_PROCESS_POSTED_INTR;

    HvWriteControl(IA32_VMX_PINBASED_CTLS,   VMCS_CTRL_PIN_BASED,  pinD);
    HvWriteControl(IA32_VMX_PROCBASED_CTLS,  VMCS_CTRL_CPU_BASED,  prim1);
    HvWriteControl(IA32_VMX_PROCBASED_CTLS2, VMCS_CTRL_CPU_BASED2, prim2);
    HvWriteControl(IA32_VMX_EXIT_CTLS,       VMCS_CTRL_VM_EXIT_CONTROLS, VM_EXIT_HOST_ADDR_SPACE_SIZE);
    HvWriteControl(IA32_VMX_ENTRY_CTLS,      VMCS_CTRL_VM_ENTRY_CONTROLS, VM_ENTRY_IA32E_MODE);

    if (ctx->Caps.HasVpid)
        __vmx_vmwrite(VMCS_CTRL_VPID, ctx->Vpid);

    if (ctx->Caps.HasPle) {
        __vmx_vmwrite(VMCS_CTRL_PLE_GAP, 0x200);
        __vmx_vmwrite(VMCS_CTRL_PLE_WINDOW, 0x4000);
    }
    if (ctx->Caps.HasTscScaling)
        __vmx_vmwrite(VMCS_CTRL_TSC_MULTIPLIER, 0x10000);

    __vmx_vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, (1u << EXCEPTION_UD));
    __vmx_vmwrite(VMCS_CTRL_MSR_BITMAP, ctx->MsrBitmapPhys);
    __vmx_vmwrite(VMCS_CTRL_PREEMPTION_TIMER, 0x100000);
    __vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, 0);

    if (!NT_SUCCESS(HvEptInit(ctx))) return STATUS_UNSUCCESSFUL;

    // APICv setup follows the initial control writes. The fallback path
    // is selected if resource initialization does not succeed.
    if (ctx->Caps.HasApicv) {
        if (!NT_SUCCESS(HvApicvInit(ctx))) {
            ctx->Caps.HasApicv = FALSE;
            prim1 &= ~CPU_BASED_TPR_SHADOW;
            prim2 &= ~(CPU_BASED2_VIRTUALIZE_APIC | CPU_BASED2_APIC_REG_VIRT |
                       CPU_BASED2_VIRTUAL_INT_DELIVERY | CPU_BASED2_VIRTUALIZE_X2APIC);
            HvWriteControl(IA32_VMX_PROCBASED_CTLS,  VMCS_CTRL_CPU_BASED,  prim1);
            HvWriteControl(IA32_VMX_PROCBASED_CTLS2, VMCS_CTRL_CPU_BASED2, prim2);
        }
    }

    // The x64 GDTR consists of a 16-bit limit followed by a 64-bit base.
    // Read it into two slots to reconstruct the base address.
    ULONG64 gdt[2] = {0};
    HvAsmReadGdtr(gdt);
    ULONG64 gdtBase = (gdt[0] >> 16) | (gdt[1] << 48);

    USHORT cs = HvAsmReadCs(), ds = HvAsmReadDs();
    USHORT ss = HvAsmReadSs(), es = HvAsmReadEs();
    USHORT fs = HvAsmReadFs(), gs = HvAsmReadGs();

    struct { ULONG64 sel, base, limit, ar; USHORT v; } segs[] = {
        {VMCS_GUEST_ES_SELECTOR, VMCS_GUEST_ES_BASE, VMCS_GUEST_ES_LIMIT, VMCS_GUEST_ES_AR, es},
        {VMCS_GUEST_CS_SELECTOR, VMCS_GUEST_CS_BASE, VMCS_GUEST_CS_LIMIT, VMCS_GUEST_CS_AR, cs},
        {VMCS_GUEST_SS_SELECTOR, VMCS_GUEST_SS_BASE, VMCS_GUEST_SS_LIMIT, VMCS_GUEST_SS_AR, ss},
        {VMCS_GUEST_DS_SELECTOR, VMCS_GUEST_DS_BASE, VMCS_GUEST_DS_LIMIT, VMCS_GUEST_DS_AR, ds},
        {VMCS_GUEST_FS_SELECTOR, VMCS_GUEST_FS_BASE, VMCS_GUEST_FS_LIMIT, VMCS_GUEST_FS_AR, fs},
        {VMCS_GUEST_GS_SELECTOR, VMCS_GUEST_GS_BASE, VMCS_GUEST_GS_LIMIT, VMCS_GUEST_GS_AR, gs},
    };

    for (int i = 0; i < 6; i++) {
        ULONG64 base = 0, limit = 0, ar = 0;
        if (segs[i].v != 0) {
            ULONG64 entry = HvReadGdtEntry(gdtBase, segs[i].v);
            HvParseSegment(entry, &base, &limit, &ar);
        }
        if (segs[i].v == fs) base = __readmsr(0xC0000100);
        if (segs[i].v == gs) base = __readmsr(0xC0000101);
        __vmx_vmwrite(segs[i].sel, segs[i].v);
        __vmx_vmwrite(segs[i].base, base);
        __vmx_vmwrite(segs[i].limit, limit);
        __vmx_vmwrite(segs[i].ar, ar);
    }

    // A valid task register is required for VM entry.
    {
        USHORT tr = HvAsmReadTr();
        ULONG64 entry = HvReadGdtEntry(gdtBase, tr);
        ULONG64 base = 0, limit = 0, ar = 0;
        HvParseSegment(entry, &base, &limit, &ar);
        __vmx_vmwrite(VMCS_GUEST_TR_SELECTOR, tr);
        __vmx_vmwrite(VMCS_GUEST_TR_BASE, base);
        __vmx_vmwrite(VMCS_GUEST_TR_LIMIT, limit);
        __vmx_vmwrite(VMCS_GUEST_TR_AR, ar);
        __vmx_vmwrite(VMCS_HOST_TR_SELECTOR, tr);
        __vmx_vmwrite(VMCS_HOST_TR_BASE, base);
    }

    // LDTR can be zero, but its VMCS fields still need to be populated.
    {
        USHORT ldt = HvAsmReadLdtr();
        ULONG64 base = 0, limit = 0, ar = 0;
        if (ldt != 0) {
            ULONG64 entry = HvReadGdtEntry(gdtBase, ldt);
            HvParseSegment(entry, &base, &limit, &ar);
        }
        __vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, ldt);
        __vmx_vmwrite(VMCS_GUEST_LDTR_BASE, base);
        __vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, limit);
        __vmx_vmwrite(VMCS_GUEST_LDTR_AR, ar);
    }

    {
        UCHAR idtr[16] = {0};
        __sidt(idtr);
        ULONG64 ibase = *(ULONG64*)(idtr + 2);
        USHORT  glimit = (USHORT)(gdt[0] & 0xFFFF);
        USHORT  ilimit = *(USHORT*)idtr;
        __vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, glimit);
        __vmx_vmwrite(VMCS_GUEST_GDTR_BASE, gdtBase);
        __vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, ilimit);
        __vmx_vmwrite(VMCS_GUEST_IDTR_BASE, ibase);
        __vmx_vmwrite(VMCS_HOST_GDTR_BASE, gdtBase);
        __vmx_vmwrite(VMCS_HOST_IDTR_BASE, ibase);
    }

    __vmx_vmwrite(VMCS_GUEST_SYSENTER_CS,  __readmsr(0x174));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(0x175));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(0x176));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_ESP,  __readmsr(0x175));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_EIP,  __readmsr(0x176));

    __vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, 0);
    __vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);
    __vmx_vmwrite(VMCS_GUEST_PENDING_DBG_EXC, 0);
    __vmx_vmwrite(VMCS_GUEST_VMCS_LINK_PTR, ~0ULL);
    __vmx_vmwrite(VMCS_GUEST_DR7, 0x400);

    ULONG64 cr0 = __readcr0(), cr3 = __readcr3(), cr4 = __readcr4();
    cr0 = (cr0 | __readmsr(IA32_VMX_CR0_FIXED0)) & __readmsr(IA32_VMX_CR0_FIXED1);
    cr4 = (cr4 | __readmsr(IA32_VMX_CR4_FIXED0)) & __readmsr(IA32_VMX_CR4_FIXED1);
    __vmx_vmwrite(VMCS_GUEST_CR0, cr0);
    __vmx_vmwrite(VMCS_GUEST_CR3, cr3);
    __vmx_vmwrite(VMCS_GUEST_CR4, cr4);

    // host cr4 without vmxe (hw clears it anyway but launch checks the field)
    ULONG64 hostCr0 = __readcr0();
    ULONG64 hostCr3 = __readcr3();
    ULONG64 hostCr4 = __readcr4() & ~(1ULL << 13);
    __vmx_vmwrite(VMCS_HOST_CR0, hostCr0);
    __vmx_vmwrite(VMCS_HOST_CR3, hostCr3);
    __vmx_vmwrite(VMCS_HOST_CR4, hostCr4);

    __vmx_vmwrite(VMCS_HOST_ES_SELECTOR, 0);
    __vmx_vmwrite(VMCS_HOST_DS_SELECTOR, 0);
    __vmx_vmwrite(VMCS_HOST_FS_SELECTOR, 0);
    __vmx_vmwrite(VMCS_HOST_GS_SELECTOR, 0);
    __vmx_vmwrite(VMCS_HOST_SS_SELECTOR, 0);
    __vmx_vmwrite(VMCS_HOST_CS_SELECTOR, HvAsmReadCs());
    __vmx_vmwrite(VMCS_HOST_FS_BASE, __readmsr(0xC0000100));
    __vmx_vmwrite(VMCS_HOST_GS_BASE, (ULONG64)ctx);   // asm reads via gs:[0]

    __vmx_vmwrite(VMCS_HOST_RSP, (ULONG64)ctx->HostStack + HV_HOST_STACK_SIZE - 0x100);
    __vmx_vmwrite(VMCS_HOST_RIP, (ULONG64)HvVmExitEntry);

    if (!NT_SUCCESS(HvMsrSetup(ctx))) return STATUS_UNSUCCESSFUL;

    ctx->TscFogStrength = 0x20;
    ctx->TscBaseOffset  = 0;
    return STATUS_SUCCESS;
}