# PlayOnline DNAS v9: mapped -404 source and patcher status transition (2026-10-16)

Branch: `note-working/magicgate-scmd-boundary-trace-20261016`. Prior [v8](playonline-dnas-status-retry-reset-20261016-v8.md).

User supplied new v3 decompilation outputs for both correct second-disc executables, CD `SCUS_972.69` and `POL_HDD.ELF`, plus community DNAS-net Patcher Mode 5 console scan offsets:
- CD raw BIN 0x1987E8 suspicious function maps to EE 0x00192B80; 0x198E1C SetStatus 6 site maps to 0x00193084.
- HDD raw BIN 0x3CBDD8 suspicious function maps to EE 0x00192C80; 0x3CC40C SetStatus 6 maps to 0x00193184.
These are **2352-byte-sector BIN offsets**, NOT file-relative ELF offsets. Mapping to actual extracted ELF addresses independently checked.

**Verified:** Each suspicious function is an error translator. It compares signed -404 and returns -1562 via a MIPS branch-delay-slot immediate (CD 0x00192BCC / 0x00192D50, HDD 0x00192CCC / 0x00192E50). This establishes a *literal code path* from Sony DNAS -404 to POL-1562 in the correct disc. It does not prove why -404 arose.

**New path:** `ProcessDnas1AuthenticationStatus` (CD 0x0080EAF8 / HDD 0x0080E308) invokes the operation CD 0x001984D8 / HDD 0x001985D8 in handler case 9. Its negative result passes to the -404 to -1562 mapper. That operation contains **its own explicit -404 return**, at CD 0x00198538 / HDD 0x00198638, if a secondary check (CD 0x001B9850 / HDD 0x001B9950) returns ZERO after prior positive initial check (CD 0x001B94E8 / HDD 0x001B95E8). Meaning of secondary check unknown; pursue this before ascribing -404 to HDD binding. The DNAS1 handler's special -109 DIFFERENT_HDD retry is separate.

The patcher Mode 3 location is `addiu v0,zero,6` executed as the branch delay slot of a status-setter switch path for input result 10, not a discovered HDD authentication fix. Changing it to 5 would bypass a software state transition, not validate ATA security or MechaCon correctness.

**Correction:** v3 Ghidra `ZeroByteSpan` annotation was wrong: assembly/decompiler show it returns buffer pointer if length nonzero, NULL otherwise, NOT requested length. Corrected expected Ghidra return `byte *` and first param `byte *`, second remains existing `long`. New script `ApplyPlayOnlineDnasErrorPath_20261016_v4.java` prepared as metadata applier in conversation, Ghidra in-app test pending; full Ghidra continuation [v27](https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/docs/PS2_MagicGate_Continuation_Handoff_20261016_v27.md).

**Next experiment**: decompile the secondary readiness/check helper controlling the direct -404 return and, if meaningful, instrument this condition in PCSX2 using existing traces. Do not conclude missing Sony HDDID alone explains this behavior, and do not ask user to obtain an original Sony HDD. No PCSX2 source code changes in this pass.
