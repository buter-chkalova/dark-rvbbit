// MSR bitmap setup and the experimental guest-visible MSR values.
// Bitmap regions follow the Intel SDM read/write layout.

#include "hv.h"

static VOID HvMsrSetRead(PVOID bm, ULONG msr) {
    if (msr < 0x2000) {
        PUCHAR b = (PUCHAR)bm + (msr / 8);
        *b |= (1u << (msr % 8));
    } else if (msr >= 0xC0000000 && msr < 0xC0002000) {
        ULONG o = msr - 0xC0000000;
        PUCHAR b = (PUCHAR)bm + 0x400 + (o / 8);
        *b |= (1u << (o % 8));
    }
}

static VOID HvMsrSetWrite(PVOID bm, ULONG msr) {
    if (msr < 0x2000) {
        PUCHAR b = (PUCHAR)bm + 0x800 + (msr / 8);
        *b |= (1u << (msr % 8));
    } else if (msr >= 0xC0000000 && msr < 0xC0002000) {
        ULONG o = msr - 0xC0000000;
        PUCHAR b = (PUCHAR)bm + 0xC00 + (o / 8);
        *b |= (1u << (o % 8));
    }
}

NTSTATUS HvMsrSetup(PHV_CPU_CONTEXT_EX ctx) {
    RtlZeroMemory(ctx->MsrBitmap, HV_MSR_BITMAP_SIZE);

    ULONG traps[] = {
        IA32_FEATURE_CONTROL,
        IA32_VMX_BASIC,
        IA32_VMX_PINBASED_CTLS,
        IA32_VMX_PROCBASED_CTLS,
        IA32_VMX_PROCBASED_CTLS2,
        IA32_VMX_EXIT_CTLS,
        IA32_VMX_ENTRY_CTLS,
        IA32_VMX_CR0_FIXED0,
        IA32_VMX_CR0_FIXED1,
        IA32_VMX_CR4_FIXED0,
        IA32_VMX_CR4_FIXED1,
    };
    for (ULONG i = 0; i < sizeof(traps)/sizeof(traps[0]); i++)
        HvMsrSetRead(ctx->MsrBitmap, traps[i]);

    HvMsrSetWrite(ctx->MsrBitmap, IA32_FEATURE_CONTROL);

    __vmx_vmwrite(VMCS_CTRL_MSR_BITMAP, ctx->MsrBitmapPhys);
    return STATUS_SUCCESS;
}

// report "vmx not enabled, not supported" so guest sees nothing.
// cr0/cr4 fixed bits report plausible values (0 would be inconsistent with a real cpu).
BOOLEAN HvMsrFakeRead(ULONG msr, PULONG64 value) {
    switch (msr) {
    case IA32_FEATURE_CONTROL:
    case IA32_VMX_BASIC:
    case IA32_VMX_PINBASED_CTLS:
    case IA32_VMX_PROCBASED_CTLS:
    case IA32_VMX_PROCBASED_CTLS2:
    case IA32_VMX_EXIT_CTLS:
    case IA32_VMX_ENTRY_CTLS:
        *value = 0;
        return TRUE;

    case IA32_VMX_CR0_FIXED0: *value = 0x80000021; return TRUE;
    case IA32_VMX_CR0_FIXED1: *value = 0xFFFFFFFF; return TRUE;
    case IA32_VMX_CR4_FIXED0: *value = 0x00000000; return TRUE;
    case IA32_VMX_CR4_FIXED1: *value = 0xFFFFFFFF; return TRUE;
    }
    return FALSE;
}