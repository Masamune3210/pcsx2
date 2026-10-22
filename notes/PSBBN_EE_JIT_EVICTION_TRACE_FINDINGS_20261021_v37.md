# PSBBN v37: actual per-block EE JIT eviction trace decoded (2026-10-21)

Repository: `Masamune3210/pcsx2`
Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Previous technical handoff: `notes/PSBBN_EE_JIT_BLOCK_EVICTION_TRACE_20261021_v36.md`.
User supplied log: uploaded `emulog(4).zip` containing `emulog.txt`, 82,886,724 bytes. This ZIP is **not** committed to Git.
Build in log: `PCSX2 v2.9.92-320-gf1b1823e0`; `EE execution profile: ExtendedMMU (setting=Extended, boot disc serial=20020207-164243)`; `EE-JIT-EVICT-CONFIG enabled=1 limit=0` (**unlimited** logging).

## High-confidence actual log observations

241,364 `EE-JIT-EVICT` entries, each for a block actually removed from BaseBlocks metadata (or retired in a whole-arena reset/shutdown). Zero suppressed records. The console is 82.9 MB uncompressed. This is a heavily instrumented diagnostic run, **not** a valid FPS benchmark.

Three normal `EE/iR5900 Recompiler Reset` notices: startup at ~9.03s (gen 0), fine/4KiB dispatch transition at ~23.79s (gen 1), 64 MiB arena exhaustion at ~41.26s (gen 2). Note last reset summary lines finish printing at 41.78s; trace logging itself spans ~0.5s. Shutdown at ~45.47s (gen 3). Only **one actual full arena-capacity exhaustion reset** appears in this shorter run. Do not conflate dispatch transition and startup with capacity exhaustion.

Generation 2, ~24.19 to 41.78s, records **200,903 block retirements**:

| Cause / action | Retired blocks |
|---|---:|
| ASID projection, old mapping **remove** | 107,848 |
| TLB projection, old mapping **remove** | 81,081 |
| Virtual clear | 1,986 |
| Host write protection | 154 |
| Manual reprotect | 92 |
| Remaining live blocks destroyed by arena reset | 9,742 |
| **Total** | **200,903** |

ASID + TLB removals account for **188,929 (94.04%)** of the generation's recorded retirements. The 9,742 end-of-generation reset retirements are different, not individual mapping-induced clears. The mapping events all say **`mappingAction=remove`**; no `mappingAction=install` caused a recorded retirement. Important caveat: an installation can still issue a physical clear request without removing metadata, especially after the prior removal already evicted the block.

A physical block at `0x01ED0CC8` was retired **382 times** over the generation (221 ASID, 159 TLB, 2 virtual clear) with **380 distinct compiled host pointers**. The same source PA is thus repeatedly generating distinct host code; this is not merely multiple event reports for a single retired metadata object. Other hot block starts `0x01ECFE60` retired ~368 times (368 distinct host pointers) and `0x01ED80B4` ~335 times (330 pointers). Median time between successive records at `0x01ED0CC8` is about **23.5 ms**. Host pointers are distinct, but this is not proof byte-for-byte equivalent code, source contents, branch context, or safe cache reuse.

Generation 2 has **24,942 distinct physical block start addresses** in 200,903 retirements (including terminal arena reset). Of those starts, 11,123 are seen more than once. Counter values for a physical start alone are not a complete code identity because source bytes, virtual PC, ASID, generated constant data assumptions, TLB permissions, and code generation mode can differ.

Concrete pathological page:
- **guest VPN `0x0FB6D000` -> physical page `0x01ED8000`** throughout all **15,451** mapping-removal eviction records observed for that VPN: **9,140 ASID-projection** and **6,311 TLB-projection**. This is one guest VPN observed repeatedly reaching the exact same physical PFN. We cannot assume all intermediate mappings or memory contents stay constant from the eviction trace alone.
- The **same physical 4 KiB page** has just **43 virtual-clear block retirements** recorded during gen 2, compared with 15,451 mapping-removal retirements; this ratio does **not** count every physical write nor establish no external writers.
- Another repeated mapping: `0x0FB75000 -> 0x01ED0000` has 9,682 mapping-removal retirements.
- Across the entire generation's ASID/TLB eviction records: 188,929 events, 333 unique affected VPNs, 461 unique (VPN, physical page) pairings; **128,814** events (68.18%) involve a VPN whose *only* recorded physical backing in these events is one particular page. Other VPNs genuinely map multiple different physical pages (e.g. 0x0040F000 appears with five), so do **not** disable all TLB/ASID invalidation blindly.

