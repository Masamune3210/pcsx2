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

Initial invariants were recorded before this mission's production changes; the
sections below distinguish implemented source from unexecuted validation.

First bounded checkpoint adds optional real eviction/host-byte/link/dispatch
mutation accounting to `BaseBlocks` and the physical clear helper. Identical
rel32 patches and identical physical-clear dispatch stores are skipped. Existing
lookup identities, source-link lifetimes and code allocation remain unchanged;
no code is reclaimed. Eight prospective helper regressions accompany the change.
Independent source review found no additional issue in those helpers. Compilation
and executable tests remain unrun under the existing user constraint.


## Coordinated implementation checkpoint

First checkpoint: `74ebb33ba5` (`perf(ee): avoid redundant code-cache patches and
account actual evictions`), pushed directly to the existing branch. Second source checkpoint: `7f0d0b85da`
(`perf(ee): memoize TLB projections and enforce translation boundaries`),
independently pushed before this completed handoff.

Implemented source in this mission:

- **Projection work avoidance.** `EEActiveTLBProjectionMemo` stores at most 48
  normalized, selected-slot inputs (roughly 1 KiB). When those inputs match,
  `SyncActiveTLBMappings` skips unordered-map allocation, page expansion and
  map diffing. Architectural writes, Random/cache descriptors and existing TLB
  side effects still occur. Equality includes selection/slot precedence, VPN,
  mask, PFN, valid/dirty and SPR behavior; it is conservative for hidden-slot
  changes. This is one last-projection memo, not an ASID-keyed retained-code cache.
  Production diff actions now share a testable remove-before-install planner.
- **Fewer exact patch stores.** `BaseBlocks::PatchLink` reads an unaligned rel32
  displacement with `memcpy`, compares the desired value and skips identical
  writes. Physical dispatch clearing similarly compares pointers before stores.
  Optional metrics distinguish attempts, actual writes and skips. Outgoing link
  records are deliberately retained under the existing arena-lifetime policy.
- **Fine-mode continuation boundaries.** Ordinary TLBR/TLBWI/TLBWR and MTC0
  EntryHi end the block after the instruction. All outgoing paths from a block
  containing those mutations use current virtual dispatch, including same-page
  links patched later. Translation-changing delay slots cannot move before a
  JAL/JALR link write. Native-mode classification/link policy is unchanged.
- **Boundary branch source handling.** A fine-mode branch at offset `0xffc`
  executes the branch and slot through the existing interpreter bridge even if
  the next page is currently mapped. The compiled extent excludes the runtime
  slot. The bridge now executes an ordinary untaken branch's slot, distinguishes
  JR targeting the slot from an untaken branch, preserves likely annulment and
  BD/EPC, charges residual cycles, and defers scheduler work until it restores
  the JIT provider and unwinds its private jump target. The JIT epilogue performs
  the normal event test. This closes the branch-slot case, **not all cross-page
  sources**: `recDI` eagerly compiles a following instruction and remains open.
- **Physical exclusive-end handling.** Block overlap/copy/high-water calculations
  add the compiled byte length to the physical start instead of translating the
  exclusive-end VPC, which can select a different mapping at the page boundary.
  This does not create complete discontiguous source ownership.
- **Projection lifecycle.** RAM-mode changes discard the active projection while
  old RAM bounds still apply; `vtlb_Init` discards it before replacing the host
  tables; state load discards it before loading architectural state. Native table
  reserve no longer resurrects the pre-reset architectural TLB during a RAM-size
  change; fine-mode internal reserve still reconstructs live aliases. The ranged
  fastmem removal path handles an unallocated fastmem view consistently with the
  whole-view removal path. No HDD savestate restriction was added.
- **Bounded diagnostic accounting.** Existing COP0 tracing gates host counters
  and timing; no new per-instruction JIT observer or native entry guard is emitted.
  Actual metadata removals/host body bytes, clear costs, patch stores, live block
  metadata, link records, compile time, source-fingerprint repeats, thunk bytes,
  backpatches and reset reasons can now be distinguished from raw clear requests.
  See the reading rules below before interpreting a capture.

These reductions are algorithmically narrower work, not measured speedups.
Conservative physical invalidation on unresolved mapping/data dependencies is
retained. Dormant reuse, per-block frees, epoch-based content validity and arena
recycling are **not enabled** by these changes.

## Validation ledger and limits

Allowed checks performed: tracked `git diff --check`; whitespace checks on all
new source/documents; native project XML parsing; registration checks for all four
new C++ test files; source review of all changed APIs/callers and lifecycle order;
relative-document link target checks. Independent reviews caught and corrected
reset memo persistence, RAM-resize alias resurrection, translation-slot swapping,
fixture global contamination and empty-fastmem fixture initialization hazards.
These checks are source evidence, not executable C++ verification.

