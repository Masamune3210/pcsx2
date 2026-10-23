# PSBBN v45: full UI-bringup link-detachment validation from emulog(9)

Branch: `note-working/psbbn-jit-retired-source-link-detach-20261023`
Prior handoff: `notes/PSBBN_RETIRED_SOURCE_LINK_ACTIVATION_AUDIT_20261023_v44.md`
Source evidence: user uploaded `emulog(9).zip` containing `emulog.txt` (56,491,930 uncompressed bytes; 670,199 lines). The log itself is not committed.
Observed executable: `PCSX2 v2.9.92-372-gdf6d366b4` (v44 correction already compiled locally by user).
Profile: Extended MMU. `EEJitLinkOwnershipWitness = true`; verbose per-block eviction and v40 source witness disabled.
Test duration: 104.61 seconds of log time; user explicitly reports allowing PSBBN to finish userspace bringup and display the UI. The log itself contains no Linux `printk` ring or framebuffer evidence to time the first visible UI frame independently.
**Source verification:** no new emulator code changes, local MSVC build or unit-test execution performed during this analysis. User's run independently verifies the corrected v44 executable at runtime.

## Validation of the repaired initialization ordering

26 `EE-JIT-LINK-DETACH-CONFIG` records: generation 1 `enabled=0 mode=0 profileExtended=1`; **every generation 2 through 26** `enabled=1 mode=1 profileExtended=1`. The policy is armed after `recBlocks.Reset()` exactly as intended. It stays armed across all **24 actual JIT arena-exhaustion resets** (reason `00000004`). One initial setup reset (reason `00000003`), one native-to-fine dispatch reset (reason `00000008`), and no observed provider reset after this transition before normal shutdown.

The v41 witness reported:
- **5,449,192** direct-link registration events in fine generations 2–26.
- **5,238,476** source-owned outgoing links redirected exactly once to `DispatcherReg` and removed from target relinking; every detachment performed a real rel32 write.
- **Zero** `untracked`, **zero** `postDetachRetargets`, and **zero** `retiredAttempts` and `retiredWrites` in *every* fine generation's target-compiled and target-removed repatch breakdown.
- **Zero** dropped tracked sites, unowned registrations, invalid rel32 offsets, compiling-owner repatches, unknown-owner repatches in final generation accounting.
- **2,872,670** retired source owners across fine generations; source metadata for **2,872,514** of them was retired with the EE execution loop active, providing broad invalidation stress. This does *not* prove that a particular native return executed a detached link after retirement.

Example gen 2 at **23.153s**, first fine arena exhaustion:
- 264,176 registered outgoing sites, 250,127 detached, **14,049 links retained** (94.68% of original records removed).
- 162,373 target-compiled live-source repatches and 1,114 target-removed live-source repatches; **zero** retired-source repatches.
- 136,501 of 144,879 tracked source owners retired, 201,041 compilations, 66,943,427 emitted block-code bytes, 67,043,957 arena bytes, 9,631 live blocks occupying 3,043,680 host bytes.
- Source detaches per-cause: ASID projection 151,524; TLB projection 96,002; remaining 2,601 from other clears.
- Compare v43 disabled-policy log `emulog(8).zip`: generation 2 retained all 264,204 sites, performed **22,349,219** repatches against retired source owners (99.23% of target-repatch activity), and reported detachment count zero. Guest stage and instrumentation are not matched for performance timing. This comparison does validate removal of the targeted pathological relinking.

Final generation 26 at normal provider shutdown **104.596s**:
- registrations=76,527; detached=73,995; untracked=0; postDetachRetargets=0.
- target-compiled liveAttempts=44,849; target-removed liveAttempts=565; both retiredAttempts=0.
- shutdown after `(VMManager) Pausing...` at 102.125s and DEV9 close/shutdown; no emulator crash, assertion or fatal diagnostic found. Normal completion through visible UI is based on the user's firsthand observation.

## The distinct remaining bottleneck is extreme recompilation and MMU churn

**24 arena-exhaustion resets**, from 23.153s through 100.729s, all with `resetReasons=00000004`; the 25th fine generation is partial at shutdown, not an exhaustion reset. The exhausted fine generations (2..25) collectively emitted:
- **4,443,518 compiled blocks** (mean 185,147 per full arena generation; min 149,267, max 208,399).
- **1,607,191,852 bytes** of translated block instructions (**1.497 GiB**) plus JIT dispatcher/other cache overhead. Total arena high-water use across these generations is ~1,534.51 MiB.
- Each generation exhausts its fixed **64 MiB** EE JIT arena even though only a small fraction of block records remain live. Redirect-and-detach does not retain or resurrect blocks, so this is expected and does not indicate regression.

At 102.122s, the last `EE-MMU-CHURN` snapshot reports:
- 663,552 synchronizations; 576,615 TLB writes; 86,937 ASID switches.
- 2,097,536 mapping removals; 2,097,608 installs; **56,478,055** mappings marked unchanged; 87 permission-only updates.
- 561,664 projection rebuilds and 101,888 skipped rebuilds, with 7,220,195 pages retained during projection comparisons.
- 4,181,633 physical-clear requests and 13,495 duplicate physical clears avoided.
- cumulative `syncUs=23,346,688` (23.347 seconds of measured synchronization routines, not a precise percentage of 104.6s wall time; overhead/threads/logging may differ).

The large log has **563,772** `cpuTlbMiss pc:` lines and **21,633** `branch delay!!` lines. This logging volume alone rules out treating wall seconds as clean performance results.

### Nonzero constant-data/context access classifications

Unlike the earlier tightly bounded 2,048-source comparison, this 24-generation full workload exhibits `EE-JIT-CONSTDATA` classifications. Deduplicating `65536-compiles` cumulative repeats and taking one terminal snapshot per generation gives:
- native gen 1: `otherReadDirect=4`, `otherWriteHandler=1`, `otherWriteDirect=11`.
- fine gen 2–25: 48 `otherReadDirect` observations and 24 `fineManagedWriteHandler` observations (one per full generation). All `fineManagedReadHandler/Direct` and `fineManagedWriteDirect` remain zero.
- Do **not** conflate these telemetry values with a proven set of unsafe blocks, but do not presume all emitted code is independent of guest mapping and memory handling. Precise per-block dependency capture and safe host-return quiescence are still needed before compiled-block resurrection.

## Interpretation and next work

**Verified under extended real guest execution:** v44 source-owner rel32 detach and on-demand guest virtual dispatch eliminate repeated target repatches to retired code across 25 fine generations; they did not visibly prevent PSBBN userspace and UI bringup.

**Unresolved:** repeated compilation of equivalent physical instruction data and many ASID/TLB-induced retirements remain, and generated host return/callee state has not been proven safe to reuse. Avoid unconditional physical-code cache resurrection. Next focused work should use the link-detachment baseline to prove a bounded, per-block source+mapping/data-dependency equivalence and a safe post-retirement host-code quiescence condition, or independently profile and optimize the **4.18 million physical-clear requests/23.35s synchronization** path without changing guest semantics.

Suggested experiments: (1) run with link-ownership and verbose TLB logs off for matched performance comparisons, keeping v44 policy enabled; (2) inspect exact code/data dependency guard and host return lifetime, restricting any reuse prototype to verified context-free blocks; (3) measure the MMU no-op projection path and skip provably duplicate work without skipping dirty/repermission/alias invalidation.

**No rebase to `testing`, merge, or new executable behavior change performed in this handoff.** Keep this document on the experimental branch and preserve v41 as unchanged measurement baseline.
