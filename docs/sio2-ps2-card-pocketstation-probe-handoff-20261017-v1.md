> **SUPERSEDED / REVERTED:** The SIO2 response change described below was speculative and has been reverted on this branch. Neither SIO0 nor SIO2 command-dispatch source now differs from the parent. The user subsequently bisected the PocketStation regression to MagicGate CDVD commit `b23b1cb69`, where non-KELF BIOS SCmd `0x8F` unexpectedly returns `0x80` after `0x80/0x81/0x82/0x83`. The evidence-based correction is on `note-working/magicgate-bios-card-auth-completion-20261017`, with its own handoff in `docs/magicgate-bios-card-auth-completion-handoff-20261017-v1.md`. Do **not** use this abandoned SIO2 branch as an implementation recommendation. The discussion below is preserved only as historical context.\n\n# SIO2 phantom PocketStation detection and HDD Utility DVD-player card checks (2026-10-17)

Branch: `note-working/sio2-ps2-pocketstation-probe-20261017` in `Masamune3210/pcsx2`.
Parent: `note-working/magicgate-authenticated-header-20261017` (contains working HDD Utility authenticated KELF/header/content logic). This is a separate SIO2 identification fix; do not alter or reduce MagicGate logging.

## Observed runtime evidence

- User successfully **completed installation** of Sony HDD Utility Disc 1.10 (USA, SCUS-97395) onto disposable 40 GiB PCSX2 HDD image. Subsequent HDD-enabled PS2 BIOS version screen shows **Browser 2.00** and **PlayStation Driver 1.10**.
- HDD-enabled Browser displays two PocketStation-style icons; selected item label `PocketStation®/1`, despite PCSX2 settings showing slot 1 PS2 64 MB file card `PS2 Saves.ps2` (formatted), slot 2 PS2 folder card `PS2 Memcard.ps2`.
- DVD Player installer reports no inserted PS2 memory card. Log confirms card changes at ~102-126 seconds, a new PS2 8 MB `MGTest.ps2` created at 118.2558 seconds and opened as `[8 MB, UNFORMATTED]` at ~125.29 seconds. A new PCSX2 file card is deliberately filled with 0xFF; not automatically formatted. Therefore that run alone does not establish failure to detect a **formatted** standard 8 MB card.
- The 7.2 MB / 116,020-line trace (user attachment `Pasted text(20261017-085448).txt`) contains no individual SIO2 card-probe command diagnostics because protocol MC_LOG_ENABLE defaults to 0. Do not claim the SIO2 issue is independently demonstrated by a byte-level capture.
- Sony's official HDD Utility manual specifies DVD Player update onto a memory card (8MB) (for PlayStation 2). PCSX2 historically has a workaround that exposes real PS1 memory cards as PocketStations, but PS2 cards should not be treated as PS1.

## Source-level defect and targeted change

- `pcsx2/SIO/Sio0.cpp::SetTxData` already rejects PS1 commands unless `mcd->IsPresent() && mcd->IsPSX()`.
- `pcsx2/SIO/Sio2.cpp::Sio2::Memcard` dispatches `PS1_READ=0x52`, `PS1_STATE=0x53`, `PS1_WRITE=0x57`, and `PS1_POCKETSTATION=0x58` on **any inserted card** without checking `IsPSX()`. `MemoryCardProtocol::PS1Read()` returns PS1 protocol data for a PS2 backing card, and `PS1Pocketstation()` supplies a zero response rather than filtering by type.
- Commit `3d0f0d1ce071bea4254e5a41091175bfa478536e` adds a SIO2-side check: for these four commands and a non-PS1 card, mark `CmdStat::DISCONNECTED`, drain the current input, and fill the response window with `0xff`. PS1 cards still take the original code path. Normal PS2 memory-card commands and all MagicGate/CDVD logs remain unchanged.

## Verification and next steps

1. **Not yet build-tested nor runtime verified**. Build this child branch and cold-boot the updated HDD-enabled BIOS with an existing formatted PS2 file card and PS2 folder card. Check whether both now display as PS2 memory cards instead of PocketStations.
2. With `MGTest.ps2` 8MB selected in slot 1, ensure it is **formatted** before retrying the optional DVD Player update. Check whether the updater detects the formatted card; if not, capture the failing path for investigation of MCMAN/SIO2 card authentication and any card-size-specific validation.
3. If the phantom remains, capture targeted SIO2 request/response bytes and MCMAN status to discriminate a PS1 fallback from failures in PS2 protocol. Do not guess or spoof success. Preserve existing MagicGate raw dumps.
4. Do not assume the PocketStation bug and DVD Player refusal necessarily share a single cause.

## Known unrelated limitations

`MemoryCardProtocol::PS1State` is currently unimplemented and raises a failure when genuinely invoked with a PS1 card, but this fix intentionally leaves that separate PS1 emulation issue alone. No copyright-protected BIOS or cryptographic keys included.
