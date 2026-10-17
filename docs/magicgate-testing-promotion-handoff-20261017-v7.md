# MagicGate implementation promoted to testing (2026-10-17, v7)

## State

Source branch: `note-working/magicgate-card-auth-protocol-20261017` (complete research history and docs retained).
Target branch: `testing`, **updated successfully** to commit [`9984a61bc1b2d9c28a2f195e70b88e707cd8d529`](https://github.com/Masamune3210/pcsx2/commit/9984a61bc1b2d9c28a2f195e70b88e707cd8d529), starting from previous `testing` head `87002889e855ed646205211f1c7878e96893bb2d`. GitHub returned success for a **non-force, expected-SHA-guarded fast-forward**. Remote branch head was then reread and verified to equal `9984a61b`.

## Integration method and scope

Created a single Git commit using the exact **blob SHAs** already present in the working source branch for **11 functional files** on top of the original `testing` tree. Compared the prospective commit before updating `testing`: 1 commit ahead, 0 behind, and exactly the 11 requested files. Kept the many dated investigative/Ghidra notes and prior experimental commits out of `testing` to avoid noise and already-retracted partial implementations. The source research branch was not reset, rebased or force-updated.

Included:
- `pcsx2/CDVD/CDVD.cpp`: authenticated KELF header/content and MechaCon card-auth SCmd routing.
- `pcsx2/CDVD/MagicGateCrypto.h`: key loading/parsing and crypto.
- `pcsx2/CDVD/MagicGateCardAuth.h`: emulated two-stage 3DES card challenge-response.
- `pcsx2/SIO/Memcard/MemoryCardProtocol.cpp`: F0/F3/F7 SIO2 card-side authentication and selected-card slot correction.
- `tools/magicgate/BuildMagicGateKeyFile.py`
- `tools/magicgate/ExtractMagicGateContentFromLog_20261017_v1.py`
- `tools/magicgate/SmokeMagicGateCardAuth_20261017_v1.cpp`
- `tools/magicgate/SmokeMagicGateCrypto.cpp`
- `tools/magicgate/VerifyMagicGateFirstBlock_20261017_v1.cpp`
- `tools/magicgate/extract_hdd_mg_header_20261017_v1.py`
- `.github/workflows/magicgate-crypto-standalone-20261017-v1.yml`

No .mgkeys values or private keys were committed. Existing deliberate verbose logging is untouched. No unrelated source files or docs were added to `testing`.

## Validation

Previously **user-verified** on source branch: Visual Studio build after SIO slot correction, PS2 card recognized (not PocketStation), working PS2-card format, 16 complete card-auth sessions, six verified HDD KELF headers, three successful HDD Browser loads, Dark Cloud 2 sustained HDD installation/gameplay/saves. PlayOnline Viewer installer now reached network/DNS activity and POL-1556 / network error -514, **not** proven DNAS authentication success.

**Newly verified** for promotion: exact 11-file Git tree contents and GitHub remote `testing` ref. **Not performed:** new MSVC build on the promoted `testing` commit, new GitHub Actions success, upstream rebase/merge/fetch on user's workstation. Do not describe the promoted commit as green CI merely because prior runtime integration passed.

## Next expected steps

1. On local Windows repo, update origin/testing from GitHub; if needed, add `upstream` pointing to `https://github.com/PCSX2/pcsx2.git` and fetch without merging.
2. Evaluate the upstream delta separately, then merge/rebase into the updated `testing` only after resolving actual conflicts and verifying the MagicGate code remains functionally intact.
3. Keep any further speculative MagicGate authentication work on a new isolated branch off current testing. Retain existing logging and this source branch for causality and handoff details.

No Git remote configuration can be applied to the user's local Windows clone through the GitHub connector.
