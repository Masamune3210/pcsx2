# PS3 ps2_gxemu Ghidra addressing correction v9 (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v8](hdd-utility-spc970-firmware-new-path-20261016-v8.md). No change to PCSX2 source; DNAS remains parked.

## Correction: 32-bit-address ABI is not automatically valid for PS3 PS2 guest-OS emulators

The previous recommendation to import `ps2_gxemu.elf` using `PowerPC:BE:64:A2ALT-32addr` was extrapolated from **regular PS3 GameOS PRX/ELF tooling** and may be incorrect. The PS3 PS2 emulator SELF is **not a GameOS application**; per PS3 Developer Wiki it is a separate guest OS / LPAR under LV1 and replaces the running LV2 GameOS environment. It uses LV1 calls instead of standard GameOS LV2 syscalls.

Public source: https://www.psdevwiki.com/ps3/PS2_Emulation
Ghidra definition: https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.3_build/Ghidra/Processors/PowerPC/data/languages/ppc.ldefs
GameOS-specific tool: https://github.com/clienthax/Ps3GhidraScripts (explicit 32addr setup, but developed around GameOS ELF/PRX; not evidence it applies to standalone PS2 guest OS).

The user's supplied archive **`ps2_gxemu.elf.gzf`** was inspected *without Ghidra* by unpacking the Java serialization header plus embedded ZIP/deflate. Its actual stored program metadata reports:
- Language Name & ID: `PowerPC:BE:64:A2ALT-32addr`
- Compiler Spec ID: `default`
- Address Size: `32`
- Original Image Base: `0x0`
- Executable Format: `Executable and Linking Format (ELF)`
- Analyzed: `true`; Ghidra `12.1.3`
- This shows that Ghidra **has already imported and analyzed under 32-bit address truncation**. It does **not** prove that's correct for the ELF.

There is a published **64-bit-effective-address** usage example for PS3 PS2 emulators (`0xFFFFFFFFFFFF8138` with `stvx` in netemu), but this specific documented bug may be netemu-only and does not prove `ps2_gxemu` has mapped ELF image segments above 4 GiB. Source same PS3 Dev Wiki `PS2 Emulation`, software emulator bug list.

**Recommended validation**: Preserve the current Ghidra database as a separate 32addr reference; separately import the same decrypted original `ps2_gxemu.elf` into a **64-bit-address** `PowerPC:BE:64:A2ALT:default` database. Compare ELF e_entry, all program-header p_vaddr/p_memsz ranges, high address references, .opd descriptors, and r2 TOC initialization *before* making final language/ABI decision. Do not assume an `entry()` infinite loop is wrong; it could be a real stub/spin/secondary entry. The user reports decompilation `void entry(void) { do { } while (true); }`; inspect actual bytes/flow before changing it.

**No established function renames, data structures, signatures or data redeclarations** from this addressing-only check. Avoid blanket processor patch and preserve the previous archive for comparison. Revisit previous 32addr-specific guidance only after ELF header and PPU address semantics are verified.
