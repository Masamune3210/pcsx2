# HDD Utility MagicGate: PS3 GXEMU port-10 state v18 (2026-10-16)

Branch `note-working/magicgate-hdd-utility-header-validation-20261016`. Prior details in `docs/hdd-utility-ps3-vuart10-framing-20261016-v17.md`.

## Ghidra progress, directly verified from user-run output

`RefinePs3GxemuVuart10Protocol_20261016_v2.java` created neutral frame-prefix (0x10) and UART transfer-state (0x18) structures; applied global-pointer datatypes/names in `ps2_gxemu.elf`:
- `0x00672B50 gVuart10SequenceCounterPointer` `dword *64`
- `0x00672B58 gVuart10TransferStatePointer` `GxemuVuart10TransferState *64`
- `0x00672B60 gVuart10ReceiveOffsetPointer` `qword *64`
- `0x00672B68 gVuart10RemainingBytesPointer` `dword *64`

Scoped r2/TOC `0x678548` was set for `SendVirtualUartFrame@0x900FC`; `ReceiveVirtualUartChunk@0x8FC68` skipped because Ghidra P-code reports an r2 writer, unclassified. Do not force it.

Verified call chain anchors:
- `0x0004946C -> ReceiveVirtualUartChunk@0x8FC68`, unowned caller site.
- `0x0024F9A4`, caller function `FUN_0024F95C`, -> `SendVirtualUartFrame@0x900FC` (emits fixed 0x70 bytes, outer header tag `0xFACE`).
- `0x00251044 -> SendVirtualUartVariablePacketAlternate@0x901FC`, unowned site.
- `0x0025106C -> SendVirtualUartVariablePacket@0x8FD48`, unowned site.
- Shared helper `0x0024F718` referenced by send and receive. Its role not yet established.
- `REPORT TO BRIAN!` debug message reported at `0x002B1A80`, no identity/trigger found.

Only a **partial user console log** was supplied; it stopped at the diagnostic string before decompile output or finish status. Do not assume output after that point.

## New Ghidra script on `testing`

https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/AuditPs3GxemuVuart10CallChain_20261016_v1.java

Read-only report of offending r2 instruction/P-code, missing caller boundaries and upstream framing functions (0x4946C/0x251044/0x25106C/0x24F95C/0x24F718). Not yet user-run/compiled. Keep established 9 VUART function names and 7 PPC64 descriptor type declarations intact.

PCSX2 blocker unchanged: authenticated retail KELF header 0x1A0 reaches native `0x90 -> 26 x 0x8D -> 0x8F` but existing stub parses encrypted BIT bytes and rejects. UART class 0x90 not proven native SCMD 0x90. DNAS work parked.
