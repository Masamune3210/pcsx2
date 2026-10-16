# PS3 ps2_gxemu LV1 / virtual UART analysis handoff v11 (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v10](hdd-utility-ps3-gxemu-64bit-database-audit-20261016-v10.md). DNAS remains parked.

## Verified data from user's Ghidra run

Program `ps2_gxemu.elf`, Language `PowerPC:BE:64:A2ALT`, Script `InspectAnnotatePs3GxemuPpu64_20261016_v2.java` completed, **APPLY=true**:
- `FUNCTIONS=2997` from Ghidra FunctionManager (not string-count estimate).
- `INSTRUCTIONS=463567` from the listing.
- `LV1_SC1_SITES=313` exact big-endian PPC opcode `0x44000022` (`sc 1`), annotated with comments and bookmarks.
- ELF memory map all low addresses: `BLOCKS_ABOVE_4G=0`. Highest RAM block `SECTION94 0x00677880..0x02BBAB27` was uninitialized read-write BSS. This confirms the **image mappings** fit within 32 bits, but neither resolves guest runtime effective addresses nor proves 32addr vs 64addr is universally correct.
- `ENTRY 00000094 instruction=b 0x00000094`, verified `DIRECT_SELF_BRANCHES=1`. The user's Ghidra infinite loop decompilation was accurate **at that entry stub**, without demonstrating the emulator's startup code is unreachable. No entry rename claimed.
- Actual software handlers and cryptography remain UNIDENTIFIED; `sc 1` locations alone do not identify MechaCon paths.

## New source-grounded target

PS3 Developer Wiki documents `ps2_gxemu` MagicGate data passing through **virtual UART port 10**. General PS3 LV1 hypercall specifications use **r11** for hypercall ID and **r3** for virtual UART port:
- 162 (`0xA2`): `lv1_read_virtual_uart` `(r3 port, r4 buffer, r5 length)`
- 163 (`0xA3`): `lv1_write_virtual_uart`
- 164 (`0xA4`): `lv1_set_virtual_uart_param`
- 165 (`0xA5`): `lv1_get_virtual_uart_param`
- 166 (`0xA6`): `lv1_configure_virtual_uart_irq`; **r3 is NOT a port number** here, it is an LPAR IRQ-bitmap address.

References: https://www.psdevwiki.com/ps3/PS2_Emulation ; https://www.psdevwiki.com/ps3/HV_Syscalls ; https://www.psdevwiki.com/ps3/HV_Syscall_Reference

Created and pushed directly to `ghidra-emotionengine-reloaded` **testing**:
`https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/TracePs3GxemuVuartHcalls_20261016_v2.java`
- Safely detects `sc 1` and resolves a **local straight-line r11 immediate** by reading raw PPC `addi/addis r11,r0,imm` and optional `ori r11,r11,imm`, refusing branch/call/syscall crossings and unknown r11 writes. Does not guess arbitrary caller-provided values or follow cross-block dataflow.
- Prints containing function and r3 port candidates. Distinguishes the IRQ helper `166` from port-bearing calls.
- For established 162–166 direct selectors, adds bookmark and EOL comment only, with no signatures or byte changes.
- Prints histogram of recognized hcall selectors and unresolved count. Full trace output still needed; new script is **NOT YET EXECUTED/COMPILED IN USER'S GHIDRA**.
- An earlier v1 draft was superseded and removed from the repo. Run **v2 only**.

## Immediate next action

User should `git pull origin testing` in Ghidra extension repo, then run `TracePs3GxemuVuartHcalls_20261016_v2.java` in 64-bit `ps2_gxemu` database and share console output. Focus on sites with hcall `162` / `163` and local port `10`; trace wrapper callers and associated packet buffers; distinguish hypervisor transport from the emulated MechaCon authentication/decryption. Do not yet infer or patch native KELF crypto.

No PCSX2 code change and no firmware binaries committed.
