# October 23 follow-up review

**Status:** Proposed changes under review. No successful driver build or runtime test is claimed.

I revisited the APIC timer code and several changes suggested for a particular Intel test setup. The exact CPU model and reproducible test results were not included with these notes, so I am not calling this a compatibility update.

## What I changed

The APIC timer divide field is not a linear number. The fallback timer path already decoded it with a lookup table, but the APICv path used a calculation that produced the wrong values for several encodings. I put the lookup in `driver/hv_apic_util.h` and use it in both places. That makes the code consistent; it does not establish correct timer behavior or a working APICv implementation.

## What I did not merge

**Memory-dump concealment.** The proposed second EPT view, memory-dump switching code, and user-facing control command are not integrated. There is no evidence that the proposed code would reliably affect a memory acquisition tool. The underlying EPT code still has unresolved address-translation and allocation problems. I am not claiming a working anti-forensics feature.

**Sleep and hibernate.** The proposed power callback does not match the documented `POWER_SETTING_CALLBACK` signature. More importantly, resetting a per-CPU context and starting VMX again is not a safe, verified wake-up procedure. No S3/S4 handling was added.

**PAT, XSAVES, and CET.** The suggested VMCS and processor-control edits need independent capability and guest-state checks. XSAVES/XRSTORS control is not a simple AVX-512 enable switch, and no specific Windows/CPU compatibility claim has been verified. These paths are unchanged.

**TSC-deadline and posted interrupts.** Intercepting an MSR and passing its value to hardware is not the same as emulating a timer. Posted-interrupt initialization is also incomplete. Neither path is presented as finished.

The current EPT walker, `Makefile`, UEFI experiment, driver lifecycle, and control interface remain unchanged. The earlier build and safety warnings still apply.

I kept this work on a separate branch so the published research snapshot can be compared with the proposed revision before anything is merged.

Reference: [Microsoft's power-setting callback documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-poregisterpowersettingcallback).