Kernel-side event hotspots for ASID mapping changes:
- `cpuPC=80025748`: 45,486 retirements;
- `cpuPC=80026974`: 24,769;
- `cpuPC=800255D8`: 15,865;
- `cpuPC=8002566C`: 15,223.
These are **CPU snapshots when the invalidation occurs**, not necessarily the exact instruction that edited a TLB slot. Almost every `tlb-projection` block-retirement record reports `cpuPC=8000004C`, consistent with the current refill path. All four listed PCs should be crosswalked with original guest vmlinux Ghidra symbols, not given invented names.

The generation-2 summary at ~41.2644s records `blockCompiles=200903`, `compiledBytes=66943718`, `cacheUsed=67043749` (67,108,864 capacity), `liveBlocks=9742`, `liveHostBytes=3078504`, `linkRecords=264393`, `repeatSourceFingerprints=108022`, `linkAttempts=11682502`, `linkWrites=11682502`. Barely ~3.08MB survives as live block metadata even as nearly 67MB has been emitted; that **does not** make the remainder safely reclaimable. Retired JIT host-code sites can remain on the call stack and remain subject to link and data cache invariants.

## What this establishes and does NOT establish

**Established**: `SyncActiveTLBMappings` invokes `ClearActiveTLBCodePage` for obsolete `EEActiveTLBPage` entries, which calls `recClearPhysical(mapping.physicalAddress,0x400,...,page<<12,"remove")`. Each such removal can retire all compiled metadata overlapping the physical page, even when the underlying physical code has not been shown to change. Source: `pcsx2/COP0.cpp` mapping Remove lambda, `pcsx2/x86/ix86-32/iR5900.cpp::recClearPhysical`, and `pcsx2/x86/EERecCodeCache.h::EERecClearPhysicalBlocks`.

**Established**: At least some physical code pages repeatedly map at the same guest VPN and are recompiled/retired hundreds of times in one cache generation; guest address-space and TLB transitions are the dominant eviction caller categories in this captured window.

**NOT established**: All these evictions are unnecessary, compiled code would remain correct after ASID/TLB change, any specific source byte remained unchanged, exact guest writer identity, direct-link return safety, or that allowing cached code to outlive translations is currently sound. Guest JIT code may bake absolute virtual PC, helper/constant data pointers, permission assumptions, and branch/link handling that depend on current mapping context.

**Unusual observation**: Each mapping-triggered recorded retirement uses `mappingAction=remove`; this tells us *when* actual metadata got destroyed, not whether installation's physical clear requests are redundant by themselves. Avoid erroneously interpreting no installation eviction entries as no installation clear work.

The diagnostic itself writes **241k** lines and logged ~29k `cpuTlbMiss` messages. Do not compare these host wall-clock durations or instant FPS against the earlier ordinary runs; logging changes the workload.

## High-information next discriminator (not yet implemented)

**Measure whether the *same physical code* is actually valid across a mapping-only transition** before attempting reuse:

1. In a bounded short, Extended-only diagnostic, select hot `blockPA` and VPN/PFN pairs above. Capture at block **compile** time: virtual compilation PC, physical start, all distinct 4KiB source physical pages (including cross-page/noncontiguous fetch), source bytes/fingerprint, compilation ASID, executable permissions, mode, and any constant-data host address dependencies.
2. At **eviction** time for the same source identity, re-evaluate the current guest virtual mapping and source bytes; classify `unchanged source bytes + same VPN/PFN`, `same bytes but changed mapping/ASID`, `changed code bytes`, `unmapped`, `changed write permission`, and `source provenance unresolved`. Do not attempt to execute retired compiled code in this diagnostic.
3. Distinguish **old map removal** from current/new context. TLB page `0x0FB6D000` physically `0x01ED8000` is the best sentinel; PC `0x80025748` is the leading ASID path. Compare with actual guest vmlinux symbols when available.
4. Track incoming/outgoing link owner-generation separately to establish why 11.68M link writes occur. Do not delete an incoming-link record just because its source block is no longer current; active helper returns and exception unwinding may still reference retired compiled code.
5. Once evidence isolates a safe subset, design generation/ASID/translation guard checks and data-dependency invalidation for retention. A blind `return` in `ClearActiveTLBCodePage` on mapping changes will likely break Linux/user games.

**Recommendation**: Treat repeated *mapping-triggered physical-code retirement* as the principal architecture hypothesis. Stop polishing same-sync duplicate clear counts. This is now stronger than the earlier v35 inference because individual concrete block lifetimes and host pointers were observed.

## Verification / branch state

This is log analysis and documentation only. No EE emulator source was changed and no tests/builds were executed in this pass. Keep the current Standard baseline and Extended experimental mode policy from v33. Continue on the same branch with a new handoff for any later source implementation.
