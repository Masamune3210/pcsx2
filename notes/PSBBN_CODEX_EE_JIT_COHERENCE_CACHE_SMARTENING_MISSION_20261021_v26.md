# Codex mission v26: make EE JIT caches dramatically smarter without losing correctness

## Mission and authority

Repository: Masamune3210/pcsx2
Work on EXISTING branch: note-working/psbbn-jit-reiserfs-mmu-witness-20261020
Starting reference: 094dd585a2840e41c699859e07c1008fd6b657c5 (v25 review). Check current branch head before editing, since other work may have landed.
Read first: notes/PSBBN_JIT_CODEX_REVIEW_AND_METADATA_EXTENT_20261021_v25.md, notes/PSBBN_JIT_CHURN_NEXT_STEPS_20261021_v24.md, notes/PSBBN_JIT_CODE_PRESENCE_FILTER_20261021_v23.md, and notes/PSBBN_JIT_TLB_INVALIDATION_DEDUP_20261021_v22.md. Earlier handoffs only when directly relevant.

OBJECTIVE: Systematically discover, evaluate, test and implement every materially promising mechanism for minimizing EE recompiler cache MODIFICATIONS while ensuring the cache is coherent, safe and architecturally correct. “Modification” includes removal of blocks, patching dispatch/link sites, host mapping changes, host write-protection and fault backpatch churn, recompilation, code emission, cache allocation/reclamation, and full resets. Guest changes should not require touching translated code unless that compiled code actually depends on the changed state.

PS2 FIRST. PSBBN/Linux is a stress test, not the specification. The target is sound and fast PS2 emulation across retail games, BIOS/OSDSYS, homebrew, Linux, savestates, interpreters and supported RAM modes. No hacks based on PSBBN addresses, no blind TLB bypasses, no blanket permanent interpreter mode, no increased cache size presented as a fix, no artificially suppressed guest exceptions. Keep normal native 64 KiB dispatch fast. Architecture changes must be justified against real code and tests.

Make as much progress as possible without waiting for step-by-step instructions. Implement safe, substantial wins, but DO NOT “optimize” by removing validity checks or retaining compiled code without proof of ALL relevant dependencies. If a deep redesign is needed, commit bounded instrumentation and foundational abstractions first and report the precise blockers. Use a new uniquely named v27+ handoff, not this file, for completion state. Do not create a branch.

## Known quantitative baseline (not a claim of improvement)

The user observed approximately 1 FPS, then manually stopped the run; it did not crash. Windows build v2.9.92-231-g6b18a18cf measured:
- 266,240 TLB mapping synchronizations, roughly 208,306 TLB writes and 57,934 ASID switches by the last full snapshot;
- roughly 2,168,135 physical-clear requests, with ~13.58 s cumulatively spent in mapping synchronization at that snapshot;
- ten repeated fine-mode EE JIT arena-exhaustion resets, each using approximately 67.04 MiB of a 64 MiB + guard allocation (~99.9% of reported capacity) and compiling roughly 188k to 202k blocks per fill;
- repeated code-cache invalidation/recompilation even after Linux had mounted ReiserFS and reached swap/multiple partitions.
A later v2.9.92-238-g562faf941 test of within-sync dedup showed 2,206,093 physical-clear requests and only 177 duplicates avoided: ~0.008%. Thus within-sync dedup is valid but NOT a meaningful cure. Most churn is across separate TLB/ASID updates. Physical clear calls are not actual blocks removed, and cache high-water bytes are not a live-set measure.

More recent source work is UNTESTED in Windows: conservative “ever compiled this physical page in this generation?” bitmap, its 128 MiB compile-time sizing correction, D-only permission update fast path, and the v25 metadata-extent safety fix. There is no measured FPS or invalidation improvement from those yet. Current counts must be gathered again.

## Non-negotiable correctness model: establish and document invariants FIRST

