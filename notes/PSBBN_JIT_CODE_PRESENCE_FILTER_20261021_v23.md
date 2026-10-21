# EE JIT physical code presence filter: completed integration review

Next: [permission-only TLB fast path and cache-retention priorities](PSBBN_JIT_CHURN_NEXT_STEPS_20261021_v24.md).

Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
The remote advanced from `62876d3827` to `90ebb45b69` during this review. Those
commits already install the filter instance, mark the source start before block
emission and all translated source pages afterwards, reset its bits alongside
`recBlocks.Reset()`, and check it in `recClearPhysical`. This note supersedes the
earlier observation that the helper was not yet connected.

## Integration correction

The new instance used `EERecCompiledPhysicalPages<Ps2MemSize::ExposedRam>`, but
`MemoryTypes.h` declares `ExposedRam` as a runtime `extern u32`. That cannot be a
non-type template argument. Use compile-time `Ps2MemSize::TotalRam` instead: the
128 MiB allocation ceiling covers both retail and development RAM configurations.
The bitmap occupies 4 KiB of host storage, not guest memory. Existing runtime
physical-address/high-water guards still apply before the filter.

The filter is deliberately conservative. A physical page's bit remains set until
the entire code-cache generation resets, including after individual invalidations.
Unmarked RAM pages can bypass metadata searches; marked pages use the original
block-overlap and incoming-link invalidation. Non-RAM and ranges crossing the
bitmap's managed boundary retain the original clear path. No TLB mapping, guest
permission, physical write invalidation or generated instruction path is relaxed.

Three added prospective cases cover the full potential RAM window and reset;
zero-size/non-RAM/overflowing clear ranges; and real `BaseBlocks` overlap/link
invalidation while the page remains marked. Existing three presence cases and
cache regressions remain registered in both the x86 CMake core target and native
`EEActiveTLBNative` harness.

## Expected impact and limits

`EE-JIT-CHURN skippedNeverCompiled` counts clears rejected by the bitmap;
`physicalClearCalls` counts those reaching the existing block-clearing helper.
Neither counts actual blocks evicted. A high skip count can reduce metadata work,
but this cannot save hot code pages from repeated TLB-driven invalidation and
recompilation. The seen bitmap saturates conservatively over a generation.

The measured cache-exhaustion record in v22 still motivates separating translation
changes from compiled-code lifetime. Do not remove all mapping-related physical
clears: blocks contain virtual-PC assumptions, direct links and potentially embedded
constant data-translation pointers. Code retention needs explicit dependency and
entry/exit rules. A larger arena alone does not reclaim invalidated host bytes.

No compiler, CMake configure, C++ test executable, Windows build or emulator was
run. The template defect and its correction are source-established; successful
compilation and performance improvement are not claimed. When authorized, run
`EERecPhysicalPagePresence.*`, `EERecCodeCache.*` and `EERecDispatchMode.*` in the
native harness, then compare identical cold-boot workload windows against v22.
