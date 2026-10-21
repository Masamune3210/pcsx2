# PSBBN EE JIT: measured 64 MiB thrash and safe within-sync physical clear deduplication (v22)

Follow-up: [physical code presence filter integration](PSBBN_JIT_CODE_PRESENCE_FILTER_20261021_v23.md)
records the completed wiring, runtime-versus-constant RAM template correction,
prospective edge coverage and source-only verification limits.

## Branch / state
- Repository: `Masamune3210/pcsx2`.
- Continue existing branch `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`, do not create nested branches.
- Latest user-tested Windows build: `PCSX2 v2.9.92-231-g6b18a18cf`.
- Evidence: **user-uploaded, not committed** `emulog(20261021-110157).txt`, 271,189 lines, 22.8 MB. Only an emulator log was supplied this time; there is no paired Linux printk ring, so do not assert a new userspace/mount stage from this attachment alone.
- New unbuilt source commits: `e8975f9ccc85a5c7599bfa5eed4e4bc65c463268` (dedup physical invalidation within one sync), `61ed8e77ad64b7b63a162ad5046d0ebb30bf7aaf` (testable helper), `d62d95eaa167ccce2dc276c69cc13c1c4f05ca8d` (use helper), `0b8204d03885f43df9b5fa28fee0559d31863a4f` and `520cc428b535af7d639384a056fe559b905485bd` (four focused GoogleTest cases), `5bb1ec7eab0d9f4c5a033562baf0bc9a00ed1673` (allocation-free inline dedup for first sixteen physical pages). **NOT COMPILED/BOOTED YET.**

## Instrumentation confirms actual churn, not merely log volume
The prior v21 instrumentation was successfully compiled and ran without a reported host crash:
- Log line 80: `EE-MMU-CHURN syncs=0 ...`, line 81: initial JIT reset, 0 cache bytes.
- Line 44268, time 20.94s: fine-mode JIT transition/reset, already 535 syncs, 360,290 removed pages, 360,294 installed pages and 720,568 physical invalidation requests. This includes early TLB setup with large page entries.
- Last full `EE-MMU-CHURN` snapshot line 269702 at time 60.0911s:
  ```
  syncs=266240 tlbWrites=208306 asidSwitches=57934
  removed=1084033 installed=1084118 unchanged=29143247
  physicalClearRequests=2168135 scratchClearRequests=8 syncUs=13579322
  ```
  `syncUs` is the cumulative **host time inside TLB synchronization**, ~13.58 seconds since startup. From the fine-dispatch transition to this snapshot, synchronization itself grew by ~10.78 seconds over ~39.15 log-timestamp seconds. It includes map rebuild/diff and physical block invalidation, but not deferred JIT recompilation.
- `cpuTlbMiss` messages: **200,895** in this log. They are guest exception diagnostics, not one looping address. Total JIT resets: **12** (startup, mode transition, then ten 4 KiB guest-mode cache-exhaustion resets).
- User-mode resets at 27.3803, 31.5793, 35.7712, 40.0074, 43.8432, 47.2670, 50.0774, 52.9139, 55.9349 and 59.0546. Every `EE-JIT-CHURN` before those resets reports `cacheUsed` between 67,043,333 and 67,044,816 bytes out of 67,108,864: **99.9% full**. Thus full EE JIT cache exhaustion/recycling is **now verified**, not hypothetical.
- Each cache-fill cycle generates ~187,736–202,049 guest blocks, ~66.94–67.00 MB of generated x86, while `physicalClearCalls` range ~94,191–189,103. Distinguish `physicalClearCalls` from count of *actual blocks invalidated*; the logger reports calls passing a high-water guard, not successful block removals.
- The user reported roughly **1 FPS**, manually stopped the prior run, and previously had similarly verbose logging: performance should not be attributed to logging without comparative profiling.

