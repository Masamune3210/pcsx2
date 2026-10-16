# PS2 HDD Utility MagicGate: GXEMU dispatcher next step v21 (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous `docs/hdd-utility-ps3-callback-dispatch-20261016-v20.md`. No PCSX2 source change; DNAS parked.

## Ghidra metadata now applied (user verified)

`RefinePs3GxemuCallbackAndDispatcher_20261016_v2.java` completed in decrypted PS3 `ps2_gxemu.elf` under Ghidra 12.1.3 PPC64 full-address model:
- `GxemuVuart10TransferState` field +0x00 refined to pointer to existing `Ppc64FunctionDescriptor24`, grounded in code sequence save r2; load env r11; load callback TOC r2; bctrl; restore r2.
- `0x0024F718` named `TranslateMappedBufferAddress`, valid partial Ghidra signature `ulong TranslateMappedBufferAddress(ulong candidateAddress)`.
- Scoped TOC `r2=0x00678548` applied to `FUN_0024F95C` using verified function descriptor. It sends 0xFACE/0x70 UART10 frame, performs optional 16-byte-block operations and timebase-byte XOR after `sc 0`. Cryptographic interpretation unproven.

## Candidate unowned dispatcher

- `0x00250D70`: PPC64 stack-frame prologue, data xref at `0x00670530` (likely OPD), *not yet verified triple*.
- `0x00251044`: call to `SendVirtualUartVariablePacketAlternate@0x901FC`.
- `0x0025106C`: call to `SendVirtualUartVariablePacket@0x8FD48`.
- `0x0025109C`: next prologue, begins separate ELF SECTION12; candidate function likely ends at `0x0025109B`, the SECTION11 final byte.
- No `blr` within candidate range seen; possible continuous state-machine dispatcher rather than ordinary returning function.
- Do not invent unverified subfunctions at those calls.

## Next runnable Ghidra Java

https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/RecoverPs3GxemuVuartDispatch_20261016_v1.java

Guarded script validates full candidate descriptor, prologue and section ends, exact sender calls, full disassembly, foreign calls and no code overlaps. Only then promotes the single function at `0x00250D70`, with generalized transport name and scoped r2 when unconflicted. Decompiles resulting dispatcher. Not yet executed/compiled by user. On failure no data changes. This establishes packet selection without claiming Sony native MagicGate S-command details.

Core PCSX2 issue unchanged: real HDD Utility sends a 0x1A0 encrypted KELF header through 0x90 / 26*0x8D / 0x8F; PCSX2 stubs fail at BIT parse. Our goal is actual MechaCon transformation, not bypass/fake success.
