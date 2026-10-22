# PSBBN v38: bounded source-to-eviction witness for TLB-driven cache churn

Repository: `Masamune3210/pcsx2`
Active branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Read first: `notes/PSBBN_EE_JIT_EVICTION_TRACE_FINDINGS_20261021_v37.md`; current fork policy in v33.
Status: **source implemented and committed; no Windows/MSVC build, GoogleTest execution, Ghidra crosswalk, or PCSX2 runtime validation yet.**

## Why this was built

The user's full `emulog(4).zip` per-block trace (v37) found 188,929 ASID/TLB removal retirements in generation 2 out of 200,903 total, with the same physical code blocks compiled and removed hundreds of times. A consistently hot observed virtual/physical pair is **guest `0x0FB6D000` → physical `0x01ED8000`**. That trace does not establish whether instruction bytes, guest code mappings, source PFNs, D-bit permissions, or generated constant-data assumptions were unchanged. **Do not permit dormant code reuse based solely on a matching physical block address.**

## Implemented

1. `pcsx2/x86/EERecSourceWitness.h` (new):
   - Fixed-capacity source-page snapshot (8 guest 4 KiB pages); each page records *actual* virtual address, translated physical address, instruction-word count, 64-bit FNV-1a-style fingerprint, and compile-time writable flag.
   - Pure `EERecWitnessAppendWord` verifies each sequential physical address *within* its guest page. On crossing a virtual page, starts a new independently translated physical page, without extrapolating a contiguous PFN.
   - Pure verdict classifier: `incomplete`, `physical-bytes-changed`, `same-bytes-virtual-unmapped`, `same-bytes-mapping-changed`, `same-bytes-mapping-same`. **Identical hashes are diagnostic evidence, not code-reuse authority.**
2. `pcsx2/x86/ix86-32/iR5900.cpp`:
   - New independent setting `[EmuCore] EEJitSourceWitness=true`, enabled **only in Extended MMU**, default **off**. Session cap `EEJitSourceWitnessLimit=2048`, clamped 1..10000. Both are latched at first JIT startup and persist across internal cache arena resets.
   - Watches blocks beginning in physical `0x01ED8000..0x01ED8fff` or starting on guest `0x0FB6D000..0x0FB6Dfff`. Other physical code remains unobserved intentionally. Max **512 concurrently tracked** blocks, **4096 guest words** per recorded block, **8 source pages** and **16 constant-data specializations** per block. Exceeding a bound produces an incomplete/unresolved record, never a proof of identity.
   - At compile-finalization (after native emission) records guest compilation PC, physical start, EntryHi/ASID, JIT dispatch mode, `s_recBlockChangesTranslation`, source-page hashes and physical backing/permission, and constant-data accesses.
   - At real block removal or arena retirement, looks up the **specific host code pointer** via a preallocated array, compares original physical RAM bytes **on every recorded page**, then compares guest mappings in `BuildEEActiveTLBPageMap` under the actual current ASID (not the transient JIT LUT that still points at the old page during Remove callback).
   - `EE-JIT-WITNESS-PAGE`: event ID, original and current physical page and hash, 4 KiB segment word count, current/old writable status, and whether virtual source remains mapped.
   - `EE-JIT-WITNESS-DATA`: event ID and compile-time direct host pointer assumption (or handler category) versus the current vTLB fastmem mapping observed **at callback time**.
   - `EE-JIT-WITNESS`: event ID tying those records together; physical block address/host pointer, source compile PC, compile/current ASIDs, mapping caller and action, source verdict, source completeness, whether mapping comparison was meaningful, collected direct-data assumptions and overflow, changed translation context, guest cycle.
   - `EE-JIT-WITNESS-SUMMARY` at resets/shutdown: session-wide matched/printed/dropped event counts, pending live records and exact watched VA/PA. The witness cap persists across arena resets.
   - **No allocator work in the block retirement ledger**, including host write-protection fault callbacks (fixed 512 slots). To avoid dynamic TLB reconstruction and logging within a protected host memory fault, those callbacks only drop their witness records/count the drop. Non-ASID/TLB eviction causes do **not** build a TLB projection and emit `mappingKnown=0` with an `incomplete` verdict. Only ASID/TLB projection removal has reliable current logical mapping comparison.
   - No code retention, invalidation skipping, link deletion, permission changes or JIT cache size changes.
