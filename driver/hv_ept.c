// EPT mapping experiments: the initial identity map uses 2 MB pages
// over a 64 GB range, with optional 4 KB page-table splits.

#include "hv.h"

PEPT_PTE HvEptWalk(PHV_CPU_CONTEXT_EX ctx, ULONG64 phys, BOOLEAN alloc) {
    PEPT_PML4E pml4 = (PEPT_PML4E)ctx->EptPml4;
    ULONG64 i4 = (phys >> 39) & 0x1FF;
    ULONG64 i3 = (phys >> 30) & 0x1FF;
    ULONG64 i2 = (phys >> 21) & 0x1FF;
    ULONG64 i1 = (phys >> 12) & 0x1FF;

    LARGE_INTEGER mp = { .QuadPart = ~0ULL };

    if (!pml4[i4].Present) {
        if (!alloc) return NULL;
        PVOID p = MmAllocateContiguousMemory(PAGE_SIZE, mp);
        if (!p) return NULL;
        RtlZeroMemory(p, PAGE_SIZE);
        pml4[i4].Value = MmGetPhysicalAddress(p).QuadPart | 0x07;
    }

    PEPT_PDPTE pdpt = (PEPT_PDPTE)(pml4[i4].PhysAddr << 12);
    if (!pdpt[i3].Present) {
        if (!alloc) return NULL;
        PVOID p = MmAllocateContiguousMemory(PAGE_SIZE, mp);
        if (!p) return NULL;
        RtlZeroMemory(p, PAGE_SIZE);
        pdpt[i3].Value = MmGetPhysicalAddress(p).QuadPart | 0x07;
    }

    PEPT_PDE pd = (PEPT_PDE)(pdpt[i3].PhysAddr << 12);
    if (!pd[i2].Present || pd[i2].Ps) {
        if (!alloc) return NULL;
        PVOID ptP = MmAllocateContiguousMemory(PAGE_SIZE, mp);
        if (!ptP) return NULL;
        RtlZeroMemory(ptP, PAGE_SIZE);

        // Expand a large-page mapping into 512 page-table entries.
        if (pd[i2].Present && pd[i2].Ps) {
            PEPT_PTE pt = (PEPT_PTE)ptP;
            ULONG64 base = pd[i2].PhysAddr << 21;
            for (ULONG k = 0; k < 512; k++)
                pt[k].Value = ((base + ((ULONG64)k << 12)) & ~0xFFFULL) | 0x07;
        }
        pd[i2].Value = MmGetPhysicalAddress(ptP).QuadPart | 0x07;
    }

    PEPT_PTE pt = (PEPT_PTE)(pd[i2].PhysAddr << 12);
    return &pt[i1];
}

NTSTATUS HvEptInit(PHV_CPU_CONTEXT_EX ctx) {
    LARGE_INTEGER mp = { .QuadPart = ~0ULL };

    ctx->EptPml4 = MmAllocateContiguousMemory(PAGE_SIZE, mp);
    if (!ctx->EptPml4) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(ctx->EptPml4, PAGE_SIZE);
    ctx->EptPml4Phys = MmGetPhysicalAddress(ctx->EptPml4).QuadPart;

    PVOID pdpt = MmAllocateContiguousMemory(PAGE_SIZE, mp);
    if (!pdpt) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(pdpt, PAGE_SIZE);
    ULONG64 pdptP = MmGetPhysicalAddress(pdpt).QuadPart;
    ((PEPT_PML4E)ctx->EptPml4)[0].Value = pdptP | 0x07;

    for (ULONG gb = 0; gb < HV_EPT_COVER_GB; gb++) {
        PVOID pd = MmAllocateContiguousMemory(PAGE_SIZE, mp);
        if (!pd) return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(pd, PAGE_SIZE);
        ULONG64 pdP = MmGetPhysicalAddress(pd).QuadPart;

        for (ULONG j = 0; j < 512; j++) {
            ULONG64 phys = ((ULONG64)gb << 30) | ((ULONG64)j << 21);
            ((PEPT_PDE)pd)[j].Value = (phys & ~0x1FFFFFULL) | 0x87;
        }
        ((PEPT_PDPTE)pdpt)[gb].Value = pdP | 0x07;
    }

    ULONG64 eptp = ctx->EptPml4Phys | 0x06 | (3ULL << 3);
    __vmx_vmwrite(VMCS_CTRL_EPT_POINTER, eptp);

    // hide our own regions. guest scan of phys mem hits #gp on these.
    HvEptHideRange(ctx, ctx->VmxonPhys, PAGE_SIZE);
    HvEptHideRange(ctx, ctx->VmcsPhys, PAGE_SIZE);
    HvEptHideRange(ctx, ctx->MsrBitmapPhys, PAGE_SIZE);
    HvEptHideRange(ctx, ctx->EptPml4Phys, PAGE_SIZE);
    if (ctx->HostStackPhys)
        HvEptHideRange(ctx, ctx->HostStackPhys, HV_HOST_STACK_SIZE);

    return STATUS_SUCCESS;
}

// FIXME: this allocation path can be reached during VM-exit handling.
// The latency and failure behavior still need controlled testing.
NTSTATUS HvEptHideRange(PHV_CPU_CONTEXT_EX ctx, ULONG64 physStart, ULONG64 size) {
    ULONG64 start = physStart & ~0xFFFULL;
    ULONG64 end   = (physStart + size + 0xFFF) & ~0xFFFULL;

    for (ULONG64 p = start; p < end; p += PAGE_SIZE) {
        PEPT_PTE pte = HvEptWalk(ctx, p, TRUE);
        if (!pte) return STATUS_UNSUCCESSFUL;
        pte->Value = 0;
    }

    if (ctx->HiddenCount < HV_MAX_HIDDEN_RANGES) {
        ctx->Hidden[ctx->HiddenCount].PhysStart = start;
        ctx->Hidden[ctx->HiddenCount].Size      = end - start;
        ctx->HiddenCount++;
    }

    HvAsmInveptSingle(ctx->EptPml4Phys | 0x06 | (3ULL << 3));
    return STATUS_SUCCESS;
}

NTSTATUS HvEptUnhideRange(PHV_CPU_CONTEXT_EX ctx, ULONG64 physStart, ULONG64 size) {
    ULONG64 start = physStart & ~0xFFFULL;
    ULONG64 end   = (physStart + size + 0xFFF) & ~0xFFFULL;

    for (ULONG64 p = start; p < end; p += PAGE_SIZE) {
        PEPT_PTE pte = HvEptWalk(ctx, p, TRUE);
        if (!pte) return STATUS_UNSUCCESSFUL;
        pte->Value = (p & ~0xFFFULL) | 0x07;
    }

    for (ULONG i = 0; i < ctx->HiddenCount; i++) {
        if (ctx->Hidden[i].PhysStart == start) {
            ctx->Hidden[i] = ctx->Hidden[ctx->HiddenCount - 1];
            ctx->HiddenCount--;
            break;
        }
    }

    HvAsmInveptSingle(ctx->EptPml4Phys | 0x06 | (3ULL << 3));
    return STATUS_SUCCESS;
}

BOOLEAN HvEptIsHidden(PHV_CPU_CONTEXT_EX ctx, ULONG64 physAddr) {
    PEPT_PTE pte = HvEptWalk(ctx, physAddr & ~0xFFFULL, FALSE);
    if (!pte) return FALSE;
    return (pte->Present == 0);
}