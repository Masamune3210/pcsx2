# PSBBN v44: emulog(8) retired-source detachment did not arm after JIT reset

Repository: `Masamune3210/pcsx2`
Working branch: `note-working/psbbn-jit-retired-source-link-detach-20261023`
Prior handoff: `notes/PSBBN_RETIRED_SOURCE_LINK_DETACH_20261023_v43.md`.
User evidence: `emulog(8).zip`, containing `emulog.txt` (9,079,570 bytes, 107,084 lines). Log is not committed.
Log build: `PCSX2 v2.9.92-367-g0de13a0a7`; Extended MMU; `EEJitLinkOwnershipWitness` enabled; older eviction tracer disabled.

## Measured failure: policy installed then cleared

**All eight** reported `EE-JIT-LINK-DETACH` summaries show `attempts=0 writes=0`, with zero `sourceDetaches` in each per-cause mutation summary. Source-link registration and retirement proceed exactly as in the baseline; the reverse ownership index is empty. This is NOT a v41 build or wrong user setting; it is a v43 source-code initialization bug.

Concrete cause, verified from code:
1. `recResetRaw()` emits dispatchers by calling `_DynGen_Dispatchers()`.
2. That function called `recBlocks.SetRetiredSourceDispatcher(DispatcherReg)` during dispatcher generation.
3. `recResetRaw()` later calls `recBlocks.Reset()`. The latter clears both link maps and explicitly sets `retiredSourceDispatcher = 0`.
4. Subsequent `BaseBlocks::Link()` calls do not insert `outgoingByOwner` entries because their guard is `if (retiredSourceDispatcher && ownerHost)`.
5. When owner metadata is retired, the new removal code sees the zero dispatcher and skips detachment.

The main problem is **ordering**, not ownership identity, source data, or unexpected guest code.

## Run evidence / comparison

- At 27.543s native -> fine 4-KiB dispatch reset; gen1 `registrations=26832`, expected no detachment because gen1 was native.
- Gen2 JIT arena exhaustion at **37.873s**, with **201,035** compilations, `cacheUsed=67043499` of 67,108,864 bytes, and 9,614 metadata-live blocks / 3,027,437 live host bytes. `registrations=264204`, `retiredOwners=136531`, `linkRecords=264204`.
- Gen2 target-compiled `retiredAttempts=11111758`; target-removed `retiredAttempts=11237461`; **22,349,219 retired-source repatch writes**, versus only **172,700** live-source repatches. Retired share **99.23%**, identical amplification mechanism as v41.
- Gen3 ends in provider shutdown at ~44.49s. `registrations=233999`, `retiredOwners=122556`, and **13,234,490** retired-source repatches of 13,386,722 total (**98.86%**).
- Tracking health: zero dropped sites, unowned source registrations, invalid link offsets, unknown-owner retargets. No asserted host JIT crash is evident in this capture; it ends following a user pause and shutdown.
- Log timings include ownership instrumentation and are not baseline FPS benchmarks. Identical source bytes and ASID/TLB churn remain established by prior v40/v42 samples, not retested with detailed source witness here.

## Committed correction

In `pcsx2/x86/ix86-32/iR5900.cpp`:
- Remove policy installation from `_DynGen_Dispatchers()`.
- **After** `recBlocks.Reset()`, install the just-generated `DispatcherReg` as retirement fallback, and only when `EEExecutionProfile::IsExtendedMMU()` AND `g_recUseGuestTLBDispatch != 0`. This keeps Standard and native modes on their original policy.
- Source-owner attribution in `recLinkStaticBlock` is likewise explicitly gated on Extended fine mode.
- Emit `EE-JIT-LINK-DETACH-CONFIG gen=<N> enabled=<0/1> mode=<N> profileExtended=<0/1>` at the actual post-reset policy installation point, where `enabled` queries **actual `BaseBlocks` state**, not merely the intended configuration boolean.

In `pcsx2/x86/BaseblockEx.h`: `HasRetiredSourceDispatcher()` exposes the installed state for test and logging only.

In `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: existing reset-lifecycle test now asserts the policy is cleared on `Reset()` and reenabled when explicitly rearmed. Earlier v43 tests still cover removal/dispatch/target relinking, batch retirement, self links, legacy native behavior, and helper owner propagation.

**Verification:** source patch and tests committed to the branch. No MSVC build, Google Test run, emulator execution, or safe-host-return proof has occurred on this corrected revision. Tests are definitions, not results. Do not claim a speed improvement.

## Required next run

Build the **latest commit on this experimental branch**, keep Extended MMU and `EEJitLinkOwnershipWitness = true`; turn old `EEJitEvictionTrace` and `EEJitSourceWitness` off.

Expect:
- `EE-JIT-LINK-DETACH-CONFIG ... enabled=0 mode=0` at native startup.
- `EE-JIT-LINK-DETACH-CONFIG ... enabled=1 mode=1 profileExtended=1` after the first guest-fine-dispatch transition and on later arena resets.
- Gen2/Gen3 `EE-JIT-LINK-DETACH attempts>0`, `untracked=0`, `postDetachRetargets=0`, fewer growing `linkRecords`, and a greatly reduced share of retired-source target repatches.
- Check host execution correctness and PSBBN progress (no new faults, crashes or stuck boot). A separate trace-free comparison is needed for wall-time/FPS claims.
- If `enabled=1` but `attempts=0`, investigate `recLinkStaticBlock` host-owner propagation and indexed source pointers rather than assuming another wrong user setting.

Leave the v41 measurement branch unchanged, and do not enable dormant compiled-block retention. The detached-link experiment only removes repeated bookkeeping over retired source code; it does not address mapping-driven recompilation or reclaim generated bytes.
