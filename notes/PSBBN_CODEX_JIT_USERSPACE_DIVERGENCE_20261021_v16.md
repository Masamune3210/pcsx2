# Codex task: diagnose and repair PSBBN EE JIT userspace divergence (2026-10-21, v16)

## Assignment

Work directly in **Masamune3210/pcsx2** on the existing branch **`note-working/psbbn-jit-reiserfs-mmu-witness-20261020`**. Do not create yet another nested branch. Read this file and the nearest prior handoff, `notes/PSBBN_JIT_GUEST_TLB_CODE_IDENTITY_ALIAS_20261021_v15.md`, before inspecting focused code. Investigate the **first incorrect user-mode instruction/data translation or guest exception transition** during SCPN-60160 v0.32 PS2 BB Navigator boot, make a general architectural correction where evidence supports it, and push source/tests and a concise updated handoff to this same branch.

The primary product is **a PS2 emulator**, not a Linux emulator. Preserve ordinary PlayStation 2 BIOS, retail-game compatibility, JIT performance, savestates, fastmem and existing recompiler invariants. Do not add guest-address hacks, ReiserFS skips, fabricated mappings, silent NOPs, or broad global changes just to advance PSBBN. Do not modify decompiler core. Prefer a small, causally justified fix and tests over accumulating guards that mask the original defect.

## Reproduction and latest verified evidence

Two files were supplied to ChatGPT in the latest test (provide them to Codex as attachments if available; **they are not checked into GitHub**):
- `emulog(20261021-033453).txt`, **134,403 lines**, Windows **PCSX2 v2.9.92-222-g77b87149e** (includes v15 source fix `b4930a94` and notes-only commit `77b87149`).
- `psbbn-kernel-log(5).txt`, 81 lines, from the same test window.

Guest: SCPN-60160 PSBBN 0.32, Sony Linux 2.4.17_mvl21, root `/dev/hda1`, PCSX2 DEV9 HDD image `bbntest.raw` (40 GiB image), EE recompiler, US BIOS 01.60. Previous interpreter tests reached farther but had separate issues, so do not assume HDD images or states were byte-identical across historical runs.

**Verified working before userspace**:
- HDD KELF authentication and Linux kernel startup.
- HDD partitions: hda1, hda2, hda4, hda5, hda6, hda7.
- `psbbn-kernel-log(5).txt` lines 75-80: ReiserFS transaction log checked and `VFS: Mounted root (reiserfs filesystem) readonly.`, followed by `Freeing unused kernel memory: 68k freed`.
- Windows log lines 43198-43201: JIT guest `SW @ 800B1578` stores `C0005290` to `C00052DC`; subsequent `LW @ 800B1580` reads back `C0005290` (mapped/writable, physical `002EF2DC`). The previous silent TLB miss/discard root-mount problem is fixed for this path.

