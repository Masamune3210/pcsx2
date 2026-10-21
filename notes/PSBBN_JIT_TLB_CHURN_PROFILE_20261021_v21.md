# PSBBN JIT: TLB churn versus code-cache churn (v21, 2026-10-21)

## Continue this branch
- Repository: `Masamune3210/pcsx2`.
- Existing branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`. Do not create another branch.
- Latest tested user build: `v2.9.92-227-g8a5a407a0`.
- Uploaded traces: `emulog(20261021-055559).txt` (465,153 lines, ~26 MiB) and `psbbn-kernel-log(6).txt`.
- New source-only diagnostic commits: `b835bd6cbca4f49a05d59fc6c0bf6c9434fb0a7b` (COP0 aggregates), `418f7a30956b01fff08339b751870fcf35936d49` (JIT compiler/invalidation counters), `bbe46fb02d3d5b534138e4cf4977653835db7f14` (declaration). These are **not compiled or runtime verified**.

## What the latest run actually established
- User manually stopped the emulator; it did **not** crash. Emulation ran at roughly **1 FPS**.
- ReiserFS root mounted and Linux reached materially later userspace work than before, including journal replay on device 03:04, SCSI driver initialization, `Adding Swap: 126972k swap-space`, and later ReiserFS checks for devices 03:06 and 03:05. The limited printk observer ends after those, not at a kernel panic.
- Host emulator log includes **113,016** `cpuTlbMiss` messages over roughly 17.48–58.1s emulator-log time: 99,423 code=8 (TLBL), 10,827 code=c (TLBS), 2,766 code=4 (TLB Modification). Counts come from local read-only parsing of the uploaded log, not source-level guesswork.
- EE JIT resets at 2.6798, 17.6401 (first fine-dispatch transition), then 25.1191, 31.3466, 37.7968, 44.3 and 53.5s. The last five occur well into Linux activity. Correlation between TLB faults and resets is strong; **code-cache exhaustion is not yet proved**, because previous logs did not expose `recPtr` usage at reset.
- Same verbose logging was active in previous runs, so do not attribute the new 1 FPS performance principally to the volume of logging without comparative profiling.

## Architecture-level performance suspect
- `COP0.cpp::SyncActiveTLBMappings()` computes an entire desired mapping projection on ASID changes and TLB writes. It then clears and reinstalls changed host vTLB entries and calls `ClearActiveTLBCodePage()` for **both** removed and installed physical pages.
- On x64, `ClearActiveTLBCodePage` calls `recClearPhysical(mapping.physicalAddress, 0x400)`, which can invalidate physical JIT blocks in `EERecClearPhysicalBlocks`. Repeated guest TLB turnover can therefore invalidate compiled code without changing its RAM bytes, triggering recompilation, subsequent 64 MiB code-cache exhaustion and full recompiler reset.
- That mechanism is **source-supported but its runtime magnitude remains unmeasured**. The physical keyed code cache, guest-virtual static link handling, branch delay slots, and compiler-PC semantics were recently changed by Codex. A naive removal of physical invalidation could retain code compiled under the wrong guest virtual context or stale self-modifying instructions; validate before making any semantic change.
- Additional fine-mode costs remain: a full `FLUSH_INTERPRETER` before memory-class instructions and pre-store EntryLo.D checks in `iR5900.cpp`, plus guest faults and fastmem fallback itself. These may explain some performance independently of code-cache churn.

## Diagnostic-only source changes
A trace-gated profiler now emits **bounded summary records**, not per-fault records. It uses the already enabled `Trace.Enabled && Trace.EE.cop0` setting:

`EE-MMU-CHURN` from COP0:
- cumulative `syncs`, `tlbWrites`, `asidSwitches`;
- `removed`, `installed`, `unchanged` 4 KiB active-map entries;
- `physicalClearRequests` and `scratchClearRequests` originating in mapping changes;
- total elapsed `syncUs` measured by `std::chrono::steady_clock` around `SyncActiveTLBMappings`.
- Printed every 4096 mapping syncs and at JIT reset. Cumulative counters permit subtraction between samples.

`EE-JIT-CHURN` from `recResetRaw()`:
- current 64 KiB/4 KiB `mode`;
- `blockCompiles` and `compiledBytes` accumulated since preceding JIT reset;
- `physicalClearCalls` that reached the recompiler's physical-cache clearing path in that interval (from **any** source, not just COP0);
- `cacheUsed` before reset and `cacheCapacity` in bytes. A near-full value supports code-cache exhaustion; a partial cache argues for a different reset trigger.
- Counters are reset after a reset snapshot; no guest state or savestate data modified.

**Limitations:** `physicalClearRequests` counts attempted clears, not proven compiled blocks removed; `physicalClearCalls` counts calls passing the address/high-water guard, not the actual number of blocks evicted. Mapping `syncUs` includes the current full projection and invalidation calls when trace is enabled, but not JIT recompilation CPU time. No additional generated per-instruction probes. Trace-gated host aggregate increments and clock queries can add some overhead to this already slow diagnostic run, so compare source counters first before absolute FPS.

**No emulation behavior, TLB semantics, physical code-cache policy, native 64 KiB dispatch, disk image, or KELF bytes changed.** C++ build/tests were not run from this environment.

## Next steps
1. Compile existing branch with Windows MSVC x64 AVX2 and cold boot the same PSBBN HDD image with EE COP0 tracing enabled.
2. Compare successive `EE-MMU-CHURN` windows with `EE-JIT-CHURN` reset snapshots. Determine whether mapping change volumes, physical-code-clear attempts and recompilation/cache exhaustion line up with the 1 FPS period. Watch especially `cacheUsed / cacheCapacity`.
3. If **mapping-induced physical invalidation** dominates, implement a carefully bounded, native-PS2-first correction: preserve physical block cache identities on mere TLB alias churn **only when** all compiled block context/link invariants are demonstrated safe. Keep invalidation on actual physical RAM writes, fastmem re-protection, self-modifying code and external cache resets. Add executable tests for ASID switches, same-PFN/different-VPN physical aliases, branch-relative targets and COW writes.
4. If `syncUs` dominates but clearing doesn't, optimize `BuildEEActiveTLBPageMap`/diff projection; if `compiledBytes` remains low, examine guest miss frequency, CP0.Random and memory prologues instead.
5. Do not merge into `testing` before native retail games, BIOS/OSDSYS, savestate and JIT speed regressions are evaluated.
6. No new guest Ghidra metadata discovered. Existing `ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java` remains valid in `Masamune3210/ps2bbndecomp`.
