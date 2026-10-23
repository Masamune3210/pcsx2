# PSBBN v57: optimize high-frequency small EE TLB deltas (2026-10-23)

**Working branch:** `note-working/psbbn-tlb-small-delta-projection-20261023` (continue branch in place, leave `testing` untouched).
**Previous handoff:** `notes/PSBBN_TLB_SYNC_ROUTE_PROFILE_20261023_v56.md`.

## v56 evidence

Supplied `emulog(20261023-190756).txt`, actual executable `PCSX2 v2.9.92-453-gf095e27a4` with `EEJitExtendedCacheMiB=1472` and `EEPerformanceProbe=true`. Normal shutdown ~83.16s, one boot, fine dispatch ~25.19s, one mid-run JIT reset ~70.58s, UI previously approximately 15 FPS. Shutdown summary:

```
EE-PERF-SUMMARY compiledBlocks=5368203 compiledHostBytes=1969400234 compileUs=23932318 tlbSyncs=786214 tlbSyncUs=23189482 physicalClearRequests=4507196 physicalClearCalls=1749320 physicalClearUs=1129703 noCodeSkips=2061840 jitResets=3 jitResetUs=1100663 inclusive=1
EE-PERF-TLB-ROUTES memoHits=96466 memoUs=16554 smallDeltas=682685 smallDeltaUs=17843262 fullRebuilds=7063 fullRebuildUs=5329665 asidSyncs=83365 deltaCandidatePages=3277720 removed=2246276 installed=2246364 permissionOnly=39 unchangedOrRetained=20777091
EE-TLB-FAULT-SUMMARY faults=691753 suppressedFaultMessages=690614 suppressedBranchWarnings=24497 verbose=0
```

Route counts reconcile exactly: 96,466+682,685+7,063 = 786,214. Durations sum to 23,189,481us vs aggregate 23,189,482us, one-microsecond rounding difference. Memo: 0.016554s total (~0.172us/call), small deltas: **17.843262s / 682,685** (~26.14us/call), full rebuilds: **5.329665s / 7,063** (~754.6us/call). The latter are ~0.9% of calls but ~23% of time. The most important optimization target is the repeated **bounded delta**, ~77% of measured sync time. Do not optimize already cheap memo-hit code or blindly increase the JIT arena.

3,277,720 candidate VPNs accumulated over bounded deltas, ~4.80 candidates per small delta. Production `PlanEEActiveTLBDelta` called `FindEEActiveTLBPage` independently for *each* candidate, requiring repeated scans of up to 48 architectural slots and repeated mask/ASID decoding. After host mapping apply, production republished *every* candidate via `unordered_map::insert_or_assign` or `erase`, even if old/new selected page and permissions were exactly identical. There are millions of candidate pages, so these costs plausibly matter. Actual expensive host mapping operations (2.246m remove and 2.246m install overall) remain intact to preserve semantics.

## v57 source changes

1. `pcsx2/EEActiveTLB.h`:
   - Added `FindEEActiveTLBPages` for a bounded candidate array. Visit the 48 architectural slots in priority order **once**, test all unresolved VPNs against each slot, decode mask/ASID once per slot, preserve global versus ASID-specific, first-slot overlapping entries, valid halves, large-page physical offsets and scratchpad behavior. The old `FindEEActiveTLBPage` remains **unchanged as an independent correctness reference**.
   - `PlanEEActiveTLBDelta` initializes each delta's old active map pointer/value, then calls batched resolver to populate `newMapping` **directly into the existing fixed-size array** (no temporary 64-element optional buffer). All changed-page candidate enumeration and fallback logic remains unchanged.
   - Added `PublishEEActiveTLBDelta`: skip publication if `oldMapping == newMapping`, including both-null entries and exactly identical backing + writable state. All modified PFNs, permission-only changes, actual removals and new installs still publish. Returns number of real updates.
2. `pcsx2/COP0.cpp`: the existing small-delta apply ordering (all host removals before installations) and scratchpad batch mapping are unchanged. Replace unconditional per-candidate active map publication with `PublishEEActiveTLBDelta`, after host side effects, before scratchpad consistency checks. When probe is active, count publications actually made and redundant publishes avoided.
3. `pcsx2/EEPerformanceProbe.h` and `pcsx2/x86/ix86-32/iR5900.cpp`: two new aggregate counters appear in the existing **single** shutdown `EE-PERF-TLB-ROUTES`: `deltaPublished=` and `deltaNoops=`. They should sum to `deltaCandidatePages`. **No new per-event logging**.
4. `tests/ctest/core/ee_active_tlb_asid_tests_20261018.cpp`: new batched vs independent one-page first-slot lookup regression across global slots, multiple ASIDs, overlapping slots, scratchpad, PFN permissions, invalid halves, large pages, unmapped VPNs, page wrap; pure publication test checks identical/absent entries remain untouched and permission/PFN changes publish; refactored the existing 9-stage ASID/scratch/overlap/delta-vs-full-builder scenarios to use new publisher.
5. `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: additional probe counter reset assertions.

**Scope:** Source-level host optimization only. No changes to architectural TLB entries, branch delay rules, MMU data accesses, recClearPhysical, vTLB host map removal/installation calls, JIT cache sizing or reset rules. Standard mode never invokes the optimized small-delta path, remaining unchanged.

## Test

Update/build current branch HEAD with MSVC. No additional INI files or setting changes needed. Continue existing:
```ini
[EmuCore]
EEJitExtendedCacheMiB = 1472
EEPerformanceProbe = true
```
Keep verbose COP0 tracing, TLB fault logs, JIT witnesses, autosaves off; render scale unchanged. If possible run the `EEActiveTLB` pure unit tests and core tests, then boot PSBBN to the same UI, monitor FPS, verify IOP/EE correctness, shutdown normally.

Compare against the **v56 682,685 small deltas / 17.843s** only at similar guest progression, noting the instrumentation overhead and capture times vary. Look for smaller `smallDeltaUs / smallDeltas`, lower overall `tlbSyncUs`, and sustained FPS improvement. Interpret `deltaNoops` to see actual redundant publications avoided. Sanity:
- `memoHits + smallDeltas + fullRebuilds == tlbSyncs`
- `deltaPublished + deltaNoops == deltaCandidatePages`
- near-equal combined route times versus total `tlbSyncUs`
- PSBBN still reaches UI and exercises HDD; no new TLB faults or incorrect permissions.
- lower total time alone is not sufficient if guest stage/time differ.

If improvement is small, next target is **cost of actual vTLB mapping remove/install vs winner planning**, via a focused aggregation probe, not more speculative semantic changes. Full rebuilds remain ~5.3s per representative run and may require separate large-page handling investigation.

**Verification status:** GitHub source commits and regression tests, **not yet MSVC compiled or PSBBN runtime-tested** by assistant. Changes remain on the same note-working branch, not `testing`.
