# EE JIT coherence mission: invariants and execution record

Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
Mission baseline: `86ed1f2fe3`, including v25's metadata-extent correction.
This is a new handoff for v26; previous measurements remain historical baseline,
not validation of subsequent changes. No guest-specific bypass is authorized.

## Coherence invariants established before further implementation

1. **Instruction content.** Every compiled instruction, including a branch delay
   slot, must be associated with its actual physical source bytes. A physical
   write must invalidate every dependent block. Current contiguous `BaseBlocks`
   metadata is insufficient for physically discontiguous source pages; v25's
   bitmap marking repairs only the negative filter, not this dependency hole.

2. **Entry eligibility.** A virtual instruction address may enter a block only
   when the current architectural translation selects that block's source and
   its compilation assumptions remain valid. Invalid/unmapped pages must fault.
   Physical identity alone does not establish equivalent virtual branch targets,
   exception PCs or delay-slot state. ASIDs may be reused or remapped.

3. **Data dependencies.** A compiled constant-address memory operation can embed
   a translated host pointer or handler. Retaining its instruction PFN does not
   validate a changed data VPN. Code reuse requires runtime translation or
   complete registered data-mapping dependencies and entry/continuation checks.
   EntryLo.D checks must occur before any write/RMW side effect; guest permission
   and independent host SMC protection remain distinct.

4. **Mid-block changes.** A translation/context change cannot permit subsequent
   generated instructions to use stale fetch/data assumptions. A dispatcher exit,
   interpreter boundary or equivalent checked continuation is required. Links
   must not bypass entry guards, including links patched after compilation.

5. **Publication and invalidation.** Register source/dependency metadata before
   publishing a callable entry. Unmap virtual dispatch before it can execute an
   invalid mapping. Retain real write invalidation independently of TLB lifetime.
   Conservative filters may reject work only when absence of affected code is
   established; false positives are safe, false negatives are not.

6. **Lifetime.** Logical invalidation does not prove that generated frames have
   unwound: a helper may return to invalidated code. Code, thunks, branch patch
   addresses and host-IP backpatch metadata need common ownership before reuse.
   Retiring a block, forgetting its lookup entry, and reclaiming its host bytes
   are separate operations. No free-on-clear or blind arena wrap is permitted.

7. **Lifecycle reset.** Savestate load, provider reset, RAM/configuration changes
   and dispatch-mode changes must not leave live host pointers or generations
   referring to discarded code. Preserve user-enabled HDD savestates; external
   disk rollback is outside this cache mission. Native 64 KiB execution remains
   the default fast path and is a required regression workload.

Dormant compiled-code reuse is **not yet established safe** under invariants 1–4.
Segment reclamation is **not yet established safe** under invariants 6–7. Keep
existing conservative invalidation until those prerequisites are demonstrable.

## Work sequence

- Map the actual write, translation, lookup, linking, protection, reset and
  reclamation paths; identify unknown/missing producer barriers explicitly.
- Add bounded actual-block/byte/mutation accounting, separating requests from
  successful evictions and logical retirement from arena high-water usage.
- Implement low-risk no-op and projection reductions without changing effective
  ASID/global/overlap/permission semantics.
- Establish cross-page source and mid-block translation-change boundaries;
  measure and reduce redundant patch writes without reclaiming active code.
- Evaluate all v26 families, including rejected alternatives and the smallest
  safe architecture for retained variants or reclamation.
- Review source/tests, run allowed source-only checks, and push independent
  checkpoints. No compiler/CMake/C++ execution, Windows build or guest run is
  claimed unless separately authorized and actually performed.

## Status

Initial invariants recorded before this mission's production changes.

First bounded checkpoint adds optional real eviction/host-byte/link/dispatch
mutation accounting to `BaseBlocks` and the physical clear helper. Identical
rel32 patches and identical physical-clear dispatch stores are skipped. Existing
lookup identities, source-link lifetimes and code allocation remain unchanged;
no code is reclaimed. Eight prospective helper regressions accompany the change.
Independent source review found no additional issue in those helpers. Compilation
and executable tests remain unrun under the existing user constraint.
