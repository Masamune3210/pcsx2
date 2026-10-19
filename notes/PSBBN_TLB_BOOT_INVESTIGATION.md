# PSBBN / EE TLB and HDD Boot Investigation

Status: **note-working; local interpreter traces acquired with additional experimental patches; general syscall fix and EE JIT MMU parity pending**
Created: 2026-10-17
Repository: `Masamune3210/pcsx2`
Base: `testing` at `82ec795368b6e57c46471a95929f33dd0f4f7eee`
Branch: `note-working/psbbn-tlb-boot-20261017`

## CACHE IHIN interpreter observer and JIT gap (2026-10-19)

**Source review and implementation committed:** PCSX2 EE `CACHE 0x0B` is **IHIN (instruction-cache hit invalidate)**, documented in the EE Core Instruction Set Manual v6.0, page 308. The uploaded capture10 logged 14,202 generic unsupported-CACHE warnings during 138.4629–142.0333s, but the kernel continued executing to at least 187.4967s. The warnings are not evidence of a permanent CPU hang.

The EE interpreter fetches via `memRead32(pc)` and has no separate emulated instruction-cache lines. In this coherent-instruction-fetch model, IHIN has no distinct line to invalidate; previous missing `case 0x0b` generated a warning for every operation. The new `Cache.cpp` case recognizes IHIN and calls a **bounded diagnostic observer**. The observer in `COP0.cpp` uses existing COP0 opt-in tracing, resets on VM/TLB observer reset, and captures exact interpreter PC, effective address, instruction opcode and CP0 data (first32 + powers of two to65536; at most43 records). No guest register, TLB, cache tag, or memory value is changed by this observer.

Commits: `14dec034` (header), `6faf3112`/`f365bdd3` (observer), `1480554f` (IHIN dispatch). **9/9 static source checks passed; no Windows/MSVC build or subsequent runtime of this latest change has been verified.**

**Do not misstate the JIT status:** `pcsx2/x86/iR5900Misc.cpp::recCACHE()` remains a no-op. Real instruction-cache coherence for recompiled guest code and the TLB/MMU exception paths still require architecture-level implementation and regression tests. A one-line `Cpu->Clear(guest_vaddr)` without mapping/alias reasoning is not an acceptable completion.

Research details and next implementation criteria: [EE_CACHE_IHIN_ANALYSIS_20261019_v1.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/EE_CACHE_IHIN_ANALYSIS_20261019_v1.md). The original capture10 log is still a conversation attachment, with compressed hash recorded in that branch.

## Latest cross-repository checkpoint: 0008 interpreter run (2026-10-19)

**Research/output source:** [PSBBN capture10 analysis](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/POST_0008_RUNTIME_CAPTURE_20261019_v1.md), derived from the user's 26,256,899-byte pasted runtime log (SHA256 `62ffce05973be78c910c73d3b10580dd5712d2ec5970cac8227c35b18431cc07`). This commit updates branch documentation only; **it does not apply experimental 0007 or 0008 code to the PCSX2 source on this remote branch**. The log reports base revision `4621a080b` despite the local working tree including further patches; do not infer which patch code was committed from that version string.

The locally executed experimental `0008` Linux-O32 guard logs 16 `abi=linux-o32` syscalls, including Linux `open` at `0x0FB77E2C`, `v0=4005`, `v1=2`. Unlike capture9, the new capture has no mistaken host `SetGsCrt` after Linux startup. The legitimate earlier BIOS `SetGsCrt` at `0x0102E424` still occurs. Heartbeats continue to 187.4967 seconds, and there are 14,202 `Cache mode b not implemented` warnings starting at 138.4629s. Neither a completed PSBBN boot nor the final screen outcome was established from this report.

