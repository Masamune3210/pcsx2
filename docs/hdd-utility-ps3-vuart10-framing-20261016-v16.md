# PS2 HDD Utility MagicGate: GXEMU UART framing v16 (2026-10-16)

Working PCSX2 branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v15](hdd-utility-ps3-vuart10-function-recovery-20261016-v15.md). DNAS parked.

User ran `PromotePs3GxemuVuart10Senders_20261016_v1.java`: all three 24-byte PowerPC64 descriptor triples verified; all three missing functions promoted, named and scoped `r2=0x678548` set. Updated Ghidra `ps2_gxemu.elf-64(1).gzf` subsequently provided and its serialized DB symbol strings checked.

**Applied functions** (do not rename again):
- `0x0008FD48` `SendVirtualUartVariablePacket`
- `0x0008FF54` `SendVirtualUartFixed64BytePacket`
- `0x000901FC` `SendVirtualUartVariablePacketAlternate`

**Applied descriptor declarations** `Ppc64FunctionDescriptor24` (0x18, entry/TOC/environment): `0x0064F728..0x0064F73F`, `0x0064F740..0x0064F757`, `0x0064F770..0x0064F787`. Environment zero, verified TOC 0x678548.

Sony PS3 `ps2_gxemu` virtual UART port 10 now shown constructing **different classes of framed messages**:
- at 0x8FD48 read BE32 input payload length from +0x44, transmit `payload+0x48`, header length fields at +0x08/0x0C equal `payload+0x38`; frame class at +0x06 = 0x50.
- at 0x901FC read BE32 payload length from +0x3C, transmit `payload+0x40`, header lengths `payload+0x30`; frame class = 0x50.
- at 0x8FF54 transmit fixed 0x40 bytes, class at +0x06 = 0x90, payload length field +0x0C=8.
- at previously renamed `SendVirtualUartFrame@0x900FC`, fixed 0x70 packet beginning `0x0000FACE`, `0x00005000`, class at +0x06=0x50, packet length +0x0C=0x60.
- Shared counter and transmit state at r2-0x59F8/0x59F0 etc.

Important: Outer UART class `0x90` is **not** proof of native MechaCon S-command `0x90`, nor is a 0x50-class packet necessarily a native KELF header. No crypto or actual MechaCon handler established.

A new Ghidra Java metadata/apply-and-probe script is committed to `ghidra-emotionengine-reloaded` branch `testing`:
https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/RefinePs3GxemuVuart10Protocol_20261016_v1.java

It creates neutral UART common-header and transfer-state datatypes, repairs guarded scoped TOC for two previously recovered RX/frame functions, labels typed global-pointer slots when safe, and inspects wrapper/transport helper/decompiled call chains and Brian diagnostic string. Not yet run in user's Ghidra; do not infer any results from it yet.

Goal remains genuine retail KELF handling for HDD Utility `SCUS_973.95`, native S-command `0x90 -> 0x8D x26 -> 0x8F` with full `0x1A0` header successfully transported but rejected by stubbed PCSX2 BIT handling. No bypass, key forgery, or PCSX2 source code change.
