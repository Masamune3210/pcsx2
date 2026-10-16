# PlayOnline DNAS v8: both Ghidra programs annotated, -109 retry scratch reset

**Branch:** `note-working/magicgate-scmd-boundary-trace-20261016`. Prior [v7](playonline-dnas-correct-disc-source-analysis-20261016-v7.md).

User confirmed successful v2 Ghidra metadata apply on **both** correct PlayOnline Viewer and Tetra Master second-disc executables: CD `SCUS_972.69` and HDD variant `POL_HDD.ELF`. Each gained 21 DNAS string labels, enum `Dnas1ObservedStatusCode.DifferentHdd=-109`, function `ProcessDnas1AuthenticationStatus` and 7 comments, with **0 failures**. Cross-probe mismatch is expected.

Source disassembly identified exact retry behavior in both variants:
- CD `0x0080F0D8` reads current DNAS 32-bit status at snapshot+4; `0x0080F0DC` compares with -109. HDD equivalents `0x0080E8E8`, `0x0080E8EC`.
- CD retry helper `0x0080DB98`, HDD `0x0080D3A8` calls a byte-zeroing routine twice, clearing 16 bytes at runtime context+0x10078 and another 16 bytes at context+0x10068.
- Runtime context global pointer **differs by executable**: CD `0x008C7940`, HDD `0x008C1E00`. Do not misattribute addresses across variants.
- Zeroing routine CD `0x00135178` / HDD `0x00135238` clears `length` bytes and returns length, source-backed `uint ZeroByteSpan(byte *buffer,uint length)`.
- Status logger CD `0x0080DE08` / HDD `0x0080D618` compares current snapshot words (+0 DNAS state, +4 status/flags, +8 progress) with previously reported values at context+0x10068 and caches updates. Runtime current snapshot at +0x10078.
- State predicate CD `0x0080DB60` / HDD `0x0080D370` returns true for state 2 or 5-7 only.

Ghidra [v26 handoff](https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/docs/PS2_MagicGate_Continuation_Handoff_20261016_v26.md) contains new source-backed `Dnas1StatusSnapshot` (0x10) and `DnasRetryClearedWindows` (0x20) structs. A metadata APPLY + optional decompiler/XREF report script `ApplyAndTracePlayOnlineDnas97269_20261016_v3.java` was prepared; user runtime test pending.

**Crucial distinction:** -109 retry handler is established. DNAS -404 producing path remains unresolved; direct -404 literals in other code are not automatically DNAS. The `[DNASLBA]` ATA trace source previously added remains pending user build/test, and should not be claimed verified. User has no genuine Sony HDD and should not be asked for one.

No PCSX2 source modifications this pass; handoff only.
