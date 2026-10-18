// Per-CPU VMX setup and resource cleanup.
// APICv setup runs as part of VMCS configuration; this file owns teardown.

#include "hv.h"

HV_CPU_CONTEXT_EX g_HvEx[HV_MAX_CPUS];
ULONG g_HvCpuCount = 0;
static USHORT g_NextVpid = 1;

static BOOLEAN HvCheckVmxSupport(void) {
    int regs[4];
    __cpuid(regs, 1);
    if (!(regs[2] & CPUID_VMX_BIT)) return FALSE;

    ULONG64 feature = __readmsr(IA32_FEATURE_CONTROL);
    if (feature & 1) {
        if (!(feature & (1ULL << 2))) return FALSE;
    } else {
        __writemsr(IA32_FEATURE_CONTROL, feature | 0x05);
    }
    return TRUE;
}

static BOOLEAN HvSelfTest(PHV_CPU_CONTEXT_EX ctx) {
    ULONG64 v = 0;
    __vmx_vmread(VMCS_CTRL_CPU_BASED, &v);
    if (v == 0) return FALSE;

    __vmx_vmread(VMCS_CTRL_EPT_POINTER, &v);
    if (v == 0) return FALSE;

    ULONG64 rip = 0;
    __vmx_vmread(VMCS_HOST_RIP, &rip);
    if (rip != (ULONG64)HvVmExitEntry) return FALSE;

    return TRUE;
}

NTSTATUS HvInitializeCpu(ULONG cpuIndex) {
    PHV_CPU_CONTEXT_EX ctx = &g_HvEx[cpuIndex];
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->Self     = ctx;
    ctx->CpuIndex = cpuIndex;
    ctx->Vpid     = g_NextVpid++;

    if (!HvCheckVmxSupport()) return STATUS_NOT_SUPPORTED;

    ULONG64 vmxBasic = __readmsr(IA32_VMX_BASIC);
    ULONG revisionId = (ULONG)(vmxBasic & 0x7FFFFFFF);
    __writecr4(__readcr4() | (1ULL << 13));

    LARGE_INTEGER maxPhys = { .QuadPart = ~0ULL };

    ctx->VmxonRegion = MmAllocateContiguousMemory(HV_VMXON_SIZE, maxPhys);
    if (!ctx->VmxonRegion) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(ctx->VmxonRegion, HV_VMXON_SIZE);
    *(ULONG*)ctx->VmxonRegion = revisionId;
    ctx->VmxonPhys = MmGetPhysicalAddress(ctx->VmxonRegion).QuadPart;

    if (__vmx_on(&ctx->VmxonPhys)) {
        MmFreeContiguousMemory(ctx->VmxonRegion);
        return STATUS_UNSUCCESSFUL;
    }

    ctx->VmcsRegion = MmAllocateContiguousMemory(HV_VMCS_SIZE, maxPhys);
    if (!ctx->VmcsRegion) goto fail;
    RtlZeroMemory(ctx->VmcsRegion, HV_VMCS_SIZE);
    *(ULONG*)ctx->VmcsRegion = revisionId;
    ctx->VmcsPhys = MmGetPhysicalAddress(ctx->VmcsRegion).QuadPart;

    __vmx_vmclear(&ctx->VmcsPhys);
    if (__vmx_vmptrld(&ctx->VmcsPhys)) goto fail;

    ctx->MsrBitmap = MmAllocateContiguousMemory(HV_MSR_BITMAP_SIZE, maxPhys);
    if (!ctx->MsrBitmap) goto fail;
    RtlZeroMemory(ctx->MsrBitmap, HV_MSR_BITMAP_SIZE);
    ctx->MsrBitmapPhys = MmGetPhysicalAddress(ctx->MsrBitmap).QuadPart;

    ctx->HostStack = ExAllocatePoolWithTag(NonPagedPool, HV_HOST_STACK_SIZE, 'tBvR');
    if (!ctx->HostStack) goto fail;
    ctx->HostStackPhys = MmGetPhysicalAddress(ctx->HostStack).QuadPart;

    HvApicTimerInit(ctx);

    if (!NT_SUCCESS(HvSetupVmcs(ctx))) goto fail;
    if (!HvSelfTest(ctx)) goto fail;

    NTSTATUS s = HvAsmLaunch(ctx);
    if (!NT_SUCCESS(s)) goto fail;

    ctx->Launched = TRUE;
    return STATUS_SUCCESS;

fail:
    HvApicvTeardown(ctx);
    if (ctx->HostStack) ExFreePoolWithTag(ctx->HostStack, 'tBvR');
    if (ctx->MsrBitmap) MmFreeContiguousMemory(ctx->MsrBitmap);
    if (ctx->VmcsRegion) MmFreeContiguousMemory(ctx->VmcsRegion);
    __vmx_off();
    if (ctx->VmxonRegion) MmFreeContiguousMemory(ctx->VmxonRegion);
    return STATUS_UNSUCCESSFUL;
}

VOID HvTeardownCpu(ULONG cpuIndex) {
    PHV_CPU_CONTEXT_EX ctx = &g_HvEx[cpuIndex];
    if (!ctx->Launched) return;
    __vmx_off();
    ctx->Launched = FALSE;

    HvApicvTeardown(ctx);

    if (ctx->HostStack) ExFreePoolWithTag(ctx->HostStack, 'tBvR');
    if (ctx->MsrBitmap) MmFreeContiguousMemory(ctx->MsrBitmap);
    if (ctx->VmcsRegion) MmFreeContiguousMemory(ctx->VmcsRegion);
    if (ctx->VmxonRegion) MmFreeContiguousMemory(ctx->VmxonRegion);
}

static VOID HvDriverUnload(PDRIVER_OBJECT drv) {
    UNREFERENCED_PARAMETER(drv);
    for (ULONG i = 0; i < g_HvCpuCount; i++) {
        KeSetSystemAffinityThread((KAFFINITY)1 << i);
        HvTeardownCpu(i);
        KeRevertToUserAffinityThread();
    }
}

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg) {
    UNREFERENCED_PARAMETER(reg);
    drv->DriverUnload = HvDriverUnload;
    g_HvCpuCount = KeQueryActiveProcessorCount(NULL);
    if (g_HvCpuCount > HV_MAX_CPUS) g_HvCpuCount = HV_MAX_CPUS;
    for (ULONG i = 0; i < g_HvCpuCount; i++) {
        KeSetSystemAffinityThread((KAFFINITY)1 << i);
        NTSTATUS s = HvInitializeCpu(i);
        KeRevertToUserAffinityThread();
        if (!NT_SUCCESS(s)) {
            for (ULONG j = 0; j < i; j++) {
                KeSetSystemAffinityThread((KAFFINITY)1 << j);
                HvTeardownCpu(j);
                KeRevertToUserAffinityThread();
            }
            return s;
        }
    }
    return STATUS_SUCCESS;
}