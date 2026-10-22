# PSBBN v41: observational EE JIT link ownership lifetime witness

Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Predecessor: `PSBBN_EE_SOURCE_WITNESS_RESULTS_20261021_v40.md`.
Status: **source implemented and committed; no Windows build or PSBBN run performed yet**.

## Why this branch step exists

The v40 physical-source witness identified repeated source-identical recompilations after active TLB/ASID projection removes a guest alias. It did **not** prove whether retiring an associated `BASEBLOCKEX` can preserve host rel32 links or outstanding native return points.

`BaseBlocks::links` is a target-physical-PC -> rel32 site multimap. The original `Remove` patches incoming sites back to the recompiler, but deliberately leaves their entries in `links` until arena reset: the removed *source* may still have generated host code or outstanding returns, and link sites remain in the arena. This makes the existing aggregate 10M+ relink writes ambiguous.

## Implemented: observational-only link ledger

- `pcsx2/x86/BaseblockEx.h/.cpp`: optional link observer receives `Register`, `TargetCompiled` and `TargetRemoved`, with actual rel32 patch-write versus skip information. `PatchLink` returns the existing write/no-write result but keeps original displacement logic. No jump-selection or removal policy changed.
- `pcsx2/x86/EERecLinkOwnershipWitness.h`: bounded (400,000 registered sites per arena) owner+site ledger. Each source owner stores original physical and virtual PC, ASID, host pointer, maximum rel32 displacement extent, finalized host size and metadata-retired flag. Stores 8 compact examples of post-retirement repatches.
- `pcsx2/x86/ix86-32/iR5900.cpp`: opt-in Extended-MMU-only setting, callback registration, block finalization, retirement observation *before* existing link redirection, generation reset/shutdown summaries, and explicit counts of translation-changing/cross-page dispatch-only link decisions.
- Every existing `BaseBlocks::Remove` call still executes its original eviction callback when enabled. When only the link witness is enabled, the wrapper observes retirement without enabling the verbose eviction tracer.
- Standard execution has its observer unset, no guest code instrumentation, and unchanged invalidation/dispatch policy.

### Configuration for diagnostic PSBBN run

Add under `[EmuCore]` in the active PCSX2 INI settings (or the matching per-game override if applicable):

```ini
EEJitLinkOwnershipWitness = true
EEJitEvictionTrace = false
EEJitSourceWitness = false
```

The setting latches on the first recompiler reset of the VM session; **fully restart emulation after editing it**. The earlier source witness can be enabled concurrently if desired, but avoid the unlimited eviction trace. This diagnostic collects allocation-heavy metadata and is **not suitable for FPS benchmarking**.

### Log records and interpretation

- `EE-JIT-LINK-OWNER`: registered sites; initial registration writes; per-generation tracked site/owner counts; dropped/unowned sites; finalized and retired owners; retirement while the EE recompiler execution loop is active; invalid site offsets; direct-dispatch decisions.
- `EE-JIT-LINK-REPATCH`: two rows, `target-compiled` and `target-removed`. Each reports attempted and actually changed rel32 patches grouped by source owner: `live` (metadata retained and host size finalized), `retired` (metadata removal callback occurred), `compiling` (owner not finalized), or `unknown` (untracked / cap exceeded).
- `EE-JIT-LINK-SAMPLE`: first eight post-retirement repatches, with source and target guest physical PCs, source virtual PC and ASID, recorded host site, source host start/size, patch destination and actual write flag.
- Summaries print at arena reset and provider shutdown, or with existing enabled EE TLB/compile metrics at 65,536-block intervals. The link witness is **separate from the older verbose eviction records**.

A `retiredWrites` result does **not** by itself prove a bad patch: the original implementation intentionally preserves link records. Conversely, `retiredDuringEEExecution` means the EE JIT execution loop was entered when retirement occurred; it does **not** prove that the particular retired owner had an outstanding host return address. `live` likewise means the source metadata survived, **not** that it is currently executable under the guest ASID.

### Expected verification

1. Build the branch on Windows and resolve compiler errors before evaluating runtime behavior. No successful build, unit test, or PSBBN boot is claimed for v41.
2. Run PSBBN with Extended MMU, link witness on, old eviction trace off. Capture `EE-JIT-LINK-OWNER`, `EE-JIT-LINK-REPATCH`, `EE-JIT-LINK-SAMPLE` and `EE-JIT-CHURN reset` records through at least one true arena-exhaustion reset.
3. Check `invalidOffsets == 0` and `droppedSites == 0` before relying on the state breakdown. If either is nonzero, examine the examples and instrumentation assumptions.
4. Compare `retiredWrites` and `liveWrites` separately at target compilation and target retirement. This is the requested **direct-link ownership split**, not a performance fix.
5. Run a separate trace-off comparison if performance is measured.

## Still unresolved: active native return / safe dormant code reuse

This witness tracks metadata state, not the host instruction pointer or return stack. A native return can still reach the middle of a compiled block after the target's `BASEBLOCKEX` has been removed. The existing dev-build entry breakpoint poisoning expressly does not make the entire host block unreachable.

Before retaining or reactivating dormant code, establish a safe execution/quiescence boundary or a generation-aware guard for outstanding host control-flow references. Also prove ASID-sensitive instruction/data fetch and embedded helper-pointer dependencies, source permissions, RAM write/DMA invalidation, all source PFNs, and translation generation. **Do not turn off physical invalidation, restore stale incoming patches, or enlarge the arena to conceal churn.**

## Verification status / handoff

- v40 source witness findings are retained unchanged.
- v41 link witness committed in the same existing branch; no new branch.
- No emulator boot, compile, test, Ghidra analysis or speed-up claim in this step.
- Next expected input: Windows compiler diagnostics or a bounded PSBBN log from this configuration.
