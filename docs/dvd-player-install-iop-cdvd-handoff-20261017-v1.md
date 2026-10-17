# DVD Player install: third IOP reboot loses CD-ROM module access (2026-10-17)

Branch: `note-working/dvd-player-install`, originally identical to `testing` at the start of this investigation. Separate from the already-promoted MagicGate runtime implementation on `testing`. **Do not merge diagnostic logging into testing as a functional change.**

## Reproduction

Input: user-provided PCSX2 `Pasted text(20261017-182201).txt`, 113,744 log lines, test build `v2.9.92-87-gb414a76dd`. Media: authentic HDD Utility Disc 1.10 USA `SCUS-97395`, existing 40 GiB test HDD and PS2 BIOS 1.60 USA. DVD Player install is being exercised. A manually enabled `Fast CDVD` setting is explicitly warned about during startup; it is a possible independent timing variable but not an established cause.

**Failed module path is not newly malformed:** it is written in the log as `cdrom0:FILESSIO2MAN.IRX;1` in all three attempts, and loads successfully twice. Do not insert separators or rewrite filenames based on the flattened path string.

| Time | Observation |
| --- | --- |
| ~37.2672s | `loadmodule: fname cdrom0:FILESSIO2MAN.IRX;1`; loads `sio2man version 2.03` as module id 25, return 0 |
| ~122.3189s | Identical requested path, same module id 25 / return 0 |
| ~144.590–146.192s | HDD Utility executes `cdrom0:FILESPS288.VOB;1` successfully |
| ~147.494–147.714s | Update reboot, with IOP Realtime Kernel 2.1 and ROM `cdvdman`, `cdvdfsv` modules |
| ~149.2942s | Unique special DVD/CDVD call `cdvdReadKey(0, 0, 4246)`; key registers logged |
| ~149.946–150.0387s | Another soft reboot followed by `rom0:UDNL` with `img0:`, then successful update reboot |
| ~150.1107s | **Third identical path** `cdrom0:FILESSIO2MAN.IRX;1` |
| ~150.2958s | `open fail name FILESSIO2MAN.IRX;1`, `loadmodule: id -203`; no succeeding `sio2man` registration |
| ~177.05s | Emulator manually paused; does not prove any subsequent recovery |

First two successful attempts similarly go through `rom0:UDNL args 11 arg img0:` and register IOP Kernel 2.1 and CDVD services. The third differs in having the CDVD `ReadKey(4246)` call shortly before it. The source code's `cdvdReadKey` special mode changes `cdvd.Key`, while CDVD `decSet` can separately transform sector DMA bytes (`mechaDecryptBytes`). The log *does not show* `decSet` writes or live values. Therefore this is a **testable candidate**, not proven causation. A corrupted sector due to stale CDVD state would be consistent with the same file becoming unfindable across IOP reboots. Other possibilities include timing / Fast CDVD and native CDVD driver state.

## Diagnostic commit

`4da2db0dc042bab77741115f520e7c1169efe8f8` changes only `pcsx2/CDVD/CDVD.cpp`, adding `[DVD_INSTALL_PROBE]` lines:

- At special `cdvdReadKey(..., arg2=4246)`, log `cdvd.decSet`, `cdvd.KeyXor`, disc type and current sector before the key updates.
- On every `cdvdWrite3A` (hardware DEC_SET register write), log old/new value and current sector, re-arm a 12-sector diagnostic budget.
- When `cdvd.decSet != 0` and a CDVD sector is about to be transformed after DMA, log the first 12 sectors' LSN, block size, and register state.

No register values, sector data, CDVD timing, crypto behavior, or existing MG logging were changed. This probes for stale hardware decryption but does not presume it. The log may show that `decSet` is always zero, which would *disprove this candidate* and shift the investigation to CDVD filesystem/read state.

**Next action:** Build this branch, repeat **with the same media, HDD and Fast CDVD settings**, and return the `[DVD_INSTALL_PROBE]` lines and area around the third failed `loadmodule`. If `decSet` is zero, inspect sector read status and native IOP `cdvdfsv` code next; do not alter file-name normalization. As a separate controlled comparison, disabling Fast CDVD may help identify a timing-dependent issue after reproducing the diagnostic run.

## Validation status

Source commit applied on GitHub; **no build or new runtime test yet**. Two successful opens + one failed open are user-log-verified. Root cause and fix remain unconfirmed. No change committed to `testing`, consistent with the rule to promote only verified functional changes.