Added **34 prospective C++ cases** across the two existing and four new suites:

| Cases | Layer covered by test source | Important limit |
| --- | --- | --- |
| 8 cache/link helper cases | Real BaseBlocks/physical-clear helpers: patch idempotence, publication, removal/overlap, current-compilation preservation, byte accounting, wide extents | No execution of emitted EE instructions or host fault paths |
| 8 projection/planner cases | Selected-input normalization, 48th slot, ASID reuse/global/overlap, masks/SPR, seeded 1,500-step independent page-selector/action replay | A model and production planner, not actual TLBWR instruction execution |
| 2 source-history cases | Diagnostic equality, bounded collisions and reset | Fingerprints are not reuse eligibility or a full compilation-context key |
| 5 boundary-policy cases | Mutator classification, translation-slot ordering, boundary-slot policy and branch-taken discrimination | Pure policy checks cannot establish generated-code behavior |
| 8 bridge cases | Real interpreter bridge with disposable host mappings: untaken/taken/likely/JR/JALR, slot-fetch faults, cycles, link/EntryHi ordering | No generated JIT branch emitter/dispatcher is executed |
| 3 Sync integration cases | Real Sync/host vmap/read/permission publication: both D directions, identical repeats, PFN/ASID/global/V, reset/init with identical inputs | No generated store or emitted constant-pointer/remap test |

The two integration fixtures initialize all vTLB/config/register state inside
GoogleTest child processes; they never restore only public MapData while leaving
private handler IDs/counts changed in the parent. Only helper/policy/history files
are added to the standalone native harness; full-core bridge and Sync files are
registered in `core_test` on x86.

**No compilation, CMake configuration, C++ test execution, emulator run, Windows
build, PSBBN replay, native game benchmark or savestate round trip was performed.**
The user's no-compilation instruction remains active. C++ buildability and all
runtime assertions remain unverified. Existing D-only behavior still needs real
emitted-store permission/fault tests in both directions; the new Sync integration
source does not substitute for that proof. No FPS, reset-frequency, CPU-time or
normal-game performance improvement is claimed. This branch is not qualified for
merging into `testing`.

## How to read the next authorized capture

Use the same guest stage, image clone, settings and trace configuration as the
historical baseline. Capture a tracing-on comparison for attribution, then a
tracing-off comparison for normal speed; bounded diagnostics still add overhead.
Record wall duration/guest-cycle deltas, workload stage, FPS, host CPU and RSS
alongside the aggregate log lines. Do not compare differently staged boots.

- `EE-MMU-CHURN` is cumulative since its existing trace reset. New
  `projectionRebuildsSkipped` counts entire memo hits;
  `projectionPagesRetained` counts the pages retained by those hits. Neither is
  a saved physical-block count. `unchanged` also includes pages on memo hits.
- `EE-JIT-CHURN` reports emitted body bytes and arena high-water use per cache
  interval. Exhaustion near capacity includes the existing 64 KiB guard reserve.
- `EE-JIT-LIFETIME` reports a raw guest-cycle snapshot and per-cache-interval
  counters. `liveHostBytes` sums body sizes of current block metadata, not total
  allocator residency or safely reclaimable bytes. `linkRecords` includes stale
  source records retained under the existing TODO. `thunkBytes` excludes generated
  dispatch/helper stubs. `compileUs` covers the compiler region after reset,
  including analysis and metadata, not just emitter instruction writes.
- `EE-JIT-MUTATION` separates known **caller causes**, not inferred physical
  writers. `virtual-clear` does not distinguish DMA/CPU/debugger; a host protection
  fault does not identify its producer. Scratchpad clears using the CPU callback
  remain in that broad category. `distinctRamPages` is distinct managed physical
  request pages per cause/interval, not distinct evictions; causes can overlap.
  `blocksEvicted`/`retiredHostBytes` count actual removed metadata/body bytes, not
  reclaimed storage. `clearUs` times the whole physical-clear helper, including
  lookup, dispatch stores, patching/removal and assertion work, excluding earlier
  bounds/presence rejections. Attempts can include repeated sites/ranges; compare
  actual writes and skips rather than interpreting attempts as unique addresses.
- `repeatSourceFingerprints` uses a bounded 4,096-slot direct-mapped history,
  retained across internal arena resets and cleared on provider reset. Matching
  VPC/physical start/length/mode/content hash is diagnostic only. Collisions can
  hide repeats; a content-hash match is not byte identity or complete context
  validity. It must never justify code reuse. History plus cause bitmaps use
  bounded static storage, roughly 170 KiB, not per-block allocations.
