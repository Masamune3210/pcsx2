# PS3 PS2 emulator ELF Ghidra analysis: correct language/ABI before writing new processor spec (2026-10-16)

**Context:** continuing MagicGate v8 from [hdd-utility-spc970-firmware-new-path-20261016-v8.md](hdd-utility-spc970-firmware-new-path-20261016-v8.md). User used RPCS3 to decrypt `ps2_emu.self`, `ps2_gxemu.self`, `ps2_netemu.self` from official PS3 firmware 4.93 and reports many Ghidra errors with **`PowerPC:BE:64:A2ALT`**. We have NOT yet inspected actual decrypted ELF, error log, or Ghidra `.gzf` from these PS3 binaries. Do not claim root cause established.

## Specific correction

Upstream Ghidra `ppc.ldefs` distinguishes:
- `PowerPC:BE:64:A2ALT`: 64-bit big-endian with 64-bit addressing, `ppc_64_be.cspec`.
- **`PowerPC:BE:64:A2ALT-32addr`**: identical 64-bit Power ISA/AltiVec instruction decoder, but **32-bit addresses** via `<truncate_space space="ram" size="4"/>` and **`ppc_64_32.cspec`**. `PowerISA-Altivec-64-32addr` is the description shown in Ghidra UI.

The existing PS3-specific project `clienthax/Ps3GhidraScripts` specifically recommends big-endian `PowerISA-Altivec-64-32addr`. It says run `AnalyzePs3Binary.java` **before** Ghidra autoanalysis, to recover import/export NIDs, process info, OPDs and r2 TOC; then autoanalysis; then `DefinePs3Syscalls.java`. Its README reports `ppc_64_32.cspec` may need `<register name="r2"/>` added to `<unaffected>` for correct cross-call preservation, and warns some Cell vector instructions (`lvlx`, etc.) unsupported. It does NOT implement relocation handling, so take special care with `ET_DYN` modules.

References:
- https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/Processors/PowerPC/data/languages/ppc.ldefs
- https://github.com/clienthax/Ps3GhidraScripts
- https://github.com/NationalSecurityAgency/ghidra/issues/570 (historical 64-32addr ELF TOC analysis limitations)
- https://github.com/NationalSecurityAgency/ghidra/issues/9886 (Oct 14 2026, Ghidra 12.1.3/12.1.4 repro: unexpected full-width `r1` uses, e.g. `tw 31,r1,r1`, can prevent decompiler stack-local recovery in 64-32addr mode)
- https://github.com/NationalSecurityAgency/ghidra/issues/9887 (AltiVec record-form compare CR6 modeling)
- https://github.com/sp00nznet/ps3recomp/blob/master/docs/PPU_RECOMP.md (PS3 .opd entries are **8 bytes `{uint32 func, uint32 toc}`**, not 16/24-byte ordinary 64-bit PPC function descriptors.)

## Priority test and next action

1. In a **fresh Ghidra import**, preserve the existing import unchanged for comparison. Import the decrypted PS3 ELF under `PowerPC:BE:64:A2ALT-32addr`, compiler `default`, big-endian. Turn off automatic analysis in the import wizard when needed so PS3 import-metadata scripts can run before analysis.
2. Verify ELF`e_machine`, effective addresses, contents and code/data ranges; confirm .opd entries (8-byte code-address+toc-address pairs, not raw executable instructions) and per-function TOC/r2 contexts. Avoid forcing `r2` globally in multi-TOC modules.
3. For `ET_DYN` modules, verify relocations rather than assuming all imported references are resolved.
4. Only after this baseline, capture **exact Ghidra warnings/errors and a few affected byte-exact instruction addresses**, plus imported ELF or `.gzf`, and classify failures by instruction decode (candidate Sleigh), call ABI/r2/stack (candidate cspec), ELF/OPD/relocation/import (loader/analyzer), or decompiler (core).
5. If failures remain, create narrow PS3/PPE-specific processor/compiler specification fixes **in a dedicated branch**, preserving generic PowerPC and Xbox 360 improvements. No actual Ghidra processor source edited in this step; no unsupported signature/metadata invented.

**Ghidra datatype candidate for actual PS3 .opd data when verified in a specific file:**
```
Structure: Ps3PpuOpdEntry
Size: 0x08
0x00 uint32_t codeAddress
0x04 uint32_t tocAddress
```
Only apply to verified OPD table ranges; don't blindly type general binary data.

**Current project focus remains native MagicGate, HDD Utility SCUS-97395, cryptographic `0x8F` header processing. DNAS parked.**