Before removing invalidations, explicitly model the distinct identities and lifetimes of:
1. Guest virtual instruction address (VPN, offset, privilege, ASID, global bit, page size/EntryHi, TLB selected entry, alias precedence); physical instruction content (PFN, byte range, content generation).
2. Recompiled block identity and compiler assumptions: starting VPC, all instruction pages and branch delay slots, constant/relative targets, virtual exception PC/BD/EPC, privilege/config/register state, and scheduling/exit assumptions. Two VPCs sharing a PFN are NOT necessarily equivalent compiled blocks.
3. Data translation dependencies: generated constant-address loads/stores in recVTLB may embed host pointers/handlers/physical destinations; an instruction-page guard alone cannot make such code reusable across a data VPN remap. Include write-permission checks and host SMC protection in the model.
4. Separate lookup caches: physical BaseBlocks and dispatch metadata; native 64 KiB recLUT; guest 4 KiB recLUTPages/hwLUTPages; vTLB, fastmem and memory-protection mappings; backpatch tables; incoming/outgoing direct links; generated thunks and compiled code arena.
5. Code *eligibility* versus code *content*: removal of one guest virtual mapping may mean only that VPN must stop dispatching to a block, not that the physical bytes changed. But eligibility may be baked into a block's compiled assumptions. Show exactly where such dependencies live.
6. Mutability and execution safety: write vs TLB change vs ASID switch vs D-only update vs instruction-page change vs data-page change; when code is executing, can an exception or TLB update happen inside the same generated block? Can an outgoing link bypass dispatch revalidation?
7. Lifetime/ownership: which object owns generated code, host-IP fault backpatches, jump links, metadata ranges and arena allocations; which caller can still return to that code after an invalidation; what reset/savestate paths destroy or preserve them.

Formally state when an existing compiled block can be entered, kept dormant, reactivated, patched or reclaimed. If an invariant is not established, fail closed rather than guessing.

## Exhaustive mechanism inventory: evaluate ALL of these families

For EVERY family, identify current behavior and exact source symbols/call sites, correctness preconditions, expected modification savings, runtime/memory/complexity cost, tests, and a recommendation (implement / prerequisite / reject / defer). Discover additional repo-specific mechanisms beyond this list.

A. Measure causes precisely.
- Instrument invalidations by CAUSE: actual CPU store/SMC, EE DMA or external producer, guest TLBWI/TLBWR, ASID, D-only permission, entry validity/PFN/page size, scratchpad, cache operation, savestate/reset, protection/backpatch, code self-check, manual write-watch, settings change.
- Count requests, distinct PFNs, actual blocks and host bytes invalidated, patches/unpatches, lookup metadata work, JIT compiles (with original VPC/physical/context), repeat compiles and cache residency/reuse, backpatch activity, time spent in each phase, code arena allocations and cache reset reasons. Avoid logging per event: bounded aggregate windows/histograms/representative traces gated by existing tracing or profiler.
- Report guest cycles/real seconds, reproducible workload phase, and time in COP0 map projection, block lookup/invalidation, generated-code emission, interpreter fallbacks, fastmem faults, and EE execution. Separate diagnostic logging overhead from guest work.

B. Avoid work when NOTHING relevant changed.
- No-op TLBWI/TLBWR detection, old/new effective projection comparison including ASID/global, PFN, V, D, page mask, scratchpad and overlapping priority; avoid invalidation on redundant rewrites.
- D-only transition keeps host mappings/code only if BOTH emitted stores and interpreter consult current permissions. Codex added HasSameBacking; validate against real generated stores, not only a predicate test.
- Avoid repeatedly diffing/rebuilding every active mapping for an ASID change or TLB write when only a narrow VPN range can be affected. Evaluate TLB-entry-indexed reverse map, page-range diff, generation counters, compact vectors, map reuse, preallocation, and large-page-aware update.
- Avoid redundant host vmap, mprotect, recMap/unmap and direct-link patches when exact backing is unchanged. Track physical code presence conservatively; avoid expensive block searches for pages without code.

C. Separate virtual-dispatch state from reusable physical instruction content.
- Avoid purging physical code merely because a virtual alias is removed, added, or reassigned; instead detach virtual dispatch safely and validate requirements when next entered.
- Consider block variants keyed by (VPC, physical source page(s), content generation, compilation mode, relevant guest context). Explicitly inventory which ASID/privilege/TLB state or constant data mapping dependencies need inclusion or guards. ASID alone is inadequate because it can be reused/remapped; PFN alone is inadequate because virtual PC semantics differ.
- Consider lazy invalidation, per-page epochs/content versions and incoming mapping epochs, guards at block entry/exit, per-VPN version/tagged dispatch, context-aware caches, watchpoint-independent content identity, hashed instruction content only where correct and cheaper, translated-code sharing when equivalence is PROVEN.
- Quantify per-entry guard overhead, fast path impact, version overflow and savestate effects. Never allow direct static links to bypass a required guard.

