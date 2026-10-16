# PS2 HDD Utility MagicGate: PS3 GXEMU follow-up v20 (2026-10-16)

Active branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Prior [v19](hdd-utility-ps3-callback-dispatch-20261016-v19.md). No PCSX2 emulator code changes made. DNAS parked.

User's Ghidra read-only `AuditPs3GxemuVuart10CallChain_20261016_v1.java` revealed:

1. PPC64 callback inside receiver `0x8FC68`: `0x8FD14` saves r2, `0x8FD18` loads callback environment r11, `0x8FD1C` loads callback TOC r2, `0x8FD20` calls `bctrl`, `0x8FD24` restores original r2. Previous r2 context skip was correct.
2. `FUN_0024F718` compares masked alias input to one global base and conditionally maps into another; no cryptography. Valid partial Ghidra declaration: `ulong TranslateMappedBufferAddress(ulong candidateAddress)`.
3. `FUN_0024F95C` calls the `0xFACE` frame sender, then manipulates optional 16-byte regions and 8-byte reversed components, XORing selected bytes with timebase LSB. Under investigation; not proven MagicGate KELF crypto.
4. `0x251044` and `0x25106C` directly invoke two different UART10 variable-framing senders inside an unowned dispatch region. Do not fabricate boundaries.

**Next action:** Run `RefinePs3GxemuCallbackAndDispatcher_20261016_v2.java` from `Masamune3210/ghidra-emotionengine-reloaded` `testing`, linked here:
https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/RefinePs3GxemuCallbackAndDispatcher_20261016_v2.java

It verifies r2 callback semantics, refines existing transfer state +0 member to pointer-to-PPC64 descriptor, conditionally renames mapped-address translation helper, verifies scoped TOC for upstream 0x24F95C, and surveys missing dispatch function boundaries (read-only). Current v2 supersedes and deletes v1 due fixed Ghidra global-name detection. **Not yet executed or compile-verified by user**.

Underlying PCSX2 issue unchanged: HDD Utility retail native KELF header length 0x1A0 transported intact via S-command 0x90 -> 26×0x8D -> 0x8F, but emulated MechaCon stub interprets encrypted header BIT data without correct cryptographic transform and returns 0x80. No bypasses; continue Sony protocol reconstruction. PS3 outer UART class 0x90 is *not* established to be MechaCon SCMD 0x90.
