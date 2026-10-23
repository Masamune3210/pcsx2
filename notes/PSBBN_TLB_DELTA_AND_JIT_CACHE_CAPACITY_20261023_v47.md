# PSBBN v47: emulog(10) full delta validation and Extended EE JIT arena experiment

**Branch:** `note-working/psbbn-tlb-small-delta-projection-20261023` (same active branch as v46, no new branch or rebase).
**Predecessor:** `notes/PSBBN_TLB_SMALL_DELTA_EXPERIMENT_20261023_v46.md`.
**User evidence:** `emulog(10).zip` containing `emulog.txt`, 76,731,898 bytes and 1,161,615 lines. Binary `PCSX2 v2.9.92-383-g86ecf5776` corresponds to v46; Extended MMU; link owner witness and high-volume TLB logging enabled. Guest continued through userspace activity, then user pause and normal shutdown at ~125 seconds; a visible UI frame is not independently timestamped in the log.

## Result of v46

At 125.058s, last printed `EE-MMU-CHURN` snapshot (cumulative; not necessarily the terminal total):
- `syncs=663552`, `tlbWrites=582117`, `asidSwitches=81435`.
- `smallDeltaSyncs=560034` (**84.4%** of all syncs); `smallDeltaPages=2743811` (~4.90 candidate VPNs per delta).
- `projectionRebuildsSkipped=96361` (~14.5%); `projectionRebuilds=7157` and `fullDeltaFallbacks=7157` (~1.1%).
- `deltaChecks=546`, `deltaMismatches=0`; no `EE-MMU-DELTA-MISMATCH` lines.
- Invalidation is not suppressed: `removed=1980648`, `installed=1980731`, `physicalClearRequests=3960098`, `duplicatePhysicalClearsAvoided=1265`.
- `syncUs=22015137` (~22.02s inside timed synchronization routines). Earlier v45 log showed 23.35s at a different workload/log duration. **Not** a matched runtime benchmark, so do not claim a precise speedup or regression.
- The original 64 MiB EE JIT cache exhausted **25** times (`resetReasons=00000004`), plus initial setup and native -> fine guest dispatch transition. 27 total resets. This remains the major visible JIT cost even after the successful delta projection.
- All fine generations report `EE-JIT-LINK-DETACH-CONFIG enabled=1 mode=1`. Last generation: 99,922 links detached, `untracked=0`, `postDetachRetargets=0`, no retired-source repatches; previous source detachment fix remains active.
- No fatal assertion, panic, or reported delta mismatch. Raw TLB-miss lines dominate the 1.16M-line log. Normal pause, DEV9 shutdown and JIT owner/provider teardown observed.

**Interpretation:** v46 is correctly exercising incremental guest TLB projections at scale with 546 sample equivalence checks against the original full map. This validates the intended instrumentation and exercised path, not every possible ASID/mask edge case. The 64 MiB JIT arena continued to reset dozens of times. Earlier identical-source witnesses make a conservative code-cache capacity experiment worthwhile without attempting unsafe retired-code resurrection yet.

## v47 executable experiment (committed code changes)

This branch now includes an **immediately testable arena expansion** on top of v46:
- `pcsx2/Memory.h`: reserve **256 MiB** EE recompiler code region instead of 64 MiB. Subsequent IOP/VU/VIF/GS cache offsets are still derived from constants and remain non-overlapping. Overall code mapping becomes 497 MiB instead of 305 MiB.
- `HostMemoryMap::EErecStandardSize=64 MiB`; `EErecGenerationSize(extendedMMU)` selects the 64 MiB Standard or 256 MiB Extended cache generation limit. Standard does not get a larger active cache, though the process reserves the larger code-memory mapping.
- `pcsx2/x86/ix86-32/iR5900.cpp`: set `recPtrEnd` from selected generation capacity at initial allocation **and every JIT reset** (which matters because EE profile selection is boot-latched after host-code memory allocation). Continue using the original 64 KiB emission slack, overflow/reset behavior, invalidation and backpatch rules. Add trace-only `EE-JIT-CACHE-CONFIG mode=... capacity=... reserve=...` with actual cache sizes.
- `pcsx2/x86/EERecLinkOwnershipWitness.h`: raise the **opt-in diagnostics-only** site cap from 400,000 to 1,500,000 for fourfold larger generations; no change to actual link-detach semantics. To compare real performance, disable the witness on **both** runs, because tracking up to a million jump sites is expensive.
- `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: add a unit test verifying active Standard=64 MiB, Extended=256 MiB and cache region layout.

This is *capacity*, not reclaiming or reusing old host code, so it should reduce the **frequency** of full arena exhaustion under the same guest workload without hiding mapping-driven code retirement. It changes no guest memory semantics. It is **not** a proof that keeping more stale compiled code is free of side effects; builds/runs must validate.

## Immediate test plan

1. Update/build the **existing** `note-working/psbbn-tlb-small-delta-projection-20261023` (do not switch to `testing`, rebase, or make another branch).
2. Run `core_test` if available and specifically verify the new `EERecCodeCacheCapacity` test and existing EE TLB/physical code-clear/link tests.
3. Under Extended MMU, rerun PSBBN through the same userspace/UI stage. The first logged `EE-JIT-CACHE-CONFIG` should read **`mode=extended capacity=268435456 reserve=268435456`**, and `EE-JIT-CHURN ... cacheCapacity=268435456`. The initial memory map should display R5900 cache `[256mb]`. If capacity still says 67,108,864, the build or profile is wrong.
4. Check for startup crash, executable memory reservation failure, host rel32 overflow, stale guest PC links, new TLB delta mismatches, new user-space faults, or failure to reach UI. Keep prior link-detach `untracked=0 postDetachRetargets=0`.
5. At similar guest-cycle and visible UI checkpoints, compare the **number of full arena resets** (`resetReasons=00000004`) with v46's **25**. Predict fewer resets if the rest of the workload is comparable; no guaranteed timing multiplier.
6. As a negative control, run a normal game or Standard profile if practical and confirm **`mode=standard capacity=67108864 reserve=268435456`**. This matters because the extra reserved memory should not alter Standard's generation semantics.
7. For wall-clock FPS and CPU cost, do separate comparable runs with old COP0/TLB verbose traces and link-ownership witness **disabled**, both on baseline and v47. These high-volume logs preclude meaningful speed conclusions.

**Verification status:** code and test changes committed. Assistant has not compiled or run these changes. User's `emulog(10)` verifies v46 only, not v47. No code reuse, no guest TLB semantic shortcuts beyond existing validated v46, no changes to `testing`.