D. Make source-page and dependency tracking complete.
- Handle physically DISCONTIGUOUS consecutive guest virtual pages. Existing BaseBlocks metadata is contiguous from first physical start even when next VPC page maps to another PFN; v25 fixed a presence-filter false negative but NOT full invalidation of a block when only its noncontiguous second physical page changes.
- Compare alternatives: forbid/split translation blocks at guest 4 KiB boundaries; compile branch plus delay slot with dedicated interpreter/specialized guard; explicit per-block physical source-page and byte-range dependencies; reverse PFN -> block references. Preserve MIPS JR/JAL/JALR and branch-likely semantics, delay-slot annulment, exact EPC/BD, overlapping blocks, self-modifying instructions.
- Data dependency tracking: constant-address loads/stores and already-patched host pointers, PC-relative targets, register-constant compiler assumptions, translated MMIO/handler identity. Decide whether runtime vTLB lookup, translation guards, or dependency invalidation is the safer cheaper mechanism.
- Distinguish code write invalidate vs data mapping change vs host protection page-trap. Verify all instruction writers: EE stores, IOP/DMA/DEV9 and other engines, debugger/cheats/patches, texture/VU interactions only when genuinely sharing relevant EE code RAM, savestate load, host direct RAM writes. Trace all notification paths and identify any missing write barriers.

E. Make invalidation granular, idempotent, and actually cheap.
- Compare byte-range vs 4 KiB/64 KiB/page-mask invalidation. Use reverse indexes from PFN/physical byte range to the exact blocks dependent on it, rather than global ordered-multimap scans. Avoid invalidating overlapping unrelated blocks.
- Track mutation generations/dirty bits with correct ordering, avoid code-cache edits if a changed byte cannot affect a compiled instruction; evaluate inline content comparison, write barriers, dirty queues and lazily validated blocks. Prove correctness for writes that restore old bytes, atomic/misaligned/128-bit writes, DMA bursts and concurrent host writes.
- Split host TLB unmap/reprotect events from actual instruction-content invalidation. Do not confuse guest EntryLo.D with host SMC dirty protection.
- Avoid repeated patch/undo/repatch of the same jump or fastmem fault site; track provenance and deduplicate stale events without letting another block own an invalid patch.

F. Make block linking coherent, conditional and ownership-aware.
- Audit BaseBlocks::Link/New/Remove and recLinkStaticBlock. Existing BaseBlocks::Remove TODO leaves outgoing link records; measure and fix safely. Record source and target ownership plus generation, unlink retired sources, prevent stale incoming jumps or a stale physical alias from bypassing dispatch/entry checks.
- Explore same-context direct links, page-local links, guarded links, indirection slots or jump islands. Choose hot fast links only when proven safe; others route via the dispatcher. Account for link replacement after recompile.
- Audit host pointer relocation and exception/fastmem backpatch tables; cached block addresses must not survive reclamation or savestate reset.

G. Retain or recycle compiled host code instead of endlessly bump-allocating.
- Measure LIVE compiled bytes versus total bytes emitted and actual reclaimed potential; record repeat compilations of the same code/context and fragmentation.
- Evaluate segregated arenas/size classes, free lists, per-page code slabs, region/trace lifetime management, bounded multi-generation caches, generational epochs, cold/hot blocks, eviction policy, LRU/clock approximations, lazy cold block retirement, code version reuse, code compaction/relocation where supported.
- Reclaim only at a safe JIT-unwound boundary with no return addresses or active exception handlers pointing into recycled memory. Retire links/backpatches and generated thunks with their owner. Keep persistent dispatcher/helper stubs separate from reusable block space. Evaluate W^X host protection and Windows virtual memory overhead.
- Compare against temporary arena enlargement as a BENCHMARK ONLY, not the solution: reducing the number of full resets is not meaningful if recompilation and RSS merely scale up.

