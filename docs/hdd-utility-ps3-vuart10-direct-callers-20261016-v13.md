# PS2 HDD Utility MagicGate PS3 VUART port-10 findings v13 (2026-10-16)

Current PCSX2 working branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v12](hdd-utility-ps3-gxemu-vuart-callers-20261016-v12.md); DNAS remains parked.

User's Ghidra result running `TracePs3GxemuVuartCallers_20261016_v2.java` on PowerPC:BE:64:A2ALT `ps2_gxemu.elf`:

- **Four verified LV1 VUART wrapper renames applied**:
```
0x000494A4 -> Lv1WriteVirtualUart
0x00049544 -> Lv1ReadVirtualUart
0x000495B8 -> Lv1VirtualUartParameterDispatch
0x00049664 -> Lv1ConfigureVirtualUartIrq
```
- Direct local port-10 UART read: call `0x0008FCA0` in `FUN_0008FC68` -> `Lv1ReadVirtualUart@0x49544`.
- Direct local port-10 UART write: calls `0x0008FF24`, `0x000900D0`, `0x000901C8` (in `FUN_000900FC`), `0x000903D8` -> `Lv1WriteVirtualUart@0x494A4`. Other three write sites have no containing Ghidra function yet.
- Shared candidate PPC64 function descriptor TOC `0x00678548`. Descriptor addresses `0x006470A0`, `0x006470B8`, `0x006470E8`, `0x00647100`. The first two 64-bit fields agree with wrapper entry and TOC; third not yet read/verified.
- `0x00049748` another LV1 read with no function: `li r11, 0xA2; or r3,r28,r28; or r4,r29,r29; or r5,r30,r30; sc 1`. It uses caller-supplied r28 as port so not proven MagicGate.

`r3=10` values come from preceding linear PPC immediate lookup, not a full dominance proof. Inspect incoming control-flow and packet buffers before naming handlers, changing code, or assuming native MG SCMD support exists in a particular function.

New Ghidra script on `Masamune3210/ghidra-emotionengine-reloaded` `testing`:
https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/InspectPs3GxemuVuart10Endpoints_20261016_v2.java

Probe read-only: emits code/disassembly and decompilation for the two port-10-containing functions plus context at all 5 port-10 call sites and ownerless read, captures remaining descriptor words and cross-references. Not run by user yet; v1 prototype removed.

The PCSX2 HDD Utility v1.10 remains failing with native `0x1A0` encrypted KELF header transferred intact but rejected by stubbed MagicGate `0x8F` BIT validation. Continue through Sony's actual host/emulator interface; do not bypass validation or revive DNAS.