**Interpretation and branch policy:**
- The BIOS-v1 vs Linux-v0 syscall ABI collision and erroneous host hook are established. Patch `0008` suppresses it in one tested Sony kernel by inspecting 13 hardcoded kernel words. It is **an experiment, not the root-cause/general design**, and must not be merged into `testing` as-is.
- Native syscall *handler ownership* must eventually determine when BIOS host-side interception is legitimate; preserving user-mode genuine BIOS requests matters.
- A proper EE JIT TLB exception path remains a separate requirement. Interpreter progress is not full compatibility and no demonstrated JIT fix exists.
- Investigate the issuing guest PC/sub-op for CACHE mode B with bounded instrumentation before changing any cache semantics. Warning count alone does not establish causality.
- **Canonical future changes/research** belong on the decomp research output branch until a general emulator correction is designed and specifically authorized for this test branch. Do not duplicate or silently reapply patches already present in a local checkout.
- The complete raw capture is currently conversation-attached rather than committed to GitHub; its analysis and checksum are recorded in `ps2bbndecomp`.

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

## Interpreter Random/Wired compile correction (2026-10-18)

User's local Windows/MSVC compile of the `0006` interpreter Random/Wired candidate reported `C2065 cpuRegs undeclared`, `C2737 wired const must be initialized`, and `C2530 random reference must be initialized` at `pcsx2/COP0.h:22–25`. The actual cause is that the `__fi COP0_AdvanceRandom()` inline **function body lived in COP0.h**, which can be included before `cpuRegs` is declared. The latter two messages are cascading parser errors, not separate uninitialized variables.

Fix: **move the exact same 13-line inline implementation into `pcsx2/Interpreter.cpp`** (immediately after `static int branch2`), and remove it entirely from `COP0.h`. `Interpreter.cpp` already sees `cpuRegs`; keeping `static __fi` preserves the interpreter hot-path inlining instead of adding an out-of-line call per guest instruction. `COP0.cpp` still holds the `MTC0 Wired` reset/read-only `Random` behavior and `R5900.cpp` still initializes Random to 47. The runtime model and TLB behavior are not intentionally changed by this placement fix.

The same placement correction is committed to `ps2bbndecomp/patches/pcsx2/0006-interpreter-random-wired-candidate.patch`. Its five hunk pre/post counts and source-context matches were mechanically checked against the original PCSX2 `testing` files (zero errors). **No local Windows/MSVC build was performed in this session; a user rebuild remains necessary.** Do not reapply the complete 0006 patch over a branch already containing it; update the branch instead.

## PSBBN bounded observer build fix (2026-10-18)

The opt-in TLB diagnostic observer from `ps2bbndecomp/patches/pcsx2/0001-psbbn-bounded-tlb-diagnostics.patch` has been applied to this branch. MSVC reported:

```text
pcsx2/COP0.cpp:87: error C2065: 'VTLB_PAGE_BITS': undeclared identifier
```

**Root cause:** `VTLB_PAGE_BITS` is declared inside the `vtlb_private` namespace in `pcsx2/vtlb.h`, which `COP0.cpp` already includes. The observer's raw vmap index referenced the constant without that namespace.

Applied exact correction:

```cpp
const auto mapping = vtlb_private::vtlbdata.vmap[0xc0005000u >> vtlb_private::VTLB_PAGE_BITS];
```

The same correction was committed to `Masamune3210/ps2bbndecomp` on `note-working/psbbn-ghidra-analysis-20261018` in the original diagnostic patch, so new checkouts will not reintroduce it. All adjacent `VTLBVirtual` uses were checked against `vtlb.h` and have valid signatures (`isHandler(u32)`, `assumeHandlerGetID()`).

**Verification:** Source/header checked; no PCSX2 Windows/MSVC build run in this session. User should rebuild the same branch and report any next compiler diagnostics. The fix changes a compile-time name lookup only, not TLB behavior or observer semantics. Do not infer successful PSBBN boot from compilation.

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

### Interpreter differential: repeated TLBWR (2026-10-17)

Switching **EE recompiler off** replaces the visible recompiler store-miss sequence with an apparent fast repetition of:

