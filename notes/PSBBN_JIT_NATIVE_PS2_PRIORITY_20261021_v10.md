# EE JIT architectural priority: native PS2 correctness (v10, 2026-10-21)

## Non-negotiable constraint

**PCSX2 is first and foremost a PlayStation 2 emulator, not a PlayStation 2 Linux emulator.** The BB Navigator guest is an MMU stress test and a valuable correctness witness, not grounds for globally trading away native game performance or compatibility. Future changes to its EE recompiler must preserve or improve normal retail PS2 software behavior, including BIOS/OSDSYS, regular KUSEG access, TLB-based games, JIT block-cache identity, code invalidation, and performance.

## Why the established 64 KiB recLUT may be intentional

Verified source structure: `pcsx2/x86/BaseblockEx.h::recLUT_SetPage` explicitly says “this value is in 64k pages” and uses `<<14` to index one `BASEBLOCK` per 4-byte guest instruction in a 64 KiB region. Original `recLUT[0x10000]` and `hwLUT[0x10000]` index a 4 GiB 32-bit EE virtual address space with `addr >> 16`. The original tables consume approximately 768 KiB (64K x 8-byte pointers + 64K x 4-byte physical offsets). This coarse granularity permits a small, simple, high-locality direct JIT dispatch table and static virtual-to-physical aliases at boot.

**Historical intent not independently verified**: source demonstrates benefits but does not establish why the original authors selected precisely 64 KiB. It is a **host JIT lookup granularity**, NOT a claim that the R5900 TLB's smallest guest page size is 64 KiB. Sony Linux routinely uses valid 4 KiB guest TLB pages.

## Current experimental change and compatibility risk

The current PSBBN investigation branch has experimental source commits `7cfa20c6`, `835e5010`, `93c98945` moving the JIT instruction-dispatch alias mapping to 4 KiB, requiring roughly 12 MiB more static tables. This is **source only and not yet Windows/MSVC-validated**. The extra memory footprint, changed dispatcher hot path and newly introduced KUSEG unmap semantics could break or slow ordinary PS2 titles. Do NOT present the patch as production-ready or merge into `testing` solely because PSBBN proceeds farther.

## Acceptance / improvement plan

1. Use current branch as a diagnostic experiment: verify whether PSBBN can execute after the guest TLBL refill at `0FB643C0`. Record the actual compile and runtime results.
2. Establish regression baseline against the original 64 KiB JIT on BIOS/OSDSYS plus ordinary PS2 games, including TLB-using and self-modifying/JIT-heavy titles, savestates, and ASID changes. Check both correctness and performance.
3. Prefer keeping the original fast 64 KiB dispatch for fixed/native mappings, with **4 KiB precision limited to guest-TLB-managed virtual aliases** (e.g., a per-page override/overlay, or similarly selective approach). Evaluate fallback overhead instead of assuming a global 4 KiB table is justified.
4. Preserve guest architectural behavior and avoid hardcoded Linux, ReiserFS, PSBBN or virtual-address exceptions. The correct abstraction is general EE TLB semantics.
5. Treat normal retail game regressions or significant JIT performance regressions as blockers, even when PSBBN makes additional progress.
6. Keep the investigation branch and handoff updated. No new branches solely for each follow-up bug; fold into `testing` only after a measured, general fix is ready.

**No functional code change accompanies this document.** It records the native-PS2-first priority explicitly for later work.
