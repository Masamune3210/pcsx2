# HDD Utility MagicGate reference v7: PS3 firmware (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v6](hdd-utility-magicgate-real-scph39001-bios-20261016-v6.md). DNAS remains parked.

## Research finding and rationale

The user's actual `SCPH-39001.mec` bytes are `03 06 00 00`, meaning **MechaCon version 3.06**, **not Dragon version 6.03**. The logged 32-bit little-endian integer `0x00000603` had previously caused a mistaken firmware-family inference. Older pre-Dragon SPC970 CXP103049 corresponds to this console. Do not risk repeated writes to the user's real console NVRAM to obtain a same-model MechaCon dump.

**PS3 PS2 emulation is a promising independent reference.** PS2 Developer Wiki documents that PS3's emulated MechaCon reports **3.09**, much closer to the older SPC970 interface than Dragon 6.xx, and that `ps2_gxemu` sends and receives MagicGate traffic through **virtual UART port 10**; an SPU6 role is suggested but not confirmed.

Source URLs:
- https://www.psdevwiki.com/ps2/MechaCon
- https://www.psdevwiki.com/ps3/PS2_Emulation

Investigate these PS3 firmware components IN PRIORITY ORDER:
1. `/dev_flash/ps2emu/ps2_gxemu.self` (decrypted image `ps2_gxemu.elf`): software-emulated PS2 IOP devices with GS hardware; documented MG traffic through virtual UART port 10. Locate virtual UART 10 packet construction/dispatch and inspect native PS2 MechaCon S-command `0x80..0x97`, especially `0x8F`, `0x90`, `0x8D`. *Documentation alone does not establish handler addresses or crypto implementations.*
2. `ps2_emu.self`: early hardware-EE/GS PS3; useful corroboration for device emulation.
3. `ps2_softemu.self`: fully software PS2 emulation retaining physical disc handling; compare the device and auth handling.
4. `ps2_netemu.self`: later PS2 Classics-only emulator with separate PS3 content decryption; may contain virtual MechaCon but do NOT conflate native KELF with PS3 PS2 Classics `ISO.BIN.ENC`.
5. `sb_iso_spu_module.self`: known PS3 Classics meta/data/VMC crypto, lower priority for native PS2 MagicGate.

Sony's PS3 `ps2_netemu` uses `ISO.BIN.ENC` with PS3-side licensing and `klicensee` encryption; this alone does NOT solve original PS2 KELF `0x8F`. Distinguish host license crypto, virtual MechaCon, and embedded PS2 BIOS/SECRMAN.

**Additional independent HDD reference:** PS3's officially supported PS2 HDD games used `IMAGE.DAT` virtual HDD images with PS2 APA/PFS partitions. Sony's `PS2_system_data.pkg` provided one; `game_ext_plugin.sprx` lists HDD-capable games, and `libps2hdd.sprx` may manage virtual HDD. The documented list contains `SCUS97269` (PlayOnline/FFXI installer). An official `IMAGE.DAT` can serve as a format/layout reference, but may not replicate a completely blank physical Sony HDD exactly. Do not restart DNAS work on this basis.

## Existing PCSX2 test oracle

HDD Utility v1.10 `SCUS_973.95`, CRC `E2B8E3DC`, full native S-command raw log showed `0x90 -> 26 x 0x8D (0x1A0 bytes) -> 0x8F`. Header arrived intact, `[MGTRACE] BIT offset=0x48 blockCount=33350 reserved=82,F5,E1`, and emulated `0x8F` returned `0x80`. PCSX2 lacks the needed MechaCon transformation, not header transport. Its 0x84/0x85 authentication outputs are hardcoded and should not be mistaken for authenticated cryptographic state. WLaunchELF similarly fails BIT check yet continues, so it is a negative control, not a successful decryption oracle.

## Next practical step

Obtain *user-provided* decrypted `ps2_gxemu.elf` (or Ghidra `.gzf` archive); import PowerPC big-endian into Ghidra and identify virtual UART 10 dispatch, actual S-command 0x8F semantics, and crypto call chains. If the code delegates to SPE or hypervisor, follow the boundary and request the corresponding module rather than inventing plaintext decryption. If user has `ps2_emu.elf`/`ps2_softemu.elf`, compare handlers second. Return all proven Ghidra metadata via runnable Java apply script when general-scan archives are supplied.

This is a **new research lead**, not a completed implementation. No PS3 firmware binaries or cryptographic secrets are committed.
