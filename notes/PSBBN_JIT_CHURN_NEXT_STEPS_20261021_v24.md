# EE JIT churn: permission-only fast path and cache-retention priorities

Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
The user requested completion of the physical-page filter, followed by continued
work on the severe recompiler cache churn. Filter integration and its compile-time
RAM-sizing correction are checkpointed at `dbb10733f3`; see v23.

## Narrow implemented follow-up: separate D from backing identity

`SyncActiveTLBMappings` previously treated an EntryLo.D change exactly like a PFN
change: remove the vTLB/fastmem alias, clear physical code, unmap instruction
dispatch, then reinstall and clear again. If the same valid virtual page still
selects the same physical page and RAM/scratchpad kind, none of its backing bytes
or host pointers changed. D is a write permission, not host code protection.

`EEActiveTLBPage::HasSameBacking` now separates backing identity from full
architectural equality. Both projection-diff loops use that comparison. A D-only
update retains host mappings, JIT dispatch and compiled code; the existing final
swap publishes the new permission map. Full `operator==` still compares D.
Mapping addition/removal, PFN changes and scratchpad/RAM changes keep their
existing paths. There is no blanket relaxation of remap invalidation.

Source basis for both directions:

- `Memory.cpp::memSetPageAddr` passes only VPN and PFN to `vtlb_VMap`; neither
  that API nor `recMapTLBPage` specializes mappings on guest D.
- Fine-mode stores always emit `recCheckTLBStore` before the store/RMW operation,
  irrespective of D at compilation. It consults the current active map.
- Interpreter stores likewise check `EEInterpreterTLBStoreNeedsModification`.
- Therefore D=1 to D=0 still faults before writes, while D=0 to D=1 permits the
  store subject to independent host self-modifying-code protection.

Existing `EE-MMU-CHURN` summaries now count `permissionOnlyUpdates` separately
from `unchanged`, `removed` and `installed`. This measures opportunity instead of
assuming D-only changes dominate Linux's mapping traffic. Two source-first
prospective cases check both permission directions after map publication, full
equality still noticing D, and rejection of changed PFN/memory kind. They test
the production helper and permission predicate, not full COP0 synchronization.
Independent source review found no actionable defect in this diff.

## What the existing measurements establish

V22 records ten fine-mode arena-exhaustion resets, each roughly 99.9% of the
64 MiB arena; the 64 KiB compiler reserve explains the remaining headroom.
Each fill generated approximately 188,000 to 202,000 blocks and 67 MB of host
code. Physical clears do not rewind the bump allocation pointer. Invalidating
and recompiling code repeatedly consumes more arena bytes even when the live
working set is small. The same log records about 13.58 seconds inside TLB sync;
that excludes later compilation time. These are prior handoff measurements,
not a fresh run performed here.

The bitmap avoids searches for never-compiled pages. The D-only path avoids
unnecessary eviction of actual code for that specific change. Neither establishes
that FPS or full-cache-reset frequency has improved until a matching run measures
it. Increasing arena size only extends the fill interval if the same churn persists.

## Recommended next work, in order

1. **Measure eviction cost and reuse potential.** Existing clear-call counts are
   not counts of removed blocks. Add bounded aggregates for actual blocks and
   host bytes evicted by cause, compilation time, live bytes at exhaustion,
   repeat compiles of the same code/context, link patches and explicit reset
   reason. Compare `permissionOnlyUpdates`, bitmap skips, TLB sync time and
   cache-fill rate over matching guest workload windows.

2. **Retain translations with explicit dependencies.** Separate virtual dispatch
   eligibility from instruction-content lifetime. Retain inactive variants keyed
   by virtual PC, physical instruction-page identity/version, compilation mode
   and demonstrated compilation assumptions. Validate mappings before rebinding
   dispatch; actual RAM writes invalidate all dependent variants. ASID alone is
   insufficient because an ASID can be reused or remapped. Cross-page targets,
   delay slots, exception PCs and direct links need equivalent validation.

   This also requires accounting for **data** translations: constant-address
   read/write generators in `recVTLB.cpp` embed the current host pointer or
   handler/physical address. A fetch-page guard alone cannot protect retained
   code when another data VPN remaps. Use runtime translation for managed
   constant addresses or track their mapping dependencies. TLBWI/TLBWR and
   EntryHi changes can currently occur inside a compiled block; establish a
   dispatcher safepoint or equivalent revalidation before subsequent generated
   instructions use changed translation assumptions. Do not simply delete
   `ClearActiveTLBCodePage` calls.

3. **Give links and allocations owners before reclaiming storage.**
   `BaseBlocks::Remove` leaves outgoing link records (an existing TODO), and
   `BaseBlocks::New` repatches historical entries targeting a recompiled address.
   Measure retired-source records and patches. Add source ownership and deferred
   cleanup at a fully unwound JIT boundary. This can reduce metadata/patch churn
   independently and is a prerequisite for safe allocation reuse.

4. **Reclaim arena segments instead of clearing everything.** Once ownership is
   explicit, retire segments and disconnect their entries/links, remove owned
   host-IP backpatch records and thunks, then reuse storage after generated code
   has unwound. Keep dispatch helpers in a permanent region. Current helpers may
   return into invalidated code; immediate free-on-clear or blind ring-buffer
   wrapping is unsafe. Backpatch thunks also consume the current arena.

5. **Reduce projection and instruction overhead separately.** If TLB sync still
   dominates after filtering, replace full unordered-map projection rebuilds
   with affected-VPN updates or reusable compact projections while preserving
   overlap priority, ASID/global selection, large pages and scratchpad. Later,
   precise exception side exits could replace full register flushes before every
   memory-class instruction, retaining exact GPR/PC/BD state on faults. These are
   independent architectural changes and need their own correctness coverage.

Global resets also clear fastmem backpatch metadata and learned faulting PCs.
Measure repeated backpatches before attributing additional cost to lost learning;
that amplification is source-plausible, not measured here.

## Validation and limits

Performed source review, whitespace checking, test registration/uniqueness and
native project XML checks. No CMake configuration, compiler, C++ test execution,
Windows build, emulator launch, HDD access or guest changes occurred. No FPS
gain, reduced cache-fill rate, successful boot or runtime conformance is claimed.

When builds are authorized, run the existing native `EEActiveTLB.*` and cache
test groups, then a controlled cold boot of the same disposable PSBBN clone and
configuration. Compare matching workload windows and validate D transitions,
COW/remaps, instruction writes, aliases, branch/delay-slot exception PCs,
BIOS/retail games and savestates before integrating into `testing`.
