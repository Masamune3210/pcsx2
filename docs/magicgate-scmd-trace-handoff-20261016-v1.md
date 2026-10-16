# MagicGate SCMD boundary trace: branch handoff (2026-10-16, v1)

**Repository:** `Masamune3210/pcsx2`  
**Branch:** `note-working/magicgate-scmd-boundary-trace-20261016`  
**Base:** `master` (there is no `testing` branch in this fork at creation)  
**Goal:** Determine which actual CDVD/MechaCon S-commands the PlayOnline Viewer installer issues and which results PCSX2 returns. Do not infer firmware command IDs from MechaCon storage offsets.

## Completed

Changed **only** `pcsx2/CDVD/CDVD.cpp` to add console event lines tagged `[SCMDTRACE]` at three existing points:
1. `cdvdWrite16()`, before dispatch: monotonically increasing request identifier, SCMD byte, argument count, and *only for SCMD 0x03* the nested MechaCon subcommand.
2. `cdvdWrite16()`, after dispatch and before parameter state clears: matching identifier, response count, response first byte. The first byte is raw `result0`, **not necessarily a status** for every command.
3. `cdvdRead18()`, when the final response byte is read: identifier, SCMD, response byte count, to distinguish responses **returned by the emulator** from responses **fully consumed by guest code**.

`static constexpr bool s_scmdTraceEnabled = true` in `CDVD.cpp` enables this on this dedicated test branch. Set to `false` to compile it out after diagnosis.

This does **not** log parameter bytes, keys, challenges, full responses, or encryption material; no SCMD implementation paths or buffers were changed. It does not patch missing MagicGate authentication behavior and makes no claim about PlayOnline's actual command sequence yet.

## Next user action

In the user's existing PCSX2 working tree, **ensure current changes are committed or safely stashed**, then execute these **separate one-line commands**:

```powershell
git fetch origin note-working/magicgate-scmd-boundary-trace-20261016
git switch --track origin/note-working/magicgate-scmd-boundary-trace-20261016
```

If the local tracking branch already exists, use `git switch note-working/magicgate-scmd-boundary-trace-20261016` instead.

Build the branch using the existing PCSX2 Visual Studio/CMake build configuration (do **not** delete build output or disturb other source branches). Launch the existing PlayOnline Viewer installation test that was previously observed to fail or exit during setup. Capture the complete **Program Log** or `PCSX2.log`, with its `[SCMDTRACE]` messages and the installer error code/screenshot. Return the log to this investigation.

Expected lines (example layout, **not observed runtime data**):

```text
[SCMDTRACE] #1 request cmd=80 params=1
[SCMDTRACE] #1 response cmd=80 bytes=1 first=00 (payload hidden)
[SCMDTRACE] #1 consumed cmd=80 bytes=1
```

The result reveals whether PlayOnline reaches SCMD `0x80..0x98`, which subsequences occur, whether `0x98` (named `sceMgReadIcvPs2` but unimplemented by upstream `cdvdWrite16`) is used, and which status or response boundary precedes the failure. If the installer never invokes MagicGate S-commands, prioritize DNAS/network/IOP behavior instead of arbitrarily implementing crypto operations.

## Related firmware findings and current research decision

Dragon 6.12 v9 confirms internal storage commands `0xA3/A4/A0/A8/A9` are **private MechaCon storage-bus operations**, *not* PS2 IOP CDVD S-command IDs. Worker `0x30C40`, periodic caller `0x6B28`, and private `0x02000028` queue state were verified through read-only Ghidra reports. Firmware storage work is now parked.

See research handoff on `Masamune3210/ghidra-emotionengine-reloaded`, `testing`:
- [PCSX2 MagicGate command crosswalk](https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/docs/ps2-magicgate-pcsx2-scmd-crosswalk-20261016-v1.md)
- [MagicGate continuation](https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/docs/PS2_MagicGate_Continuation_Handoff_20261016_v9.md)

## Verification

- **Source committed and re-read from GitHub:** yes, `CDVD.cpp` has request, result and guest-consumption tags.
- **Local C++ compile:** not performed here.
- **PCSX2 runtime/PlayOnline trace:** not performed here.
- **MagicGate fix:** not yet attempted; evidence first.
- **Next expected step:** user build + PlayOnline test + return log; then trace actual handlers and isolate first failing status.
