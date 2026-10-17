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

## New evidence: Sony vendor ATA security control and DESR DMA gating (2026-10-17)

**Source:** User-provided PS2/PSX hardware documentation excerpt headed "Sony custom ATA commands", with links to [DVRP](https://www.psdevwiki.com/ps2/DVRP) and [CXD9764GP](https://www.psdevwiki.com/ps2/CXD9764GP). Treat it as a hardware-research lead to validate against DESR firmware and a specific model, **not** an instruction to modify retail PS2 emulation immediately.

### Two different 0xEC operations

The ATA **command** register and the ATA **Features** register have different roles:

| ATA command register | ATA Features register | Meaning |
| --- | --- | --- |
| `0xEC` | N/A | Standard ATA IDENTIFY DEVICE. |
| `0x8E` | `0xEC` | Sony vendor command `ATA_C_SCE_SECURITY_CONTROL` / subcommand `ATA_SCE_IDENTIFY_DRIVE`. Returns a **512-byte Sony HDD identity/security sector**. |
| `0x8E` | `0xF1` | `ATA_SCE_SECURITY_SET_PASSWORD`. |
| `0x8E` | `0xF2` | `ATA_SCE_SECURITY_UNLOCK`. |
| `0x8E` | `0xF3` | `ATA_SCE_SECURITY_ERASE_PREPARE`. |
| `0x8E` | `0xF4` | `ATA_SCE_SECURITY_ERASE_UNIT`. |
| `0x8E` | `0xF5` | `ATA_SCE_SECURITY_FREEZE_LOCK`. |
| `0x8E` | `0x20` | `ATA_SCE_SECURITY_READ_ID`. |
| `0x8E` | `0x30` | `ATA_SCE_SECURITY_WRITE_ID`. |

**Do not conflate** a log line saying `ATA: C_IDENTIFY_DEVICE` with `DEV9: HDD_SCE SONY-SPECIFIC SECURITY CONTROL COMMAND ec`; the latter means the `0x8E` command was issued with Features = `0xEC`. Earlier generic descriptions calling that Sony-specific command standard IDENTIFY DEVICE were incorrect.

### DESR-specific requirements stated by the source

- Official Sony ATAD, including the copy used by the DESR DVRP, requires the Sony `0x8E/0xEC` request to **complete without timing out or returning an error**. For that basic ATAD acceptance test, the excerpt says the Features subcode need not be specially interpreted and **a zero-filled 512-byte response is sufficient**. This does *not* establish that every PSX/DESR firmware stage accepts null identity fields; other consumers may interpret the returned sector.
- The PSX/DESR has an HDD-aware **CXD9764GP**. According to the source, the `0x8E/0xEC` identification is required before that controller enables HDD **DMA commands**, which in turn affects boot. This hardware permission/gating must be modeled separately from the ATA command's response if confirmed in a target BIOS/DVRP trace.
- A modified DVRP firmware can reportedly desynchronize the CXD9764GP into allowing DMA without the proprietary command. This is a research counterexample, **not** a replacement for normal hardware behavior in a faithful emulator.
- The **DESR-x500 and DESR-x700** families require the DVR partition to boot, and the source says the DVR partition needs the **48-bit ATA command set regardless of HDD capacity**. As a result, even a 40 GB HDD that suffices for a standard PS2 HDD Utility setup is *not* necessarily bootable in those DESR models.
- PS2SDK's homebrew ATAD doesn't require Sony's proprietary HDD identification; software such as OPL may therefore work with ordinary ATA disks. Passing a homebrew ATA workload is not enough to establish support for official ATAD/DVRP.

### Current PCSX2 fork: confirmed implementation gaps (inspected against `testing` on 2026-10-17)

These are **existing code paths**, not proposed speculative changes:

- `pcsx2/DEV9/ATA/Commands/ATA_Command.cpp`: `IDE_ExecCmd(0xEC)` calls ordinary `HDD_IdentifyDevice()`; `IDE_ExecCmd(0x8E)` calls `HDD_SCE()`. The opcodes are already correctly separated.
- `pcsx2/DEV9/ATA/Commands/ATA_SCE.cpp`: `HDD_SCE()` branches on `regFeature`. It implements the `0xEC` feature via `SCE_IDENTIFY_DRIVE()` and transfers **512 bytes** of `sceSec` through PIO. The other listed vendor-security subcommands (`0xF1–0xF5`, `0x20`, `0x30`) **abort as unimplemented**. Do not replace them with blanket success without a real firmware use case.
- `pcsx2/DEV9/ATA/ATA_State.cpp`: the emulator loads a raw `.hddid` identity override derived from the HDD image filename if it exists and is no more than 512 bytes; otherwise synthesizes a Sony-branded 512-byte sector (default `SCPH-20401`, vendor string, hardcoded internal ID fields). `ATA.h` holds `sceSec[512]`. This is **not** yet a fully modeled per-console/per-drive identity or DESR security state machine.
- `ATA_State.cpp::Open` sets `lba48Supported` based on the HDD image **size** exceeding the 28-bit LBA limit. `ATA_Command.cpp` gates multiple 48-bit operations on that flag. For a **40 GB DESR HDD**, the present heuristic would disable the 48-bit command set even when an actual compatible drive would support it. This is a concrete future DESR boot issue. Avoid changing the retail PS2 default just to solve it; a verified hardware-profile capability is preferable.
- PCSX2 currently does **not** emulate the described **CXD9764GP conditional DMA authorization** as an independently validated DESR-specific behavior. Supporting an ordinary Sony ID-sector response alone may not suffice to reach the XMB.

### Minimal future tests when this idea is explicitly resumed

1. Pick a documented DESR model/revision and obtain lawful, hashed BIOS/XFROM/DVRP/drive evidence. Note whether the target is x500/x700 before selecting an HDD image.
2. Trace the first `0x8E` security command: log **both command and Features registers**, result/error flags, whether the 512-byte sector is consumed, and subsequent CXD9764GP DMA enable state. Cross-check against the actual firmware.
3. Compare a response of all zeros with the existing synthetic Sony sector and a user-supplied `.hddid` on disposable images. Distinguish "ATAD continues" from "DVR boot completes" and "content decrypts."
4. Independently model/advertise **48-bit ATA support** from the emulated drive's capability or DESR hardware profile rather than inferring it purely from disk size; test a 40 GB image *with* advertised 48-bit support separately from the normal PS2 profile.
5. Only add vendor-security subcommands or CXD9764GP gating once captured DESR commands require them. Preserve ordinary PS2/HDD Browser/MagicGate regressions and do not silently stub success for unsupported operations.

**Current scope:** update research priorities and test criteria only. This evidence does not un-park DESR emulation, authorize changes to the `testing` branch, or replace the ongoing DVD Player installation debugging.

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
