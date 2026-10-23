// Fallback APIC timer state for CPUs without the selected APICv path.
// Timer MSR accesses are handled in software.

#include "hv.h"
#include "hv_apic_util.h"

static ULONG64 HvGuestTsc(void) {
    ULONG64 off = 0;
    __vmx_vmread(VMCS_CTRL_TSC_OFFSET, &off);
    return __rdtsc() + off;
}

VOID HvApicTimerInit(PHV_CPU_CONTEXT_EX ctx) {
    RtlZeroMemory(&ctx->ApicTimer, sizeof(ctx->ApicTimer));
    ctx->ApicTimer.Divide = 1;
}

VOID HvApicTimerReadMsr(PHV_CPU_CONTEXT_EX ctx, ULONG msr, PULONG64 out) {
    HV_APIC_TIMER *t = &ctx->ApicTimer;

    switch (msr) {
    case IA32_X2APIC_LVT_TIMER: {
        ULONG64 v = t->Vector & 0xFF;
        if (t->Periodic) v |= (1ULL << 17);
        *out = v;
        return;
    }
    case IA32_X2APIC_TIMER_INITIAL:
        *out = t->Count;
        return;
    case IA32_X2APIC_TIMER_CURRENT: {
        if (!t->Enabled) { *out = 0; return; }
        ULONG64 now = HvGuestTsc();
        if (now >= t->DeadlineTsc) { *out = 0; return; }
        *out = (t->DeadlineTsc - now) / t->Divide;
        return;
    }
    case IA32_X2APIC_TIMER_DIVIDE:
        *out = t->Divide;
        return;
    }
}

BOOLEAN HvApicTimerWriteMsr(PHV_CPU_CONTEXT_EX ctx, ULONG msr, ULONG64 value) {
    HV_APIC_TIMER *t = &ctx->ApicTimer;

    switch (msr) {
    case IA32_X2APIC_LVT_TIMER:
        t->Vector   = (ULONG)(value & 0xFF);
        t->Periodic = (value >> 17) & 1;
        if ((value >> 16) & 1) t->Enabled = FALSE;
        return TRUE;

    case IA32_X2APIC_TIMER_INITIAL:
        t->Count = (ULONG)value;
        if (t->Count == 0) {
            t->Enabled = FALSE;
            return TRUE;
        }
        t->StartGuestTsc = HvGuestTsc();
        t->DeadlineTsc   = t->StartGuestTsc + ((ULONG64)t->Count * t->Divide);
        t->Enabled       = TRUE;
        return TRUE;

    case IA32_X2APIC_TIMER_CURRENT:
        return TRUE;

    case IA32_X2APIC_TIMER_DIVIDE: {
        t->Divide = HvDecodeApicDivide((ULONG)value);
        return TRUE;
    }
    }
    return FALSE;
}

VOID HvApicTimerTick(PHV_CPU_CONTEXT_EX ctx) {
    HV_APIC_TIMER *t = &ctx->ApicTimer;
    if (!t->Enabled) return;

    ULONG64 now = HvGuestTsc();
    if (now < t->DeadlineTsc) return;

    ULONG64 info = 0x80000000ULL | (0ULL << 8) | (t->Vector & 0xFF);
    __vmx_vmwrite(VMCS_CTRL_VM_ENTRY_INTR_INFO, info);

    if (t->Periodic) {
        t->StartGuestTsc = now;
        t->DeadlineTsc   = now + ((ULONG64)t->Count * t->Divide);
    } else {
        t->Enabled = FALSE;
    }
}