# Dark Rvbbit

### Experimental Windows Hypervisor Security Research Project

**Windows x64 / Intel VT-x research project**  
**Research period:** May 1 – October 13, 2026  
**Status:** Experimental, unfinished research snapshot. No verified release build, stable runtime, or working-driver claim.

I started Dark Rvbbit with a fairly specific question: how much of a Windows system's view of memory and hardware is shaped by what happens below the kernel? That question led me from basic VMX setup into EPT, VM-exits, timing, interrupt handling, and eventually a few experiments I never managed to finish properly.

This is where I stopped. I'm publishing the source because I think the questions and the code are still worth looking at, even with the rough edges. **It is not a ready-to-run hypervisor or a demonstration that any concealment technique works.**

## The parts I find most interesting

**Memory has more than one story.** The EPT work looks at page translation and splitting large mappings into smaller ones. The DKOM experiment approaches kernel data structures from a different angle, using shadow-page ideas. I wanted to see how those pieces fit together. They do not form a complete or reliable implementation in this snapshot.

**Time is not just the TSC.** Once I started looking at timing behavior, APIC timers became part of the same problem. The repository has separate TSC, APIC-timer, and APIC-virtualization paths because I was interested in how the guest's different time sources relate to each other. I did not reach a point where I could publish trustworthy consistency measurements.

**The edges matter as much as the VMX core.** There is a separate control driver, a user-mode tool, a PE inspection script, and an unfinished UEFI experiment. They show the broader direction of the research, not a complete end-to-end system.

None of these individual techniques is new. What may be useful here is seeing several related low-level questions explored in one codebase, including where the approach breaks down.

## What is in the repository

| Path | What to expect |
| --- | --- |
| `driver/hv_main.c`, `hv_vmx.c`, `hv_asm.asm`, `hv_exit.c` | VMX/VMCS setup, entry/exit scaffolding, and exit dispatch logic. Not validated as a stable launch/unload path. |
| `driver/hv_ept.c`, `hv_dkom.c` | EPT mapping experiments and shadow-page/DKOM research. Contains known correctness issues. |
| `driver/hv_tsc.c`, `hv_apic.c`, `hv_apicv.c`, `hv_msr.c` | Timing, interrupt, and MSR-related experiments. Several paths are incomplete. |
| `control/`, `tool/` | Kernel control interface and a small user-mode client. The interface has not been security-reviewed for deployment. |
| `tools/winload_scanner.py` | A static PE inspection helper. Its results and compatibility have not been validated. |
| `uefi/` | An unfinished DXE experiment, not a working boot-time integration. |
| `Makefile`, `build.bat`, `*.inf` | Original build and packaging inputs. Not a reproducible release pipeline. |

A reasonable reading order is `hv_main.c` → `hv_vmx.c` → `hv_exit.c`, then the EPT and timing modules. I would leave `uefi/` until last: it is mostly a record of an unfinished direction.

## What is unfinished or unverified

I would rather be explicit about this than leave someone else guessing:

- **Build:** I cannot provide a verified, successful end-to-end build of the drivers and tool. There are source/build problems beyond simply choosing the right Intel CPU. In particular, `driver/hv_dkom.c` refers to a structure field that is not declared in `driver/hv.h`.
- **EPT correctness:** parts of the mapping code treat physical-address values as if they were directly usable virtual pointers. Memory allocation and failure paths also need proper review. I would not trust these paths on a live system.
- **DKOM:** the shadow-page experiment is unfinished. Entries that cross page boundaries are not handled reliably, and cleanup/error handling needs work. This is not a proven concealment mechanism.
- **APIC and timing:** APIC MMIO emulation is incomplete; I have no reproducible measurements establishing correct timer behavior or consistency with the TSC path.
- **Lifecycle and portability:** launch, per-processor setup, rollback, unloading, and behavior across Windows builds and Intel CPU generations have not been validated as a whole.
- **UEFI:** the embedded driver blob is empty, the checked-in winload offsets are placeholders, and there is no complete, tested boot integration.
- **Control interface:** the IOCTL boundary and permissions need a separate security review before any attempt to use the project beyond a disposable lab.

There may be other bugs. This is a research snapshot, not the outcome of a complete verification effort.

## Build status and compatibility

The supplied build files were written with **Visual Studio 2022** and **Windows Driver Kit 10.0.22621+** in mind. The original build entry point is `nmake /f Makefile`, but **that command is not a confirmed working build**. An appropriate Windows development environment is necessary; it is not sufficient to solve every issue listed above.

No compiled binaries, signatures, hardware compatibility matrix, or reproducible successful runtime logs are included. I have not established working support for any particular Windows release, Intel CPU model, or processor count. The earlier research log contains observations from experiments, not a tested-support list.

I do not recommend loading these drivers or the UEFI component on a real machine. If you study the code, keep it to static review or a purpose-built, isolated lab with recovery options.

## Why publish it unfinished?

From May to October 2026 I kept returning to the same set of problems, and the project grew beyond the original experiment. Some paths were promising, others were fragile, and a few are clearly incomplete. At this point I would rather share the work as it stands than make it sound more finished than it is.

If you're familiar with Windows internals or hardware virtualization, I hope the source is useful as material to question, review, or learn from. Please treat it as **an investigation with documented gaps**, not as a drop-in foundation that only needs minor hardware-specific changes.

[Changelog.md](Changelog.md) is the dated research log. It records the direction of the work, including setbacks, but is not independent proof that the reported experiments are reproducible.

## Scope

For education, code review, and authorized security research only. This repository contains low-level code that can affect kernel memory and system startup. No guarantee of correctness, stability, stealth, or fitness for deployment is made.

## Author

buter-chkalova

## License

MIT License. See [LICENSE](LICENSE) for the full text. This license permits reuse and modification with attribution; it does not imply that the code is safe to run or that any behavior is validated.
