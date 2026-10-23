# PSBBN TLB small-delta projection experiment (2026-10-23)

Branch: `note-working/psbbn-tlb-small-delta-projection-20261023`
Base: validated v44/v45 source-link detach branch, `note-working/psbbn-jit-retired-source-link-detach-20261023`.
Why: User explicitly requests a **new functional build to test**, not another report. UI-bringup log `emulog(9).zip` recorded 663,552 TLB synchronizations, 561,664 projection rebuilds, 56,478,055 unchanged page comparisons, ~23.35s of sync routine timings, 24 JIT arena resets. The link-detachment optimization worked across UI startup but did not alleviate heavy TLB projection churn.

Goal: new Extended-MMU fast path for **small** changed TLB slots. Enumerate affected guest virtual pages from changed old/current architectural TLB slot projections; re-evaluate the winner for only those pages, preserve first-slot overlap priority, and use the *existing* map/physical-code/dispatch invalidation actions on actual changes. Keep original full projection fallback for missing memo, broad page masks, too many candidate pages or Standard profile.

Verification required before accepting: compile and run EE active TLB unit/integration tests, PSBBN boot through UI, invariants for ASID changes, global/overlap fallback, scratchpad, dirty permission, PFN remap, code invalidation, flush/reset, and no new crashes. Compare `EE-MMU-CHURN` counters `deltaSyncs`, `deltaPages`, `fullProjectionFallbacks`, physical clear counts, `syncUs`, JIT arena exhaustion and actual emulation progress with v45 baseline. Disable verbose trace for timing-only matched run. This is NOT dormant JIT code reuse and does not eliminate code-cache exhaustion by itself.

Current state: implementation follows in subsequent commits. **No Windows build or PSBBN run** performed by assistant.
