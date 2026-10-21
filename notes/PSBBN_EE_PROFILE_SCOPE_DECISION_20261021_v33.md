# Current direction: preserve this fork's Standard behavior; focus on PSBBN Extended MMU (v33)

Repository: `Masamune3210/pcsx2`
Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Previous technical checkpoint: `notes/PSBBN_EE_PROFILE_ISOLATION_SHARED_CACHE_REVIEW_20261021_v32.md`.

## User decision, superseding the previous objective

This is principally the user's personal PCSX2 fork. **Do not undertake a wholesale upstream-restoration campaign for Standard.** The user explicitly reconsidered the instruction to make Standard identical to upstream. Leave the current Standard implementation intact **as long as it is reasonably sane**. An ordinary game regression, once reproduced and understood, can justify reverting or refining the offending change later. Do not assume every deviation from upstream is a fix, nor assume upstream's deliberate compatibility compromises are errors. Conversely, do not reflexively revert existing work solely because it is absent upstream.

This overrides v31/v32's broad aim of isolating every post-upstream change immediately. Their historical comparisons and risk inventories remain useful evidence, **not a mandate to rewrite the baseline**.

## Current development objective

1. Keep existing Standard behavior as the fork's working default. Do not expand the profile split indiscriminately or claim Standard is upstream-equivalent.
2. Continue improving **Extended MMU** for PSBBN/Linux: guest TLB/ASID correctness, instruction and data translation dependencies, cache coherency, and recompiler thrash. The root performance issue remains severe invalidation/recompilation; source-only changes are not proof of an FPS win.
3. Share genuine, demonstrated improvements when beneficial. Treat potential corrections to TLB Random, exception semantics, SMC, fastmem, DI, memory writes, and link ownership as hypotheses until validated, not as either guaranteed hardware fixes or automatically forbidden Standard changes.
4. Prefer narrow, reversible fixes supported by code analysis, targeted regression tests, and eventually a Windows runtime reproduction. When a game regresses, isolate the responsible change, confirm whether a reversion or targeted correction is safer, and document why.
5. Preserve dormant-code reuse/reclamation and blanket bypasses as **disabled** until all instruction/data dependencies, writer barriers, code/link ownership, and unwind/savestate safety are established. A personalized fork does not make stale code or disk corruption acceptable.
6. Keep branch handoffs current. Do not create a branch or merge into `testing` on assumptions. Avoid unnecessary refactoring whose only benefit is parity with upstream.

## Existing implementation and validation status

- The session-scoped `EEExecutionProfile` mechanism and Auto/Standard/Extended selection remain as committed. For an HDD-only PSBBN boot, `[EmuCore] EEExecutionProfile = Extended` remains the explicit selection until content-based detection is trustworthy.
- Standard currently disables four optional cache shortcuts: 48-slot projection memoization, D-only permission shortcut, within-sync physical-clear dedup, and never-compiled physical-page negative filtering. These gates were chosen conservatively before this policy clarification and are not being reverted speculatively.
- Existing shared changes in `COP0.cpp`, `vtlb.cpp`, `iR5900.cpp`, `BaseBlocks`, interpreter, RAM writer paths, DEV9, and savestates remain unchanged. That is an intentional decision, **not** a claim of proven compatibility.
- v32 fixed a boot-profile latch reset bug on normal `VMManager::Shutdown` and CPU-thread teardown. Its pure regression tests remain prospective.
- No new code changes are required by this policy decision. The immediate implementation work remains source review and safe Extended-only improvements where dependencies are clear. **No Windows MSVC build, emulator boot, native-game A/B run, or new tests have been executed for v31–v33**. Do not claim otherwise.

## Next actions for this branch

1. Review actual current Extended-mode entry, exit, JIT cache and TLB reset invariants, retaining the existing Standard path unless a concrete bug is identified.
2. Strengthen executable-test coverage for fine-mode `recDI` boundary continuation and translation barriers, noncontiguous source PFNs, constant data host-pointer dependencies, physical writers, and direct-link invalidation. Only commit code changes justified by the established invariants.
3. Benchmark and troubleshoot the next PSBBN build when the user has Windows access; use actual block evictions, compiled bytes, TLB sync time, reset count and gameplay/boot behavior rather than request counts alone.
4. If a retail title breaks, record the game, path, profile, last good commit, exact symptom, and reproducible differential. Prefer the smallest revert or fix and keep known working behavior accessible.

**Decision summary:** Standard is *this fork's current Standard*, not an upstream reconstruction project. Extended remains the main OS/MMU experiment. Test-driven, targeted changes take precedence over both speculative global fixes and blanket reversions.
