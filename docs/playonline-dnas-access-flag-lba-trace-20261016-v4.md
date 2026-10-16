# PlayOnline DNAS / MagicGate: direct access-flag LBA trace (2026-10-16 v4)

**Active branch:** `note-working/magicgate-scmd-boundary-trace-20261016` from the user's PCSX2 fork.
**Goal:** Determine if the guest reads the three documented DNAS access-flag LBA pairs and whether their 16-byte leading contents match, without access to a genuine Sony HDD. **Do not require or suggest original hardware**.

## New primary data and reasoning

The user provided PS2 Developer Wiki's DNAS notes. DNAS combines four independent device/context identifiers:
- Disc ID through DSP/MechaCon.
- Console GUID/Console ID and model ID through MechaCon EEPROM.
- Sony HDD ID through the proprietary ATA `0x8E` command feature `0xEC`.
- It may also compare the first 16 bytes of *two* consecutive HDD sectors at the selected access flag path. Three possible LBA starts:
  - `hdd:__net/etc/access_flag` -> `0x41008` and `0x41009`.
  - `hdd:__net/etc/access_flag2` -> `0x4100C` and `0x4100D`.
  - `hdd:__net/etc/access_flag25` -> `0x41010` and `0x41011`.
The specific file selection and interpretation remain unverified for PlayOnline; these are *candidate checks*, not a proven source of -404.

Verified from PCSX2 CDVD source: `SCMD 0x03 sub=0x00`, which appears frequently in the previous log, returns **MechaCon version** (not console ID). Actual console-ID reading uses `SCMD 0x03 sub=0x45`; this was **not** seen in the current v2 SCMD trace. An obfuscated IOP RPC or other CDVD/MechaCon commands could provide the actual DNAS IDs, so lack of `0x45` does not prove no console-ID request.

Known v2 PlayOnline runtime trace had 256 SCMDs and multiple private ATA feature `0xEC` reads, but did not itself log DNAS -404's guest-code origin. The source currently substitutes a fixed HDD serial and zeroed `0x50..0x7F` when no `.hddid` file exists. The user confirmed no `.hddid` is present.

## Code implemented

Changed [`pcsx2/DEV9/ATA/ATA_Transfer.cpp`](https://github.com/Masamune3210/pcsx2/blob/note-working/magicgate-scmd-boundary-trace-20261016/pcsx2/DEV9/ATA/ATA_Transfer.cpp) inside **`ATA::IO_Read()`** *after* successful existing file read and *before* read completion. Checks only whether the read LBA range overlaps the three 2-sector pairs. Emits:
- `[DNASLBA] pair=41008 read=... sectors=... first16equal=... first16zero=... second16zero=...`, only if both sectors were included in the current I/O operation;
- `[DNASLBA] partial pair=41008 read=... sectors=... (no byte comparison)` if a partial pair was read.
The same applies to `0x4100C` and `0x41010`. **No HDD sector data, DNAS keys, console/HDD serials or raw identity bytes are logged.** This handles both DMA/PIO readers passing through IO_Read.

If these candidate LBAs are not read, the log has **no `[DNASLBA]` lines**. That is a meaningful negative but does not prove DNAS does not use other access-flag paths.

All modifications are read-only diagnostics. No PCSX2 emulation behavior or HDD image contents changed.

## Exact user next step

In existing PCSX2 checkout on this branch: preserve uncommitted changes and run a single PowerShell line:

```powershell
git pull --ff-only
```

Then rebuild with the user's working Release or Release AVX2 configuration, launch PlayOnline until the DNAS -404 screen, and send the full PCSX2 log with `[SCMDTRACE]`, `[DNASLBA]`, and error timestamp/screenshot. The branch has been edited on GitHub but the **new edit is not compilation- or runtime-verified**.

A **no-rebuild alternative** is to read the two fixed LBAs per pair directly from the existing HDD image, comparing only the first 16 bytes; this answers *what is on disk* but **not whether DNAS ever reads it**. Prefer the dynamic trace when the user has a built PCSX2 checkout.

## Interpretation

- If `[DNASLBA]` logs a mismatch, investigate which access_flag file is used and whether the mismatch maps to a guest error condition; do not modify the user's HDD image based on a guess.
- If pair prefixes match, the access-flag equality check might pass, but matching is not enough to establish console/HDD binding.
- If no candidate LBA is read, prioritize console-ID/HDD-ID BBSYS101/libid2 SIF RPC, Disc ID acquisition, and the actual return-site of -404 instead of chasing sectors.

## Related documents

- [v2 protocol trace](playonline-dnas-hdd-binding-trace-findings-20261016-v2.md)
- [v3 HDD ID and BBSYS101 RP Call](playonline-hddid-sector-and-bbsys-rpc-20261016-v3.md)

## Verification state

Code committed, readback verified. C++ build and end-to-end PlayOnline run pending user test.
