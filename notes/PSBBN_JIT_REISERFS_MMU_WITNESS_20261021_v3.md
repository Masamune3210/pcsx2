# PSBBN journal: 2026-10-21 uploaded Windows log and savestates (v3)

## Branch handoff

Continue `note-working/psbbn-jit-reiserfs-mmu-witness-20261020` in Masamune3210/pcsx2. This updates and supersedes the v2 witness interpretation without creating an additional branch. Current branch retains the prior DEV9 ATA-4 implementation. This is a source-only correction; no new Windows build or PSBBN boot has yet verified the updated witness.

## Files analyzed

- `emulog(20261021-003800).txt`: Windows PCSX2 `v2.9.92-189-gb0c06fcd2` (matches preceding handoff commit), 42,004 lines, EE JIT. No Linux printk ring lines because that observer emits separately.
- `emulog(20261021-003757).txt`: unrelated 176-line ARMSX2-NX Nintendo Switch startup log, no BIOS configured; contains no PSBBN witness, not part of root mount failure.
- Four ZIP-formatted PCSX2 savestates `PSBBN-auto-1792543027160412-00{1..4}.p2s`. All structurally valid ZIPs. State 001 captured before Linux (`PC=00081FC0` at 13 s). States 002–004 after panic (`PC=8002B300` at 23, 33, 43 s). Kernel code in state 002 confirms exact guest instructions `800B1578=AC6202DC` (`sw v0,0x2dc(v1)`), `800B157C=8E02011C` (`lw v0,0x11c(s0)`), `800B1580=8C4302DC` (`lw v1,0x2dc(v0)`), `800B1584=14600006` (branch nonzero). All 002–004 retain `*(uint32_t *)(eeRAM + 0x1FA611C) == 0xC0005000`, matching the superblock's journal pointer. No need to re-extract all 32 MiB EE RAM to establish these facts.

## Runtime evidence

- Recompiler `vtlb_Miss` repeatedly reports stores `0xC0005000..0xC00053DC` while Linux clears the `0x115C0`-byte journal structure. `0xC00052DC` is explicitly recorded as a **JIT store miss** at log line 41887 during `memset`; that particular log entry alone does **not** identify the later slot pointer store, since its sampled `cpuRegs.pc` is stale.
- At 17.6043s the initial witness emitted exactly two `PSBBN-JOURNAL-WORD` lines (`phase=2,phase=3`). Both reported `archMapped=0`, `vmapHandler=0`, `ASID=00`, `Status=10010c01`. This confirms absence of an active TLB mapping at the observed virtual addresses.
- Important **instrumentation bug uncovered**: `recompileNextInstruction()` in `pcsx2/x86/ix86-32/iR5900.cpp` increments the JIT compiler global `pc` **before** calling `opcode.recompile()`. Therefore a `recLW()` check `pc == 0x800B1580` actually instrumented the previous instruction at `0x800B157C`, which reloads `v0` with the journal base. The old `recSW()` guard `pc == 0x800B1578` could never hit the actual SW at that address. Previous v2 statements that these phase records wrapped `LW @ 0x800B1580` are wrong.
- Interpretation of old witness: phase2 `v0=C0005290, v1=C0005000`, phase3 `v0=C0005000, v1=C0005000` traces the **preceding successful journal-pointer load**. It is not a sample of the disputed load result. Arch/vTLB unmapped findings remain valid.
- The original debugger run, separately, did stop **after** the real `LW @ 0x800B1580` with `v1=0` and entered `journal-2005`. Original debugger `get_list_bitmap` returned `v0=C0005290` after selecting free slot. Source `vtlb_Miss` in JIT reports exceptions but does not inject guest TLB refill and retry; a valid code-level correctness gap consistent with a discarded write and a zero-on-miss read.

## Changes committed this turn

- `pcsx2/x86/ix86-32/iR5900LoadStore.cpp` now matches *compiler* `pc=0x800B157C` for actual SW at `0x800B1578`, and `pc=0x800B1584` for actual LW at `0x800B1580`. Emits the precise guest instruction addresses in observer calls.
- Changes `iFlushCall(FLUSH_FULLVTLB)` to `iFlushCall(FLUSH_EVERYTHING)` at these **two** instruction-specific observers. The former is `0x000` (caller-saved register freeing only), while `FLUSH_EVERYTHING=0x1ff` synchronizes JIT-allocated GPR, XMM and constant state; otherwise recorded `v0/v1` can remain stale. No changes to other memory operations.
- Existing `PSBBNTraceJournalWordAccess` observer remains read-only, correctly resets its 32-event cap with `PSBBNResetTlbTrace()` and never dereferences optional `ppmap`.
- No guest memory writes or fabricated mappings, no changes to TLB refill/exception behavior, and no reverting DEV9 work.

## Next boot and interpretation

Build the updated branch, enable Trace + EE COP0 **before** the boot, and run EE JIT without pausing breakpoints. Expect four `PSBBN-JOURNAL-WORD` events (phases 0,1 for `SW @ 800B1578` and phases 2,3 for `LW @ 800B1580`). These are expected outputs, not verified yet.

- If phase0 has `v0=C0005290, v1=C0005000, ea=C00052DC`, and phase1 reports no backing write, the failing memory store is isolated; inspect the exact JIT generated path and its fault.
- If phase1 reports the correct physical data but phase2/3 diverge, inspect ASID/TLB remap or reads.
- The archMapped=0 witness plus source JIT miss handling already strongly suggests missing guest TLB exception delivery. Implement **architecturally correct** exception/abort/retry semantics (precise PC, BD/EPC, GPR writeback) rather than hardcoding a journal address or directly mapping C0005xxx.

## Verification

Uploaded log and all four ZIP states parsed locally; actual guest opcodes and journal pointer checked directly in EE RAM. Corrected GitHub source committed. Rebuild/Windows execution of corrected witness NOT performed in this environment.