```text
[ 1802.0182] COP0_TLBWR 0:0,fb88101,0,0
```

The exact same five fields repeat hundreds/thousands of times in the user's reported log excerpt. This output is printed unconditionally on the interpreter-side `R5900::Interpreter::OpcodeImpl::COP0::TLBWR()` code path, `pcsx2/COP0.cpp`, **even though the line is routed through a development-console warning and not itself an error return**.

The fields come from source as `Random:PageMask,EntryHi,EntryLo0,EntryLo1`. So the observed state is `Random=0`, `PageMask=0` (4 KiB pages), `EntryHi=0x0FB88101`, and both `EntryLo` registers `0` (neither page valid). TLBWR picks `j=Random&0x3F`; all reported calls target index 0 with those values, and `MapTLB` maps only if an EntryLo valid bit is set. An invalid TLBWR *can be normal* (invalidation or flush), so this alone does not establish the causal failure.

**Do not assume** this is the same fault/PC as the recompiler's stores at `pc=0x801A4930` and `addr=0xC0005000...`. The interpreter trace currently lacks guest PC, COP0 `EPC`/`Cause`/`Status`/`BadVAddr`, `Wired` and associated transitions. There may be repeated guest-exception entry, incorrect `Random` register evolution, or an independent initialization/invalidation loop. Need evidence, not a workaround.

