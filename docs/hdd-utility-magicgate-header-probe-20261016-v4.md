# HDD Utility MagicGate raw logging patch correction v4 (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v3](hdd-utility-magicgate-header-probe-20261016-v3.md).

The user attempted to apply the previously delivered `magicgate-complete-raw-trace-20261016-v1.patch` locally and received `patch failed: pcsx2/CDVD/CDVD.cpp:2416`. This was **a patch-generation defect**, not evidence of changed branch code: its second hunk expected **two blank lines** before `CDVD_LOG("cdvdWrite16 ...")`; the actual branch source has **one**. Original hunk location headers were also stale.

Corrected conversation-delivered file: **`magicgate-complete-raw-trace-20261016-v2.patch`**. It removes the spurious blank context line, corrects all three hunk line headers to original source positions `2410`, `2427`, and `3101` with corresponding new offsets, and retains raw-request/response/staged-buffer diagnostic behavior without altering S-command results or validation.

Verification: fetched the exact current branch source with GitHub connector and compared affected snippets; verified corrected patch against a synthetic full-length source fixture built from the exact old hunk contexts, running `git apply --check` then `git apply` successfully. **Not validated on the user's local checkout or in a compiled PCSX2**; local changes may differ, so use `git apply --check` first. GitHub source update is STILL blocked by safety checks; this patch has NOT been committed.

Next command (from the root of this branch): `git apply --check "path\\to\\magicgate-complete-raw-trace-20261016-v2.patch"` followed by `git apply "path\\to\\magicgate-complete-raw-trace-20261016-v2.patch"`. No `--reject` or forced application required. Compile and rerun HDD Utility v1.10 to collect `[MGRAW]`, `[MGFLOW]`, `[MGTRACE]`; DNAS remains parked.
