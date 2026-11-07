# Dark Cloud 2 MODMIDI / MODHSYN export and transition analysis

Branch: `analysis/dark-cloud-2-midi-hsyn-ghidra-20261106`
Base: `a821f07110a711569d8fb387c6bbca3fe16461cd`
Date: 2026-11-06

## Why
When Dark Cloud 2 changes music upon entering menus or other scenes, the first notes of the new track can sound wrong before recovering. Determine whether the issue is guest MIDI/synth controller state, libsd/SPU2 voice reuse, or timing; do not force a blanket SPU2 reset without evidence.

## Sources
- User-supplied `MODMIDI.IRX.gzf`: Ghidra database, original program MD5 `2644bd1d697ad93e3199e5a1cc5762b7`.
- User-supplied `MODHSYN.IRX.gzf`: Ghidra database, original program MD5 `19acd74ebe4d6209d28f1e056d8b3603`.
- Existing `pcsx2/IopModuleNames.cpp` in `testing` for proven IOP ordinal-to-name mapping.

## Findings
- MODMIDI exports: `sceMidi_SelectSong` 0x239c, `sceMidi_SongPlaySwitch` 0x2c38, `sceMidi_SelectMidi` 0x1a5c, `sceMidi_MidiPlaySwitch` 0x18d4, `sceMidi_ATick` 0x0560.
- MODHSYN exports: `sceHSyn_AllNoteOff` 0x1c40, `sceHSyn_AllSoundOff` 0x1ca4, `sceHSyn_ResetAllControler` 0x1d08, `sceHSyn_ATick` 0x182c, `sceHSyn_VoiceTrans` 0x036c.
- MODHSYN has direct `libsd` imports. MODMIDI's observed imports are `intrman`, `loadcore`, `stdio`, and `sysclib`. The inter-module integration needs decompilation/call graph evidence.
- Export 25 in MODMIDI and export 28 in MODHSYN were not observed in the supplied symbol records; script never fabricates them.
- Current evidence does **not** prove a controller reset omission or SPU2 fault.

## Artifacts
- `ghidra_scripts/AuditDarkCloud2MidiModules_v1.java`: apply proven API names without altering unresolved signatures, collect caller/callee relationships and data references, and export decompiled transition functions to `G:\Users\Johnny\Downloads\2`.

## Follow-up findings from existing Ghidra reports
- `sceMidi_SelectSong` stops active playback using `sceMidi_SongPlaySwitch(..., 0)` and resets song sequencing fields, then initializes a new song. This does **not** by itself demonstrate controller reset.
- `sceMidi_SongPlaySwitch(..., 0)` delegates to `sceMidi_MidiPlaySwitch(..., 0)`; `sceMidi_MidiPlaySwitch` in turn calls `FUN_000014cc` on stop and `FUN_00001630` on start.
- `sceHSyn_AllNoteOff`, `sceHSyn_AllSoundOff`, and `sceHSyn_ResetAllControler` delegate independently to per-channel helpers `FUN_000060d4`, `FUN_00006214`, `FUN_000010b0`. Each traverses 16 channels at stride `0x38` bytes.
- The existing report does not decompile the helpers above; no conclusive causal inference or generic helper rename is justified yet.
- `sceHSyn_GetChStat` writes 16 bytes to its third argument, one per channel, using bits 0x01, 0x02, and 0x04 to report state. Third parameter can be partially refined to `byte * outChannelStateFlags`.
- `sceMidi_MidiPlaySwitch`'s final int parameter uses 0 for stop and 1 for start/play.

## New script
- `ghidra_scripts/TraceDC2MidiHsynState_v2.java`: read-only follow-up script run on the existing projects. Exports the 26 helper/wrapper bodies, call graphs and data references to `G:\\Users\\Johnny\\Downloads\\2`. No reimport required.

## Verification and next step
- Names and addresses extracted from archived GZF symbol strings; cross-checked against the repository's ordinal definitions.
- Java script has not been executed inside Ghidra; verify compile and runtime on each archive.
- Open the existing Ghidra projects (already analyzed); run the follow-up script on each and inspect its two report files, especially reset/stop/start ordering and event dispatch via ATick. Compare register writes/timing against SPU2 on a reproduced transition before modifying emulator behavior.
- This branch contains analysis tooling only. No functional `testing` changes.
