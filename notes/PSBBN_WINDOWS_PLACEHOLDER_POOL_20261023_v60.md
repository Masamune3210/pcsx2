# PSBBN v60: recycle Windows fastmem placeholder map nodes

Working branch: `note-working/psbbn-tlb-small-delta-projection-20261023`. Do not merge into or write to `testing` until validated.
Previous handoff: `notes/PSBBN_FASTMEM_REVERSE_INDEX_OPTIMIZATION_20261023_v59.md`.

## v59 user runtime verification

User supplied `emulog(20261023-214528).txt`, `PCSX2 v2.9.92-477-g0f9a8d54f`, confirming v59 code was compiled and PSBBN ran to the UI. Extended MMU enabled, 1472 MiB JIT active cache, performance probe enabled, no reappearance of unconditional DEV9 read/write logs or obvious mapping failure. Normal shutdown ~69s; one guest boot; EE fine guest TLB dispatch enabled ~17.4002s, VM paused ~64.7936s. Three JIT resets including startup, fine dispatch and one later internal reset, same pattern as v58.

v59 counters:
```
EE-PERF-SUMMARY compiledBlocks=4821896 compiledHostBytes=1730417257 compileUs=19768947 tlbSyncs=692411 tlbSyncUs=19844047 physicalClearRequests=4216723 physicalClearCalls=1651501 physicalClearUs=946095 noCodeSkips=1869185 jitResets=3 jitResetUs=963392 inclusive=1
EE-PERF-TLB-ROUTES memoHits=96238 memoUs=16091 smallDeltas=589125 smallDeltaUs=15137607 fullRebuilds=7048 fullRebuildUs=4690348 asidSyncs=84241 deltaCandidatePages=2985800 deltaPublished=2963654 deltaNoops=22146 removed=2101112 installed=2101202 permissionOnly=2 unchangedOrRetained=20771505
EE-PERF-TLB-STAGES projectionCalls=1192346 projectionUs=1246390 vmapRemoveCalls=2101112 vmapRemoveUs=5847607 vmapInstallCalls=2101194 vmapInstallUs=9675558 codeClearCalls=4201145 codeClearUs=1077008 lutRemoveCalls=2101112 lutRemoveUs=41452 lutInstallCalls=2101194 lutInstallUs=40604 publicationCalls=589125 publicationUs=88814
EE-TLB-FAULT-SUMMARY faults=597105 suppressedFaultMessages=596007 suppressedBranchWarnings=22165 verbose=0
```

Per-call comparison vs v58 (different run durations/guest progress):
- vmap removal v58 ~3.272 us, v59 **2.783 us** (~14.9% faster).
- vmap installation v58 ~4.922 us, v59 **4.605 us** (~6.4% faster).
- all TLB sync v58 ~30.984 us, v59 **28.659 us** (~7.5% faster).
- small deltas v58 ~27.522 us, v59 **25.695 us** (~6.6% faster).
- Aggregate vmap remove + install v59 **15.523165 seconds**, or ~78.2% of `tlbSyncUs=19.844047` seconds; compare v58 ~16.919943 seconds total at slightly fewer operations. This is not an FPS claim: only the user can observe steady-state FPS and these runs are not frame matched.

v59's preallocated `FastmemReverseIndex` reduces allocator work measurably, but **two million+ host Map/Unmap operations remain**. The underlying Windows placeholder bookkeeping is a separate source of tree-node allocation. Do not revert successful v59 code or keep altering guest TLB semantics on this evidence.

## Code inspection and v60 change

`common/Windows/WinHostSys.cpp::SharedMemoryMappingArea::Map` and `Unmap` use Windows placeholder functions `VirtualAlloc2`, `VirtualFreeEx(...MEM_PRESERVE_PLACEHOLDER)`, `MapViewOfFile3(...MEM_REPLACE_PLACEHOLDER)`, `UnmapViewOfFile2(...MEM_PRESERVE_PLACEHOLDER)`, and `VirtualFreeEx(...MEM_COALESCE_PLACEHOLDERS)`.

Each 4KiB fastmem remap also updates `SharedMemoryMappingArea::m_placeholder_ranges`, originally `std::map<size_t,size_t>`, splitting/erasing/merging placeholder intervals and allocating/deallocating red-black tree nodes via the general C++ allocator. These nodes can be recycled: this is where a **slab/pool allocator** may be appropriate, unlike v59's already allocation-free reverse index.

Source modification **only `common/HostSys.h`**:
- Under `_WIN32`, add `std::pmr::unsynchronized_pool_resource m_placeholder_node_pool` and change `PlaceholderMap` to `std::pmr::map<size_t,size_t>` using that pool in the field initializer.
- Pool declared *before* map to guarantee map nodes destruct before the pool.
- No changes to virtual ranges, placeholder splitting/merging algorithms, OS API calls, aliases, permission flags, Windows protection, or fastmem ownership; Linux/macOS shared memory map representation is unchanged.
- Use the per-instance pool only for tree nodes of that mapping area. It is unsynchronized because the existing `std::map` was already not concurrency-safe and all mutation is expected to be externally serialized; no thread concurrency guarantees are added.

Test modification `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: Windows-only `SharedMemoryMappingArea.PooledPlaceholderNodesSurviveRepeatedSplitAndCoalesce`, exercising 32 rounds of out-of-order mapping/unmapping eight host pages with actual Windows placeholder APIs, including mappings using `VirtualAlloc2` and unmap using `MEM_PRESERVE_PLACEHOLDER`. This tests re-use of range tree nodes with real underlying host allocator. The test is skipped when not building for Windows, using `#if defined(_WIN32)`.

An isolated C++20 `std::pmr::map` initialization sample passed `g++ -std=c++20 -fsyntax-only` in assistant container. **This does not verify PCSX2 MSVC compile or Windows runtime.**

## v60 test protocol

No INI changes from v59. Keep:
```ini
[EmuCore]
EEJitExtendedCacheMiB = 1472
EEPerformanceProbe = true
```
Continue autosave disabled, COP0 per-event logs off, resolution unchanged.

1. Build branch HEAD with MSVC. The focused unit test, if enabled in the Windows core tests, must pass with the actual Windows VirtualAlloc2/Unmap calls.
2. Boot PSBBN to comparable UI, observe sustained FPS and any hangs/map/protection errors.
3. Pause/exit normally, capture `EE-PERF-TLB-STAGES` and route timings.
4. Compare avg vmap remove and install time to v59's **2.78 us** and **4.61 us** per operation and user-observed FPS. A plateau means general allocator recycling was not the dominant cost; repeated **Windows kernel placeholder and shared-memory mapping APIs** deserve more attention. Do not infer a guaranteed FPS gain merely from replacing map's allocator.
5. Any mapping regression should be localized to Windows placeholder pool lifetime; do not touch v59's independent reverse index or guest MMU unless evidence requires it.

Unfinished: no assistant MSVC build or Windows guest/runtime test. Current branch sources and tests committed; test results pending.
