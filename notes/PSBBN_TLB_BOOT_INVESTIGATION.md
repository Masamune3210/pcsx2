# PSBBN / EE TLB and HDD Boot Investigation

Status: **note-working; research only; no emulator changes committed**
Created: 2026-10-17
Repository: `Masamune3210/pcsx2`
Base: `testing` at `82ec795368b6e57c46471a95929f33dd0f4f7eee`
Branch: `note-working/psbbn-tlb-boot-20261017`

## Objective

Investigate and eventually support Sony's PlayStation Broadband Navigator (PSBBN) in PCSX2, with correct EE TLB/MMU behaviour, HDD/DEV9/ATA operations, and boot/loader behaviour. Initial target: an English-localized BB Navigator environment from CosmicScale's **PSBBN Definitive Project**, but keep original Japanese PSBBN as an independent reference.

This branch is a parking place for reproducible research, instrumentation, implementation, and regression tests. **Do not substitute emulator-specific shortcuts for root-cause fixes without documenting them.**

## Relevant software

- [CosmicScale/PSBBN-Definitive-Project](https://github.com/CosmicScale/PSBBN-Definitive-Project)
  - English (and other language) localization and enhanced PSBBN v0.32 environment.
  - Full PSBBN/HOSDMenu install uses Sony PSBBN with patched system components, including `osdboot.elf` (CRC security check bypass) and **OSDMenu MBR**, a homebrew replacement for Sony's original MBR loader.
  - Installer operates on a PC-attached drive; Windows launcher uses WSL. **It formats the selected drive.** Never point it at a production HDD/image.
  - Current installation guide specifies minimum 32 GB drive; this is a *physical-hardware installer requirement*, not proof of any specific PCSX2 image size requirement.
  - Documentation: [README](https://github.com/CosmicScale/PSBBN-Definitive-Project/blob/main/README.md), especially “Install PSBBN and HOSDMenu,” “PSBBN,” “OSDMenu MBR,” and “Known Issues.”
- Original Japanese BB Navigator should be captured/tested separately where possible; the modified Definitive boot chain is not a byte-for-byte substitute for Sony's original one.
- Current fork has ongoing MagicGate/KELF and HDD work. Do not assume those paths are complete solely because individual decrypt/authentication tests succeed.

## Prior investigations and upstream references (as of 2026-10-17)

| Source | Evidence / significance | Status / caution |
| --- | --- | --- |
| [PCSX2 issue #11910](https://github.com/PCSX2/pcsx2/issues/11910) | PCSX2 lacks full guest-managed EE TLB support; PS2 Linux and PSBBN depend on it. | Open. |
| [PCSX2 issue #11746](https://github.com/PCSX2/pcsx2/issues/11746) | PSBBN boot failing in DEV9/ATA path; reported `ATA failed, -502` on a decrypted KELF-based boot attempt. | Open; older report, not a current reproduction on this fork. |
| [PCSX2 issues #11892](https://github.com/PCSX2/pcsx2/issues/11892), [#11918](https://github.com/PCSX2/pcsx2/issues/11918), [#11909](https://github.com/PCSX2/pcsx2/issues/11909) | Investigate unsupported HDD commands, HDD↔IOP DMA semantics, pending ATA IRQ handling. | Separate possible blockers; revalidate before attributing failures. |
| [PCSX2 PR #14847](https://github.com/PCSX2/pcsx2/pull/14847) | Proposed changes to guest ASID handling, refill/modified/address exceptions, delay-slot state and BIOS syscall interception; author reported reaching PS2 Linux interactive shell. | **Closed, NOT merged.** Review identified correctness/maintainability problems and rejected hacky logic. **Study as a hypothesis/reproducer only; do not blindly cherry-pick.** |
| [CosmicScale issue #388](https://github.com/CosmicScale/PSBBN-Definitive-Project/issues/388) | Maintainer noted in Dec 2025 that PCSX2 could not run PSBBN; commenters identified TLB and MagicGate/MBR boot requirements. | Historical compatibility evidence, not proof about the latest local fork. |

## Correctness-first investigation plan

### 0. Establish reproducible baseline
- Record exact fork commit, BIOS/ROM version, PCSX2 settings, HDD image geometry and format, PSBBN/Definitive Patch version, bootloader, and guest console region.
- Work on a **copy** of the HDD image; preserve original snapshots/hashes.
- Capture full log plus failing guest PC, COP0/TLB state, IOP/ATA operation and interrupts at first divergence.
- Compare original PSBBN and Definitive Project separately if both are available; identify patches that alter security, kernel, MBR, or launch sequence.

### 1. Distinguish boot stages
1. BIOS/OSDSYS HDD detection and system update/MBR selection.
2. MBR execution and HDD reads, ATA commands, DMA, and interrupts.
3. `osdboot.elf` or equivalent chain, KELF authentication/decryption where applicable.
4. PSBBN Linux 2.4.17-derived kernel loading and MMU/TLB setup.
5. Kernel transition to userspace and application/process switching.
6. PSBBN UI, channels, HDD filesystem, launch/return-to-menu behavior.

Record first *observable* failure at the correct stage. Avoid assuming every earlier successful return implies later successful boot.

### 2. EE MMU/TLB fundamentals to audit against hardware semantics
- Guest TLB entries: VPN2, ASID/global, even/odd page halves, PFN, page mask, permissions/valid/dirty, cached/uncached treatment.
- Guest `EntryHi`/`EntryLo`/`PageMask` and TLB instructions (`TLBR`, `TLBWI`, `TLBWR`, `TLBP`), including replacement and ASID/global effects.
- Correct virtual-to-physical translation across kernel and userspace address segments.
- Separate address-space mappings across process switches; no stale VTLB/recompiler mappings.
- Exceptions: TLB refill, invalid, modified, address error; `BadVAddr`, `Context`, `EntryHi`, `Cause`, `EPC`, `Status`, delay-slot (`BD`) semantics and exception vectors.
- JIT/recompiler fastpaths, TLB invalidation, exception dispatch, and interpreter/recompiler equivalence.
- BIOS syscall HLE dispatch must not collide with legitimate Linux/PSBBN syscalls. Fix specific dispatch correctness, not via a Linux-process string or heuristic special case.

### 3. Prove each layer
- Run isolated guest TLB instruction/exception microtests first, including ASID context switching, global entries, dirty/invalid mappings and branch delay slots.
- Run compatible non-Linux PS2 regressions and existing emulator test suites.
- Try unmodified PS2 Linux boot (when available) as an independent MMU/userland exercise, with a trace to its first failure.
- Then try PSBBN; separate DEV9/ATA and MagicGate failures from MMU/TLB failures.
- Ensure passing outcomes reproduce after a cold boot with no savestate shortcuts.

## Guardrails

- This branch has **no implementation yet**. PSBBN and PS2 Linux are **not** claimed working.
- Do not merge broad PR #14847 wholesale; only adopt individually verified concepts backed by hardware/architectural evidence and regression tests.
- Prefer fixing shared emulator inaccuracies, not modifying Sony software just to avoid them; modified Definitive Project boot stages must be called out.
- Keep detailed logs only in local test artifacts as appropriate; do not commit BIOS files, licensed system executables, keys, or disk images.
- Rebase on `testing` when beginning implementation, preserving an identifiable experimental baseline.

## First real PSBBN 0.32 boot trace (2026-10-17)

**Observed:** Original Japanese PSBBN 0.32 installer reported completion of BB Unit initialization and installation. Installer disc was subsequently removed; the recorded restarted boot stayed on the PlayStation 2 logo. Evidence below is from the user's approximately 1,057,412-line PCSX2 trace of that session, **not yet a controlled fresh-power-on trace**. The drive being tested in the recorded boot was a disposable 40 GiB `bbntest.raw` image. The BIOS was USA v1.60 (2002-02-07), a potential separate compatibility variable.

### Ordered boot evidence

1. Initial OSDSYS HDD detection attempt at about `653.7648` logged `HDL:read 0 sec`, `ATA: C_READ_SECTOR`, `cannot alloc memory`, then `new power off`. This was **earlier in the session, while the installer disc was involved**, so do not conflate with the later boot path.
2. After the disc was removed at approximately `1162.872`, later restart at `1243+`:
   - `1245.1040`: `loadmodule: fname rom0:HDDLOAD args 28 arg -osd`
   - `1245.6353`: `HDL:read 0 sec`
   - `1245.6875`: `HDL:securiyt decript.` (verbatim guest message)
   - `1247.3923`: `HDL:END ver.1.00`
   - `1247.6971`: loads `rom0:UDNL` with `img0:` argument (IOP image boot stage).
   - `1249.8776`: `Input ELF format filename = pfs0:/p2lboot/osdboot.elf`
   - `1250.3924`: `Loaded, pfs0:/p2lboot/osdboot.elf`
   - Subsequent IOP reboots and HDD/PFS activity continue through `1253+`.
   - `1251.7260`: `pfs: warning: null buffer returned` (potential separate issue, not yet causally connected).
3. `1253.0593` through `1253.0618`: 50 logged EE **store** TLB misses at the same `pc=0x801a4930`, addressed sequentially `0xc0005000`, `0xc0005004`, ... `0xc00050c4`. This address range is in guest TLB-mapped KSEG2. The log does not prove which guest mapping or instruction should have been used.

**Interpretation:** This run moves well past mere BIOS HDD recognition and visibly loads `osdboot.elf` from the installed HDD. Do **not** state that it necessarily boots the final PSBBN Linux kernel or that TLB is the sole cause of the hang. The final TLB messages are a strong direct lead.

### Concrete emulator-source finding

Checked `pcsx2/vtlb.cpp` on fork `testing`, around lines 514–550, at the time of this audit:

- `vtlb_Miss(u32 addr, u32 mode)` on the **interpreter** path (`Cpu == &intCpu`) invokes `cpuTlbMissW` or `cpuTlbMissR`, then `CancelInstruction()`.
- On the **recompiler** path it constructs the `TLB Miss, pc=...` message and, when `PauseOnTLBMiss` is enabled, pauses/raises a host debugger report. Without that setting, it logs and returns **without delivering an equivalent guest TLB exception** at that point.
- The logger uses `static int spamStop` and emits at most **50 errors** in a normal non-development build. Therefore the 50 visible misses are a logging cap, **not evidence the misses stopped**.

Relevant exact source: [fork vtlb.cpp on testing](https://github.com/Masamune3210/pcsx2/blob/testing/pcsx2/vtlb.cpp).

### Minimal discriminating next tests

1. Preserve an untouched backup/hash of the installed test HDD. Record full executable hash, BIOS, region, settings, and boot origin.
2. Run **from a full cold boot with no disc**, on a copied HDD image, first using existing EE recompiler then **EE interpreter**, with identical settings except the engine. Avoid introducing new emulation hacks for this comparison.
3. Enable **Pause on TLB Miss** for the recompiler and capture, at the **first** miss, guest PC/instruction or recompiled block, general registers, COP0 `EntryHi/EntryLo0/EntryLo1/PageMask/Index/Status/Cause/EPC/BadVAddr`, and emulator VTLB state. Check whether the expected guest TLB entry was installed and whether its ASID/global, valid and dirty flags permit the write. Note: a single logged PC can be block-level/stale for recompiler mid-block faults, per existing `vtlb.cpp` comment.
4. If interpreter proceeds beyond this point, compare first divergent guest operation and exception behavior. If interpreter also fails, check missing/invalid guest mappings, earlier exception registration, invalid page mask handling, and BIOS-vs-original-Japanese region mismatch.
5. Revisit `pfs: warning: null buffer returned` only if trace/differential tests connect it to the boot failure. Do not assume the log proves a broken HDD filesystem.

**State:** Documentation only. No fixes, workarounds, emulator binaries, keys, BIOS, or game content committed.

## First next step

Collect a **non-destructive baseline run** of PSBBN (Definitive Project) on the current fork using a disposable HDD image, identifying whether the first failure occurs in HDD/ATA/MBR/KELF or at Linux EE TLB initialization. In parallel, inspect PCSX2's existing TLB logic and review closed PR #14847 *line by line* to extract hypotheses for testable fixes.
