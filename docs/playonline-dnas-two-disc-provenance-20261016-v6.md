# PlayOnline DNAS investigation: important two-disc distinction (2026-10-16 v6)

**Branch:** `note-working/magicgate-scmd-boundary-trace-20261016`. Continuation of [v5](playonline-ffxi-erx-recovery-handoff-20261016-v5.md).

## User correction

`SCUS_972.66` is **Final Fantasy XI Online**, the game/install disc. `SCUS_972.69` is **PlayOnline Viewer and Tetra Master**, a separate install disc. In the prior Ghidra investigation, we focused on `INSTALL.ELF`, `DANCER.ENC`, `POLKEY.DAT`, `SCUS_972.66` and encoded ERX modules from the **first** disc. Those findings are valid for FFXI, but the DNAS failure being debugged in PCSX2 arose during PlayOnline Viewer installation on the **second** disc.

Do not treat FFXI first-disc `LastDnasError` error formatting, `POLKEY.DAT`, or Dancer overlay as evidence that we have located the verifier or failure path in `SCUS_972.69`. Preserve useful recovered IRX files, established Ghidra metadata, and decoder. The user has clarified that PlayOnline Viewer and Tetra Master is the second disc even though the FFXI first disc does not visibly label itself disc one.

The existing SCMDTRACE and ATA/DEV9 investigations **already pertain to the failing PlayOnline second-disc test** (`SCUS_972.69`), including the prior `[SCMDTRACE]` request accounting. Do not discard that evidence. The read-only `[DNASLBA]` source modification remains uncompiled/unverified in a new runtime test.

**Next step:** obtain/inspect `SCUS_972.69` file listing and relevant executable and IOP modules, or load any already available earlier Ghidra export of that precise disc. Follow DNAS/HDD identity RPC and return code -404 in *that* program, not defaulting back to FFXI game install disc.

**Do not suggest sourcing/dumping a genuine Sony HDD or HDDID:** user has none. New Ghidra scripts must apply all established metadata, not merely report it.

No PCSX2 source code changes in this handoff-only update.
