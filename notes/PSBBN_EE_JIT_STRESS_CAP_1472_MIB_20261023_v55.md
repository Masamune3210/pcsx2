# PSBBN v55: deliberately oversized 1472 MiB EE JIT generation experiment

Branch: `note-working/psbbn-tlb-small-delta-projection-20261023`; continuation only, do not modify `testing`.
Previous handoff: `notes/PSBBN_EE_PERF_PROBE_RESULTS_AND_CACHE_AB_20261023_v54.md`.

## Why

User reports **a large subjective performance gain** changing `[EmuCore] EEJitExtendedCacheMiB` from 256 to 512 after the EE-side unconditional DEV9 log removal. User requested a very large setting (suggested 2 GiB) to test whether improvements continue as arena resets become less frequent. This is an **experiment**, not evidence a cache this large should be a normal default or permanently kept.

Before this experiment, v53 reported over two VM boots: 7,121,809 EE compilations, ~2.509GB cumulative emitted host code, 24.53s compilation, 28.84s TLB synchronization, 1.32s actual physical code clear, 1.42s JIT reset work, 12 resets. Those cumulative values do not establish a unique single-boot working set.

## Bound and architectural limitation

The host reserves **one contiguous data-plus-code mapping** with `SysMemory::AllocateMemoryMap()`, using `HostMemoryMap::MainSize + CodeSize`. `MemoryTypes.h` includes 128MiB EE RAM and a **separate 128MiB IOP backing array**, even though only a small amount of IOP RAM may be exposed. The total MainSize is 285MiB.

Generated x64 host code contains rel32 links (`BaseBlocks::PatchLink`), RIP-relative code/data access and generated dispatchers. Extending the EE region to 2 GiB would move other JIT regions beyond the signed 32-bit relative reach. Even a nominal 1536 MiB EE region would make the combined map **2062 MiB**, exceeding the 2GiB conservative limit. We must not make the selected `2048` key silently wrap or treat it as safe.

Chosen intentionally large option **1472 MiB (1.4375 GiB)**:
- EE JIT reserved maximum: **1472 MiB**, `0x5c000000` bytes.
- Other JIT buffers: 241 MiB.
- Overall code mapping: **1713 MiB**.
- Overall contiguous code + data mapping: **1998 MiB**, leaving **50 MiB** before 2048 MiB boundary.
- Additional compile-time `static_assert(... <= 0x7e000000ull)` guarantees at least 32MiB of margin if future memory structures grow.
- 1472 MiB is 5.75x the original 256 MiB active generation. It is a stress option, not a production recommendation. Windows executable memory reservation may be much larger even when 64/256/512 MiB is selected because offsets are statically fixed.

## Changes

1. `pcsx2/Memory.h`: increase `HostMemoryMap::EErecSize` from 512 to **1472 MiB**; allow 1472 as one enumerated Extended cap in `EErecGenerationSize()`; preserve ordinary Standard 64MiB generation and Extended 256MiB default. Shift following code-region offsets automatically. Assert the 32MiB signed-relative headroom.
2. `pcsx2/x86/ix86-32/iR5900.cpp`: permit `[EmuCore] EEJitExtendedCacheMiB = 1472` as an explicit opt-in. No reset, allocation or JIT branch policy altered beyond the permitted generation capacity. All other unsupported values, **including 2048**, continue to fall back to 256.
3. `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: verify selected option, standard unchanged, invalid 1024/2048 fallback to 256, JIT layout ordering and signed-rel32 mapping bound.

No EE TLB semantics, invalidation heuristics, cryptographic operations or graphics rendering changes. v53 performance probe remains available.

## Run protocol

- Fetch/build current working branch. Windows/MSVC compile and emulation runtime **not yet verified by assistant**.
- Keep your observed successful **512MiB** run's FPS / EE-PERF-SUMMARY and console as baseline. Keep PSBBN autosave disabled for matched comparisons: `[EmuCore/PSBBN] AutoSaveIntervalSeconds = 0`. Extended MMU remains selected, verbose tracing/witnesses remain off, GS resolution unchanged.
- Change only `[EmuCore] EEJitExtendedCacheMiB = 1472`. Leave `EEPerformanceProbe=true` to capture aggregate totals. Fully restart VM to latch the selection.
- Startup must show `EE-JIT-CACHE-CONFIG mode=extended capacity=1543503872 reserve=1543503872 configuredMiB=1472`. Capacity and reserve are distinct concepts for the other selections; in this test they match because the stress option uses the entire reserved EE region.
- Observe sustained FPS at the **same PSBBN screen/stage**, time of JIT resets and CPU utilization. Normal shutdown should emit `EE-PERF-SUMMARY`. Beware a VM restart mid-run, which merges counters over different guest workloads and makes raw totals misleading.
- If 1472 substantially beats 512, capacity/whole-generation recycling remains important. If FPS plateaus or regresses, the extra generated host code working set, cache-locality effects, guest TLB churn or other mechanisms need attention. Regardless, there are still hundreds of thousands of TLB syncs and no optimization is proven by merely raising the cap.
- Restore `EEJitExtendedCacheMiB=512` after stress test if preferred. Note the binary still statically **reserves** the larger code window at startup even with 512 active; restoring the previous smaller reservation itself requires returning to a pre-v55 build.
- Preserve logs with file logging on for the one-line profiler summary.

Current status: source changes + tests committed, documentation committed, no user/CI build validation yet.
