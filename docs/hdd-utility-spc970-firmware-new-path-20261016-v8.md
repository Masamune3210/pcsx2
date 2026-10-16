# HDD Utility MagicGate reference v8: original SPC970 research becomes available (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v7](hdd-utility-ps3-magicgate-firmware-reference-20261016-v7.md). DNAS remains parked.

## Major new external lead

September 2026 researchers DiscoStarslayer and Libby developed a method for extracting pre-Dragon **Sony SPC970** MechaCon ROM (including some v3 hardware), and released a native **Ghidra 12.1.3 processor/ROM loader**:
- https://github.com/DiscoStarslayer/ghidra-spc970
- https://github.com/Libbers/SPC970-MechaLIBerator
- https://www.psdevwiki.com/ps2/MechaCon

The `ghidra-spc970` README explicitly says the loader chooses `spc970:LE:16:default`, maps four ROM banks (active bank FD for v3), maps SRAM, labels hardware/command/MG buffers and identifies SCMD/NCMD/MG dispatcher tables and S-box references. This is likely more directly useful than 6.x Dragon/ARM code for the user's **SCPH-39001 v3.06 USA**.

`SPC970-MechaLIBerator` README explicitly identifies **SCPH-39001** among validated hardware, with a **raw four-byte version `01 03 06 00`** and CXP103049-401GG layout for a tested unit. Do **not** assume every SCPH-39001 uses the exact same chip suffix/ROM/region; compare version, region, and dump metadata. Note user's earlier supplied `SCPH-39001.mec` has **`03 06 00 00`** (Ghidra conversation inspection), because dump methods can encode extra fields differently. The user's existing `.mec` file was read-only observed, not a real raw MechaCon firmware dump.

**Safety:** the dumper deliberately performs numerous low-level NVRAM writes and warns it can permanently damage the hardware. User specifically declined a same-console dump because of this risk. Never require this exploit on their original console. Obtain a third-party already-public dump if permitted/available instead. News reports mention multiple published images, but an exact publicly accessible 3.06 USA image has **not yet been verified** and should not be asserted as present.

Complementary Sony PS3 emulation:
- https://www.psdevwiki.com/ps3/PS2_Emulation documents `ps2_gxemu` MagicGate traffic through virtual UART 10, with uncertain SPU6 processing role.
- https://www.psdevwiki.com/ps2/MechaCon notes PS3 virtual MechaCon reports 3.09, within same old 3.x family.
- Earlier v7 doc also covers PS3 official virtual HDD `IMAGE.DAT` APA/PFS layout, `PS2_system_data.pkg`, PS3 PS2 emulator variants.

## Priority now

1. Locate existing publicly supplied **SPC970 v3.x** ROM for v3.06/region-compatible F/G-chassis, without touching user's real console. If found, import with `ghidra-spc970` into separate Ghidra project. Confirm version/chip/region, then inspect identified MG dispatch handler corresponding to native `0x8F` (header finalization), `0x90` (header transfer start), `0x8D` (data append). Return code and datatype findings as Ghidra Java apply scripts as normal.
2. In parallel, reverse-engineer PS3 `ps2_gxemu.elf` virtual UART-10 MagicGate handler as a software reference for old-version command behavior.
3. Keep exact PCSX2 test oracle: official `SCUS_973.95`, CRC `E2B8E3DC`, header size `0x1A0`; raw `BIT 0x48` invalid (`0x8246` read as count, encrypted-looking bytes). Do not bypass cryptography/parser.
4. Do not confuse PS3's `ISO.BIN.ENC` / `klicensee` encryption with native KELF header decryption; the PS3 virtual HDD APA/PFS path is supplementary, not evidence the installer succeeds yet.

**No code or Ghidra database modifications made** for this lead, other than research handoff docs. No original Sony firmware/keys copied into repository.
