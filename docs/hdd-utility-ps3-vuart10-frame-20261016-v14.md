# PS2 HDD Utility MagicGate: PS3 GXEMU frame findings v14 (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v13](hdd-utility-ps3-vuart10-direct-callers-20261016-v13.md). DNAS parked.

## New verified PS3 Ghidra findings

Read-only `InspectPs3GxemuVuart10Endpoints_20261016_v2.java` output confirmed 5 virtual UART port-10 callers:
- `0x0008FCA0` read from `Lv1ReadVirtualUart@0x49544`, owner `FUN_0008FC68`: persistent position+remaining bookkeeping, callback when received bytes complete; source `0x0004946C` calls this function.
- `0x0008FF24`, `0x000900D0`, `0x000901C8`, `0x000903D8`: writes using `Lv1WriteVirtualUart@0x494A4`. Only `0x901C8` has owner `FUN_000900FC`; others missing containing Ghidra functions.
- `FUN_000900FC` builds a `0x70` byte port-10 message with candidate frame signature `0xFACE` at +0, `0x5000` at +4, `0x60` at +0x0C, addresses represented as `DAT_00005005`/+0x10 and `DAT_00005000`/+0x18. Source call `0x0024F9A4` invokes this function. **No proven mapping from frame code to native MechaCon S-command or cryptographic handler**.

All four hypercall-wrapper descriptors validated as 24-byte BE64 triples (entry, r2 TOC, third zero) with r2 TOC `0x00678548`, at `0x6470A0, 0x6470B8, 0x6470E8, 0x647100`. Ghidra datatype `Ppc64FunctionDescriptor24` is directly applicable. A two-function transport rename is proposed, requiring callsite verification.

## Next action

User pull `Masamune3210/ghidra-emotionengine-reloaded` `testing`, run:
https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/ApplyAuditPs3GxemuVuart10Frames_20261016_v1.java

Script verifies descriptor bytes, conditionally applies datatype and two function renames, prints missing function boundary candidates and TOC-relative slots. Not user-run/compiled yet. Existing symbols/types left intact in case of conflict. Investigate 0x8FF24/0x900D0/0x903D8 callers and 0x0004946C/0x0024F9A4 origins next, to reach packet dispatch or MagicGate operations.

No PCSX2 code change, bypass, or 3.06 MechaCon dump attempted. Full native KELF crypto remains unimplemented.
