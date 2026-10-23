# PSBBN v48: 256 MiB cache runtime results and selectable 64–512 MiB follow-up

**Active branch (unchanged):** `note-working/psbbn-tlb-small-delta-projection-20261023`
Previous handoff: `notes/PSBBN_TLB_DELTA_AND_JIT_CACHE_CAPACITY_20261023_v47.md`.
Evidence: user-uploaded `emulog(20261023-153739).zip`, containing `emulog.txt` of **82,627,755 bytes / 1,230,292 lines**. Log not committed.
Executable in log: **`PCSX2 v2.9.92-390-gc898c9b6b`**, matching the v47 implementation. Test through ~129.1s, with pause at 128.17s and normal provider/DEV9 shutdown; no logged JIT assertion, kernel panic, delta mismatch or unexpected termination. The log does not independently timestamp a visible UI frame.

## Confirmed 256 MiB result

Host memory map explicitly says `R5900 Recompiler Cache [256mb]`. Every `EE-JIT-CACHE-CONFIG` says `mode=extended capacity=268435456 reserve=268435456` as expected. Fine guest 4-KiB dispatch entered at ~27.0s, and retired-source link detachment remained armed after each reset.

**Six** full JIT arena exhaustions (`EE-JIT-LIFETIME reason=reset resetReasons=00000004`): 46.7419, 64.1458, 74.2229, 91.5862, 104.0942, and 116.2743 seconds. There are also the initial setup reset and the expected native-to-fine transition, which must **not** be counted as arena exhaustions.

The six exhausted 256-MiB generations compiled **780,046, 752,763, 744,648, 782,977, 703,097, 633,563** blocks, respectively. All reached ~268,370,000 bytes of cache use; they did not fail for another reason. At exhaustion, only ~1.6–4.2 MiB of host code remained represented by live metadata. Hence a larger arena reduces *reset frequency*, but it does **not** prevent rapid mapping-triggered block retirement or recover retired host code bytes.

Compare v46 `emulog(10).zip` (64 MiB, approximately 125s) with this v47 log (256 MiB, approximately 129s): **25 vs 6 arena exhaustion resets, or ~76% fewer resets**, in similar-duration but non-identical, instrumented sessions. This is a real reset-count reduction and directionally consistent with 4x capacity, **not** an apples-to-apples FPS or performance guarantee.

Final periodic TLB snapshot ~127.925s (`syncs=729088`): `smallDeltaSyncs=625822`, `smallDeltaPages=2950492`, `fullDeltaFallbacks=6885`, `projectionRebuildsSkipped=96381`, `deltaChecks=611`, **`deltaMismatches=0`**, `physicalClearRequests=4144432`, cumulative `syncUs=23117038`. Fine-generation link witness at provider shutdown: gen8 `registrations=581827`, `detached=579852`, `untracked=0`, `postDetachRetargets=0`; neither repatch event recorded any retired-source attempts. Optional witness cap 1,500,000 was never reached. The 638,007 `cpuTlbMiss pc:` lines and 22,647 `branch delay!!` lines make time-based comparisons noisy.

## Immediately testable v48 follow-up: selectable Extended JIT arena

On the **same branch**, commits after v47 now implement the capacity setting:

- `pcsx2/Memory.h`: reserve at most **512 MiB** for the EE code arena. `HostMemoryMap::EErecGenerationSize(bool extendedMMU, u32 extendedMiB=256)` supports **64, 128, 256, or 512 MiB** in Extended mode; Standard always uses 64 MiB. Unsupported settings fall back to 256 MiB. Other IOP/VU/VIF/GS code regions move together by the existing offset constants, do not overlap, and total code map becomes 753 MiB; all capacity calculations remain below the x86-64 signed rel32 2-GiB displacement magnitude where applicable, but a Windows build/runtime check is still mandatory.
- `pcsx2/x86/ix86-32/iR5900.cpp`: read `[EmuCore] EEJitExtendedCacheMiB` **once per EE provider/VM session**, default 256, using the same first-JIT-reset settings latch as the other diagnostics. Recalculate `recPtrEnd` after that initialization and on every subsequent cache reset using the selected value. The original 64-KiB emission slack, reset causes, dispatch, physical-source invalidation and link-detachment behavior are unchanged. Report `EE-JIT-CACHE-CONFIG mode=... capacity=... reserve=... configuredMiB=...` **unconditionally per generation**, so no high-volume TLB logging is needed to confirm which capacity is active.
- `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: unit coverage for Standard invariant=64, all four Extended choices, invalid-value default and region boundaries.

**Verification:** Source changes and tests committed to the same v46/v47 branch. **No local compile or execution of new v48 selectable settings has occurred.** The 256 MiB behavior is validated only by the earlier v47 user executable, not by a v48 build.

## Test directions (next run)

1. Update the same `note-working/psbbn-tlb-small-delta-projection-20261023` branch to the latest commit and rebuild. Run `core_test` if available (focus `EERecCodeCacheCapacity.StandardPreservesOriginalLimitAndExtendedIsSelectable`).
2. In the **existing** `[EmuCore]` section of PCSX2's active config, set `EEJitExtendedCacheMiB = 512`. Do not duplicate sections. **Fully reboot the VM** so the value is latched. Extended MMU must remain selected. Console should show `EE-JIT-CACHE-CONFIG mode=extended capacity=536870912 reserve=536870912 configuredMiB=512`.
3. Run the same PSBBN userspace/UI workload and record full resets with `resetReasons=00000004`. Compare with **six** in this 256 MiB v47 session (not raw wall seconds alone), and watch for JIT register/link errors, guest TLB faults, missing UI, host memory reservation issues and any surprise cache/relative branch regression.
4. For a controlled A/B without rebuilding, change to `EEJitExtendedCacheMiB = 256`, reboot VM, run to comparable guest cycles / UI progress, and check the log prints `capacity=268435456 reserve=536870912 configuredMiB=256`. Both selections reserve the same **512 MiB virtual EE code region** in v48; the actual generation limit changes.
5. For timings, disable `EEJitLinkOwnershipWitness`, `EEJitEvictionTrace`, `EEJitSourceWitness`, and verbose COP0/TLB tracing on both configurations. The one-line cache config remains visible. Include wall time, guest cycles, average CPU use, crashes and number of reset occurrences, not just cache count. Keep traces for a separate correctness run.
6. Negative control: Standard profile must log `mode=standard capacity=67108864 reserve=536870912` regardless of the Extended setting.

If the selectable cache mechanism doesn't build or the 512 MiB run fails, fix it **on this branch**. If 512 MiB yields diminishing returns, stop increasing the cache and redirect effort to reclaiming retired host code only after proving native return quiescence, or reduce physical projection invalidation safely; do not adopt unsafe unconditional code reuse.

No new branch, rebase, or modification to `testing`.
