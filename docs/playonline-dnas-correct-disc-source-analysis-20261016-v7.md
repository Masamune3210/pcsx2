# PlayOnline DNAS study v7: correct SCUS-97269 CD image and verified -109 retry (2026-10-16)

**Tracking branch:** `note-working/magicgate-scmd-boundary-trace-20261016`. Previous [v6 provenance correction](playonline-dnas-two-disc-provenance-20261016-v6.md).

The user has supplied the genuine disc *image* (not genuine Sony HDD) for **PlayOnline Viewer and Tetra Master (USA, Canada)**, `.bin/.cue` MODE2/2352. Root `SYSTEM.CNF` explicitly boots `SCUS_972.69`. This is the **second** disc originally exercised in the PCSX2 DNAS -404 investigation, not the first-disc FFXI `SCUS_972.66` installer studied earlier.

Two distinct stripped EE ELFs exist:
- `SCUS_972.69` (CD boot) entry 0x00100008, file size 1,735,512, SHA256 b3c429e3691f3198e18ca76a4e2640596206da3b40272a2ff1d3cb94658abcea.
- `_/POL_HDD.ELF` (HDD-oriented) entry 0x00100008, file size 1,735,896, SHA256 90d8dbeb0b48b377ef471de184dcdaa207d6447abc5245df5a1df4371728cb41.

The executable **directly implements DNAS1-related initialization and status processing**, logging `sceDNAS1GetStatus() ERROR %d`, `sceDNAS2CryptInit() end %d`, DNAS slot IDs and `sceDNAS1_SS_DIFFERENT_HDD retry`. Source disassembly verifies an explicit **signed -109 (0xFFFFFF93, -0x6D)** comparison immediately before the DIFFERENT_HDD retry logger:
- CD `0x0080EAF8` status-handler function, `0x0080F0E0` -109 literal, retry log call `0x0080F0EC`, retry helper `0x0080F0F4`.
- HDD ELF `0x0080E308` same equivalent handler, `0x0080E8EC` -109 literal, logger `0x0080E8FC`, retry helper `0x0080E904`.

**Do not mistake -109 for -404**. The source of -404 (SS_INVALID_HDD_BINDING by external documentation) is still unproven in this executable; the new finding establishes this client contains explicit HDD-related DNAS error status and recovery handling. The CD boot also contains marker `PsIIlibdnas2....` embedded in opaque region; not established as parseable independent executable.

Nine encrypted second-disc `_/0/*.ERX` recovered with established rolling-XOR seed PRNG algorithm, all validate as IOP ELF type 0xFF80: `sqloader` (LDR000), `pegas` (NDI000), `ndglue` (NDI002 and POE002 separate variants), `sdglue` (PPP008), `avetcp` (TCP000), `avetcp2` (TCP001), `avetcp3` (TCP002), `avetcp4` (TCP003). No named BBSYS101/libid2 in this set. The `POLKEY.DAT` blob on this disc differs byte-for-byte from first-disc FFXI POLKEY.DAT despite identical 0x10010 length.

A local ZIP was prepared for user, `PlayOnline_SCUS97269_Ghidra_Recovery_20261016_v1.zip`, with EE ELFs, IOP modules, manifest, Ghidra APPLY script `AnnotatePlayOnlineDnas97269_20261016_v1.java`. Do **not** commit user-supplied game bytes to GitHub. Detailed metadata and Ghidra next step in [Ghidra handoff v24](https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/docs/PS2_MagicGate_Continuation_Handoff_20261016_v24.md).

**Next task when user resumes**: import correct CD executable into Ghidra, apply script then decompile the identified DNAS handler and follow status -404 upstream to the actual ATA/MechaCon RPC and host emulation. Existing branch `[SCMDTRACE]` baseline came from same failing CD; `[DNASLBA]` runtime instrumentation remains untested. Do not ask for a genuine HDD or repeat completed first-disc work. User has indicated intent to rest.
