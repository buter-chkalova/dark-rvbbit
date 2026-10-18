// TSC timing experiment. A periodic VM-exit changes the configured
// offset and, where supported, the multiplier.
//
// Consistency with APIC timer behavior is an open validation question.

#include "hv.h"

static ULONG64 HvRand64(void) {
    LARGE_INTEGER t = KeQueryPerformanceCounter(NULL);
    ULONG64 x = (ULONG64)t.QuadPart;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return x;
}

VOID HvTscTick(PHV_CPU_CONTEXT_EX ctx) {
    if (!ctx->TscFogStrength) {
        __vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, 0);
        if (ctx->Caps.HasTscScaling)
            __vmx_vmwrite(VMCS_CTRL_TSC_MULTIPLIER, 0x10000);
        return;
    }

    ULONG64 jitter = (HvRand64() % (ctx->TscFogStrength * 2)) - ctx->TscFogStrength;
    __vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, ctx->TscBaseOffset + jitter);

    if (ctx->Caps.HasTscScaling) {
        ULONG64 mult = 0x10000 + (HvRand64() % 0x68) - 0x34;
        __vmx_vmwrite(VMCS_CTRL_TSC_MULTIPLIER, mult);
    }
}