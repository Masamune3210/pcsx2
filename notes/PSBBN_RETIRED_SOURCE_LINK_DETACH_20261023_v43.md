# PSBBN v43: source-owned link detachment experiment

Repository: `Masamune3210/pcsx2`
Active experiment: `note-working/psbbn-jit-retired-source-link-detach-20261023`
Measurement baseline (unchanged): `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Evidence: user-supplied `emulog(7).zip`, `emulog.txt` (9,145,625 bytes, 108,584 lines); **not committed**.
Measured Windows executable: `PCSX2 v2.9.92-356-gf36316aad`, Extended MMU. The experiment's new code has **not** been built or run.

## Measured problem: source already retired, link still repeatedly repatched

This run correctly enabled `EEJitLinkOwnershipWitness=true` without the older eviction/source trace. The owner witness reported zero unowned registrations, zero dropped sites, zero invalid host rel32 extents, zero unknown-source repatches, and zero compiling-source repatches. All witnessed target-relink attempts changed an actual rel32.

| Snapshot | Generation 1, fine-switch reset | Generation 2, arena reset | Generation 3, provider shutdown |
|---|---:|---:|---:|
| Compilations | 20,444 | 200,567 | End-of-run, after >=131,072 |
| Registered source jump sites | 26,832 | 263,918 | 232,590 |
| Tracked source owners | 17,295 | 144,746 | 127,709 |
| Source owners retired | 10,743 | 136,287 | 120,144 |
| Target-compiled patches, retired owners | 955 | 10,928,158 | 6,981,385 |
| Target-removed patches, retired owners | 10,328 | 11,081,658 | 7,133,173 |
| Target patch attempts, metadata-live owners | 18,099 | 172,482 | 152,097 |
| **Percentage targeting retired source metadata** | **38.40%** | **99.22%** | **98.93%** |
| Link site/owner tracking failures | 0 | 0 | 0 |

Generation 2 totals: **22,009,816 retired-source rewrites among 22,182,298 target-repatch attempts**. Generation 3: **14,114,558 retired-source rewrites among 14,266,655 attempts**. These counts apply to emitted outgoing rel32 jump sites whose **owner's BaseBlocks metadata was retired**. They are not proof that host execution/returns into those sites are impossible.

Consistency check: generation-2 `EE-JIT-LIFETIME linkAttempts=11,360,068` exactly equals **263,918 initial registered link patches + 11,090,294 target-compiled repatches + 5,856 direct-dispatch-only links** (`dispatcherTranslation=23`, `dispatcherCrossPage=5833`). The separate per-cause target-removal mutations total **11,092,004** repatches. Thus the apparently differing lifetime and owner counts have a precise accounting explanation.

Two JIT events matter:
- 17.6569s: native -> fine 4 KiB guest dispatch reset, reason 8; cache ~5.81 MB.
- 26.5985s: **actual 64 MiB arena exhaustion**, reason 4; 200,567 emitted blocks, 66,941,996 generated block bytes, 67,043,405 cache-used bytes, 9,709 live blocks / 3,061,782 live host bytes.
- 33.34s: provider shutdown, not another full arena reset. The log shows a user pause before shutdown, not a demonstrated new PSBBN fault.

At the generation-2 arena reset, TLB projection invalidation totaled 80,791 retired blocks and 4,256,058 target-removal link patches (`clearUs=709422`), while ASID projection retired 107,790 and performed 6,814,376 target-removal patches (`clearUs=1066737`). The logged timings include observation overhead, so they are not uncontaminated performance measurements.

## Design and committed code

**Do not drop retired rel32 link entries without fixing their bytes.** Their source's metadata can be gone while generated host code may still return from a memory/event helper, then proceed to a jump. A dropped link that still directly targets a now-retired, different-ASID or later-replaced block would be unsafe.

The experiment makes a narrower and more defensive change **only in Extended fine dispatch**:
1. `BaseBlocks::Link` accepts an optional emitting *host source* pointer; it stores iterators into its existing target-PC multimap in a reverse `outgoingByOwner` multimap. Native callers pass zero and do not construct a reverse index.
2. `EERecLinkStaticBlock` forwards the optional owner; `recLinkStaticBlock` supplies `s_pCurBlockEx->fnptr` only while guest 4 KiB instruction dispatch is active.
3. Before a `BaseBlocks::Remove` batch's original target-removal repatches, for **each removed owner**: patch every tracked outgoing rel32 site to **`DispatcherReg`**, notify the observer, remove its target link record, and erase the owner index. `DispatcherReg` rechecks `cpuRegs.pc` against the active virtual dispatch LUT when execution reaches the site, including if an in-flight retired source resumes. **Never use `JITCompile` for a proactive source detach:** it assumes its target is currently uncompiled and can trip an assertion if already live. Incoming links to still-live source owners retain the original recompile/remove handling.
4. Generate/enable the policy at each `recResetRaw` in fine mode only; `Reset` clears both maps and the dispatcher pointer. Standard mode and Extended native mode retain old link lifetime.
5. Keep all existing physical instruction/data source invalidations. This optimization **does not retain, resurrect, or share compiled blocks**.
6. Add `EE-JIT-LINK-DETACH` summary fields `attempts`, `writes`, `untracked`, and `postDetachRetargets`. Per invalidation cause, `EE-JIT-MUTATION` now emits `sourceDetaches` and `sourceDispatchWrites`.

Modified: `pcsx2/x86/BaseblockEx.h/.cpp`, `EERecCodeCache.h`, `EERecLinkOwnershipWitness.h`, `ix86-32/iR5900.cpp`; added focused GTest regression cases in `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`. Existing v41 measurement branch remains untouched.

## Verification and immediate handoff

- **GitHub source edits are committed. No MSVC compile, unit test execution or emulator runtime test has been performed on the new branch.** Unit test definitions cover retiring source-to-live-target, unknown native callers, source-to-self, multi-source batch, native legacy behavior, per-arena reset and helper owner propagation. Tests must actually be built/run.
- The next build/run should retain `EEJitLinkOwnershipWitness = true`, `EEJitEvictionTrace = false`, `EEJitSourceWitness = false`, with Extended profile. Do not use a performance comparison with the ownership witness enabled.
- Expected results: `EE-JIT-LINK-DETACH untracked=0 postDetachRetargets=0`, no inconsistent rel32 extents, much fewer `linkRecords` and `retiredAttempts`. `sourceDetaches` will cause one dispatcher patch per retired outgoing site rather than millions of future target repatches. Confirm PSBBN boot and any guest-visible behavior before treating policy as safe.
- For speed measurement run separate **both-tracers-off** runs on baseline versus experiment, same game/settings and comparable progress, recording compile counts, arena resets, ASID/TLB churn and guest-cycle advancement. The 64 MiB arena still fills due to recompilation and is **not** enlarged by this experiment.
- Host-native outstanding return addresses, full generated-code dependency proofs, and dormant compiled-code reuse remain unresolved. Removing rel32 site records after redirect-to-dispatch does not prove host blocks safe for reclamation.

## Decision rationale

The v41 diagnostic revealed a distinct pathological algorithmic cost even if safe code retention remains unproven: stale source-link entries grow for the entire arena generation and are rewritten repeatedly whenever their **target** compiles/retires. Redirect-and-detach is designed to preserve the in-flight source's ability to continue through guest-virtual dispatch while eliminating that repeated link maintenance. It is **an experiment awaiting validation**, not a proven fix or performance result.
