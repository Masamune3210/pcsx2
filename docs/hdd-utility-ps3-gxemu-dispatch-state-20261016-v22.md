# PS2 HDD Utility MagicGate: PS3 GXEMU state dispatcher v22 (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Prior [v21](hdd-utility-ps3-gxemu-dispatcher-20261016-v21.md). DNAS parked; no PCSX2 source edits yet.

User's `RecoverPs3GxemuVuartDispatch_20261016_v1.java` verified the exact PPC64 descriptor at `0x00670530` (entry `0x00250D70`, TOC `0x00678548`, environment 0), and promoted **`0x00250D70 -> DispatchVirtualUartPacketTransfers`**. Body `0x250D70..0x25109B`, 203 decoded PPC instructions, 22 branches, 0 returns, 0 nested prologues, 1 descriptor foreign reference, no foreign internal calls, section boundary `SECTION11`/ `SECTION12` immediately after. It is an infinite-loop worker. It has an actual `r2` write at `0x250EF4`; function-scoped r2 context was **correctly withheld**.

The dispatch state comes through pointer TOC slot `0x00677788 = r2-0xDC0`, status through pointer slot `0x00677770 = r2-0xDD8`, runtime pointed-to addresses not known. Decompiled state mapping:
- 0: idle; 1: calls `SendVirtualUartVariablePacketAlternate@0x901FC` with UART envelope subtype +0x17=0x09 at site `0x251044`.
- 2: processes response flags at +0x38, a BE32 field at +0x20, conditional callback, status handling involving 3 and 11.
- 3: calls `SendVirtualUartVariablePacket@0x8FD48`, envelope subtype +0x17=0x0A at `0x25106C`.
- 4: handles another response/status case, checks BE64 field +0x40 and if above 65535 substitutes 0x40; status writes 8 or 11.
- At end resets state and response pointer and waits for next iteration.

No MechaCon crypto or correspondence between UART subtype and actual native S-command has yet been verified.

**Next script** committed to Ghidra `testing`:
https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/ghidra_scripts/AnnotatePs3GxemuVuartDispatchStates_20261016_v1.java

Pending user execution. Creates the directly applicable enum `GxemuVuart10DispatchState` (4 bytes, known 0..4), guards enum-pointer/status-pointer declaration at TOC slots `0x677788/0x677770`, types the verified descriptor at `0x670530` with `Ppc64FunctionDescriptor24` (24 bytes) and documents dispatch states. Performs read-only scan for r2-relative slot access and callback handling, without inventing crypto types or overriding r2.

Review script's scan results to find who sets dispatch state and where UART responses enter the worker. Existing named functions, data structures and symbols in previous handoffs remain already applied. No need to re-fetch GitHub repo metadata or repeat earlier analyses unless changed.