H. Reduce recompilation cost when modification cannot be avoided.
- Incremental block repair or recompile only affected suffix/ranges where semantics allow; track compiler assumptions, scheduler charge, branch-likely status and registers.
- Reuse stable code patterns carefully with invalidation epochs, safe block hashing, tiered compilation (quick cold blocks/optimized hot blocks), hot-path promotion and cold-code eviction if compatible with existing code generator.
- Optimize memory-class register flushing and store checks only after their precise exception invariants are locked down. Compare guarded fast mapped access with side exits versus global FLUSH_INTERPRETER, preserving exact GPR/FPR/PC/Cause/BD state. Keep native 64 KiB path untouched unless a general verified win.

I. Synchronization and reset lifecycle.
- TLB update inside a compiled block must cause a safe dispatch revalidation before subsequent stale code/data assumptions are used. Examine generated COP0 helpers, CPU event tests, interpreter fallback, throw/fault paths, pending jumps, ASID changes and guest TLB exception handlers.
- Audit savestate load, reset, interpreter/JIT switch, ELF reload, native dispatch transition, RAM-size changes, ROM, scratchpad, branch/fault handler and fastmem backpatch metadata reset. Generation handles/host pointers must not survive incorrectly.
- Correctness under high TLB turnover, ASID reuse, global entries, overlapping entries, large PageMask, wired/random, D/V bit transitions, COW/fork, same PFN through multiple VPNs, and noncontiguous mapping.

J. Alternative architectures and comparison.
- Assess existing PCSX2 upstream mechanisms and credible DRC/recompiler designs (tagged block caches, translation-block invalidation, TLB epochs, shared physical code versions, dispatch indirection, per-page reverse links, RCU/epoch reclamation). Source and invariant comparisons only; do not blindly transplant another emulator's unsafe assumptions.
- Explicitly include rejection arguments and evidence for high-risk options such as blindly skipping TLB invalidations, caching by physical PFN alone, global cache-size expansion, ring-buffer overwrites, free-on-clear, or always routing through the interpreter.

## Execution plan and ownership

Phase 0, baseline and dependency map: read only directly relevant files/handoffs. Produce a concise architecture diagram/text map of all invalidation entry points and the link/backpatch/code lifetime. Classify the actual modified objects at each event. Establish current test and build capabilities. Do not recursively download entire repo or logs without need.

Phase 1, measurable truth: add or refine bounded counters that distinguish (a) mapping updates with unchanged physical backing, (b) logical deactivation without guest bytes changing, (c) actual compiled blocks evicted, (d) code bytes thrown away, (e) repeat compilation of identical code/context, (f) direct-link patches, and (g) host time. Benchmark against the known v22/v23 baseline on identical workload phases whenever executable. Report counters even if performance does not improve.

Phase 2, low-risk wins: fix provably redundant updates, missed negative filters, unnecessary full projection rebuilds, duplicated host mapping/protection changes, stale link metadata, cache work on data-only physical pages. Add focused production-path tests and commit small independent changes with precise reasoning. Preserve native dispatch.

Phase 3, foundational coherence: resolve noncontiguous source-page tracking and code/data dependency ownership; define block entry guard and safe TLB-change safepoints; establish link/backpatch ownership. These are prerequisites to retaining inactive code across remaps.

Phase 4, ambitious win: prototype one safe strategy that materially reduces RECOMPILATION (rather than just clear-call count), such as mapping-detached dormant block variants with validated per-source-page/content versions, or a generation-safe region reclaim design. Implement fully only if semantic dependencies and lifetimes are covered by executable tests. Otherwise report tested design, counter-evidence, safety constraints and next focused implementation task.

Phase 5, audit: run relevant unit, native EEActiveTLBNative, EEActiveTLB, EERecCodeCache/dispatch, and build tests. If hardware/Windows/PSBBN unavailable, do not claim testing. Leave branch testable and checkpoint the exact validation gap.

All phases should identify opportunities for a safer algorithm that makes FEWER host cache writes while doing MORE cheap read-only validation. But do not exchange necessary invalidations for unvalidated stale reads.

## Required test matrix: executable assertions, not only prose

