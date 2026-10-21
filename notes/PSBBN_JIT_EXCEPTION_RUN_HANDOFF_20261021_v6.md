# PSBBN EE JIT fault recovery: current head / run handoff (v6)

**Branch:** `note-working/psbbn-jit-reiserfs-mmu-witness-20261020` in `Masamune3210/pcsx2`.
**Current source head:** `246eaff5370f0c6dabfa57a4679f37deb3a68059`.
**Verification:** code committed; no Windows build, compile validation or guest Linux boot has been performed for this revision.

## New evidence
Uploaded `emulog(20261021-004822-1).txt` from Windows `v2.9.92-191-gb59ccfd14` proves corrected exact-PC four-phase witness: SW at `800B1578` attempts to write `C0005290` into `C00052DC` (unmapped) and LW at `800B1580` reads back zero. `archMapped=0, vmapHandler=0` throughout. Earlier logs show the first KSEG2 misses in memset and `journal-2005` after failure. Companion `emulog(20261021-004822).txt` is unrelated Switch ARM emulator startup noise. Full details: `notes/PSBBN_JIT_JOURNAL_WITNESS_20261021_v4.md`.

## Source changes after last known Windows run
- `4884a15d`: x64 EE JIT memory-class instructions flush live registers and compiler PC, arm a precise memory exception context, and expose `recHandlePreciseTlbMiss()` to abort the translated block after raising architectural TLBL/TLBS.
- `68e68e11`: when `vtlb_Miss` sees that context, inject a guest TLB exception and exit instead of returning fake zeros or dropping writes. Interpreter code unchanged. ARM/non-x86 path unchanged.
- `246eaff5`: clear `cpuRegs.IsDelaySlot` after a *successful* translated memory operation in a delay slot, so the BD state cannot leak into later instructions.
- All source remains on the existing witness branch, preserving its DEV9 ATA-4 work. Detailed implementation and caveats: `notes/PSBBN_JIT_PRECISE_TLB_RECOVERY_20261021_v5.md`.

## Next action
1. Build head `246eaff5` under Windows MSVC Release AVX2 x64 (or existing project configuration).
2. Cold boot BB Navigator with EE JIT and the matching HDD image; log `cpuTlbMiss`, `PSBBN-TLB` guest exceptions, entry writes, and printk ring. Do not reload a post-panic savestate for this test.
3. Verify guest `BadVAddr` and `EPC` on first `0xC0005000` fault, successful TLB refill mapping/retry, and `/dev/hda1` mount. Further failures must be classified by exact kernel and ATA phase.
4. Expect potential speed degradation: this is an unoptimized state-flush barrier at memory-class instructions. Performance and guest behavior are unknown. Optimize to a lazy exception-only spill/deopt if functionally correct.
5. If any host compile or runtime error occurs, capture the first error or exception; do not patch guest memory to hide it.

**Ghidra database:** No new Sony binary metadata to apply. Previously confirmed journal symbols and directly runnable Java metadata script remain in `Masamune3210/ps2bbndecomp`.