3. `pcsx2/x86/iR5900.h` and three `recVTLB.cpp` const-access emission sites:
   - `recRecordConstantDataLookup(u32 guestAddress, bool store, bool direct, uptr assumedHostPtr=0)` captures **actual assumed direct host pointers** for constant read/write/JIT quad-read; still preserves existing aggregate counts gated by COP0 trace. For managed guest virtual data, it reports assumptions, not full dependence provenance. Direct pointers can embed MMU-dependent assumptions even when instruction bytes match.
4. `pcsx2-qt/Settings/EmulationSettingsWidget.ui/.cpp`:
   - Under Settings → Emulation → System Settings, new **Compare PSBBN JIT Source at Compilation and Eviction (Very Slow)** checkbox, with **Maximum Source Witness Comparisons** spinbox (default 2048; 1..10000).
   - Both use existing global/per-game setting binder. Full VM shutdown and fresh boot required.
5. `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`:
   - Tests separate hash-vs-translation classifications and conservative unknown verdicts.
   - Tests that adjacent guest pages may map to noncontiguous physical pages (`0x01ED8FFC` → `0x00028000`) without falsely claiming source contiguity, and that unexpected PFN or write-permission changes within one guest page fail the snapshot.
   - Tests are added to the **existing** ctest source; no new build registration expected. Still **unrun**.

## Diagnostic procedure

1. Pull and build the active branch. Full boot required to latch new settings. Verify `EE execution profile: ExtendedMMU` and `EE-JIT-WITNESS-CONFIG enabled=1 watchPA=01ed8000 watchVA=0fb6d000`.
2. In Emulation → System Settings, enable **Compare PSBBN JIT Source at Compilation and Eviction (Very Slow)**. Use **512–2048** maximum comparisons initially. The earlier **Log Each EE JIT Block Eviction** option is a **separate** much noisier setting: turn it **off** for this run unless needed; previous trace had limit 0/unlimited and produced 241k records.
3. Boot the cloned HDD for a short reproduction of several ASID switches, enough to accumulate witness events. Upload the raw emulog. **Do not use FPS measured with the witness enabled as a benchmark.**
4. Pair the three `EE-JIT-WITNESS*` log families by `event=` and compare `verdict`, original/current PFNs, writable bits, compile/current ASIDs and pointers, mapped status, and caller `mappingAction`.
5. A `same-bytes-mapping-same` result **still does not make code safe to reuse**: no proof of all dynamic data aliases, unresolved const-data references, outgoing direct-link ownership, active host return addresses, branch delay contexts or complete source provenance. Likewise `sourceComplete=0` or `dataOverflow=1` means the witness cannot support reuse.

## Known caveats / future work

- Physical source identity is fingerprinted *after emission*, not a guaranteed atomic code fetch-time snapshot. If source changes during emission, snapshot can be incomplete or misleading; this is a discriminator, not a correctness mechanism.
- The 64-bit noncryptographic fingerprint can collide. A matching hash is a lead, not an equality proof.
- The source collector handles 8 virtual pages and 4096 words; any overflow is marked incomplete. It records page start/pointer and hashes rather than dumping full source bytes. Review branch-boundary helpers when interpreting spans across noncontiguous PFNs.
- Direct host-data pointers collected at compile are compared with current vTLB **during block removal**, which can occur before the new projection's full host data-map installation. Pointer equality or difference alone is therefore not definitive about the mapping after the switch.
- Only three known constant-data host pointer specialization families are collected. Other hard-coded guest data dependencies, active JIT return paths and generated direct links are NOT inventoried by this witness.
- Host write-protection callbacks deliberately do not print source witness events (unsafe fault-handler context). The original per-block `EE-JIT-EVICT` trace remains independently available, subject to its own signal-logging caveats.
- Compiler/source and per-block event bookkeeping use bounded fixed arrays, but calculating a full TLB map and scanning watched source words is extremely expensive when enabled. Only test Extended on an expendable HDD clone.
- If the same `event` reports `same-bytes-virtual-unmapped`, do not confuse the architectural TLB state at eviction time with the old JIT dispatch alias; this was deliberately corrected.
- **Next target:** analyze the first recorded unchanged-physical-code cases and find which generated block types embed no unstable virtual-data assumptions; separately instrument source-block lifetime and incoming/outgoing link patch ownership before attempting guarded re-use or reclamation.

No source-level claim of speed improvement, Linux boot resolution or build success is made.
