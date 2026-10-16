# Official HDD Utility MagicGate continuation v2 (2026-10-16)

**Branch:** `note-working/magicgate-hdd-utility-header-validation-20261016`, created from `testing`. Previous [v1](hdd-utility-magicgate-header-probe-20261016-v1.md). **Primary objective:** boot/install official Sony HDD Utility Disc v1.10 USA, `SCUS-97395`, onto clean `testdrive.raw`. **DNAS investigation explicitly parked** by user.

## User confirmed first instrumented run

User compiled/reran v1 diagnostic branch build PCSX2 `v2.9.92-6-g101c2413a` and supplied 845-line runtime log `Pasted text(20261016-171555).txt`. Confirmed same disc CRC `E2B8E3DC`, same new 40GB virtual HDD 83,886,080 sectors. New diagnostics at ~26.746s:

```text
[MG] hcode=0 cnum=0 a2=0 length=0x1A0
[MG] ELF_size=0x5370 Hdr_size=0x1A0 unk=0x100 flags=0x22C count=0 zones=USA
[MGTRACE] BIT offset=0x48 blockCount=33350 reserved=82,F5,E1 calculatedHeader=0x824C0 declaredHeader=0x1A0
[MGTRACE] BIT_FORMAT_REJECT reservedNonzero=1 lengthMismatch=1
[MG] ERROR - Make sure the file is already decrypted!!!
```

Both initial `mg_maxsize==mg_size==GetBufferU16(header+0x14)` and calculated BIT offset bounds PASSED; actual failure is BIT fields. Raw bytes at `mg_buffer+0x4C..0x4F` are `46 82 F5 E1`, interpreted as little-endian 16-bit count 0x8246 = 33,350, and three reserved bytes including the overlapping high count byte. For an unencrypted correctly located BIT with declared size 0x1A0 and offset 0x48, expected block count is 20 (0x14), and reserved bytes zero. The discrepancy **strongly supports missing MagicGate decryption or incorrect BIT format/offset**, but cannot alone settle between those possibilities. The later reset and TLB miss at PC 0x0 is downstream of rejection, not root cause.

A secondary early message `ATA: C_READ_SECTOR`, `cannot alloc memory`, `new power off` is in the boot OSDSYS HDDLOAD branch before HDD Utility executable runs; keep distinct and do not claim causality. The utility has not formatted a clean HDD or produced an official __net baseline.

## Source audit

`pcsx2/CDVD/CDVD.cpp` `cdvdWrite16` S-command implementations:
- `0x80..0x87`: acknowledge authentication phases, some return **hardcoded** response bytes, without meaningful cryptographic processing.
- `0x8D`: append write data bytes to `mg_buffer`.
- `0x8E`: read/consume from buffer.
- `0x88/0x8F`: parse/validate currently buffered MG header; parser does not decrypt encrypted BIT contents.
- `0x90`: reset header transfer and set expected size/type. `0x91`: read BIT length. `0x94..0x97`: copy key buffers from header (not proof of correctly derived MagicGate keys).
The logged failure arose from `0x88/0x8F` handling after visible length/offset validations.

**Do NOT force status success, zero the BIT fields, or fabricate block count to proceed**. Such bypass would neither implement KELF decryption nor provide confidence in downstream KELF loading.

## Branch commits (existing)

- `04841c4dac9ec51cb7321bc3e13aff5ff0df1a5d`: add safe length, BIT bounds/format diagnostics, preserve rejection.
- `c5b40a124cbb79ade196574f485179cc77b63702`: add `[MGFLOW] CALL/RETURN` for S-commands `0x80..0x97`, reporting input/result lengths, buffered/expected sizes, data type and return code. **Does not log raw command payloads, header bytes, protected keys or ciphertext**; special-case `0x8E` since its first result byte is user data, not a status. No functional response changes.

**Current verification:** first diagnostic commit was user-built and reproduced failure with precise BIT result. Second `[MGFLOW]` commit is **not yet user-built or runtime-tested**. No source-level compile verification claimed.

## Next action

Build latest branch and rerun **unchanged** HDD Utility Disc and blank HDD image, collect all `[MGFLOW]`, `[MGTRACE]` and adjacent `[MG]` lines in a single test. Check whether authentication phases `0x80..0x87` are invoked, which fixed acknowledgments are used, and when header bytes arrive before `0x88/0x8F`. Then implement/fix the actual KELF/MechaCon processing stage consistent with disassembled real firmware's SECRMAN and DECKARD flow, not by skipping header validation. Existing user-supplied Ghidra SECRMAN/DECKARD analyses remain useful but not yet directly tied to this failing command sequence.

No game/proprietary binaries or keys are committed to GitHub. DNAS remains suspended.
