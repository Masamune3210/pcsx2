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

## Verification and next step
- Names and addresses extracted from archived GZF symbol strings; cross-checked against the repository's ordinal definitions.
- Java script has not been executed inside Ghidra; verify compile and runtime on each archive.
- Import both archives, run the script on each, then inspect its two report files, especially reset/stop/start ordering and event dispatch via ATick. Compare register writes/timing against SPU2 on a reproduced transition before modifying emulator behavior.
- This branch contains analysis tooling only. No functional `testing` changes.
