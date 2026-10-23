# PSBBN v49: 6-FPS report and hot TLB fault console-spam mitigation

**Working branch:** `note-working/psbbn-tlb-small-delta-projection-20261023` (same branch as v46–v48; no rebase/new branch).
**Evidence:** user reports average ~6 FPS despite reduction from 25 arena exhaustions at 64 MiB to 6 at 256 MiB. Earlier `emulog(20261023-153739).zip` (v47 executable) contained approximately **638,007** `cpuTlbMiss pc:` error lines and **22,647** `branch delay!!` warnings in ~129 seconds. That is ~5,000 avoidable console error/warning writes per second *in addition* to real guest TLB work and COP0 tracing.

## What this changes

PCSX2 `pcsx2/R5900.cpp` previously called `Console.Error(...)` on each EE recompiler TLB miss and `Console.Warning("branch delay!!")` on each exception in a delay slot. During PSBBN, guest TLB refill and delay-slot exceptions are **expected execution**, not fatal host errors. Their synchronous formatting / logging can dominate or distort apparent FPS.

The new code changes **diagnostic logging only**, leaving all guest exception, EPC/BD, EntryHi, Context, BadVAddr and MMU/CPU behavior unchanged:
- Under boot-latched `EEExecutionProfile::IsExtendedMMU()`, suppress the *per-miss* `Console.Error` by default and suppress the corresponding TLB Mod/TLBL/TLBS delay-slot `Console.Warning`.
- `[EmuCore] EEVerboseTLBFaultLog = true` restores the original verbose fault and delay-slot output for diagnostics. Setting is read and cached at first applicable fault, never per exception, and reset on EE CPU reset.
- **Standard profile preserves original exception log conditions and warnings.** Extended non-TLB delay-slot warnings remain.
- Print one `EE-TLB-FAULT-LOG enabled=0|1 profileExtended=1` line on first applicable Extended fault. Aggregate `faults`, `suppressedFaultMessages`, `suppressedBranchWarnings`, `verbose` in `EE-TLB-FAULT-SUMMARY` at EE provider shutdown. These are CPU-thread-owned counts, not performance timers.
- Added pure policy functions in `pcsx2/EEExecutionProfile.h` and unit tests in `tests/ctest/core/ee_rec_coherence_policy_tests_20261021.cpp`. Hook declaration in `R5900.h`; call in `ix86-32/iR5900.cpp`. No changes to TLB projection, recompiler code cache sizes, link detach, renderer or guest state.

## How to test this functional performance hypothesis

1. Update **the same branch**, build with the usual Visual Studio/MSVC toolchain, run `core_test` if available. The added tests are `TlbFaultLogSuppressionKeepsStandardAndExplicitVerboseBehavior` and `BranchDelayDiagnosticSuppressionIsOnlyForExtendedTlbExceptions`.
2. Boot PSBBN in **Extended MMU** using the existing 256-MiB JIT cache (`[EmuCore] EEJitExtendedCacheMiB = 256`). **Do not increase to 512 MiB for this experiment.** Ensure `EEVerboseTLBFaultLog = false` or leave it unspecified.
3. For FPS comparison, turn **PCSX2 Trace.Enabled off** (or disable EE COP0 trace), since `PSBBNTlbTraceEnabled()` is `EmuConfig.Trace.Enabled && EmuConfig.Trace.EE.cop0`, and disable `EEJitLinkOwnershipWitness`, `EEJitEvictionTrace`, `EEJitSourceWitness`. These high-volume diagnostic paths were enabled in the earlier logs and their cost invalidates timing-based conclusions. Run to the same UI activity, note live FPS, guest progress and host CPU/GPU activity.
4. Confirm the new log contains `EE-TLB-FAULT-LOG enabled=0 profileExtended=1`, without hundreds of thousands of `cpuTlbMiss pc:` lines; upon shutdown expect `EE-TLB-FAULT-SUMMARY ... faults=... suppressedFaultMessages=... suppressedBranchWarnings=...`. **High fault count is still real guest behavior, even if console output is suppressed.**
5. For a controlled within-binary A/B (optional), reboot VM with `EEVerboseTLBFaultLog = true` and **identical other settings**; this intentionally restores ~5,000 logs/sec under the recorded workload. Compare FPS and stalls at the same visible UI checkpoint. Turn it back off for normal performance use.
6. If performance remains around **6 FPS with all tracing disabled**, logging is not the dominant bottleneck. Next focus should be a low-overhead EE/JIT vs GS vs vTLB invalidation timing breakdown, not additional JIT arena capacity. Do not infer 23s of accumulated traced MMU timing explains every frame by itself.
7. The optional 256/512 capacity setting from v48 remains available but is not changed in this experiment.

## Verification status

Source and tests committed, **not compiled or run by assistant**. User's 6-FPS report is the runtime observation, and the high logging counts are from the existing capture. Improvement magnitude unknown until a trace-off matched FPS run. Keep additional findings/edits on this same branch; leave `testing` untouched.
