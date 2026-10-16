# HDD Utility MagicGate: GXEMU dispatch producer tracking v24 (2026-10-16)

Branch `note-working/magicgate-hdd-utility-header-validation-20261016`. Continue from `docs/hdd-utility-codex-native-mechacon-crosswalk-20261016-v23.md`. Parallel native Dragon hardware cipher task assigned to Codex on `Masamune3210/ps2mgdecomp` master: `TASK_NEXT_DragonMagicGateCipherContract_20261016_v1.md`. Keep cross-firmware work separate.

## PS3 Ghidra state, user verified

Ghidra 12.1.3 `ps2_gxemu.elf` PowerPC:BE:64:A2ALT: user successfully ran `AnnotatePs3GxemuVuartDispatchStates_20261016_v1.java`.
- Existing `DispatchVirtualUartPacketTransfers@0x250D70` verified with 24-byte BE64 OPD `0x670530`, TOC `0x678548`, environment 0, and exact UART send callsites `0x251044` state 1/subtype09 and `0x25106C` state 3/subtype0A.
- Created enum `GxemuVuart10DispatchState` size4 with 0 idle,1 send subtype09,2 process response,3 send subtype0A,4 process response limit.
- Applied TOC slots `0x00677788` enum pointer `gVuart10DispatchStatePointer` and `0x00677770` dword pointer `gVuart10DispatchStatusPointer`, and typed OPD `0x00670530` with `Ppc64FunctionDescriptor24`.
- Verified dispatcher callback sequence at `0x250EE8..0x250EFC`: saves r2; reads callback environment and TOC from function descriptor; indirect `bctrl`; restores r2. Correct not to set body-wide r2.
- Native PS3 state writers just discovered: `0x24F910 li r0,4; 0x24F914 ld r11,-0xDC0(r2); 0x24F91C stw r0,0(r11)` and `0x24FC50 li r0,2; 0x24FC54 ld r11,-0xDC0(r2); 0x24FC5C stw r0,0(r11)`. These writer instructions have **no containing Ghidra function** yet, and the calling triggers/state1/state3 producers are not known.

## Next Ghidra script

https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/TracePs3GxemuVuartDispatchProducers_20261016_v1.java

Committed to `testing`, not run/compiled by user yet. Guarded EOL comments for local proven writes of state 4/2; read-only scan of remaining state/status writers, neighboring candidate PPC64 OPD triples `0x6703C0..0x670578`, and missing function boundaries `0x24F8F0`/`0x24FC08`. Do not promote functions or impose TOC context until descriptor and code-body evidence warrants it.

The prior PCSX2 HDD Utility blocker remains: authentic 0x1A0 ciphertext header `SCmd90 (00 A0 01 00 00), 26x SCmd8D, SCmd8F` arrives intact but stub interprets ciphertext BIT as plaintext. Native Dragon 6.12 report separately reconstructed header auth->Kbit/Kc unwrap->BIT handling and eight-byte hardware cipher selectors, but hardware equivalence/keys are not yet proven. No bypasses or emulator code changes. DNAS parked.
