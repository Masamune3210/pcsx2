# PSBBN v59: remove per-remap fastmem reverse-map allocation

**Branch:** `note-working/psbbn-tlb-small-delta-projection-20261023` (continue same note-working branch; do not commit to `testing`).
**Preceding handoff:** `notes/PSBBN_TLB_MAPPING_STAGES_20261023_v58.md`.

## v58 measured proof

Supplied user run `emulog(20261023-210512).txt`, executable `PCSX2 v2.9.92-471-g1abef457e` confirms v58 was built and ran. ExtendedMMU, JIT active/reserved **1472 MiB**, `EEPerformanceProbe=true`. Single boot, fine 4 KiB dispatch from ~17.558s to ~66.496s pause; three JIT resets including initial (2.293s), dispatch switch (17.558s), later (~61.850s). PSBBN proceeds to UI. Normal shutdown profiler lines:

```
EE-PERF-SUMMARY compiledBlocks=4798409 compiledHostBytes=1733588586 compileUs=19730828 tlbSyncs=694379 tlbSyncUs=21514702 physicalClearRequests=4144293 physicalClearCalls=1639923 physicalClearUs=961322 noCodeSkips=1808302 jitResets=3 jitResetUs=996769 inclusive=1
EE-PERF-TLB-ROUTES memoHits=96639 memoUs=16088 smallDeltas=590557 smallDeltaUs=16253148 fullRebuilds=7183 fullRebuildUs=5245465 asidSyncs=82629 deltaCandidatePages=2905606 deltaPublished=2883503 deltaNoops=22103 removed=2064920 installed=2065005 permissionOnly=2 unchangedOrRetained=20773243
EE-PERF-TLB-STAGES projectionCalls=1195480 projectionUs=1305105 vmapRemoveCalls=2064920 vmapRemoveUs=6756321 vmapInstallCalls=2064997 vmapInstallUs=10163622 codeClearCalls=4128765 codeClearUs=1091113 lutRemoveCalls=2064920 lutRemoveUs=42534 lutInstallCalls=2064997 lutInstallUs=40699 publicationCalls=590557 publicationUs=83999
EE-TLB-FAULT-SUMMARY faults=600598 suppressedFaultMessages=599493 suppressedBranchWarnings=21508 verbose=0
```

~6.756s vTLB remove and ~10.164s vTLB install sum **16.920s**, ~78.6% of all measured TLB sync time (~21.515s). Compare to ~1.305s selected projection work and ~1.091s clear code. These mapping stages include vTLB table work, fastmem reverse-map update and possibly host OS Map/Unmap, not only Windows calls. Instrumentation overhead is high (millions of clock probes) and means the measured ratio is approximate. The route totals vary by guest workload; do not interpret shifts as proven FPS improvement without user observation.

## Source-level issue

`pcsx2/Memory.cpp::memClearPageAddr/memSetPageAddr` call `vtlb_VMapUnmap/vtlb_VMap`; these call `vtlb_RemoveFastmemMapping/vtlb_CreateFastmemMapping`. The reverse index (`pcsx2/vtlb.cpp::s_fastmem_physical_mapping`) was `std::unordered_multimap<u32,u32>` of mainmem offset -> guest vaddr. Each remap erased a node by `equal_range` and recreated it using `emplace`, leading to repeated hash bucket lookups and allocator churn for millions of PSBBN remaps. `vtlb_UpdateFastmemProtection` uses this reverse map to update every guest alias, which must remain accurate.

**Separately proven bug:** In the existing *direct remapping* case inside `vtlb_CreateFastmemMapping`, the old bucket was looked up using incoming **new** `mainmem_offset`, not the prior mapping offset. That can retain stale old reverse aliases and misdirect later protection updates. In normal PSBBN small-delta path removals precede installations, so do not assume this bug explains all the measured 16.9 seconds. Nonetheless it is a correctness bug and is fixed here.

## v59 implementation

1. **New `pcsx2/FastmemReverseIndex.h`**, an allocation-free intrusive reverse index:
   - Each virtual page is an index into preallocated `next`/`prev` vectors; a `heads` vector indexes every backing-file page.
   - O(1) insert, O(1) delete of a known virtual page and old backing offset; no per-alias map allocation, no bucket search.
   - Lists allow multiple simultaneously mapped guest aliases of one physical page. Iteration yields all aliases for page-protection updates.
   - `Initialize`, `Clear`, `Reset` handle VM startup / full mapping reset / shutdown; no guest-visible behavior added.
2. `pcsx2/vtlb.cpp`: swap only reverse-index storage and operations. **Unchanged**: host OS `Map/Unmap` conditions, `s_fastmem_virtual_mapping`, guest vmap/ppmap, forward mapping, host protection flags, 4K/host-page coalescing, physical code invalidation, JIT fetch LUT and TLB projection ordering. Correctly capture the old physical backing before clearing the forward mapping during direct replacement. Adapt reverse alias iteration in `vtlb_UpdateFastmemProtection` and full reset lifecycle. Direct map failures still leave the virtual page unmapped, without adding a reverse link.
3. `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: basic multiple-alias insertion, middle/head/last unlink, direct remap old/new bucket, after-clear reuse, plus 12,000 deterministic randomized remaps checked periodically against a separate vector mapping oracle; no duplicate aliases and no stale ones after reset.

The reverse-index arrays use about 8.3MiB more contiguous storage at a 4GiB guest virtual space plus ~0.28MiB heads for the fixed backing-file region, replacing unordered_multimap per-node storage and allocator overhead. The page-protection alias traversal is still live and exact. This affects fastmem generally, not just ExtendedMMU, and therefore requires normal emulator regression checks in addition to PSBBN.

**Limitations:** OS-level fastmem Map/Unmap syscall overhead remains. Expect possible reduction in vmap stage time, but do not promise a specific FPS gain. If timings remain similar, next target is the host shared-memory mapping calls, not yet another TLB projection rewrite.

## Test expected next

Update/build branch HEAD (Windows/MSVC compile **not verified by assistant**). No INI changes: keep `[EmuCore] EEJitExtendedCacheMiB=1472`, `EEPerformanceProbe=true`, scheduled PSBBN autosave off, high-volume tracing off, GS config untouched.

1. Run the `FastmemReverseIndex` core unit tests if practical.
2. Boot PSBBN, enter the same UI scene, observe sustained FPS, error messages, glitches or crashes.
3. Shut down normally to capture `EE-PERF-TLB-STAGES`, the route counts and overall summary.
4. Compare per-operation `vmapRemoveUs/vmapRemoveCalls` (~3.27us v58) and `vmapInstallUs/vmapInstallCalls` (~4.92us v58), plus sustained UI FPS. Different boot durations/guest operations and instrumentation overhead mean totals should not be compared without normalization.
5. If a correctness regression occurs, revert v59 reverse-index code only; keep the proven v57 bounded delta and earlier profiling fixes.

**Verification:** GitHub source/docs/tests applied; user MSVC build and runtime still pending. Local sandbox has no PCSX2 checkout/Windows toolchain; standalone helper has not been built here.
