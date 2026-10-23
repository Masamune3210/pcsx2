# PSBBN v53: low-overhead performance breakdown after removal of EE-side DEV9 console flood

**Existing branch:** `note-working/psbbn-tlb-small-delta-projection-20261023`. All source and tests committed here; do not modify `testing`.
**Predecessor:** `notes/PSBBN_EE_DEV9_UNCONDITIONAL_REGISTER_LOG_FIX_20261023_v52.md`.
**User input:** `emulog(20261023-181454).txt`, `PCSX2(3).ini`, observed FPS still mostly single to low double digits but occasional bursts near 30 after v52.

## v52 run evidence

- Executable `PCSX2 v2.9.92-430-g448d43be6`: correct v52 code (commit `448d43be`).
- Log has **1,144 lines total**, down from >100,000 in v51; `DEV9 read/write`, `[MGRAW]`, `[MGFLOW]` each **zero**. Prior `Console.WriteLn` -> `HW_LOG` conversion **works** under trace-off conditions.
- Extended profile boot-latched and 256 MiB JIT cache active; per-reset `EE-JIT-CACHE-CONFIG mode=extended capacity=268435456 reserve=536870912 configuredMiB=256` observed.
- `EE-TLB-FAULT-SUMMARY faults=604096 suppressedFaultMessages=602977 suppressedBranchWarnings=21789 verbose=0` over run to ~67.76 seconds. In fine guest dispatch from ~23.23s to ~66.88s, this is ~13.8k TLB faults/sec on average (approximation, includes a small number of early faults and any pause periods); fault work itself remains, even with diagnostics suppressed.
- `EE/iR5900 Recompiler Reset` appears 8 times including initial boot, fine-dispatch transition, then at **31.52, 39.19, 45.28, 53.92, 59.37, 65.43s**. Exact causes not printed with trace off; cache exhaustion is plausible based on prior v47 profiling, but cannot label all six as proven exhaustion in this run.
- `[EmuCore/PSBBN] AutoSaveIntervalSeconds = 10`, capture at ~18.86, 28.93, 38.96, 49.01, 59.02s (synchronous VM collection with async compression/write). Autosaves can create local hitches. `VMManager::ResetPSBBNScheduledSaves()` source explicitly says **0 disables**.
- GS config `Renderer=-1`, Windows startup selects **D3D12 on RX 6800**, `upscale_multiplier=6`, `OsdShowCPU/OsdShowGPU/OsdShowGSStats/OsdShowFrameTimes=false` and no OSD FPS. At 6x, rendering can consume time independent of EE JIT and TLB; there is no evidence here assigning percentage to CPU or GPU.
- `[EmuCore/TraceLog] Enabled=false`, link witness, eviction trace and source witness all false, TLB error log false. Performance is not dominated by the previous thousands-of-logs-per-second path anymore. One-time MagicGate unsupported-key warnings remain (~555 raw `[MG]` lines) and are not the primary ongoing problem.

## Implemented v53: opt-in aggregate EE performance probe

`pcsx2/EEPerformanceProbe.h`: small header-only atomic counters. `[EmuCore] EEPerformanceProbe = true` latches at the initial EE recompiler reset and is **false by default**, enabled only in Extended mode. No guest-side emitted instrumentation; all work on host-side JIT compilation / TLB sync / physical code clear paths. Disabled path requires only a branch/relaxed atomic load.

`pcsx2/x86/ix86-32/iR5900.cpp`:
- Count each successful compiled EE block, output host code bytes, and aggregate inclusive host `compileUs` (existing `recRecompile` start/finish).
- Count total `recClearPhysical` calls, skip count for never-compiled physical code pages, and measure inclusive `physicalClearUs` for actual `EERecClearPhysicalBlocks` executions.
- Time `recResetRaw` `jitResetUs` across all resets and include count, without clearing probe counters during routine arena resets.
- On provider shutdown, emit exactly **one `EE-PERF-SUMMARY compiledBlocks=... compiledHostBytes=... compileUs=... tlbSyncs=... tlbSyncUs=... physicalClearRequests=... physicalClearCalls=... physicalClearUs=... noCodeSkips=... jitResets=... jitResetUs=... inclusive=1`**.
`pcsx2/COP0.cpp`: time `SyncActiveTLBMappings` independently of verbose COP0 trace.
`tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: added `EEPerformanceProbe.OptInCountersResetAndRecordOnlyWhenExplicitlyInvoked`, verifying counter independence and reset when disabled.

All categories are **inclusive and some overlap**: e.g. TLB sync directly calls code-clear, which is separately timed. Do not add their durations or interpret each as disjoint percentage of wall time. This is a coarse decision-support profiler, not a complete EE/JIT/GS flame graph. Atomic/timestamp overhead exists when enabled and can skew performance slightly. Leave heavy COP0/witness logs OFF.

## Two provided, versioned INI test configurations

Starting from supplied `PCSX2(3).ini`, save files `PCSX2_PSBBN_perf_6x_v53.ini` and `PCSX2_PSBBN_perf_1x_v53.ini`:
- identical except `[EmuCore/GS] upscale_multiplier=6` versus `1`;
- `[EmuCore] EEPerformanceProbe=true`;
- `[EmuCore/PSBBN] AutoSaveIntervalSeconds=0` (proven source opt-out);
- OSD show FPS, CPU, GPU, GS stats and frame times enabled;
- all HDD/image/graphics renderer inputs and Extended 256 MiB selection preserved, console and file logging enabled for one-line summary;
- all verbose witnesses and COP0 trace remain off.

**Test stages:**
1. Update and compile branch HEAD in Visual Studio; run the `EEPerformanceProbe` unit test where possible (source committed but not compiled by assistant).
2. Back up active INI, load the **6x** variant, fully restart VM. Confirm `EE-PERF-PROBE enabled=1 categories=compile,tlbSync,physicalClear,jitReset` and no PSBBN auto-save scheduled.
3. Run PSBBN to the same visible UI and wait comparable elapsed/guest activity; observe OSD EE/GS CPU and GPU metrics, frame time and sustained FPS. Pause/shutdown **normally** to obtain `EE-PERF-SUMMARY`.
4. Repeat with the **1x** variant, full restart and otherwise identical behavior. Compare FPS and EE/GPU OSD. If 1x raises FPS substantially, investigate GS/D3D12 rendering. If near identical and `compileUs` / `tlbSyncUs` / `physicalClearUs` large, focus next on the largest **measured** host activity. If all are small despite low FPS, investigate other overhead using an OS sampling profiler, considering guest execution, GPU stalls and host waits.
5. Both variants have autosave off, so their relative comparison isolates the **render scale**. Compare the new 6x variant with original autosave-enabled run only cautiously because the workload/checkpoints differ. If autosave stutter is of interest, restore interval 10 with the 6x variant for another separate A/B.
6. Do not change the EE JIT arena to 512 MiB for this test. Keep `EEJitExtendedCacheMiB=256`. Do not turn on trace to chase bottlenecks unless carefully isolated.
7. When profiling overhead itself must be measured, repeat once with `EEPerformanceProbe=false` for a separate controlled performance measurement.

**Build/test status:** all source changes and a unit test committed, **not MSVC compiled or PSBBN runtime verified by assistant**. v52 run validated only the prior unconditional DEV9 logging fix. Do not promise specific FPS gains.