**Failure sequence in the latest build**:
1. Line 44267: `EE JIT enabled 4KiB guest TLB instruction dispatch at 0fb643c0` after a real guest instruction-fetch TLBL/refill. Architectural entry `EntryHi=0fb64001`, `EntryLo0=0007b85a`; Linux ERET retries `0fb643c0`. The native 64 KiB dispatcher had worked through BIOS/HDDLOAD.
2. Lines 44634, 44657 and 44743: `EE JIT cross-page delay-slot interpreter fallback: branch=0fb73ffc slot=0fb74000`. Prior Windows host crashes from null `PSM` reads are absent in this test; this alone does not validate correct fallback branch/BD/register semantics.
3. Lines 47509-47513: suspicious preceding faults involving `2abc0be0`, `2ac24e20`, and `0fb71898`. Track their guest instruction/return provenance instead of starting analysis at the eventual repeated `0x6f60` load.
4. Lines 47514-47559: approximately **46 repeated diagnostic** `TLB Miss, pc=0x401500 addr=0x401500..0x4015b0 [load]` messages. Examine whether these are actual guest instruction fetches, an interpreter-fallback behavior, or diagnostic artifacts (do not automatically equate `pc` in this message with a precise instruction-fetch EPC).
5. Lines 47561-47570: unrecognized EE instructions such as `76282820`, `72746e65`, `72676f72`, `736e6f69`, `74636574`. These words resemble ASCII/text data, suggesting **wrong code bytes or corrupted control flow**, but the log does **not** prove which.
6. From line 47571 until shutdown: exactly **86,719** log entries `cpuTlbMiss pc:fb62d44, ... addr: 6f60, status=10c11, code=8`. PC logged as `0x0FB62D44` (potentially next-PC convention); failing guest instruction likely `0x0FB62D40`, load from `0x00006F60`, guest `TLBL` exception code 2. Verify precision and whether the guest handler/ERET restores or changes PC/GPR/ASID. The run was paused around emulator time 28.09s.
7. There are **no** `EE JIT repaired instruction alias` reports and **no** `R5900 Recompiler: Instruction fetch has no physical backing` fatal reports in this log. There are no later userspace success indicators. The v15 change to initially unmap TLB-managed 4 KiB code pages did **not** eliminate the failure.

In prior build v2.9.92-219, the same userspace pattern also occurred. The current log therefore supports an ongoing architecture-level bug, **not proof that the latest 4 KiB table initialization itself caused it**.

## Relevant implementation, already changed on this branch

- `pcsx2/x86/ix86-32/iR5900.cpp`: legacy `recLUT/hwLUT` 64 KiB fixed aliases; new `recLUTPages/hwLUTPages` 4 KiB per-page aliases; demand-driven `g_recUseGuestTLBDispatch`; `recMapTLBPage`, `recUnmapTLBPage`, `recReserveRAM` fine-table initialization; `HWADDR`, `PC_GETBLOCK`, both generated dispatchers, `recError`, `recRecompile`, `recHandleUnavailableCodePage`, and cross-page delay-slot fallback. Changes are experimental, and several have never had independent native-game regression tests.
- `pcsx2/COP0.cpp`: `SyncActiveTLBMappings`, `ResetActiveTLBMappings`, `WriteCP0EntryHi`, `MapTLB`, `TLBWI/TLBWR`, ASID-selected `BuildEEActiveTLBPageMap` projection and 4 KiB JIT alias hooks. Look especially at **order of `Cpu->Clear`, memSetPageAddr/memClearPageAddr, and recMapTLBPage/recUnmapTLBPage**. `Cpu->Clear` may derive a physical address via `HWADDR`; inspect whether it uses the old, new, or unmapped alias at each transition. This is a hypothesis, not a proven bug.
- `pcsx2/EEActiveTLB.h` and related tests: architectural match/page size/ASID/global/dirty-bit semantics, overlapping TLB entries, valid/invalid and scratchpad mappings.
- `pcsx2/vtlb.cpp`: JIT `vtlb_Miss` recovery, vmap/pmap and handlers. Earlier guest data TLBL/TLBS delivery is verified working for the ReiserFS path, but exception delivery elsewhere needs validation.
- `pcsx2/R5900.cpp`: `cpuTlbMiss`, `cpuTlbMissR/W`, `cpuExceptionInternal`, exception vector/EPC/BD/EXL semantics.
- `pcsx2/Interpreter.cpp`: `EEInterpretCrossPageBranchFromJIT`, its temporary switch to `intCpu`, cancellation `fastjmp`, branch-delay-slot execution and return to JIT. Examine side effects on `cpuRegs.pc`, `cpuBlockCycles`, `Cpu`, Random, GPR state and event timing. No assumption that the fallback is correct merely because it stopped the host crash.
- `pcsx2/x86/ix86-32/iR5900LoadStore.cpp`: memory-op flush/precise exception tracking and original journal witness.
- Previous handoffs `v9` through `v15` document key changes and regressions. Treat earlier interpretations as hypotheses wherever a later run contradicted them.

