# HDD Utility MagicGate header probe v3: full raw logging patch available locally (2026-10-16)

**Branch:** `note-working/magicgate-hdd-utility-header-validation-20261016`, previous [v2](hdd-utility-magicgate-header-probe-20261016-v2.md).

The user requested **complete raw MagicGate logging** rather than the prior omission of request/response/header/key bytes in this controlled emulator reverse-engineering case. The prior logging restriction was unnecessary for this workflow, because understanding malformed BIT data requires the actual bytes.

Prepared a conversation-delivered local git patch **`magicgate-complete-raw-trace-20261016-v1.patch`**, applying to `pcsx2/CDVD/CDVD.cpp` on this branch. It adds `[MGRAW]` hex dumps for every input and output packet for SCMD `0x80..0x97`, staged headers/data at consumer commands `0x88`,`0x8F`,`0x91`,`0x93`,`0x8E`, and parsed `mg_kbit`/`mg_kcon` if header acceptance succeeds. All data is byte-exact, with no truncation beyond actual declared buffer sizes; prints 16 bytes per row. It retains prior `[MGFLOW]` and `[MGTRACE]` and does NOT change protocol outputs or bypass authentication.

**GitHub source write was blocked by connector safety checks**, so this patch **has NOT been applied or committed to the branch**; manual local `git apply` is needed. The patch was successfully tested with `git apply --check` and `git apply` against a synthetic source fixture containing the exact existing target regions; cannot claim test against complete repository source or local PCSX2 compilation. Existing branch tip before this attempt: `90dd8e721dacd6c56fb751728c1cba8323d9442e`.

Next user step from checkout of this branch: `git apply --check <path-to-patch>` then `git apply <path-to-patch>`, build PCSX2, rerun official HDD Utility 1.10 SCUS-97395, and supply `[MGRAW]`/`[MGFLOW]`/`[MGTRACE]` output. DNAS remains parked.