Relevant exact code:
- [COP0.cpp TLBWR / WriteTLB / MapTLB](https://github.com/Masamune3210/pcsx2/blob/testing/pcsx2/COP0.cpp)
- [R5900.cpp cpuTlbMiss and cpuException](https://github.com/Masamune3210/pcsx2/blob/testing/pcsx2/R5900.cpp)

**Next diagnostic:** From the earliest repeated TLBWR in the *interpreter* run, capture guest PC and decoded nearby instructions, `Random`, `Wired`, `EntryHi`, both `EntryLo` values, `Index`, `Status`, `Cause`, `EPC`, `BadVAddr`, plus the preceding TLB exceptions and changes. Verify whether Random should move and whether the guest code is attempting to initialize/flush entries or service recurring faults. Collect only bounded samples, not another million lines.

### Recovered PSBBN v0.32 osdboot ELF and second stage (2026-10-17)

Using the user's extracted `hdd0:/__system/p2lboot/osdboot.elf` (KELF) **plus the full verbose PCSX2 MagicGate log**, reconstructed the authenticated plaintext directly from the decrypted BIT and `SCMD 0x8E` output. **No guesswork and no console keys needed to reproduce from this log.**

Original KELF: 1,097,904 bytes, SHA256 `164a607b2496c6079f9d299584be1820aef59141985b1ede05207855698d376f`.
Parsed PS2SDK `SecrKELFHeader_t` (LE): content_size=0x10BD00, KELF_header_size=0x03B0, unknown5=0x0B00, flags=0x022C, BIT_count=0, mg_zones=1 (Japan). Decrypted BIT table at ~1248.435 in user's trace has block_count=53, table length 856 bytes, content length 1,096,960 bytes. Descriptors completely cover content without gaps: 29 flagged=3 blocks of 6,128 total protected bytes, 24 flagged=0 unprotected blocks of 1,090,832 bytes. The logged verified content plaintext totals exactly 6,128 bytes for this KELF. Applying it to protected blocks, and retaining unprotected bytes directly, produces an actual `ELF32 MIPS little-endian EE` recognized by `file` and `readelf`.

Recovered EE ELF: size=1,096,960, SHA256 `ce10a21c5c741fcc4b0a4f4561e8df516558a1b6d1a3ffa7d09af3a794c72bed`; entry=0x00800000; main PT_LOAD=0x00800000, size=0x10A9BC; secondary PT_LOAD=0x0090AA00. Matches the guest `loadelf` output in the original trace. Do not load the original KELF directly into Ghidra.

The EE ELF's `.text` is only 0x258 bytes. Disassembly reveals an additional **XXTEA-style self-decrypting second stage** using a key stored in its own `.data` (NOT a MagicGate key); delta 0x9E3779B9 from `.sdata`, ciphertext [EE 0x00800290,0x0090A990), 272,832 LE u32 words, six XXTEA rounds. Bootstrap copies this plaintext to 0x01000000 and jumps to it.

Recovered raw second-stage EE image: 1,091,328 bytes, SHA256 `423e87063db94a01c425bfb67719a2410c1d27fa36fbe08fbadb1c3f5bf85eff`. **Ghidra import as raw R5900 little-endian, guest base 0x01000000**.

Two complete, valid IOP IRX module ELFs (MIPS little endian e_type 0xFF80, with relocation sections intact) were carved from stage 2:
- stage2 offset 0x63B30, mapped position 0x01063B30, size 275,200 bytes, SHA256 `8e1751b9421c9f05d59716c5a2ff1398320122a5c3e682028da5b397c460989a`.
- stage2 offset 0xA6E30, mapped position 0x010A6E30, size 310,832 bytes, SHA256 `bcf8b05b3263c4427638a9859f299bd61cff186217d9e8f164ac2688d1577e4d`.
Import these IRXs using the separate IOP loader (not EE); both contain text pointing to `IOPBTCONF`.

PS2 Linux strings are visible in the second-stage image, including the original memory-card startup warning.

**Still unresolved:** the repeatedly faulting guest PC `0x801A4930` (physical alias 0x001A4930) is outside the recovered stage1 main LOAD, and is not inherently at the raw stage2 base. Map the code's actual load chain before assigning that PC to a named binary. Do not assume completing KELF/XXTEA extraction solves the TLB issue.

A separate pure-Python reproducibility helper `recover_psbbn_osdboot.py` was generated in this conversation, taking the extracted KELF and the PCSX2 log and generating the four files; code and commercial binary dumps have **not** been committed.

### User Ghidra analyses and next-stage kernel discovery (2026-10-17)

The user supplied four Ghidra 12.1.3 `.gzf` projects, preserving completed analysis. The packed Ghidra DBs were inspected for persisted metadata and symbols; source binaries were cross-checked independently with ELF parser/LLVM disassembly. These are **not** headless Ghidra analysis results or a decompilation audit.

Import architectures were correctly set:
- `osdboot.decrypted.elf`: `r5900:LE:32:default` (ELF loader).
- `osdboot.stage2.bin`: `r5900:LE:32:default` (raw binary loaded at `0x01000000`).
- `osdboot.stage2.iop1.irx` and `osdboot.stage2.iop2.irx`: `MIPS:LE:32:default` (IOP IRX ELF loader).
Both IRX projects record `Relocations applied=82`, `partial=11`, `failed=0`, `unknown=0`, `unsupported=0`, `import tables=4`, `export tables=0`. `readelf` independently finds 86 `rel.text` plus 7 `rel.rodata` entries = 93 records per IRX, so 82+11 accounts for all relocation entries. Review the 11 partial cases before assuming perfect cross-references.

**Unexpected shared loader:** Both IOP IRXs have identical 0x1640-byte `.text` layout, with only SIX bytes different between text sections; their 0x50-byte `.rodata` sections match byte-for-byte. The executable ELF entry is 0xE00 in both. The substantive differences are in large `.data` payloads (0x416D0 and 0x4A200). These appear to be *shared IOP loader/bootimage variants with different embedded payloads*, not unrelated IRX drivers. Treat that interpretation as a working model until their runtime unpacking is traced. The Ghidra EE raw stage2 project detects some apparent R5900 functions *inside the embedded IRX regions*; these must not be conflated with independently analyzed IOP functions.

**Important discovery: next kernel pathname in stage2.** Offset `0x103970` of the raw second stage (guest address `0x01103970`) contains the literal null-terminated `pfs0:/p2lboot/vmlinux`. Raw disassembly at guest `0x01000460`–`0x01000468`:

```asm
0x01000460: lui   v0, 0x0110
0x01000464: addiu v0, v0, 0x3970
0x01000468: sw    v0, 0x14(sp)
```

This assigns a default boot-path pointer (conditional on zero preexisting pointer in preceding code). It is therefore an **active code reference**, not merely a stray unused embedded filename. Additional strings near `0x01104CD0` and `0x01104D28` describe kernel CRC and ELF validation errors; `0x01104298` describes `encpack_header decode error`. `0x01105EF0` references `hdd%d:__linux.%d`. They document a kernel-loading/configuration layer whose exact behavior remains to be reconstructed.

For Ghidra start at `0x01000460`, follow its enclosing function and calls to kernel path resolution, `encpack` validation, memory segment load, and transfer. Resolve use-sites of `0x01103970`, `0x01104298`, `0x01104CD0`. For a more meaningful address-to-code mapping of recompiler failure PC `0x801A4930`, **extract `pfs0:/p2lboot/vmlinux` from the HDD copy next** and inspect its actual file format; do not assume ordinary ELF until header verified. If absent, verify which PFS mount backs `pfs0:` and record file-access attempts.

A raw 4-byte-aligned scan found **zero exact `TLBWR` opcodes (0x42000006) within the decrypted first-stage ELF or the stage2 binary**. This does not prove a runtime mapping or that no instruction is generated/copied later. It supports looking for the repeated interpreter TLBWR in a subsequently loaded executable rather than attributing it directly to stage2.

**State:** source-level evidence only; no modifications to uploaded Ghidra archives, no emulator behavior changes, and no copyrighted binary artifacts committed.

### Original PSBBN vmlinux extraction resolves exact fault PC (2026-10-17)

User extracted three additional files from the BB Unit HDD image:
- `pfs0:/p2lboot/vmlinux`: 3,038,863 bytes, SHA256 `07e44b50e2558d188caa306f60c138ad6015bbabcd3bc32fba35eeb6e8a870fc`.
- `hdfill/hdfill.elf`: 541,248 bytes, SHA256 `ffb7f54fd437af7d68178803dfab9e6acea452d8a988e5dcbe708bfadcb00414`.
- `hdfill/HDFILLER.PAK`: 4,142,080 bytes, SHA256 `999b9838eecafe6645f559c12b358ec033cc48db26b9300944ca1573c888fb7c`.

**Critical finding:** `vmlinux` is an intact, unstripped ELF32 MIPS little-endian MIPS-III kernel, version **Linux 2.4.17_mvl21**, compiled by `inamoto@imo.sm.sony.co.jp` with gcc 2.95.2, build `#1 Tue Jun 17 18:36:14 JST 2003`. ELF entry=0x80010490 `kernel_entry`; LOAD at 0x80010000 (filesz 0x1E5000, memsz 0x20DB50). `.text` 0x80010000–0x801C5E5F, `.bss` 0x801F5000–0x8021DB4F, 9,385 total symbol-table entries; .mdebug is also present. **Import as ELF at linked addresses, not raw / arbitrary base**.

Recompiler's previously opaque `pc=0x801A4930` resolves *exactly* to symbol `__bzero` (at 0x801A48FC), which is a second entry to optimized `memset` (at 0x801A48E0). `0x801A4930` is loop header `addiu a0,a0,0x40`. Starting at 0x801A4934, sixteen 4-byte `sw a1,-0x40(a0)` through `sw a1,-4(a0)` instructions form a 64-byte block fill. Back edge: `0x801A4974 bne t1,a0,0x801A4930`. This **perfectly accounts** for recorded repeated store misses at `0xC0005000, 0xC0005004, ...`: Linux was attempting to zero KSEG2 memory lacking usable virtual mapping in PCSX2. The precise caller, expected page-table state and first failed write are not captured; don't conclude this is simply a kernel bug or solely a recompiler bug.

Searched the full kernel `.text` for instruction word `0x42000006` (TLBWR): exactly **two static sites**:
- `0x800258B8`, `update_mmu_cache+0x128`, function begins 0x80025790; adjacent `tlbwi` at 0x800258BC.
- `0x80025A08`, `except_vec0_r5900_spr+0x28`, function begins 0x800259E0; branch-delay-slot `tlbwr`, `eret` 0x80025A14. Earlier code explicitly stages `EntryLo0=0x80000006`, `EntryLo1=0x6`, then TLBWR. **Interpreter reports both entry-lo values=0**, so do not assume the repeated warning is from this particular site. The kernel may install copied vectors and relocated addresses at runtime.

Other relevant kernel symbols:
- `0x80025A20` `handle_tlbl` (refill/load), `0x80025BC0` `handle_tlbs` (refill/store), `0x80025D60` `handle_mod` (TLB modified exception).
- `0x80025350` `local_flush_tlb_all`.
- `0x801D0628` `start_kernel` (init text), `0x801D1C80` `setup_arch`.
- `0x8021DB48` `pgd_current` (.bss, size 8).
- `0x801E358C` `init_mm` (data, size 132).

**HDFILL side files:** `hdfill.elf` is itself KELF, not a plaintext ELF: header size=0x3A0, declared content_size=0x83EA0, and 0x3A0+0x83EA0=0x84240 exact file size; flags at offset 0x18=0x022C. Additional MagicGate data would be required for static ELF recovery (not needed for the kernel failure). `HDFILLER.PAK` starts with `fpkr` magic and is not a standard ELF or ZIP; content has repetitive/highly compressible areas. Its format and role are **not established**; don't interpret as executable or assume its purpose from filename. These two files are separate from the now-identified vmlinux TLB path.

**Actionable Ghidra metadata:** script `ghidra_scripts/AnnotatePsbbnVmlinuxTlb_20261017_v1.java` checks exact linked instruction bytes then adds comments/bookmarks at relevant kernel symbols and both TLBWR sites. Requires importing vmlinux correctly and analysis; do not automatically rename imported symbols or override resolved datatypes. Static verification only: no local Ghidra instance was run to compile/execute the script.

**Next needed diagnostic:** bounded logs of interpreter TLBWR with **true executing guest PC** and `EPC, BadVAddr, Cause, Status, EntryHi, EntryLo0, EntryLo1, Random, Wired` plus context around first event. Capture preceding writes to `0xC0005000`, not the million-line print flood. Check whether COP0 Random advances and whether page-table mapping for target virtual page was ever installed. Compare to original Linux refill path before implementing fixes.

### Ghidra vmlinux analysis archive and CCC import warnings (2026-10-18)

User's Ghidra import of original Sony vmlinux reported **4,769 imported functions**, **3,871 globals**, **161 skipped unmapped/unnamed functions**, **30 function-creation failures**, and **one unmapped label**. CCC's first ten discarded-address warnings include `profile_setup`, `trap_init`, `check_bugs`, `init_arch`, `init_generic_irq`, `null_rtc_get_time`, `paging_init`, `loadmmu`, `r5900_flush_cache_range_d64i64`, and `add_temporary_entry`.

User subsequently supplied `vmlinux.gzf` (12,020,937 bytes, SHA256 `a26d015f0695e0a5a9fffa5dee65d34eac74ee6dc2bafdd01aa8cfd5d12108d0`). The archive is Java serialization with one local ZIP entry, `FOLDER_ITEM`. Raw DEFLATE successfully recovered its **41,451,520-byte** Ghidra packed DB payload; verified CRC32 `e7f2c302`, exactly matching the ZIP data descriptor. No archive corruption is indicated. Packed DB strings include all ten CCC-warning symbols as well as kernel TLB function names, but packed string presence alone does **not** establish their final symbol/function status in Ghidra. No local Ghidra runtime was available for direct DB or script execution.

**Reason for warnings:** Verified CCC upstream source `chaoticgd/ccc/src/ccc/mdebug_importer.cpp`, around lines 575–604. Its `detect_fake_functions()` deliberately unassigns an **.mdebug function candidate's address when multiple function symbols collide at that address** and an external mdebug function record does not match its name. Therefore the warning is a *heuristic disambiguation*, not a Ghidra failure to load the ELF and not proof that the named kernel function doesn't exist. It might discard duplicate incorrect mdebug definitions even where a correct function name exists elsewhere. Avoid globally disabling the heuristic without verifying each duplicate. The user's installed CCC build matches this diagnostic site; the GitHub Java extension branch may be older than locally built CCC integration (runtime reports JSON v14), so don't blindly modify the older importer implementation.

Independently verified via `readelf -sW vmlinux` that each of the first ten warned names has a **real, section-allocated ELF `.symtab` STT_FUNC record**:

```text
0x801D0000 profile_setup                     0x38  .text.init
0x801D0860 trap_init                         0xAF4 .text.init
0x801D1358 check_bugs                        0xA0  .text.init
0x801D13F8 init_arch                         0x6F8 .text.init
0x801D2120 init_generic_irq                  0x48  .text.init
0x800213F0 null_rtc_get_time                 0x0C  .text
0x801D22A8 paging_init                       0x64  .text.init
0x801D2630 loadmmu                           0x5C  .text.init
0x80023B90 r5900_flush_cache_range_d64i64  0x408 .text
0x801D28C8 add_temporary_entry              0x104 .text.init
0x801D0038 name_to_kdev_t                    0x100 .text.init
0x801D0258 checksetup                        0xAC  .text.init
```

These should be Ghidra function labels at the corresponding addresses when the ELF itself is loaded. The native ELF `.symtab` has **4,711 size-positive FUNC rows at 4,711 unique addresses**, indicating no ambiguity for those addresses in that table (zero-size aliases are separate). **Do not recreate a second overlapping function for `__bzero`** at `0x801A48FC`: it is a zero-sized ELF function alias within `memset`. The sole logged unmapped symbol `_end` is an ELF `OBJECT` of zero size at `0x8021DB50`, which is precisely the **exclusive high boundary** of loaded `.bss`, not a lost executable or missing memory allocation. Do not extend a memory block solely to host `_end`.

**Actionable new script:** `ghidra_scripts/ReconcilePsbbnVmlinuxSymbols_20261018_v1.java`. Prompt user for exact original ELF `vmlinux` (NOT gzipped archive); verify its SHA256, MIPS ELF class, entry and four known instruction encodings before edits. Read the ELF `SHT_SYMTAB` directly, reconcile mapped `STT_FUNC` and `STT_OBJECT` symbols into Ghidra: create missing functions where no existing body overlaps, fill generic names, add import labels when a valid function already has a separate non-generic name, leave user-chosen function names and signatures alone, mark zero-size aliases as labels. In addition, add read-only comments/bookmarks for verified TLB routines, `__bzero` stores, `pgd_current`, and `_end` boundary. Print applied, already-present, skipped and overlap counts so any residual gaps can be investigated exactly. No unknown datatypes or speculative calling conventions are applied. **Not run in Ghidra yet**, needs user execution/console output for validation. This is an exact ELF `.symtab` reconciliation, not a fix to CCC's upstream duplicate-function scoring.

**Next check:** Run script on imported vmlinux Ghidra project, capture summary counts. Inspect any remaining skipped overlap/function-creation sites, especially `.text.init`, before altering the importer. For runtime PSBBN TLB issue, next gather true interpreter TLBWR guest PC+COP0 state and correlate with page table before modifying emulator logic.

## First next step

Collect a **non-destructive baseline run** of PSBBN (Definitive Project) on the current fork using a disposable HDD image, identifying whether the first failure occurs in HDD/ATA/MBR/KELF or at Linux EE TLB initialization. In parallel, inspect PCSX2's existing TLB logic and review closed PR #14847 *line by line* to extract hypotheses for testable fixes.