## Investigation goals (in priority order)

1. **Find the earliest incorrect transition**, not merely the last 86k repeated misses. Reconstruct the few dozen guest instructions, precise virtual PCs, instruction words, physical pages, ASID/EntryHi, architectural TLB matches, vTLB mappings, active JIT `HWADDR`, and guest control-flow registers around the first suspicious jump/data decode (roughly lines 47509-47571). Add bounded read-only witness probes if the existing logs cannot establish this.
2. Distinguish: **(a)** correct guest fetch at the wrong PC, **(b)** correct PC with wrong physical opcode bytes, **(c)** stale compiled block after ASID/TLB mapping/invalidation change, **(d)** malformed interpreter cross-page fallback/control flow, **(e)** wrong exception vector, EPC/BD, EXL or restored guest register state, **(f)** valid guest exception that Sony Linux cannot recover from. Do not assert a diagnosis before comparisons.
3. Compare with a meaningful reference: EE interpreter boot with the same exact HDD snapshot/configuration, or tightly scoped validation tests and instrumentation if interpreter execution is too slow. Compare byte-accurate register/translation snapshots near divergence, not just guest frames per second or broad log strings.
4. Fix the **general EE MMU/JIT invariant** exposed by this test, preserving native 64 KiB dispatch as default. Minimize overhead on regular memory and compiled-branch hot paths. Do not put a global `FLUSH_INTERPRETER` or a heavy map scan in every ordinary game instruction if it can be handled at map-change/slow paths.
5. Supply and run focused tests for TLBWI/TLBWR, ASID switch, KUSEG/KSEG2 code dispatch, code/data aliases, block invalidation (self-modifying code), 4 KiB boundary JR/JALR + delay slots, accurate EPC/BD, and the first-fetch null-physical guard. Retain or extend existing tests rather than duplicating fixtures. If a test cannot run in the Codex environment, say so.
6. For the PSBBN boot criterion: still mount ReiserFS, preserve the journal pointer witness, **avoid decoding ASCII as code**, avoid the `0x00006F60` TLBL loop, then verify the actual later boot stage. Never claim “PSBBN boots” based solely on a root mount.
7. For PS2-first criterion: avoid ordinary BIOS/retail-game regressions and significant JIT slowdown. Keep the 64 KiB path intact unless a measured general fix requires otherwise. Check savestate replay, translated-code cache reset, and generated-code link/backpatch consistency.

## Deliverables

- Commit a small, **root-cause-driven** fix and pertinent tests directly to the existing branch. If not enough evidence for a safe fix, commit useful scoped instrumentation and explicitly report what remains unknown; do not fabricate results or repeatedly replace one guard with another.
- Update a **new uniquely named** `notes/PSBBN_CODEX_..._v17.md` (do not overwrite older handoffs) with findings, exact source locations, implementation decisions, changes, build/test status, remaining risks and next action.
- State which commits were pushed. If GitHub write/push is unavailable, provide a normal git patch with one terminal command per line for the user.
- If you identify provable PSBBN binary metadata while analyzing Ghidra material, return directly applicable names, structures, signatures and addresses; no new Ghidra claims are established by the current host-side logs.
- Do not import the entire 12 MB PCSX2 emulog into the repository without necessity. If Codex cannot access those uploads, this brief includes the high-value line references/excerpts; request only the focused log region or add bounded diagnostics.

## Non-goals

- No MagicGate or HDD-sector modifications. Those subsystems have already passed their relevant checks in this run.
- No hardcoding `0x0FB62D40`, `0x00006F60`, `0x00401500`, or other PSBBN addresses into emulation behavior.
- No blanket interpreter mode, disabling fastmem, bypassing guest TLB errors, or suppressing unknown opcodes to disguise incorrect execution.
- No new branch per hypothesis, speculative claims of full Linux boot, or unverified merge into `testing`.
