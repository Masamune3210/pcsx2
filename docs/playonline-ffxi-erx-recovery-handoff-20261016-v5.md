# FFXI ERX wrapper recovery and MagicGate/DNAS analysis handoff (2026-10-16 v5)

**Branch:** `note-working/magicgate-scmd-boundary-trace-20261016`. Extends [v4 access-flag trace](playonline-dnas-access-flag-lba-trace-20261016-v4.md).

## Discovery: these ERX binaries are encrypted IOP modules, not EE modules

The user's uploaded FFXI (`SCUS-97266`) binaries `SCE000.ERX`, `SCE001.ERX`, `SCE002.ERX`, `HID000.ERX`, `LDR000.ERX`, `NDI001.ERX`, `NDI002.ERX`, `TCP000.ERX` are high-entropy, no visible ELF signature. Their encryption is fully recoverable using code in `SQIOPMEM.IRX` (PlayOnline_Memory_Manager/SQUARE).

**Direct source disassembly** of `SQIOPMEM.IRX`:
- `.text+0x94`: linear congruential generator, 32-bit state at section-linked `.bss+0x100` (virtual layout 0x20C0). `state = state * 0x41C64E6B + 0x3021`; result `(state >> 16) & 0x7FFF`.
- `.text+0xC4`: in-place decoder `void DecodeRollingXorBuffer(byte * buffer, int length, uint seed)` (function signature for Ghidra; return register ignored). Traverse `i=n-1..1`, XOR current with preceding encrypted byte; then traverse `i=0..n-1` and XOR low byte of PRNG result.
- `.text+0x44C`: loader routine whose instruction at `.text+0x69C` calls `+0xC4` with `$a0=buffer, $a1=fileLength, $a2=fileLength`.
- Decrypted wrappers have bytes 0x04..0x0F containing null-terminated original IRX name; actual ELF starts at 0x10. Do not falsely interpret first four bytes as exact file size.
- Every one of eight recovered executables validates as little-endian MIPS ELF32, processor-specific `0xFF80` (PS2 IOP relocatable) with valid section headers. No Ghidra runtime import test yet.

| ERX filename | Original IRX filename | .iopmod embedded identity / version |
|---|---|---|
| SCE000.ERX | sio2man.irx | sio2man 2.04 |
| SCE001.ERX | libsd.irx | Sound_Device_Library 3.01 |
| SCE002.ERX | dev9.irx | dev9 2.05 |
| TCP000.ERX | avetcp.irx | AVE_TCP_31_Resident_Lib 1.01 |
| HID000.ERX | squsb.irx | USB_Device_Driver_Interface/SQUARE 1.01 |
| LDR000.ERX | sqloader.irx | PlayOnline_IRX_Loader/SQUARE 1.01 |
| NDI001.ERX | ndglue.irx | SQUARE_NETDEV_Lib 1.01: AN986 USB Ethernet |
| NDI002.ERX | ndglue.irx | SQUARE_NETDEV_Lib 1.01: DEV9 SMAP Ethernet |

Two different `ndglue.irx` implementations MUST be preserved independently. The other three supplied unencrypted IRX modules are `IOPSOUND.IRX` (IopSoundDriver/SQUARE v1.44), `SQIOPMEM.IRX` (PlayOnline_Memory_Manager/SQUARE v1.01), `YMATPREV.IRX` (ymatprev_driver v1.01).

The verified Python decoder is [`tools/recover-ffxi-erx-20261016-v1.py`](../tools/recover-ffxi-erx-20261016-v1.py); invocation:
```powershell
python .\tools\recover-ffxi-erx-20261016-v1.py "G:\Users\Johnny\Downloads\Source Files\PS2 Games\Final Fantasy XI - Online (USA, Canada)\MODULES" "G:\Users\Johnny\Downloads\FFXI_Recovered_IRX_20261016_v1"
```
No original Sony HDD is available; do not suggest dumping or sourcing one. Recovered ELF modules are proprietary user-supplied binary content and **must not be committed** to public git. User received a local ZIP of the 11 IRX files plus script, CSV, and Ghidra annotations.

## Useful INSTALL.ELF symbols

`INSTALL.ELF` is EE MIPS (entry `0x00100008`) with **25,611** symbol table records, not stripped. Addresses from ELF's original symbols:
- `0x00169B70`: `XiHardInitialize__FiPPc`, initialization/load modules;
- `0x0026EC48`: `crand`, same PRNG family;
- `0x0026EC70`: `IrxDecrypt`, called from `sqPolconCheckNetDevice` at `0x0026ED98`, NOT proven to decrypt this game's disk ERX files;
- `0x002AF808`: `sqSysMemLoadModuleErxBase`, dispatches over SIF RPC;
- `0x002AF9D8`: `sqSysMemTransferLoadModuleErx`;
- `0x002AFA38`: `sqSysMemLoadModuleErx`;
- `0x002AFA68`: `sqSysMemPushModuleErx`;
- `0x00593CD4`: `LastDnasError` existing symbol, unknown datatype/use pending decompilation. Preserve unresolved type.

Other encrypted inputs `FFXI_POL.ENC` and `POLKEY.DAT` do not decode with this ERX algorithm; `HDD.SYS` references `/image/ffxi/prog/ps2/ffxi_pol.pex` and `/polkey.dat`.

## Relation to research goal

None of these decoded modules **declares** itself BBSYS101, libid2 or DNAS. This solves the misleading ERX/EE classification and makes FFXI network and loader modules analyzable, but does not itself locate the PlayOnline DNAS -404 return point. Failing installer test ran PlayOnline (`SCUS-97269`), NOT FFXI (`SCUS-97266`). The next useful action is to inspect the failing disc's executable and modules for actual DNAS identity collection/RPC and return checks; independently, Ghidra-import `SQIOPMEM.IRX` and label the three routines above.

## Verification

Reversed directly from assembly, all 8 input ERX decode deterministically and validate as IOP ELF with `file(1)` and parsed ELF section bounds. No PCSX2 build changes and no Ghidra import/runtime test in this pass. Prior `[DNASLBA]` PCSX2 instrument remains pending runtime test.
