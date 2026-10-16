# HDD Utility MagicGate: PS3 GXEMU three function starts v15 (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`, previous [v14](hdd-utility-ps3-vuart10-frame-20261016-v14.md). DNAS parked.

## Completed in user's Ghidra

`ApplyAuditPs3GxemuVuart10Frames_20261016_v1.java` applied `/PS3/PPU/Ppc64FunctionDescriptor24` (0x18 bytes) to four existing LV1 VUART wrapper OPDs, added labels and `PpuGlobalTocBase@0x00678548`, and renamed `FUN_0008FC68 -> ReceiveVirtualUartChunk` and `FUN_000900FC -> SendVirtualUartFrame`.

The audit established probable missing contiguous PPU functions, including entry-point data references:
- `0x8FD48..0x8FF50`: 0x8FF24 port-10 UART write; descriptor pointer at 0x64F728.
- `0x8FF54..0x900F8`: 0x900D0 port-10 UART write of `0x40` bytes; descriptor pointer at 0x64F740.
- `0x901FC..0x90404`: 0x903D8 port-10 UART write; descriptor pointer at 0x64F770.

Each start has `stdu r1,-frame(r1)`, preceding and ending `blr`, and 4-byte alignment. Actual function-body promotion and the three new descriptor TOC fields are **still subject to validation**.

## Next executable action

A Ghidra 12.1.x Java script is now committed to `Masamune3210/ghidra-emotionengine-reloaded` branch `testing`:
https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/PromotePs3GxemuVuart10Senders_20261016_v1.java

It validates exact bytes, disassembly coverage, nonoverlap, 24-byte OPD `entry/r2 TOC/environment` triplets, then applies function bodies, general transport names, three data declarations and guarded function-scoped `r2` context. It decompiles the three newly recovered senders in one run. **NOT YET RUN or compiled in the user's Ghidra.**

Pending: user-run output; recover transport frame fields and callbacks toward MagicGate-specific dispatch. Do not conflate outer virtual UART packet `0xFACE` / type `0x5000` with PS2's MechaCon `0x8F` / `0x90` command numbers without dataflow proof. Native PCSX2 HDD Utility's `0x1A0` KELF header still fails `0x8F` BIT parsing; no crypto implementation or PCSX2 source changes performed.
