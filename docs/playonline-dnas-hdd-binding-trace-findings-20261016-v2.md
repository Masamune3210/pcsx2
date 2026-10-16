# PlayOnline DNAS -404: actual PCSX2 protocol trace findings (2026-10-16, v2)

**Repo / branch:** `Masamune3210/pcsx2`, `note-working/magicgate-scmd-boundary-trace-20261016`  
**User test:** PCSX2 `v2.9.92-5-gfab33873e` using PlayOnline Viewer and Tetra Master (`SCUS-97269`) on an emulated 40 GiB HDD. Screenshot from prior test: `POL-1562 (DNAS Error -404)`, PS2 hardware information error. New log: `Pasted text(20261016-031224).txt`, 6050 lines, captured only in conversation (not publicly archived because log contains user-local configuration). **The latest log ends around emulation t=69.99s; it does not itself print -404.** Correlate error with screenshot only, not a proven same-frame return.

## Observed trace

256 S-command *requests*, each with matching emulator *response* and guest *consumption* events. Counts by command:
```text
0x41 70; 0x08 28; 0x40 24; 0x43 24; 0x8D 18; 0x8F 10; 0x1E 10;
0x03 9; 0x8E 8; 0x12 6; 0x17 4; 0x05 4;
0x80 3; 0x81 3; 0x82 3; 0x83 3; 0x84 3; 0x85 3; 0x86 3; 0x87 3; 0x88 3;
0x1A 2; 0x90 2; 0x91 2; 0x92 2; 0x93 2; 0x1B 2; 0x15 1; 0x24 1.
```

Three complete SCMD MagicGate authentication sequences at t=6.78s, 7.29s and 13.50s, of the form:
```text
0x80(1 input byte) -> 0x81(1) -> 0x82(16) -> 0x83(8) ->
0x8F(0) -> 0x84(0, 13-byte result) -> 0x85(0, 13-byte result) ->
0x86(16) -> 0x87(8) -> 0x8F(0) -> 0x88(0)
```
Each returned its handler's first status byte `00`, and the guest consumed all result bytes. This only proves **PCSX2's emulated success**, not real cryptographic validation or agreement by PlayOnline/DNAS.

Separate KELF/HDDOSD MagicGate data transfers include SCMD `0x90` (header start), repeated `0x8D` blocks, `0x8F` (header finalize), `0x91` (BIT length), `0x8E` (data read), `0x92/0x93` length setup, `0x8F/0x8E` data transform. PCSX2 logs KELF header and BIT count; HDDOSD ELF loads. No `0x98` command in these 256 requests. Its missing handler is **not demonstrated as the cause of this test's failure**.

At emulation t=31.554s, 43.400s, 43.749s, 55.010s, 57.580s, 69.976s, 69.979s, 69.993s, PlayOnline/IOP uses the ATA **command `0x8E`, feature `0xEC`** (not the same as CDVD SCMD `0x8E`). Source: `ATA::HDD_SCE` and `ATA::SCE_IDENTIFY_DRIVE` in `pcsx2/DEV9/ATA/Commands/ATA_SCE.cpp`; sends exactly **512-byte** `sceSec` buffer over PIO. No explicit ATA error was logged for feature `0xEC`.

## Verified source: HDD identity fallback

[`ATA_State.cpp`](https://github.com/Masamune3210/pcsx2/blob/note-working/magicgate-scmd-boundary-trace-20261016/pcsx2/DEV9/ATA/ATA_State.cpp) computes `Path::ReplaceExtension(hddPath, "hddid")`, tries to read it, accepts it when size <=512, and otherwise fills a synthesized `sceSec[512]` record.

When no `.hddid` file is available:
- `sceSec[0x00:0x20]`: literal `Sony Computer Entertainment Inc.`.
- `[0x20...]`: fixed HDD model `SCPH-20401`.
- `[0x30...]`: fake capacity tag `"  40"`.
- `[0x40:0x44]`: hardcoded fake HDD serial `00 00 00 01`.
- `[0x44:0x50]`: fixed values.
- `[0x50:0x80]`: **all zero bytes**, although code comments describe unique data needed by real hardware.
- `[0x80:0x200]`: zero bytes.

The connected HDD image in user's log is `F:\Emulation\Emulation Files\pcsx2\HDDOSD.raw`, so local override path by this source logic is `F:\Emulation\Emulation Files\pcsx2\HDDOSD.hddid`. **Log does not say whether override exists or was loaded.**

Source currently treats *zero-length* `.hddid` file as valid and copies zero bytes, leaving all `sceSec` zero; this is a potential validation issue, but no fix should be presumed yet.

## Independent definition of DNAS -404

Historically documented `sceDNAS2_SS_INVALID_HDD_BINDING` = -404, current PS2/HDD binding does not match registered console/HDD combination. See [PS2 DNAS guide](https://gamefaqs.gamespot.com/ps2/915821-playstation-2/faqs/42217) and [PS2 Developer wiki DNAS](https://www.psdevwiki.com/ps2/DNAS). The PS2 Dev Wiki [Hard Drive](https://www.psdevwiki.com/ps2/Hard_Drive) says HDD identification offsets `0x50..0x7F` are related to DNAS/encryption. This narrows the likely cause but does **not** independently prove how this installer translates its internal errors or what server response was received.

## Immediate next user action (no rebuilding)

Run this **one PowerShell line** to determine whether PCSX2 has an override, without exposing raw HDD identification contents:

```powershell
$p='F:\Emulation\Emulation Files\pcsx2\HDDOSD.hddid'; if (Test-Path -LiteralPath $p) { $b=[IO.File]::ReadAllBytes($p); if ($b.Length -ge 0x80) { [pscustomobject]@{Found=$true;Bytes=$b.Length;UniqueRegionNonzero=@($b[0x50..0x7F] | Where-Object { $_ -ne 0 }).Count} } else { [pscustomobject]@{Found=$true;Bytes=$b.Length;UniqueRegionNonzero='Too short'} } } else { 'HDDOSD.hddid missing: PCSX2 uses synthesized HDD identity' }
```

Ask user to return output only. Do not make up HDD unique bytes or request credentials. If override exists, further correlate encrypted HDD content / certificate with console identity; if missing, verify exact DNAS operations and consider providing authentic user-owned HDD ID from legitimate hardware test without posting secrets.

## Verification and remainder

Trace branch built and ran. Static source and console log reviewed, no source fixes made in this pass. The next dynamic instrumentation, **only if needed**, should report override-vs-default, sizes/validation and command returns without printing unique HDD ID or secrets. Scope remains MagicGate, HDD binding, actual PlayOnline failure, not arbitrary MechaCon firmware analysis.
