# PSBBN v46: bounded incremental TLB projection experiment

**Branch:** `note-working/psbbn-tlb-small-delta-projection-20261023`  
**Baseline:** `note-working/psbbn-jit-retired-source-link-detach-20261023` (validated v44/v45 link policy)  
**Trigger:** `emulog(9).zip` full UI bringup, 663,552 TLB synchronization events, 561,664 full page-map projections, 56,478,055 unchanged page comparisons, 4,181,633 physical-clear requests and approximately 23.35 seconds cumulative timed synchronization, while link detachment succeeded across 25 fine-mode JIT generations. **Do not confuse sync routine timer with a clean guest FPS percentage.**

## Functional changes committed for user testing

- `pcsx2/EEActiveTLB.h`: `EEActiveTLBProjectionMemo::CollectChangedPages` enumerates the union of old/new virtual pages contributed by changed selected TLB slots. It rejects missing/stale memo, masks too broad for a 64-page budget, or combined changes over the cap. `FindEEActiveTLBPage` recomputes the first matching architectural TLB slot at an individual VPN, with ASID/global, scratchpad, validity, PageMask and PFN offset semantics identical to the full builder. `PlanEEActiveTLBDelta` supplies old/new page data without changing emulator state.
- `pcsx2/COP0.cpp`: On Extended MMU when the existing memo does not wholly match, attempt the small-delta plan. Apply the existing removals, physical code clearing and virtual dispatch invalidation first; then installs; preserve permission-only mappings, unchanged pages and scratchpad behavior; publish the changed live-map entries and update the memo. **Standard**, broad masks, unknown previous state and changed windows larger than 64 pages use the original full-map builder and two-phase planner. The full-map scratchpad publication sequence is unchanged.
- Under existing `PSBBNTlbTraceEnabled()` instrumentation, append to `EE-MMU-CHURN`: `smallDeltaSyncs`, `smallDeltaPages`, `fullDeltaFallbacks`, `deltaChecks`, `deltaMismatches`. `projectionRebuilds` now counts **actual** full map rebuilds, not delta updates. At every 1,024th traced delta candidate, independently rebuild a complete desired map and compare it with the proposed delta overlay **before** altering host state. On mismatch emit `EE-MMU-DELTA-MISMATCH` and fall back to full reproject; ordinary trace-free emulation pays no sampling overhead. Invariants: `syncs = projectionRebuildsSkipped + smallDeltaSyncs + projectionRebuilds` (for clean accumulators).
- Tests: `ee_active_tlb_asid_tests_20261018.cpp` exercises true ASID transitions, COW-style PFN/dirty changes, invalid entries, first-slot collisions, SPR/scratchpad and broad-masked fallback and checks exact map equivalence. `ee_active_tlb_sync_tests_20261021.cpp` adds a real host-vTLB test **explicitly selecting Extended MMU**, changing one virtual-to-physical mapping while another guest VPN's host pointer stays unchanged; D-only permission updates must not remap host backing. Both test files were modified in place on this experimental branch.

## Verification status

Committed source and test definitions; **Windows/MSVC compilation, GTest execution and PSBBN runtime are unverified**. This branch intentionally enables the optimization in Extended MMU without a new opt-in switch, so testing the branch itself tests the feature. It retains the validated v44 link-detachment policy; neither dormant source-code retention nor permanent recompiler memory reclamation is implemented.

### Test instructions

1. Fetch and switch to `note-working/psbbn-tlb-small-delta-projection-20261023`. Build with your normal Visual Studio/MSVC PCSX2 setup.
2. Run `core_test` (or the EE active TLB tests in that binary). Verify no assertion/failing death test, especially `SmallTLBSlotDeltaRemapsOnlyChangedRealHostPage`, `SmallDeltaProjectionMatchesFullMapAcrossASIDsRemapsAndPermissions`, and `SmallDeltaRespectsFirstEligibleOverlappingSlot`.
3. Run PSBBN through userspace to the UI, using **Extended MMU**, the same HDD/savestate/boot settings and matching guest progress as v45. Retain existing COP0/TLB tracing for the first correctness capture so `EE-MMU-CHURN` and sampled check counters are emitted. **The key result is nonzero `smallDeltaSyncs`, zero `deltaMismatches`, and no new guest exceptions, failing root mount or UI regression.** Also ensure `EE-JIT-LINK-DETACH` still has `postDetachRetargets=0` if that witness remains enabled.
4. For performance, do a separate matched run with high-volume TLB and link-owner witnesses **disabled on both baseline and v46**; record guest-cycle advancement, arena reset rate/compile counts, CPU usage, wall time to the same UI checkpoint. Do not use log-heavy runs to claim speedups.

### Contingencies / next expected step

- Compiler errors: capture full diagnostic and exact failing path/line, amend this branch, do not modify `testing`.
- `EE-MMU-DELTA-MISMATCH`: retain the log, check which old/new slot contributions were omitted and correct/fallback safely; do not waive the discrepancy.
- Equal counters or no gain: if `smallDeltaSyncs=0`, investigate selected-slot change geometry (large mask/ASID fanout) before raising the 64-page budget.
- The 64 MiB JIT code-cache exhaustion and possible compiled physical-code reuse remain separate, unresolved problems. The purpose of this experiment is to **reduce active guest TLB projection work without weakening invalidations**, not to solve all recompilation churn.
