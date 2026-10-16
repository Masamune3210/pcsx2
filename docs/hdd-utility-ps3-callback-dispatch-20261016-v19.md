# PCSX2 HDD Utility MagicGate: PS3 GXEMU callback and dispatch handoff v19 (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous document: `docs/hdd-utility-ps3-vuart10-call-chain-20261016-v18.md`. DNAS parked. No PCSX2 code change from this analysis.

User's `AuditPs3GxemuVuart10CallChain_20261016_v1.java` output proves:

- `ReceiveVirtualUartChunk@0x8FC68` changes r2 **temporarily for an ELFv1 indirect function-descriptor callback**, not because the Ghidra processor spec incorrectly preserves r2. `0x8FD14` saves r2, `0x8FD18` loads environment to r11, `0x8FD1C` loads callback TOC into r2, `0x8FD20` bctrl, `0x8FD24` restores r2. The existing `Ppc64FunctionDescriptor24` structure (entry, toc, environment, 0x18) is correct. Do not force r2 context across the callback.
- `FUN_0024F718` uses alias masking `0xFFFFFFFFFFFDFFFF` and two global base pointers to translate one mapped buffer address, returning input unchanged otherwise. Suggested generalized name/signature `ulong TranslateMappedBufferAddress(ulong candidateAddress)`, preserve current 8-byte datatype.
- `FUN_0024F95C` sends a fixed `0x70` packet via UART10 and then processes three conditionally present 16-byte blocks and up to three reversed 8-byte values; XORs 5 bytes of each selected component with a nonzero time-base low byte. This is potentially security/protocol-related but **NOT proven MagicGate KELF crypto**. It contains an `sc 0` operation after the UART send.
- Unowned instructions at `0x00251044` and `0x0025106C` invoke the two different UART variable-length senders. Surrounding branches `0x250DC0`, `0x250ECC`, `0x250EE4` strongly suggest they are within a larger dispatcher with missing function boundaries. Do not promote arbitrary fragments. Nearby known subsequent function starts `0x25109C`.

Next Ghidra script pushed to `ghidra-emotionengine-reloaded` `testing`:
https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/RefinePs3GxemuCallbackAndDispatcher_20261016_v1.java

This validates and applies callback pointer datatype refinement, guarded mapped-address-translation name/parameter, attempts scoped r2 context at `0x24F95C` only with verified OPD, decompiles upstream routine, scans the unowned send-dispatch region read-only. **Not yet run in user's Ghidra**. No PCSX2 behavior changed.

Original Sony official HDD Utility v1.10 `SCUS-97395` issue remains intact: native KELF data `0x1A0` arrives via SCMD `0x90 -> 26x 0x8D -> 0x8F`, but emulated MagicGate code rejects ciphertext BIT instead of real decryption. Distinguish external PS3 UART class 0x90 from native MechaCon SCMD 0x90. Additional Ghidra 12.1.3 project evidence and applied types/names are in previous v7/v8 Ghidra handoffs.
