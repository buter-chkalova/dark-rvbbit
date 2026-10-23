// APIC timer divide values are encoded, not linear.
#pragma once

#include <ntddk.h>

static __inline ULONG HvDecodeApicDivide(ULONG value) {
    switch (value & 0xF) {
    case 0xB: return 1;
    case 0x0: return 2;
    case 0x1: return 4;
    case 0x2: return 8;
    case 0x3: return 16;
    case 0x8: return 32;
    case 0x9: return 64;
    case 0xA: return 128;
    default:  return 1;  // Reserved encoding; no verified hardware behavior.
    }
}
