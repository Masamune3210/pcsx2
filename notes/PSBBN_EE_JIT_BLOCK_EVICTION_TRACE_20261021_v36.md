# Current diagnostic: why individual EE JIT blocks are removed (v36)

Repository: `Masamune3210/pcsx2`
Continue branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Context: `notes/PSBBN_EE_EXTENDED_STANDARD_MATCHED_LOG_20261021_v35.md`; policy: v33. This checkpoint adds a diagnostic only, not code-cache reuse or a performance optimization.

## Why

The latest PSBBN Extended run remained around 4 FPS as observed by the user. The matched Standard/Extended guest-cycle interval showed no material sustained throughput gain. v35 reports one arena generation with ASID projection evicting 88,277 blocks and writing 12.4 million link displacements, but aggregate counters do NOT establish why any specific block was retired. Block-metadata ownership, data translation dependencies and cross-ASID execution context remain unresolved.

The user explicitly asked for *per-block eviction reasons*, accepting a potentially severe performance hit during a diagnostic run.

## Implementation

1. `pcsx2/x86/BaseblockEx.h`: optional read-only `BaseBlocks::RemovalObserver` hook at the exact metadata retirement site. Its signature is `void (*)(const BASEBLOCKEX&, void*)`. Called exactly once for each block actually removed by `BaseBlocks::Remove`, before its incoming links are redirected. When observer is null, normal behavior is unchanged; no changed source-link retirement policy, executable memory reclaim or guest execution change.
2. `pcsx2/x86/EERecCodeCache.h`: optional observer and context forwarded through both `blocks.Remove` call paths in `EERecClearPhysicalBlocks`, including the subset removed while an unrelated block is actively compiling. Requests that do not remove metadata produce no per-block line.
3. `pcsx2/x86/EEJitCoherence.h`, `pcsx2/COP0.cpp`: carry optional guest virtual page and `mappingAction=remove|install|none` with the existing physical clear cause. For TLB/ASID changes, distinguish old-entry removal from new-entry installation; for other callers the mapping action is `none` and the virtual alias is unknown unless a virtual-clear caller supplied it.
4. `pcsx2/x86/ix86-32/iR5900.cpp`: opt-in `EE-JIT-EVICT` events include:
   - `gen`: current JIT arena generation;
   - `cause`: actual invalidation **caller category** (`asid-projection`, `tlb-projection`, `virtual-clear`, `host-write-protection`, `manual-integrity`, `manual-reprotect`, `overlap-recompile`, etc.);
   - `mappingAction`: `remove`, `install`, `none`;
   - `blockPA`, `blockEnd`, `guestWords`: physical block *metadata* address and contiguous extent;
   - `hostPtr`, `hostBytes`: compiled host-code identity and size;
   - `requestPA`, `requestEnd`: exact requested invalidation physical range; `aliasVA`: virtual page responsible, or `ffffffff` if unknown;
   - `overlap=1|0`: whether the block's **metadata** extent overlaps the invalidation range. `overlap=0` can flag collateral retirement by metadata/indexing, not necessarily an error;
   - `cpuPC`, `entryHi`, `asid`, `cycle`: **snapshot at the time of invalidation**, NOT necessarily the original guest writer or the PC at which the block was compiled;
   - `mode`, `resetReasons`.
   - Full JIT arena resets remove all remaining live blocks without `BaseBlocks::Remove`. They are also reported with `cause=arena-reset`, no physical request range and `EE-JIT-EVICT-SUMMARY` counts. Provider teardown uses `cause=provider-shutdown`.
5. **Session-capped, default OFF**. Global/per-game Qt setting in **Settings → Emulation → System Settings**:
   - **Log Each EE JIT Block Eviction (Very Slow)** toggles `[EmuCore] EEJitEvictionTrace = true`, default false.
   - **Maximum Eviction Log Entries** is `[EmuCore] EEJitEvictionTraceLimit = 20000` by default. `0` explicitly requests unlimited output, potentially gigabytes and pathological slowdown. UI bounds other values to 1,000,000. Events beyond the cap continue to increment session counters without per-block output. Prints an `EE-JIT-EVICT-LIMIT` notice once and cumulative `EE-JIT-EVICT-SUMMARY` at arena reset and provider shutdown.
   - Settings are latched at the first JIT reset of the VM; fully shut down and reboot the emulated PS2 to turn tracing on or off reliably. The cap persists across all internal arena resets and resets only at provider shutdown.
6. Tests added to existing `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: `EvictionObserverReportsOnlyBlocksActuallyRemoved`, verifying overlap removal, compiling-block exemption and no duplicate notifications, and `EvictionObserverOptionalAndDoesNotChangeRemovalResults`, verifying the original no-observer path. Existing test registrations still apply; no new build targets.

**Verification:** Source-only inspection of declarations, call sites and UI binding. **NO MSVC build, Qt uic run, native core tests, emulator boot, or runtime trace verification was performed.** Do not treat these new diagnostics as runtime verified.

## Recommended user diagnostic run

1. Pull/build this branch.
2. In per-game Settings → Emulation → System Settings, set **EE Execution Profile: Extended MMU / OS** and check **Log Each EE JIT Block Eviction (Very Slow)**.
3. Set Maximum Eviction Log Entries to **10000** for the first run. Leave `0` (unlimited) off until log volume is understood.
4. Fully shut down and start the virtual PS2. Verify `EE-JIT-EVICT-CONFIG enabled=1`, `EE execution profile: ExtendedMMU`, and `EE-JIT-EVICT` records in emulog.
5. Run just far enough to capture several ASID changes and one arena reset. Upload the complete emulog; interpret alongside `EE-JIT-MUTATION` and `EE-JIT-LIFETIME`. **Do not compare FPS of this verbose diagnostic run** to normal gameplay.
6. Prefer root-cause study from **per-block clusters**: same blockPA repeatedly evicted due to alias removals, consecutive remove/install of the same physical backing, metadata-overlap false positives, repeated recompiles with unchanged source context, and number of full-arena retirements. The observer is not enough by itself to prove safe dormant-code reuse.

## Limitations and next work

- Cause is the **caller of invalidation** rather than definitive proof of which CPU/IOP/DMA/host writer changed actual guest instructions. A host write-protection fault does not identify the origin of the write.
- `blockPA` is metadata's physical start, not an explicit recorded source VPN list; noncontiguous instruction fetch pages and embedded data pointers are still not safely tracked.
- Direct-link records from retired source blocks remain in the link map. This trace records retiring targets, **not** owner/lifetime provenance of each link-patch site. Safe source ownership must be established before deleting those links or reclaiming host-code memory.
- `EE-JIT-EVICT` does not count mere dispatch-table writes where no `BaseBlocks` metadata was removed. Existing `EE-JIT-MUTATION` counters cover dispatch writes.
- Logging on the CPU thread itself can severely perturb scheduling and JIT performance. Use it for causality, not benchmarks.
- If no `EE-JIT-EVICT` events appear despite enabling the UI, first check whether the trace was boot-latched and whether the selected profile's physical clear path removed actual metadata. A raw TLB request without a block removal is not itself an eviction.

The next work should analyze the resulting block identities and direct-link patch site ownership, not add more generic invalidation heuristics or enlarge the 64 MiB code arena.
