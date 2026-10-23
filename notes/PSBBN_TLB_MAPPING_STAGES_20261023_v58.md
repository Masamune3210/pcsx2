# PSBBN v58: separate host fastmem remaps from TLB projection work

Branch: `note-working/psbbn-tlb-small-delta-projection-20261023`. Continued in place; `testing` untouched.
Prior handoff: `notes/PSBBN_SMALL_TLB_DELTA_BATCH_LOOKUP_20261023_v57.md`.

## Input, actual v57 validation

User supplied `emulog(20261023-205530).txt`, executable `PCSX2 v2.9.92-466-g0005fbf4c` confirms the entire v57 source was built and run. Single PSBBN boot with 1472MiB EE JIT stress arena, Extended MMU, `EEPerformanceProbe=true`; reaches PSBBN execution/UI, normal pause then shutdown. JIT resets at timestamps 4.0874 (initial), 19.8946 (fine dispatch transition), 62.3853 (later). No recurrence of unconditional DEV9 read/write spam. No MSVC failure reported.

v57 shutdown:
```
EE-PERF-SUMMARY compiledBlocks=5095055 compiledHostBytes=1856408332 compileUs=21069171 tlbSyncs=748141 tlbSyncUs=19189962 physicalClearRequests=4313560 physicalClearCalls=1680579 physicalClearUs=984341 noCodeSkips=1936929 jitResets=3 jitResetUs=979037 inclusive=1
EE-PERF-TLB-ROUTES memoHits=96433 memoUs=14967 smallDeltas=644500 smallDeltaUs=14419944 fullRebuilds=7208 fullRebuildUs=4755050 asidSyncs=81751 deltaCandidatePages=3073999 deltaPublished=3051812 deltaNoops=22187 removed=2149467 installed=2149556 permissionOnly=2 unchangedOrRetained=20765649
EE-TLB-FAULT-SUMMARY faults=655274 suppressedFaultMessages=654145 suppressedBranchWarnings=23400 verbose=0
```

Against v56 (same 1472MiB setting but different run duration/guest progress):
- Small-delta: 17,843,262us / 682,685 = 26.14us/call; v57 14,419,944us / 644,500 = **22.37us/call**, ~14.4% lower average.
- Full fallback: v56 5,329,665us / 7,063 = 754.6us/call, v57 4,755,050us / 7,208 = **659.7us/call**. No source change directly targeting full fallback was made; this improvement may reflect workload variation and must not be attributed to v57.
- Memo hit ~0.17us/call v56, 0.155us/call v57.
- v57 vmap remove 2,149,467 and install 2,149,556, still **4.299 million host vTLB mutations** despite faster bounded projection planning.
- The new v57 delta publication bypass skipped 22,187 / 3,073,999 candidate updates = **0.72%**. Most mapping changes were real (PFN, validity, permissions), so this suboptimization cannot plausibly explain all per-call speedup. The batched winner resolver is the more likely contributor, but no isolated toggle exists to prove its share.

## Direct source finding motivating v58

`pcsx2/COP0.cpp::applyMappingAction` performs, on each removal, `memClearPageAddr()`, `clearCodeForMapping()` and `recUnmapTLBPage()`, and on installations, `memSetPageAddr()`, `clearCodeForMapping()` and `recMapTLBPage()`.

`pcsx2/Memory.cpp` is a trivial forwarding wrapper: `memClearPageAddr` calls `vtlb_VMapUnmap(...,0x1000)`; `memSetPageAddr` calls `vtlb_VMap(...,0x1000)`.

`pcsx2/vtlb.cpp::vtlb_VMapUnmap` calls `vtlb_RemoveFastmemMappings` and alters vmap/ppmap; `vtlb_VMap` can call `vtlb_CreateFastmemMapping`. Under fastmem, these can remove/create OS-level memory views (`s_fastmem_area->Unmap/Map`), update hash-indexed reverse mapping and virtual mapping tables. These are plausible major costs but had **not** been timed separately, so do not conclude OS mapping is the measured bottleneck yet. `recMapTLBPage`/`recUnmapTLBPage` update generated instruction-fetch lookup arrays.