- Reset reasons are a mask: startup=1, provider=2, arena=4, dispatch-mode=8,
  thunk-exhaustion=16, gamefix=32. Combined reasons can be reported. Current live
  metadata and per-interval totals reset together; MMU totals have a different
  window. Derive deltas rather than adding mixed-window totals.

Missing measurement remains explicit: real producer attribution, full phase
separation (interpreter fallback/fault/EE execution), host map/protect call costs,
complete compilation-context reuse, active-frame residency and allocation size/
reuse-distance distributions. The instrumentation does not claim to complete all
of family A by itself.

## Architecture findings and ranked continuation

The completed [v28 writer/lifetime audit](PSBBN_CODEX_EE_JIT_WRITE_LIFECYCLE_MAP_20261021_v28.md)
maps CPU, DMA/external, cache writeback, host patch, load/reset, fastmem fault,
link and arena paths at baseline `86ed1f2fe3`. The completed
[v29 mechanism matrix](PSBBN_CODEX_EE_JIT_MECHANISM_MATRIX_20261021_v29.md)
evaluates every family A–J and the alternatives rejected/deferred, with exact
symbols, avoided mutations, costs, safety gates and falsifiers. It compares
version-pinned PCSX2 and QEMU sources without treating those designs as PS2 proof.
The matrix proposes a bounded dormant-variant pilot with runtime translation of
managed constant data accesses, complete source/content dependencies, guarded
entry and unchanged conservative arena lifetime. It remains a design.

Immediate queue, ranked by established safety need and measurable opportunity:

1. Run the registered helper/core/native tests once builds are explicitly
   authorized, then emitted-store/branch/mutator/reset tests and native BIOS/game/
   savestate regressions. Obtain matched Windows/PSBBN mutation/time captures.
   Actual retired blocks and compile time determine whether remap reuse merits
   its costs; raw requests and ten historical resets are insufficient.
2. Resolve `recDI`'s eager following instruction at page/block boundaries while
   preserving its intentional interrupt-delay compatibility behavior and precise
   faults. Add a real emitted-code falsifier before claiming one-PFN sources.
   The native path's physically discontiguous slots likewise require explicit
   source dependencies before a global retained-code architecture is safe.
3. Complete generated constant data-pointer/handler dependency ownership or use
   runtime translation for guest-managed constant accesses. Test data-only remaps
   with unchanged code VPN, both D directions, MMIO, fault and backpatched sites.
4. Establish writer/content barriers across manual protection mode and every
   producer. A physical clear is not a content epoch: manual-page writes may have
   no clear callback. PINE raw writes appear to run on the server thread; prove
   synchronization or marshal them safely before any content-version scheme.
5. Add stable source/target generation ownership for outgoing links, host-IP
   fault metadata and thunks. Prove an unwound execution safepoint before any
   host-byte reuse; ordinary invalidation permits returning into retired code.
6. If repeat compilation/actual eviction evidence justifies it, implement the
   bounded dormant-variant pilot with all required source/data/context guards.
   Separately evaluate region/clock reclamation using live/dead residency and
   owner fanout. Reject PFN-only/ASID-only hits, blind clear suppression, ring
   overwrite, free-on-clear, global arena enlargement as a solution, and a
   permanent interpreter substitute.

Do not retire these gates merely because helper tests eventually pass. Real
emitted-code, physical writer, host-fault, savestate and native-speed evidence
remain necessary. Continue on this existing branch with checkpoint pushes.


## Revision ledger

| Revision | Scope | Validation at checkpoint |
| --- | --- | --- |
| `86ed1f2fe3` | User's v26 mission and implementation baseline | Historical measurements only; inherited Windows/source gaps remain |
| `74ebb33ba5` | Idempotent BaseBlocks link/physical dispatch writes, actual removal/body-byte metrics, eight cache helper cases, initial invariants | Independent source review; source checks; pushed |
| `7f0d0b85da` | Projection memo/planner; fine translation/branch boundaries; reset lifecycle; bounded telemetry; remaining 26 prospective cases and registrations | Independent source review; whitespace, registration and project XML checks; pushed; no C++ execution |
| Follow-on documentation checkpoint | Completed v27 record plus v28 producer/lifetime audit and v29 A–J alternatives matrix | Relative links and whitespace checked; use branch log to identify document-only commit |

Reproduce the checkpoint comparison without a build:

```sh
git log -4 --oneline
git diff --stat 86ed1f2fe3..7f0d0b85da
git diff --check 86ed1f2fe3..7f0d0b85da
git status --short
```

When executable validation is later authorized, run the full registered suites
and the generated-code/runtime falsifiers listed above; do not interpret this
source-only command list as the required runtime matrix.
