# PCSX2 HDD Utility / PS3 GXEMU recovered UART state producers v25 (2026-10-16)

Current PCSX2 branch `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v24](hdd-utility-ps3-gxemu-state-producers-20261016-v24.md). Native Dragon 6.12 MMIO/KELF contract separately assigned to Codex on `Masamune3210/ps2mgdecomp`, see `TASK_NEXT_DragonMagicGateCipherContract_20261016_v1.md`.

## Latest Ghidra audit completed

User executed `TracePs3GxemuVuartDispatchProducers_20261016_v1.java` against PS3 firmware 4.93 `ps2_gxemu.elf`, Ghidra 12.1.3 `PowerPC:BE:64:A2ALT`.
- Verified state-4 writer `0x24F91C` and state-2 writer `0x24FC5C`, both store through TOC-0xDC0 state pointer `0x00677788`.
- Newly found **state-1 writer `0x24FFDC`**, calling path not yet promoted in Ghidra; queues `SendVirtualUartVariablePacketAlternate` via dispatcher state1. State3 setter remains unknown, and current constant/local code scan is not exhaustive.
- State pointer slot `0x00677788` currently decodes to actual mapped writable memory `0x02BBA8E0` in the user's Ghidra database. A label for target not yet applied.
- Independently confirmed 16 exact PowerPC64 function descriptors at `0x006703C8..0x00670547`, each 24 bytes with TOC `0x00678548` and environment zero, covering 3 already defined functions (`0x24F890`, `0x24F95C`, `0x250D70`) and 13 missing Ghidra functions. Not all body boundaries yet validated.
- Dispatcher indirect callback at `0x250EE8..0x250EFC` does proper save r2, load descriptor TOC/environment, bctrl, restore r2. No function-wide constant TOC forced.

## Next Ghidra action

Script committed to `Masamune3210/ghidra-emotionengine-reloaded` branch `testing`:
https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/RecoverPs3GxemuVuartControlFunctions_20261016_v1.java

It verifies missing PPC64 function OPDs and complete bounded code extents individually. Only safe, conflict-free function bodies are promoted; the state4 and state2 queue functions receive guarded generic names and remaining bodies keep unresolved signatures and `FUN_` names until C proves behavior. Existing 24-byte OPD datatype and conditional scoped r2 context applied where safe. Direct state variable at mapped `0x02BBA8E0` receives enum datatype/label only if Ghidra writable block and data conflict checks pass. Key recovered functions are decompiled to expose state1 source and possible state3 writer. **Not yet run by user**.

Native PCSX2 blocker unchanged: genuine HDD Utility sends complete encrypted 0x1A0 KELF header `SCmd90(00 A0 01 00 00), 26x SCmd8D, SCmd8F`, existing stub reads ciphertext as plaintext BIT and fails. Need actual MechaCon cryptographic transform, not fake success. PS3 UART states/subtypes have not been correlated with MechaCon SCmd IDs. DNAS parked. No PCSX2 code changes.
