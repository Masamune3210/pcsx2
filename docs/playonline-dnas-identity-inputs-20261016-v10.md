# PlayOnline DNAS v10: actual source of local -404 consistency decision (2026-10-16)

Branch: `note-working/magicgate-scmd-boundary-trace-20261016`. Previous `docs/playonline-dnas-404-provenance-20261016-v9.md`.

User supplied two 786-line `ApplyPlayOnlineDnasErrorPath_20261016_v4.java` Ghidra reports from the **correct PlayOnline Viewer disc** (CD `SCUS_972.69` and HDD `POL_HDD.ELF`). They confirm local -404 when `CheckDnasIdentityBufferConsistency` returns zero after primary prerequisite >0, and map it through DNAS error translator to `POL-1562`. CD 0x001B9850; HDD 0x001B9950.

Identity consistency check: obtains first 16-byte candidate via CD 0x001B7D68 / HDD 0x001B7E68, compares first 6 against compiled-in bytes `1A 00 02 07 30 00` (CD 0x001FD398/HDD 0x001FD498). If equal returns 2. Otherwise reads another 16 bytes CD 0x001B9788/HDD 0x001B9888 and compares all 16 to first, returns 1 if equal or zero if not. Zero is converted to -404 by CD 0x001984D8/HDD 0x001985D8. Negative acquisition errors propagate; local init state not equal 1 returns -8.

Producer 1 builds first 8 bytes from two distinct lower-level responses for command IDs `0x24` and `0x26` via CD functions 0x001B6D20 and 0x001B6E08 (HDD +0x100), zeros remaining 8. Both command functions invoke common command-availability predicate CD 0x001133C8/HDD 0x00113488 and command-transport CD 0x00111220/HDD 0x001112E0. These numeric codes are **not yet proved CDVD SCMD codes**; don't conflate with earlier SCMDTRACE log.

Producer 2 reads/derives 16 bytes from a temporary 20-byte buffer produced by 512-byte transfer/processing routine CD 0x001B8B40/HDD 0x001B8C40, copies bytes 4..19, and wipes scratch. This path performs lower-level ops CD 0x001B8900 and 0x001B85E0 (HDD +0x100), semantics unknown. No proof yet that either source is physical HDDID, console ID, or MechaCon despite related prior research.

Next targeted Ghidra metadata applier/exporter `ApplyAndTracePlayOnlineIdentitySources_20261016_v5.java` supplied locally and recorded in Ghidra [v28 handoff](https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/docs/PS2_MagicGate_Continuation_Handoff_20261016_v28.md). Await decompilation of source/command transports before implementing additional emulator change. Existing `[DNASLBA]` instrumentation has not been runtime-tested. No true Sony HDD exists and user need not acquire one.
