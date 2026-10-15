# Sony PSX (DESR) emulation in PCSX2: parked research roadmap

**Status:** PARKED / IDEA ONLY  
**Created:** 2026-10-15  
**Target:** Sony PSX DVR / DESR series (the PS2-based Japanese digital video recorder), **not** the original PlayStation commonly nicknamed "PSX".  
**Intended home:** PCSX2, not a new standalone emulator.  
**Priority:** No active work. Do not create a separate implementation project just because this note exists.

## Decision and hard prerequisite

This branch and document exist solely so the idea is not forgotten.

**Do not start PSX/DESR emulation before PCSX2 MagicGate support is finished and validated.** In particular, the current work on SECRMAN, KELF processing, HDD authentication/security, and ordinary PS2 boot-flow correctness takes precedence. Finishing those tasks is useful to PCSX2 independent of any eventual DESR support.

This note does **not** authorize implementation work, external downloads of proprietary firmware, a test assignment, an open pull request, or a new recurring investigation. Resume only when explicitly requested.

## Desired eventual outcome

Support the Sony PSX DESR family as an **additional hardware profile** within PCSX2 while preserving ordinary PS2 behavior. The first meaningful success after foundational work would be booting the system software/XMB with correct hardware interactions; faithfully reproducing the DVR functions is a later, separate objective.

Avoid confusing:
- Original PlayStation ("PS1", historically nicknamed "PSX")
- The redesigned PS one console
- Sony's actual PSX digital-video-recorder hardware (DESR-xxxx)
- The PS2 hardware already emulated by PCSX2

## Why this may be feasible

Sony PSX uses substantial PS2-derived hardware and software. PCSX2 already supplies a large part of the necessary EE/IOP/GS/system-emulation foundation, but the DESR is **not** merely a PS2 BIOS skin. It adds distinct boot media, peripheral interfaces, storage/security behavior, and DVR components.

An independent emulator, [Iris](https://github.com/allkern/iris), documents preliminary DESR support merged into its main branch. Its README reports that it can run the DESR BIOS boot animation, then encounters error screens; it does **not** claim working XMB or complete DVR emulation. Its implementation is a useful future point of comparison, not proof that PCSX2 can simply flip a switch.

## Potential work areas (research hypotheses, not verified PCSX2 requirements)

| Area | Question to answer before implementation |
| --- | --- |
| BIOS / system identity | How does DESR BIOS detect and initialize its hardware, and where does normal PS2 behavior diverge? |
| Memory map / hardware profile | Which RAM sizes, mirrors, register ranges, timings, and console-type checks differ by DESR model/revision? Validate from hardware/software evidence rather than copying published numbers uncritically. |
| XFROM flash | How are onboard flash contents mapped, read, validated, and used during boot? |
| Storage | How do normal PS2 HDD partitions, DESR-specific storage/partitions, ATA behavior and the DVR side interact? |
| MagicGate / SECRMAN | Which real authentication, KELF loading, key-handling, and HDD security paths are exercised? Avoid unconditional success stubs that conceal missing behavior. |
| DEV9 / SPEED / DVRP | Which registers and commands communicate with the DVR processor, and which responses are required for normal system boot? |
| DVRP firmware | What functions run on the separate Fujitsu FR60-family controller and what must be emulated versus modeled as an interface? |
| XMB/system software | What are the verifiable transitions between boot stages, and what is the first blocking point? |
| DVR functions | Video input/recording/playback, tuner, and multimedia services belong to a later scope after initial system software boot. |

**Model caution:** The DESR family contains revisions with potentially meaningful boot/storage differences. Choose a specific target model only after comparing documented behavior, required dumps, and test coverage. First-generation DESR-5000/7000 is a possible starting candidate, **not** a committed target.

**DECKARD caution:** DECKARD is relevant to ongoing PS2 hardware-variation research, but the PSX is not a DECKARD-based PS2. Do not import assumptions about DECKARD into DESR hardware emulation.

## Prerequisite gates before un-parking

- [ ] MagicGate behavior in PCSX2 is implemented well enough to pass meaningful positive **and negative** authentication cases.
- [ ] SECRMAN/KELF-related behavior is validated against real IOP-module behavior and suitable software tests rather than guessed bypasses.
- [ ] Ordinary PS2 HDD boot/initialization and security workflows are reproducible; HDD Utility Disc and PlayOnline are useful test oracles where applicable.
- [ ] The relevant PS2 BIOS/IOP/EE boot flow is mapped sufficiently to distinguish a genuine DESR-specific divergence from a pre-existing PCSX2 deviation.
- [ ] There is time and explicit agreement to start another workstream.

The above are **gates to revisit the idea**, not a claim that they alone will make the DESR XMB boot.

## Possible staged approach, ONLY if resumed

1. **Establish evidence and test inputs.** Identify legitimately dumped DESR BIOS, flash and related components available for lawful testing; record model/revision, hashes, and provenance. Never commit proprietary dumps, keys, or copyrighted system software.
2. **Build a verified boot trace.** Document actual stage transitions from BIOS through XFROM and any HDD stages to the intended XMB entry point. The exact sequence and variations must be established from firmware/hardware traces, not assumed from this note.
3. **Compare Iris and PCSX2.** Read the current Iris DESR initialization implementation and compare PCSX2's console identification, memory map, DEV9/ATA access, and initial errors; record the smallest experimentally demonstrable differences.
4. **Add a gated DESR hardware profile.** Keep retail PS2 defaults and tests intact. Implement only required hardware differences with logging/diagnostics rather than premature broad stubs.
5. **Reproduce boot milestones.** Aim for a boot animation, then progress beyond initial errors, then correctly execute enough of system initialization to reach XMB. Record first divergent reads/writes/calls for each failure.
6. **Defer DVR multimedia features.** Tuner/recording/video hardware and complete DVRP behavior come after boot correctness and are separately scoped.

**Regression rule:** PS2 retail emulation must not silently change to accommodate the DESR. Prefer hardware-profile-specific behavior supported by dumps, traces, and tests.

## Reference starting points

These are research leads and are not guarantees of completeness or accuracy. Recheck upstream state when work resumes.

- [Iris emulator](https://github.com/allkern/iris): maintainer documents preliminary merged PSX DESR BIOS support, boot animation, and subsequent error screens.
- [Iris releases](https://github.com/allkern/iris/releases): preliminary PSX support and BIOS-based system detection listed in release notes.
- [dvrpwned](https://github.com/pcm720/dvrpwned): DVRP FR60 firmware/security research, HDD identification behavior, and reverse-engineering pointers.
- [desrdev/ghidra-fr60](https://github.com/desrdev/ghidra-fr60): Ghidra support aimed at the DVRP's Fujitsu FR60-family processor.
- [DESR software issues and model distinctions](https://gist.github.com/uyjulian/1be6201531ee62bd0fc4d87e574d2f6c): practical model-specific HDD, XFROM, and boot behavior, useful for forming test hypotheses.
- [wLaunchELF_R3Z](https://github.com/NathanNeurotic/wLaunchELF_R3Z): contemporary software supporting DESR-specific XFROM / DVR HDD devices.
- [PCSX2 fork](https://github.com/Masamune3210/pcsx2): eventual target repository.

## When this note is revisited

Check the status of MagicGate first. If it is not complete, **stop here**. If it is complete, reassess current PCSX2 and Iris code, available legal test images, relevant DESR research, and what work can be isolated without regressions. Update this document before writing implementation code.

**No work is presently in progress on Sony PSX emulation.**