At minimum design/add focused tests for:
- consecutive identical TLBWR, D=0->1 and D=1->0, valid/invalid, PFN replacement, global versus ASID entries, ASID switch/reuse and overlapping-entry precedence; TLB page sizes and 48-slot behavior;
- same PFN under two VPNs, same VPN under two ASIDs/PFNs, same physical code bytes compiled from differing VPCs, code aliases with different branch targets, and remapping while a direct link exists;
- guest JR/JALR and branch-likely at the last dword of a guest page, noncontiguous next PFN, a fault in a delay slot, EPC/BD/BadVAddr correctness and invalidation from the second PFN;
- guest instruction writes through EE CPU, fastmem/manual SMC paths, DMA/other real EE-RAM producers, bulk writes, cross-page stores, and savestate restore;
- data VPN remapped with unchanged code VPN while an old compiled constant-address fastmem pointer exists;
- TLB/EntryHi change *within* a compiled block followed immediately by a memory instruction or a branch into a remapped VPN;
- exact incoming/outgoing link invalidation after block deletion, recompile, multiple aliases, code arena reset/reuse, and host backpatch cleanup;
- RAM-mode changes, native BIOS/retail execution under 64 KiB mode, interpreter/JIT parity for guest faults, and repeatable normal-game performance.

Use property/model-based comparisons for small synthetic TLB sequences where practical: reference interpreter / architectural TLB projection vs JIT's observed translations, code validity and exceptions. Randomized mutation sequences should be reproducible with logged seed. A test that checks a helper predicate is not proof of full end-to-end behavior; distinguish levels in reports.

## Mechanism evaluation table and acceptance bar

Create a committed table covering the families above. Minimum fields:
- mechanism / exact current hot path;
- what cache state it *avoids modifying*;
- semantic preconditions and dependencies;
- failure mode if wrong;
- observed event frequency / probable benefit;
- per-guest-op overhead, memory cost and complexity;
- unit/integration benchmark, status, decision and next action.

Prioritize on measured avoided *actual invalidated live blocks*, *recompilation time*, *host code arena churn*, *host mapping edits*, and *CPU time*; a lower raw invalidation request count alone is NOT success.

Safety gate: no stale instruction bytes, stale constant data translations, missed permission faults, wrong EPC/BD, stale outgoing/incoming branch links, lost external SMC invalidations, use-after-free host code, host faults, savestate corruption or native PS2 regression. A cache entry's validity must be demonstrable at the exact point it can execute.

Performance gate: demonstrate change on the same guest-workload stage, trace configuration and image clone, report time/FPS, total guest cycles, sync time, blocks invalidated and regenerated, host JIT bytes, resets, and memory overhead. If only source tests are possible, state estimated benefit as a hypothesis and await Windows measurements.

Do not merge into testing until native correctness and speed have been checked.

## Deliverables and commit discipline

1. An exhaustive, repo-grounded invalidation/coherence dependency map, including call-graph paths for all CPU, TLB, host memory and external-write invalidators, plus known holes and safe entry barriers.
2. A ranked mechanism/cost/safety matrix, including rejected candidates and why, plus a concrete smallest-safe-to-implement architecture proposal for major wins.
3. Focused source changes and tests committed in small reviewable units directly to the EXISTING branch; prefer a dependable correctness foundation over a flashy unverified shortcut. If a source change creates a semantic hazard, revert it instead of relying on an obscure warning.
4. A newly named notes/PSBBN_CODEX_EE_JIT_COHERENCE_..._v27.md (or newer unique filename) handoff with decisions, evidence, code revisions, verification status, remaining high-risk work and immediate next action.
5. Exact source commits and honest build/runtime status. If repo connector/push unavailable, supply a normal git patch. Do not reuse filenames, do not ship 20+ MiB emulator logs to GitHub, and give terminal commands one per line.
6. Keep the mission alive across multiple sessions: after substantial findings, update handoff and leave a compact ranked follow-up queue. Do not wait for the user at each phase unless correctness cannot be resolved.

Success is NOT “the JIT cache resets less because it is bigger” or “physical invalidations were disabled.” Success is the cache surviving frequent Linux TLB/ASID turnover with dramatically fewer unnecessary mutations, physical write coherence preserved, and native PS2 workloads neither broken nor slowed.
