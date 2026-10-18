// Placeholder for an embedded driver image.
// This snapshot intentionally contains no embedded binary payload.
// gHvDriverSize is zero, so the UEFI path is not a complete feature.

#include <Uefi.h>

extern CONST UINT8 _binary_dark_rvbbit_sys_start[];
extern CONST UINT8 _binary_dark_rvbbit_sys_end[];

CONST UINT8 gHvDriverBlob[] = { 0x00 };
CONST UINTN gHvDriverSize   = 0;