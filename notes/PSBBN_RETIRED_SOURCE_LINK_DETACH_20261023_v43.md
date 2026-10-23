# PSBBN retired-source outgoing link detachment experiment (2026-10-23)

Branch: `note-working/psbbn-jit-retired-source-link-detach-20261023`
Baseline: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020` at the v42 log audit.
Evidence: user-uploaded `emulog(7).zip`, PCSX2 `v2.9.92-356-gf36316aad`.

## Why this branch exists

The v41 link ownership witness proved that 22,009,816/22,182,298 (99.22%) generation-2 rel32 target repatches and 14,114,558/14,266,655 (98.93%) generation-3 repatches targeted host jump sites emitted by **retired** source block metadata. All link sites were tracked without overflow or invalid offsets. Guest mapping/ASID projection removes live metadata but BaseBlocks retains the old source's jump sites until a full 64 MiB arena reset.

We cannot simply stop maintaining stale links: some retired host code might still return into the middle of the source block. However, outgoing **direct** jumps in that retired source can be replaced by a **virtual dispatch** jump, which reads `cpuRegs.pc` and resolves the active guest mapping at execution time. Importantly this must be `DispatcherReg`, **not** `JITCompile`: the latter assumes that the active target dispatch slot is currently invalid, which is not necessarily true when only the **source** is retired.

## Current state / decision

Experimental implementation under way. Preserve all physically triggered code invalidations, cross-page and translation-changing dispatch fences, all normal direct linking from live source blocks, and Standard/native behavior. Keep the v41 branch unchanged for before/after comparison.

## Required verification

1. Add owner-indexed outgoing rel32 links only for Extended-MMU **fine** dispatch. On source metadata retirement, patch each tracked outgoing site to `DispatcherReg` once and detach that site from the incoming-target link multimap. Do not detach unknown or native source sites.
2. Add unit tests covering in-flight-retired-source fallback, living source relinking, untracked native compatibility, self-target links, multi-block removal and reset.
3. Maintain v41 diagnostics with additional detachment counts and zero post-detach retargets, validate MSVC build locally, then run PSBBN and compare link-write amplification, guest correctness, and arena exhaustion against `emulog(7).zip`.
4. Do not claim safety or FPS improvements without execution tests. Leave dormant JIT code reuse disabled: this is strictly link bookkeeping control, not compiled-block resurrection.
