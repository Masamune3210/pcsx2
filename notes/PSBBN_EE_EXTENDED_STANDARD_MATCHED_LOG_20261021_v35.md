# PCSX2 296 Extended vs Standard log comparison, 2026-10-21 (v35)

Repo: `Masamune3210/pcsx2`
Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Current policy: `notes/PSBBN_EE_PROFILE_SCOPE_DECISION_20261021_v33.md`.
UI selector: `notes/PSBBN_EE_EXECUTION_PROFILE_QT_UI_20261021_v34.md`.

## Source and confidence

User supplied **two** compressed PCSX2 emulogs from identical `v2.9.92-296-g96ea57576` binaries:

- **Standard**: `emulog(1).zip`, 258,782 log lines, `EE execution profile: Standard (setting=Auto, boot disc serial=20020207-164243)`.
- **Extended**: `emulog(2).zip`, 182,584 log lines, `EE execution profile: ExtendedMMU (setting=Extended, boot disc serial=20020207-164243)`.

All comparisons below come from actual logs, not source predictions. The logs are not checked into GitHub (they are uploaded in the conversation). Source inspection used current branch `BaseBlocks::Link/New/Remove`, `EERecClearPhysicalBlocks` and `recClearPhysical`.

The user reports approximately **4 FPS** on Extended, compared with about **1 FPS** in the prior Standard session. **These are user-observed instantaneous values, not log-derived FPS.** The matched guest-cycle comparison below does not demonstrate an overall 4x emulator throughput gain. Do not represent this as one.

## Hard measurements

| Metric | Standard | Extended | Interpretation |
|---|---:|---:|---|
| Guest execution mode after 4 KiB instruction dispatch | yes | yes | transition from native mode occurs at exact same guest PC `0x0fb643c0` |
| Guest cycle at transition | 3,138,150,883 | 3,138,150,883 | same guest checkpoint |
| Wall-clock after transition until ~3.68B guest cycles | ~21.54 s | ~22.21 s | Extended is ~3% **slower** over this matched interval, contrary to claimed 4x aggregate speedup; stage and logging may influence UI FPS |
| Full 64 MiB arena-exhaustion resets before ~3.68B cycles | 4 | 4 | exact same count of capacity resets after initial mode transition |
| Total TLB sync at 155,648-sync checkpoint | 155,648 | 155,648 | exact matching counter |
| Cumulative time in TLB mapping sync at 155,648 | 8,448,351 us | 8,407,751 us | only ~0.5% reduction overall, though instrumentation and workload variation limit attribution |
| Projection rebuilds at 155,648 | 155,648 | 112,993 | Extended skipped 42,655 (~27.4%) |
| Physical code clears skipped because page never compiled | 0 | tens of thousands per arena generation | policy activated; raw request saving is not the same as saved guest execution time |
| Permission-only shortcut at 155,648 | 0 | 0 | This optimization saw no eligible transitions at summary checkpoint |
| Same-sync duplicate physical clears avoided at 155,648 | 0 | 104 | negligible compared with 1,570,257 requests |
| Guest `cpuTlbMiss` print lines, whole log | 188,732 | 116,680 | different run durations; very high synchronous logging volume itself may disturb FPS measurements |
| `branch delay!!` print lines in Extended | not yet tabulated | 6,442 | noisy logging and exception hotspot; not itself proof of correctness failure |

Arena generations near ~3.648B guest cycles in Extended:
- Reset at 72.5955 sec, `cacheUsed=67,043,455 / 67,108,864`, `blockCompiles=199,102`, `compiledBytes=66,970,938`.
- Immediately before this reset, live metadata represented `7,460 blocks`, `2,414,070 host bytes`, while `259,682 linkRecords` persisted. A mere ~3.6% of total emitted code bytes were still represented by live block metadata. Note helper code and allocator overhead are not included in live metadata, so this ratio is indicative but not literal reclaimable bytes.
- `repeatSourceFingerprints=107,652` during that same generation, highly suggestive of substantial recompilation of previously seen guest source but not proof that cached code was reusable under the same translation/data dependencies.

Next generation at 75.2377 sec (65,536-compiles checkpoint; no arena reset yet):
- `liveBlocks=6,033`, `liveHostBytes=2,064,439`, `linkRecords=181,475`.
- `linkAttempts=13,679,600`, `linkWrites=13,679,600`, `linkSkips=0`.
- Mutation attribution per cause since 72.5955 reset:
  - **ASID projection**: `blocksEvicted=88,277`, `retiredHostBytes=27,142,874`, **`linkWrites=12,445,381`**, `requests=90,167`, `neverCompiled=53,191`, `clearUs=203,263`.
  - TLB projection: `blocksEvicted=35,567`, `retiredHostBytes=12,853,789`, `linkWrites=1,049,435`, `requests=22,850`, `neverCompiled=12,554`, `clearUs=42,698`.
  - Virtual-clear: `blocksEvicted=1,132`, `linkWrites=11,987`.
  - Direct host-write-protection: `blocksEvicted=5`, `linkWrites=0`.
  - Manual reprotect: `blocksEvicted=58`, `linkWrites=65`.

**Source-supported interpretation:** `BaseBlocks::Link()` registers target-PC -> host jump address in a `std::multimap`. `BaseBlocks::Remove()` patches every recorded incoming edge to the recompiler; `BaseBlocks::New()` patches every recorded edge for the same target to the new compiled code. The branch already observes that outgoing link records can outlive removed source blocks and are retained until complete arena reset. This can cause enormous repeated writes to dead source code. The source proves the records persist; the log DOES NOT separately distinguish dead-source patch operations from live-source operations. Thus 12.4M writes is a compelling target for new instrumentation, **not** authorization to delete link entries without execution-unwind/ownership proof.

## Prioritization changed by this run

1. **Owner-aware link ledger and precise lifecycle counters**: Track a generated host link's source block/arena generation, target, live-versus-retired source status, current target validity, and whether removed source blocks can still be active on any return/exception path. Add trace-only counters for patches to registered live sources vs retired sources, and distribution of patch fan-out per target. Do not remove old records until safely unwound; do not recycle code blindly.
2. **Reduce unnecessary ASID-based code eviction** only after identifying full instruction and constant-data translation dependencies, entry/link guards and writer barriers; code cannot be retained on source PFN identity alone. The ASID path dominates block eviction and link patching.
3. **Reduce active TLB projection work**: 155,648 syncs and ~8.4s measured time, with most 48-slot full projection work still occurring. Consider incremental diff / per-entry index but preserve precedence, global entries, page sizes, V/D permissions and all exception semantics.
4. **Keep bounded logging in future performance comparisons**: 116k TLB-miss prints in a short run are perturbative. Compare a short diagnostic run to a trace-disabled runtime test on a cloned HDD to distinguish logging cost. Do not suppress user-required fault diagnostics without a toggle or aggregated counters.
5. **No premature dormant reuse/reclaim**: lifetime source link ownership, active execution return addresses, host backpatches, noncontiguous instruction source PFNs, data host pointers, savestates and external writer notifications remain unresolved.

## Next implementation boundary

The profile selector works by log (`ExtendedMMU (setting=Extended)`). The runtime optimization is effective at skipping work, but neither the reset frequency nor matched guest-cycle throughput has materially improved.

Safe next code is **bounded, trace-gated instrumentation of link source ownership / patch fan-out**, accompanied by tests and a v36 handoff. A bigger cache, clearing fewer counters, or blindly retiring link records is NOT an adequate root-cause correction.

No source code was modified during this diagnostic analysis. Do not claim any unrun tests, new Windows builds, or improvements based on this report.
