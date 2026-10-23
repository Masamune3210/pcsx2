# PSBBN v51: guest DEV9 register logging via IOP BIOS output paths

Working branch: `note-working/psbbn-tlb-small-delta-projection-20261023` (continued in place). Prior handoff: `notes/PSBBN_IOP_DEV9_MAGICGATE_LOGGING_AUDIT_20261023_v50.md`.

## v50 executable evidence

Supplied: `emulog(20261023-171536).txt` and `PCSX2(2).ini`. Binary `PCSX2 v2.9.92-408-gedd7849b1` confirms the complete v50 HEAD was compiled. 8,792,273 bytes / 228,717 lines, emulator start to clean shutdown ~67.5s, PSBBN IOP progression through `pfs0:/p2lboot/osdboot.elf` and later continued HDD traffic. `EEExecutionProfile=Extended`, Extended JIT 256 MiB, ownership witness off, global trace off.

- `DEV9 read8/16/32` lines = **61,374**, `DEV9 write8/16/32` lines = **166,195**; total **227,569** (~99.5% of all log lines). All 227,569 match the strict shape `DEV9 (read|write)(8|16|32) <8 hex address>: <width-sized hex value>` (8-bit 183,189 lines, 16-bit 44,001, 32-bit 379). These are **guest diagnostic output strings**; do not suppress or change actual DEV9 memory access operations.
- `[MGRAW]` / `[MGFLOW]` **zero**: v50 opt-in raw MagicGate tracing fix works.
- `DEV9: IF_CTRL Unknown Bit 4/5 Set` **2** total (first bit occurrence only) and shutdown summary `DEV9-IFCTRL-WARN-SUMMARY bit4=17021 bit5=17021 suppressed=34040`: v50 repeated-warning fix works.
- `EE-TLB-FAULT-SUMMARY faults=574552 suppressedFaultMessages=573452 suppressedBranchWarnings=20642 verbose=0`: earlier TLB log suppression remains active.
- **No** `IOP-DEV9-CONSOLE-TRACE` and **no** `IOP-DEV9-CONSOLE-SUMMARY`. The v50 guest-output filter at the hardware `0x1f80380c` stdout register did **not** activate. Lower line count relative to v49 must not be mistaken for improved filtering or runtime speed: these captures differ in duration/guest progress.
- The supplied three explicit v50 toggles are physically in the `[EmuCore/Gamefixes]` section of `PCSX2(2).ini`, not `[EmuCore]`. This is a placement error, but because all default to false it does **not** explain ongoing register spam. If setting toggles explicitly, put them under `[EmuCore]`.

## Source investigation

`pcsx2/IopBios.cpp`: guest drivers can emit log strings through *two additional routes* that entirely bypass hardware stdout register handler `pcsx2/ps2/Iop/IopHwWrite.cpp`:
- `sysmem::Kprintf_HLE()`: formats the IOP guest Kprintf call and passes completed text directly to `iopConLog(ShiftJIS_ConvertString(tmp, 1023))`.
- `ioman::write_HLE()`: fd 1 directly passes guest stdout to `iopConLog(ShiftJIS_ConvertString(s.data(), a2))`.

The exact origin of these 227,569 strings is not yet runtime-confirmed; the shared sink instrumentation will discriminate those paths on the next run. v50 only filtered the hardware register stdout sink, leaving these alternate direct-to-logger paths untouched.

## v51 implementation

- `pcsx2/IopHw.h`: declare shared `IopMemory::ShouldSuppressGuestDEV9ConsoleTrace(std::string_view, GuestDEV9ConsoleSource)` and source enum (`HardwareStdout`, `IomanStdout`, `Kprintf`).
- `pcsx2/ps2/Iop/IopHwWrite.cpp`: consolidate exact DEV9 guest log policy into shared helper. Preserve v50's `[EmuCore] IOPVerboseDEV9RegisterLog` false-by-default override and Extended-only gate. Count suppressed lines separately for the three sources. First matching record logs `IOP-DEV9-CONSOLE-TRACE verbose=0 profileExtended=1 firstSource=Kprintf|ioman-stdout|hardware-stdout`. At device shutdown `DEV9close()` prints `IOP-DEV9-CONSOLE-SUMMARY suppressedRegisterLines=... hardwareStdout=... iomanStdout=... kprintf=...`.
- `pcsx2/IopBios.cpp`: call helper after Kprintf formatting but before string conversion/log printing; similarly call in fd=1 output handler without touching return value, PC, register state or guest write data.
- `pcsx2/EEExecutionProfile.h`: tighten detector: require full register record with exactly 8 hex address digits, `: ` delimiter and the correct 2/4/8 hex value digits; permit optional line terminator. Do *not* suppress trailing extra text or a multi-line message after the first record.
- `tests/ctest/core/ee_rec_coherence_policy_tests_20261021.cpp`: exercise every width and read/write combination, optional CRLF, negative warning/near-miss, and multi-line case.

No JIT, TLB, DEV9 emulation, MagicGate cryptography or other guest behavioral change. This is an output-only change.

## Verification

Build the branch HEAD in Windows/MSVC, then boot PSBBN with the same Extended settings and file logging on. If settings explicit, insert these keys under **`[EmuCore]`**:
```ini
MagicGateProtocolTrace = false
IOPVerboseDEV9RegisterLog = false
DEV9VerboseIFCtrlWarnings = false
```
The first matching guest line should produce an `IOP-DEV9-CONSOLE-TRACE ... firstSource=...` and the shutdown summary should show nonzero counters on the correct source. Expect near-zero `DEV9 read/write` guest trace lines by default, while `DEV9:` host errors, normal IOP messages, KELF verification and shutdown summaries still appear. If counters remain zero, root origin is still elsewhere and must be investigated; do not claim the fix succeeded. Confirm error-free PSBBN progress and the same screen/UI checkpoint; do not infer FPS from log size. `IOPVerboseDEV9RegisterLog=true` should restore all six diagnostics.

**Verification state**: changes committed, not compiled or runtime tested by assistant.