## Source-supported pathological behavior
`pcsx2/COP0.cpp::SyncActiveTLBMappings()` recomputes the active TLB projection. For each changed mapping, it clears the host vTLB alias then calls `ClearActiveTLBCodePage()` for the old mapped physical page; for the installed replacement it calls it again on the new physical page. On x64, the helper calls `recClearPhysical()`, potentially tearing down compiled guest physical blocks even though the bytes of RAM did not change. Under ASID/TLB turnover it can issue multiple identical physical invalidations **inside one synchronous mapping update**, with no opportunity for guest code to execute between calls. Avoiding those redundant calls does not relax the normal requirement to invalidate on *later* mapping changes, RAM writes or external resets.

**Separate, difficult architectural issue:** The physical-keyed EE JIT may contain blocks compiled with assumptions about their original virtual PC/ASID (including branch-relative behavior). Do **NOT** remove all TLB-driven physical invalidation merely to improve FPS without proving correct code-cache identity, exit/branch linking, exception PCs and context switching. Likewise do not globally enlarge the 64 MiB code cache as a purported root-cause fix.

## Committed conservative change
- Extracted `EEPhysicalCodeInvalidationSet` into `pcsx2/EEActiveTLB.h` so its behavior can be unit tested. It tracks 4 KiB physical page numbers **only for the duration of a single SyncActiveTLBMappings call**.
- `COP0.cpp` calls `ShouldInvalidate()` before `ClearActiveTLBCodePage()` on both old and new mappings. It clears each RAM physical page at most once per synchronous projection. At the next projection, a new tracker permits that page's invalidation again. Scratchpad and beyond-exposed-RAM pages retain original clearing paths, with no dedup.
- For the normal short-update path, the first **16 unique physical pages** are stored inline in a small fixed array without allocating. Large guest page masks spill into an unordered_set. This minimizes added allocator churn in ordinary retail PS2 software.
- Existing `EE-MMU-CHURN` output now includes `duplicatePhysicalClearsAvoided`; `physicalClearRequests` still counts the remaining real requests. This allows the next build to measure whether dedup eliminated a meaningful amount of work. No extra per-instruction generated code was added.
- Four tests added to `tests/ctest/core/ee_active_tlb_asid_tests_20261018.cpp`: repeated same physical PFN with changed writable bit, separate-synchronization re-invalidation, scratch/non-RAM exemption, and overflow beyond sixteen unique pages.
- Guest TLB translations, virtual mappings, permission checks, data accesses, kernel/filesystem behavior, fixed native 64 KiB dispatch and self-modifying-code invalidation policy are otherwise unchanged.

## Next steps
1. Compile the existing branch with Windows MSVC x64 AVX2; run `EEActiveTLBNative` tests (specific new tests above). Build is **not** verified here.
2. Cold-boot PSBBN with EE COP0 tracing enabled and measure `duplicatePhysicalClearsAvoided`, `physicalClearRequests`, `physicalClearCalls`, `syncUs`, cache-fill period, actual FPS. Compare against v21's numbers above over matching guest workload windows rather than only total time: boot paths vary.
3. If counters show most clears are unique and cache still fills in 3–5 seconds, stop optimizing this duplicate-clear path; investigate physical code-cache identity and whether mapping churn invalidations can be removed safely through ASID/virtual-context tagging and guarded per-page exit dispatch. Consider a bounded profiler for *actual compiled blocks* invalidated vs request count.
4. If synchronization timing dominates independent of physical clears, target the full `BuildEEActiveTLBPageMap` rebuild and diff on every guest TLB write/ASID transition. Preserve overlapping entry priority, large pages, scratchpad and store D-bit fidelity.
5. Native PS2/BIOS/retail-title performance, savestates, JIT linking and self-modifying-code correctness remain blockers for merging into `testing`. No special-case PSBBN addresses, no blanket guest TLB bypass or forced page mapping.
6. No new provable guest Ghidra symbols/types/structures. Preserve `ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java` in the separate `Masamune3210/ps2bbndecomp` repository.
