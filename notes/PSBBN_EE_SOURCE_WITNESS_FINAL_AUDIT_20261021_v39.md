# PSBBN v39: source witness implementation and safety audit

Active branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
Read v37 for actual eviction evidence, v38 for field-by-field witness design, and this v39 last for corrections and verification state. Do not treat the earlier documents as approval for code retention.

## Implementation in place

- New **Extended-only, opt-in** `EEJitSourceWitness` with session-wide cap `EEJitSourceWitnessLimit` (default 2048, 1..10000).
- Specifically watches compiled blocks starting from guest VA page `0x0FB6D000` and/or physical PA page `0x01ED8000`. Not a global probe. Startup console says `EE-JIT-WITNESS-CONFIG` if successfully latched.
- Compile-side record: original virtual PC, physical PC, exact per-guest-page translated PA (even if adjacent guest pages are physically noncontiguous), FNV64 instruction hash and word count, TLB write permission, compiled ASID/EntryHi, JIT mode, code-changes-translation flag; up to 16 baked constant read/write pointer assumptions from the **three actual recVTLB specialization sites**.
- Retirement-side comparison: original PA bytes reread from physical RAM, current **architectural** active-ASID TLB projection and writable state, current const-data vTLB pointer/handler snapshot. Important distinction: during TLB removal, the JIT LUT may still contain the old mapping, so it is *not* authoritative; the collector uses `BuildEEActiveTLBPageMap` for ASID/TLB invalidation requests. Event IDs tie `EE-JIT-WITNESS-PAGE`, `EE-JIT-WITNESS-DATA` and `EE-JIT-WITNESS` verdict lines together.
- Classification distinguishes physical byte changes, current virtual unmapping, changed backing/permissions, same backing/permissions, and incomplete evidence. Matching 64-bit hashes never authorizes retaining compiled code.
- One fixed-size 512-slot array for live witnesses, 8 source pages, 4096 instructions max per block, 16 data assumptions max. NO unbounded hash map, no heap allocation in the retirement ledger. Overflows are recorded/dropped rather than inferred. Session output cap remains in force across whole-JIT resets.
- **Signal/teardown safety correction:** block retirement triggered by `host-write-protection` or `provider-shutdown` does not read original RAM, reconstruct TLB state, allocate, or print detailed source evidence, because the host signal/SEH or teardown context may not allow those operations. The fixed witness record is retired and its drop counted. For other non-ASID/TLB causes, the architecture mapping is marked unresolved and verdict incomplete; no TLB map allocations are performed.
- No change to `EERecClearPhysicalBlocks`, standard cache eviction policy, block lifetimes, direct-link deletion or memory reclamation. Standard is untouched by the witness except cheap compile-time call argument preparation in the existing const-data trace helper.

## GUI and test

Settings → Emulation → System Settings:
1. `EE Execution Profile = Extended MMU / OS`.
2. `Compare PSBBN JIT Source at Compilation and Eviction (Very Slow)` = enabled.
3. `Maximum Source Witness Comparisons` = 512 initially, 2048 default. Fully **stop the emulated PS2** and cold boot before testing.
4. The old `Log Each EE JIT Block Eviction (Very Slow)` is a separate independent checkbox. Disable it or give it a small cap; the previous `limit=0` run produced 241,364 lines.

Added GoogleTests to existing `ee_rec_code_cache_tests_20261020.cpp` for: verdict truth table; incomplete evidence; independently mapped noncontiguous source pages; mismatched PFN or writable bit within one page; refusing the ninth guest page. Tests remain **unrun**.

## Risk / caveats

- Source is sampled at the end of compiler emission, not atomically at instruction fetch time. Every page hash has a finite (noncryptographic) collision probability.
- A record is created only for blocks **starting** on watched VA or PA page; a block beginning elsewhere but crossing the watched page can be missed.
- Current vTLB pointer during Remove is a snapshot, possibly before complete projection installation. Pointer equality is **not** safety proof. Only the known constant-address specialization families are captured. Other dynamic data and host JIT link/return dependencies remain unresolved.
- The witness can slow the guest substantially; do not compare FPS with prior runs.
- Source-level cross-reference and GitHub changed-file audit performed. `iR5900.cpp`, `iR5900.h`, `recVTLB.cpp`, Qt UI, new header, existing unit test and AGENTS tracked. No compile or PSBBN executable test has been performed in this cloud session. Await user MSVC build and fresh emulog for evidence.
- If compile breaks, prioritize fixing the exact diagnostics and source signatures instead of treating the witness as proven. Do not assume a source-reviewed change is a passing MSVC build.

## Next action after runtime log

Group events by `event=`. Focus first on `same-bytes-mapping-same` and `same-bytes-virtual-unmapped` for the sentinel VA/PFN; look for `dataChanged`, `dataOverflow`, `translationChanging`, changing ASID and source-page completeness. The next architecture question is **which code needs guest-translation guards, and which compiled block host/data dependencies preclude retention despite unchanged physical instructions**. Only then consider retained code under explicit generation/translation guards and proper direct-link/host-return lifetime management.