There is not enough evidence to safely eliminate or reorder host mapping, alias invalidation, physical code removal, or fastmem behavior. Avoid semantic shortcuts until staged measurements.

## v58 implementation: seven opt-in cumulative stages

Existing `[EmuCore] EEPerformanceProbe=true` is the only activation; default false and Standard unaffected. `pcsx2/EEPerformanceProbe.h`: independent stage events and batched counter update helper, reset with existing probe:
- `projectionCalls/projectionUs`: selected-slot change detection, bounded delta winner scan, full desired-map build. It **does not** capture full-map diff-loop overhead, memo matching, general C++ callback bookkeeping.
- `vmapRemoveCalls/vmapRemoveUs`: time inside `memClearPageAddr`, includes vTLB removal and any underlying Windows fastmem unmap.
- `vmapInstallCalls/vmapInstallUs`: time inside `memSetPageAddr`, includes vTLB install and any underlying Windows fastmem map.
- `codeClearCalls/codeClearUs`: time inside `ClearActiveTLBCodePage` after existing duplicate-PFN filtering. Unlike `physicalClearUs`, this category can include interpreter/scratch behavior and bounds/skip overhead; these two are overlapping measurements.
- `lutRemoveCalls/lutRemoveUs`, `lutInstallCalls/lutInstallUs`: time to update generated code fetch LUTs.
- `publicationCalls/publicationUs`: time to publish a bounded delta to active page map (one call per small sync, not pages).
`pcsx2/COP0.cpp`: use a single opt-in `measureStage` around each preexisting action and aggregate per sync into atomics via `RecordBatch`. Keeps remove-before-install order, page snapshot, active map publish, scratchpad, and all guest-visible MMU actions identical.
`pcsx2/x86/ix86-32/iR5900.cpp`: print **one additional shutdown-only `EE-PERF-TLB-STAGES`** with all seven count/time pairs, in addition to existing `EE-PERF-SUMMARY` and `EE-PERF-TLB-ROUTES`. No per-page log.
`tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: verify RecordBatch counters, zero-count exclusion, lifecycle reset and no double-accounting of syncs.

## Next user test

Build the current branch head in Visual Studio. Keep previous INI, **no settings changes** (`EEJitExtendedCacheMiB=1472`, `EEPerformanceProbe=true`, global trace false, autosaves off). Boot same PSBBN UI, observe FPS and any regressions. Normal shutdown. Capture `EE-PERF-TLB-STAGES` plus existing summaries.

- If `vmapRemoveUs+vmapInstallUs` dominates, measure OS fastmem Map/Unmap and reverse hash-map work in `vtlb.cpp` next; avoid blindly removing a mapping required to enforce ASID isolation.
- If `projectionUs` dominates, optimize broader full fallback builder/diff or remaining candidate processing with correctness tests.
- If `codeClearUs` dominates, investigate invalidation policy and compiled-page tracking while preserving physical code ownership.
- If `lut...` unexpectedly dominates, investigate recLUT page-index updates.
- If instrumented categories explain little of `tlbSyncUs`, remaining general snapshot/hash allocation/full-diff/application overhead or timing instrumentation may matter; sample EE host thread with Windows profiler.

**Instrumentation accuracy:** stage timers add 2 steady_clock reads for each of millions of host-mapping operations under opt-in mode. They can introduce additional overhead. Compare stage **relative cost**, not a direct FPS before/after or sums as CPU budget; durations exclude timestamp/recording overhead and are not guaranteed to sum to total `tlbSyncUs`.

**Verification:** v57 user-run validated; v58 source/tests committed but not compiled or PSBBN runtime tested by assistant.
