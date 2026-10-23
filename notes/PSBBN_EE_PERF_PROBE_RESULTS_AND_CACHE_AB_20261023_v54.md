# PSBBN performance investigation: v53 runtime probe interpretation (2026-10-23)

## Starting point and evidence

Same active branch: `note-working/psbbn-tlb-small-delta-projection-20261023`, current v53 functional revision `769b8954f08c9d9d9f50a3e3b2f154f2c84d3633`. Source-only probe was added in v53. No new functional optimization is established in this note.

Supplied run: `emulog(20261023-183354).txt` (2078 logical lines, 128428 bytes), executable `PCSX2 v2.9.92-436-g769b8954f`, confirms correct v53 executable. `EE-PERF-PROBE enabled=1` at timestamp 17.4154. Normal shutdown 111.767; run includes a VM reset at 77.35, two PSBBN boots. Aggregated counters reflect both boots. Startup selected ExtendedMMU, 256MiB *active* JIT generation, 512MiB *reserved* region, D3D12 RX 6800. No scheduled PSBBN autosave entries visible in this capture (user was asked to disable via AutoSaveIntervalSeconds=0).

Shutdown line exactly:
```
EE-PERF-SUMMARY compiledBlocks=7121809 compiledHostBytes=2509483393 compileUs=24526442 tlbSyncs=993477 tlbSyncUs=28835212 physicalClearRequests=6444438 physicalClearCalls=2339643 physicalClearUs=1317374 noCodeSkips=2711738 jitResets=12 jitResetUs=1418064 inclusive=1
```

Interpretation:
- EE compilation: 7,121,809 blocks, 2,509,483,393 bytes cumulative emitted host code (~2.34GiB), 24.526442 s measured; average 3.44 us per compiled block.
- TLB projection sync: 993,477 calls, 28.835212 s measured; average ~29 us per synchronization.
- Physical invalidation requests 6,444,438; 2,339,643 proceed to actual host code-block clearing (1.317374 s across actual clear calls); 2,711,738 skipped due to never-compiled-page test. Remaining requests may be bounds rejects/other paths; not classified by this summary.
- Whole JIT reset count 12, measured total 1.418064 s; log resets at 17.4156, 32.8875 (first 4K dispatch transition), 40.8096, 48.1243, 54.0641, 62.4976, 68.3276, 73.7919, 77.3541 (new VM/reset), 92.2348 (second 4K dispatch), 99.6866, 107.2404. Eight between boot dispatch transitions/resets look consistent with arena cache exhaustion, but reset reasons are not printed in trace-off mode. Never label all resets as proven arena exhaustion from this log alone.
- These timings are **inclusive and some overlap**, especially `tlbSyncUs` includes some code clear work separately counted in `physicalClearUs`. JIT compiler timing and TLB sync timing account for over 53 seconds of host work by themselves, in the ~94s profiling interval. They are not independent whole-CPU percentages or proof of the precise per-frame stall.
- `EE-TLB-FAULT-SUMMARY faults=201038 suppressedFaultMessages=200309 suppressedBranchWarnings=9832 verbose=0` reports only the later VM provider lifetime; do not compare it as an equivalent ~94s fault count or conclude faults have become rare.
- MagicGate raw dump and EE DEV9 hot-path log floods remain absent. The `[MG]` key warnings are mainly during boot/decryption, not the source of hundreds of thousands of steady-state logs.
- Low GPU usage and ~75% EE utilization were already observed by user; scaling resolution down is not the next recommended experiment.

## Source reading

`pcsx2/COP0.cpp:SyncActiveTLBMappings` has three control-flow routes:
1. Projected TLB memo unchanged (early return)
2. Small delta up to 64 candidate VPNs, `PlanEEActiveTLBDelta` resolves first-slot winner for each VPN
3. Full active TLB rebuild fallback.

Both ASID shifts (`WriteCP0EntryHi`) and TLB register writes (`MapTLB`) can drive synchronization. The perf probe only exposes **total time/calls**, not route or event causes, and the older COP0 trace has route counts but is deliberately disabled for representative performance. `EEActiveTLB.h` uses linear scans of up to 48 TLB entries for memo/first-slot resolution. Do not infer a particular route is expensive until measured.

The measured code clear operation is only ~1.3s, while sync as a whole takes ~28.8s, so blindly optimizing `EERecClearPhysicalBlocks` or suppressing more logs would not address the majority of host time.

## Next experiment: 256 vs 512 MiB EE cache (single setting)

The source already supports `[EmuCore] EEJitExtendedCacheMiB = 512`, no functional code change required. Previously 256 vs 64 changed reset counts but FPS was still heavily affected by unconditional logging. With the logging flood fixed, this is a worthwhile **controlled experiment**, not a proven fix.

1. Keep `EEPerformanceProbe=true`, `AutoSaveIntervalSeconds=0`, ExtendedMMU, global EE COP0/hardware trace/witnesses false; keep GS 6x and all other settings the same. Record current **256 MiB** capture as baseline.
2. Set `[EmuCore] EEJitExtendedCacheMiB = 512`, fully restart the emulator's VM, boot to the same PSBBN UI screen, observe FPS during comparable workload, then shutdown normally.
3. Capture `EE-PERF-SUMMARY`, active `EE-JIT-CACHE-CONFIG`, reset count and elapsed time. If increasing cache improves compilation/reset time and sustained FPS, source of unnecessary recompiles includes generation recycling; if FPS barely changes, next target remains the **measured 28.8s TLB sync cost**.
4. Do not enable verbose COP0 tracing in the same comparison (its recompile fingerprints perturb measurements). Do not change res scale or other settings concurrently.
5. For future optional code instrumentation, measure memo hit vs small delta vs full rebuild timing, ASID causes, mapping removals/installs, and skip/physical clear counts. Avoid unproven changes to guest mapping semantics.

## Verification / branch state

v53 compiled and executed by user; probe summary is credible with stated limitations. An attempted *source-only extension* to record per-route timing was **not completed**; its temporary header change was reverted to the v53 content before this handoff was written, so the branch's latest functional behavior remains v53. No v54 source performance optimization is committed or claimed. No local MSVC/PSBBN run performed by assistant.
