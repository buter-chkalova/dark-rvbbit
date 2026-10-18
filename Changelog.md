# Dark Rvbbit — research log

**May 1 – October 13, 2026**

This is a cleaned-up version of my development notes, kept in date order. It shows what I was working on and what went wrong along the way. The entries are not a benchmark, a compatibility list, or independently reproduced test results. The October 13 date marks the end of this research snapshot, not a successful release.

## October 13, 2026
- Reviewed the exception-handling path around the control driver's VMFUNC calls after earlier crashes in the test environment.
- Tightened the image-identification check used in the winload experiment. My original notes mentioned Windows 22H2 and 23H2; I am **not** claiming verified compatibility with either build today.

## October 9, 2026
- Worked on APIC timer calculations. The divider encoding was easier to misread than I expected, and an early version ran far too fast.
- Comparing timer behavior across environments helped me spot that discrepancy, but the current source is still not a validated timer implementation.

## September 28, 2026
- Continued the shadow-page/DKOM experiment. The single-page idea was easier to reason about than the multi-page case.
- The multi-page path remains incomplete, particularly around entries crossing page boundaries and cleanup after failures.

## September 14, 2026
- Experimented with changes to TSC offset and multiplier behavior. Offset-only adjustments had not answered the timing questions I was trying to explore.

## August 22, 2026
- Revisited MSR bitmap handling and guest writes to the feature-control register.

## August 5, 2026
- Changed the identifying condition in the experimental VMFUNC-related exception path after an unexpected match.

## July 19, 2026
- Reworked EPT-violation handling after crashes in the instruction-fetch path. This area still needs careful validation.

## July 2, 2026
- Added VPID-related work and reviewed invalidation behavior. No reproducible performance benchmark is included with this snapshot, so there is no speedup claim here.

## June 15, 2026
- Traced a stack problem in the early VM-entry/exit work.

## June 1, 2026
- Started breaking down EPT large-page mappings. Earlier address calculations had problems, and the research code ended up using a 64 GB initial mapping ceiling.

## May 15, 2026
- First VM-exit round-trip experiments, mainly around CPUID handling. These notes do not establish that the final snapshot can launch successfully.

## May 1, 2026
- Started with VMCS state and the launch path. Guest segment and task-register state were immediate stumbling blocks.

---

**Final note:** I stopped here. The code is left available for inspection, with its limitations stated plainly in [README.md](README.md). No functional fixes or new test results are implied by these documentation edits.
