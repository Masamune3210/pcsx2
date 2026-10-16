# HDD Utility MagicGate header validation probe (2026-10-16)

**Branch:** `note-working/magicgate-hdd-utility-header-validation-20261016` from `testing`.
**Status:** Focused MagicGate source instrumentation pending local build and HDD Utility v1.10 SCUS-97395 rerun. PlayOnline/DNAS is expressly parked.

## Why this exists

The official HDD Utility Disc v1.10 USA `SCUS_973.95`, CRC `E2B8E3DC`, boots on a blank 40-GB PCSX2 HDD image but stops while a protected executable is being processed. Console log:

```text
[MG] hcode=0 cnum=0 a2=0 length=0x1A0
[MG] ELF_size=0x5370 Hdr_size=0x1A0 unk=0x100 flags=0x22C count=0 zones=USA
[MG] ERROR - Make sure the file is already decrypted!!!
```

These are PCSX2's OWN diagnostics from `pcsx2/CDVD/CDVD.cpp`, `cdvdWrite16` SCMD `0x90` and `0x88/0x8F`, not text emitted by the HDD Utility or by Ghidra. `fail_pol_cal()` hardcodes result `0x80`.

## Exact source-driven deduction

The `0x88/0x8F` handler prints the second line only after its initial `mg_maxsize==mg_size==header_size` check passes. With header count 0 and flags 0x022C, `mg_BIToffset` computes **0x48**: starts 0x20, adds 0 entries, no `flags&1` extra, adds 8 for `flags&0xF000==0`, plus 0x20. The 64-KiB `cdvd.mg_buffer` makes the offset-range rejection impossible for this sample. Thus the message came from the subsequent BIT check `buffer[0x4D..0x4F] !=0` or `GetBufferU16(buffer,0x4C)*16+0x48+24 != 0x1A0`. If reserved bytes are zero, the format requires **20 blocks**. The log does not show the true block count or reserved bytes.

The current SCMD implementation is largely a stub: authentication commands `0x80..0x87` issue fixed/success responses, including hardcoded challenge payloads; key returns `0x94..0x97` copy data from the buffered header, not proven decryption. Do not claim that correcting this BIT check alone completes real MagicGate decryption, and do not bypass/check-force to pretend success.

## Next step

Instrument **existing code** in `pcsx2/CDVD/CDVD.cpp`, specifically distinguish initial header mismatch, BIT location/length and reserved bytes, logging lengths/flags but not protected keys/data. Add bounds safety for rejected header before dereferencing the BIT fields. Build user's PCSX2 fork and run HDD Utility v1.10 with unchanged disc and `testdrive.raw`; collect `[MGTRACE]` and adjacent `[MG]` lines. When precise culprit is known, determine whether the KELF header is genuinely encrypted (due to missing MechaCon authentication) or PCSX2's parser has wrong offsets for this header variant. Never assume hardcoded success is an acceptable emulator fix.

No user-supplied game/media or proprietary key material in GitHub. DNAS secondary branch remains untouched.
