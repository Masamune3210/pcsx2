# HDD-enabled MagicGate integration: full-log runtime verification (2026-10-17, v4)

Branch: `note-working/magicgate-card-auth-protocol-20261017`
Test build: PCSX2 `v2.9.92-87-gb414a76dd`
Source evidence: user-supplied `Pasted text(20261017-164638).txt`, 670,680 lines (32,083,911 bytes), local file not committed. This follows the user-verified correction of PS2 card identification and formatting, and the completed MSVC build correction `bee3eef2`.

## Verified interactions

- **16 card authentication sessions completed**: each SCmd `80, 81, 82, 83, 84, 85, 86, 87, 88` occurs 16 times and returns `00`. Card phase completion goes through `8F:01` then `8F:00` for each of the two challenge-response phases. No `8F:80` rejection was captured anywhere in the log.
- **All 0x8F responses**: `200 × busy 01` and `200 × success 00`, with no other values. Accounting exactly matches `16 × 2 card phases = 32`, `6 header verifications`, and `162 content transfers`, total 200 complete operations.
- **Three authenticated HDD boot cycles**: the log shows `[MG] Verified KELF header: blocks=53 BIT bytes=856 content bytes=213136` three times, and `[MG] Verified KELF header: blocks=50 BIT bytes=808 content bytes=913888` three times. After each, `pfs0:/osd100/hosdsys.elf` is successfully loaded, at timestamps `65.7570`, `221.1578`, and `744.1310` seconds.
- **HDD-disabled sanity run first**: initially HDDLOAD reports ATA initialization failed -5 at 20.9979s. User enables `testdrive.raw` (40,960 MiB, 83,886,080 sectors) before subsequent successful boot runs.
- **Memory-card operations**: initially `test2.ps2 [8 MB, UNFORMATTED]`; two `(FileMcd) Warning: checksum sector overwritten` messages (78.4673s, 79.9040s) while card formatting/writes occur. Later `test2.ps2` is removed, and slot 0 uses `PS2 Saves.ps2 [64 MB, Formatted]`; `OSD [MemoryCardSave0]` logs successful saves at 225.9590s and 748.9524s. Crucially there are **zero** `(FileMcd) Warning: writing to uncleared data` records, unlike the pre-fix regression. User previously confirmed BIOS PS2 card recognition and formatting visually.
- **Full gameplay exercised**: log boots `cdrom0:\\SCUS_972.13;1` with game CRC `1DF41F33` twice; Rich Presence near log end shows `[Ch2] - Max is in Sindain Station` in Dark Cloud 2. No observed MagicGate regression in this session.

## Noise and other warnings (not blockers to the verified MagicGate paths)

- Log dominated by DEV9 transactions during game session: around **249,871** `DEV9: SPD_R_PIO_DIR 8bit write 1`, **188,865** `DEV9: *DEV9writeDMA8Mem: size 2000`, and **121,800** `DEV9: HDD_WriteDMA` records across the major game segment. Existing oververbose logging is intentional; do not trim it.
- Existing PFS warnings include `pfs: warning: you may need 12 buffers, but I have only 8` and `pfs: warning: null buffer returned`. Their presence alone does not contradict successful HDD OSDSYS loads.
- `Failed to parse CHD TOC` around 169.9475s during disc activity, but subsequent game ELF launches; the error is not evidence of MagicGate failure.
- `sceMidi_MidiPlaySwitch Error Not Stop` occurs during gameplay, unrelated to MechaCon auth. It may merit independent audio/game inspection if symptoms are observed, not as a reason to alter crypto.
- PCSX2 complains `patches.zip` is absent because this is a manual build; not a MagicGate issue.
- Extra retail key groups `MG_CARDKEY_1/_2`, `MG_CARDIV_1/_2`, `MG_CARDKEY2_1/_2`, `MG_CARDIV2_1/_2` are logged as intentionally unsupported 184 times each across repeated .mgkeys reads. CEX group 0 works; support for other key groups is a distinct feature, not evidence of regression.

## Verification status and next steps

**PASS in user's actual BIOS/HDD/game workload**: both memory card appearance and formatting were previously user-confirmed; this full run now proves the new card-authentication state machine coexists with KELF verification, protected content, repeated HDD Browser loads, and game launches/saves. Do not use the old one-shot 0x8F workaround. No changes to emulator code or logging warranted from the supplied trace.

Not yet established: independent physical hardware cryptographic-vector parity, cross-version BIOS/model coverage, devkit key groups, savestate persistence for virtual card auth state, card-bound F1/F2 operations, or standalone CI green status. These remain separate future validations/features, not blockers for this verified regression.
