# PSBBN v50: source-level logging cleanup after 444k-line quiet-profile run

Working branch: `note-working/psbbn-tlb-small-delta-projection-20261023`; **same branch as v46–v49**. No rebase, branch switch or changes to `testing`.
Prior: `notes/PSBBN_EE_TLB_FAULT_LOGGING_PERFORMANCE_20261023_v49.md`.
Evidence: `emulog(20261023-161932).txt` and `PCSX2(1).ini`, supplied by the user. The log is 19,134,144 bytes / 444,183 lines and covers ~74 seconds, with a normal emulator shutdown after 72.8s pause.
Binary reported `PCSX2 v2.9.92-401-g1f6cfb02f`, the v49 code with the TLB fault suppression enabled. User's INI confirms `[EmuCore/TraceLog] Enabled=false`, `EE.cop0=false`, `EEJitLinkOwnershipWitness=false`, `EEVerboseTLBFaultLog=false`, `[Logging] EnableVerbose=false`, file+system+IOP+EE console enabled. EE boot profile Extended, selected 256 MiB arena (512 MiB region reserved).

## Exact evidence and interpretation

Single-pass category counts over the actual log:
- **353,916** `DEV9 read8/read16/read32/write8/write16/write32` lines (230,353 write8; 55,018 read8; 40,439 read16; 27,727 write16; 379 write32).
- **53,158** `DEV9: IF_CTRL Unknown Bit 4/5 Set` errors emitted by the emulator's DEV9 register write implementation.
- **32,188** `[MGRAW]` lines and **3,778** `[MGFLOW]` lines from unconditional CDVD MagicGate SCMD/buffer traces.
- **555** `[MG]` messages (many are ignored unsupported key field warnings, but important crypto/authentication diagnostics remain).
- Combined above: **443,595 lines of 444,183**, ~99.87% of the whole log. **No literal COP0 trace lines**, no `cpuTlbMiss pc:` and no `branch delay!!` spam in this capture.
- `EE-TLB-FAULT-SUMMARY faults=580233 suppressedFaultMessages=579117 suppressedBranchWarnings=20959 verbose=0`. v49's TLB fault message suppression is confirmed working.
- `EE-JIT-CACHE-CONFIG mode=extended capacity=268435456 reserve=536870912 configuredMiB=256`.
- Logging during this run is still not suitable for matched FPS benchmarking, despite global emulator trace being disabled.

**Important:** The `DEV9 read/write` lines are not the same as the host's `DEV9: Unknown ...` errors. The source of the former is *likely* the guest IOP stdout bridge, because the emulator DEV9 register handlers do not print those exact strings and the IOP stdout is enabled. The new filter is scoped to *the IOP stdout rendering step only* and logs an exact suppression count; if the count stays zero while those messages remain, the hypothesis is wrong and the next investigation must locate the alternate emitter. **This is not yet verified by a new runtime.**

## Implemented v50 experiment

1. `pcsx2/CDVD/CDVD.cpp`: `s_mgProtocolTraceEnabled` loaded once in `cdvdReset()` from `[EmuCore] MagicGateProtocolTrace` (default false); `cdvdWrite16()` gates `[MGFLOW]` plus full `[MGRAW]` dumps behind it. Cryptographic operations, auth warnings, and any other message not under `trace_mg` remain as before. `MagicGateProtocolTrace=true` restores original complete flow/raw diagnostics.
2. `pcsx2/EEExecutionProfile.h`: `EEIsGuestDEV9RegisterTrace(std::string_view)` returns true only for six exact IOP stdout prefixes (three read widths, three write widths, requiring a trailing space).
3. `pcsx2/ps2/Iop/IopHwWrite.cpp`: at the already existing IOP STDOUT completed-line bridge `iopHwWrite8_Page3()`, drop only matching trace lines in Extended profile, `[EmuCore] IOPVerboseDEV9RegisterLog = false` default, preserving other IOP output, all IOP guest execution and all hardware register operations. On first matching line print `IOP-DEV9-CONSOLE-TRACE verbose=0 profileExtended=1`; aggregate dropped complete lines and report them once through `IopMemory::ReportGuestDEV9ConsoleTraceSummary()` at device shutdown. Standard profile unchanged. `true` restores legacy output. Declare in `pcsx2/IopHw.h`.
4. `pcsx2/DEV9/DEV9.cpp`: preserve initial error for each previously unexplained `SPD_R_IF_CTRL` bit 4 and bit 5, then suppress duplicate `Console.Error` calls during Extended execution, count bit occurrences and report one `DEV9-IFCTRL-WARN-SUMMARY` at close. `[EmuCore] DEV9VerboseIFCtrlWarnings=true` or Standard profile restores unlimited legacy warning output. Read once per DEV9open. All FIFO and reset logic remains unmodified.
5. `tests/ctest/core/ee_rec_coherence_policy_tests_20261021.cpp`: regression test verifies exactly the six matching trace prefixes and many near misses (actual warning/error messages, other IOP output).

No TLB, JIT, MagicGate cryptographic state, DEV9 IO, or other guest behavioral logic changed in v50. No compilation or emulation testing by assistant.

## User's next test

Fetch/update the **existing** working branch and build Windows/MSVC. Keep `[EmuCore] EEExecutionProfile=Extended`, `EEJitExtendedCacheMiB=256`; leave `EEJitLinkOwnershipWitness=false`, `EEVerboseTLBFaultLog=false`, `[EmuCore/TraceLog] Enabled=false`. The new keys default false but can be explicit:
```ini
[EmuCore]
MagicGateProtocolTrace = false
IOPVerboseDEV9RegisterLog = false
DEV9VerboseIFCtrlWarnings = false
```

Keep file logging **enabled** for one correctness/diagnostic run. The key signals are:
- `IOP-DEV9-CONSOLE-TRACE verbose=0 profileExtended=1` followed at shutdown by `IOP-DEV9-CONSOLE-SUMMARY suppressedRegisterLines=...`, ideally ~354k at comparable guest progress.
- `DEV9-IFCTRL-WARN-SUMMARY bit4=... bit5=... suppressed=...` with only one actual Console.Error for each unknown bit (can inspect remaining errors without flood).
- No `[MGRAW]` or `[MGFLOW]` by default, while `[MG]` meaningful warnings/verification messages remain. Restoring `MagicGateProtocolTrace=true` should recover the previous raw stream during a separate diagnostic run.
- No new fatal error, IOP/EE hang or lost PSBBN UI startup.
- If guest-console suppressed count is 0 and DEV9 read/write lines still appear, stop asserting IOP stdout is the source and pursue exact alternate callback.

Then use a *matched trace-off, witness-off, verbose-fault-off* pair with system/file logging also disabled (same visible UI checkpoint, guest progression, fps and host CPU usage) to measure whether logging is the main bottleneck. Restore file logging to capture problems. If FPS stays ~6, focus on actual guest memory faults / EE compilation / mapping invalidation / GS, not further cache expansion.

**Current status:** all source and GTest changes committed on active branch. No local MSVC build or runtime verification of v50.
