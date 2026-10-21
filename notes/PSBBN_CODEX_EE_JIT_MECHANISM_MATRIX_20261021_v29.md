# EE JIT coherence: exhaustive mechanism and architecture matrix (v29)

Date: 2026-10-21. Repository: `Masamune3210/pcsx2`. Existing branch:
`note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
Read-only architecture review started at `86ed1f2fe3`; this document is the only
file owned by this review task. The coordinating
[v27 handoff](PSBBN_CODEX_EE_JIT_COHERENCE_INVARIANTS_20261021_v27.md) records
the subsequent implementation checkpoints and their unexecuted validation gaps.
Source anchors and table baseline descriptions remain pinned to the reviewed
revision; the final implementation status below takes precedence for new work.
No compiler, CMake, C++ tests, emulator, Windows build or guest workload was run
for this document. No runtime performance improvement is established.

This evaluates every v26 family A–J against the
[v27 invariants](PSBBN_CODEX_EE_JIT_COHERENCE_INVARIANTS_20261021_v27.md),
[v26 mission](PSBBN_CODEX_EE_JIT_COHERENCE_CACHE_SMARTENING_MISSION_20261021_v26.md),
[v25 extent review](PSBBN_JIT_CODEX_REVIEW_AND_METADATA_EXTENT_20261021_v25.md),
[v24 priorities](PSBBN_JIT_CHURN_NEXT_STEPS_20261021_v24.md),
[v23 presence filter](PSBBN_JIT_CODE_PRESENCE_FILTER_20261021_v23.md), and
[v22 measurements](PSBBN_JIT_TLB_INVALIDATION_DEDUP_20261021_v22.md).
The dependency/call-path audit and final coordinating handoff carry producer and
safepoint implementation details. This matrix must not be read as authorization
to retain blocks before those dependencies are proved.

## Evidence, costs and validation notation

Historical measurements are observations from the cited handoffs, not fresh
results. Last v22 full snapshot: **266,240** mapping synchronizations,
**208,306** TLB writes, **57,934** ASID changes, **2,168,135** physical-clear
requests and **13,579,322 microseconds** in synchronization. There were ten
fine-mode arena-exhaustion resets, approximately **188k–202k compiles per fill**
and about **67.04 MB (63.94 MiB) reported usage** near the
67,108,864-byte (64 MiB) capacity. More precisely the reported usage was
**67,043,333–67,044,816 bytes**, not a measurement of live code.
The user observed approximately 1 FPS and manually stopped the run.
A subsequent dedup run reported **177 duplicates avoided / 2,206,093 requests**,
about **0.008%**: further polishing within-sync dedup has weak measured priority.
The v22 `unchanged=29,143,247` count describes unchanged projected page visits,
not 29 million complete no-op TLB writes.

**Unknown until new measurements:** actual removed blocks/bytes by cause;
resident/live versus dead arena bytes; repeat-compilation identity/context;
D-only and whole-projection no-op frequency; producer/SMC frequency; link patches
and stale-source records; projection-only time; code emission time; guard cost;
variant hit rate; fault learning lost at reset. Every predicted benefit below is
conditional on these unknowns. A request saved is not an eviction saved.

Costs use `P` = projected 4 KiB pages, `B` = live block records,
`E` = dependency edges, `K` = dependencies on an entered block,
`L` = link sites, `W` = changed byte range, `S` = resident code bytes.
Complexities are design estimates, not benchmark results. `op=0` means no new
steady-state guest-instruction check; host work at mapping/write/compile events
is still charged. Table columns explicitly separate state avoided, safety,
failure, opportunity, cost, validation and action.

Validation levels: **L0** source/structural review; **L1** executable helper/model
assertions; **L2** production COP0/cache/dispatch integration;
**L3** emitted JIT versus interpreter, real fault/backpatch and writer execution;
**L4** Windows native BIOS/retail and matched PSBBN workload measurements.
An existing or proposed test name below means source exists or a required test
is specified; it does **not** mean it executed. This session has L0 only.

## Exact source anchors and present behavior

| Ref | Source and symbols | What the reviewed code establishes |
| --- | --- | --- |
| S1 | [COP0.cpp](../pcsx2/COP0.cpp): `SyncActiveTLBMappings`, `ClearActiveTLBCodePage`, `MapTLB`, `WriteCP0EntryHi`, `ResetActiveTLBMappings` | Rebuild effective map, remove/install changed backing, clear physical code on mapping transitions; D-only backing retention; publish active permission map by swap. |
| S2 | [EEActiveTLB.h](../pcsx2/EEActiveTLB.h): `BuildEEActiveTLBPageMap`, `EEActiveTLBPage::HasSameBacking`, `EEActiveTLBStoreNeedsModification`, `EEPhysicalCodeInvalidationSet` | ASID/global/V/D/PageMask/SPR selection; deterministic first-slot overlap selection; 16-inline-PFN within-sync dedup with set spill. |
| S3 | [iR5900.cpp](../pcsx2/x86/ix86-32/iR5900.cpp): `PC_GETBLOCK`, `HWADDR`, `recMapTLBPage`, `recUnmapTLBPage`, `recGetInstructionPtr`, `recHandleUnavailableCodePage`, `recLinkStaticBlock` | Separate native 64 KiB LUT and fine 4 KiB aliases; physical-backed dispatch and guarded instruction-readability handling. |
| S4 | [iR5900.cpp](../pcsx2/x86/ix86-32/iR5900.cpp): `recRecompile`, `recompileNextInstruction`, `recCheckTLBStore`, `dyna_block_discard`, `dyna_page_reset`, `recClearPhysical` | Physical block key, contiguous extent/copy, source-PFN bitmap, conservative clear filter; memory-class flush and current-D store check; manual integrity discard. |
| S5 | [EERecCodeCache.h](../pcsx2/x86/EERecCodeCache.h): `EERecClearPhysicalBlocks`, `EERecGetPhysicalBlock`, `EERecLinkStaticBlock`, `EERecCompiledPhysicalPages`, dispatch-mode helpers | Overlap invalidation uses physical LUT; managed cross-VPN static links route through dispatcher; native linking retained; bitmap marks actual and metadata pages. |
| S6 | [BaseblockEx.h](../pcsx2/x86/BaseblockEx.h), [BaseblockEx.cpp](../pcsx2/x86/BaseblockEx.cpp): `BASEBLOCKEX`, `BaseBlockArray::insert/erase`, `BaseBlocks::New/Link/Remove/Reset` | Sorted physical block array; target-keyed patch records; removal unpatches incoming sites and leaves outgoing-record TODO at reviewed baseline; array insertion/erase moves records. |
| S7 | [recVTLB.cpp](../pcsx2/x86/ix86-32/recVTLB.cpp): `vtlb_DynGenReadNonQuad_Const`, `vtlb_DynGenReadQuad_Const`, `vtlb_DynGenWrite_Const`, dynamic generators, `vtlb_DynBackpatchLoadStore` | Constant generators read `vtlbdata.vmap` at compilation and emit host pointers or handler/paddr identities. The comments explicitly rely on TLB changes clearing generated code. |
| S8 | [vtlb.cpp](../pcsx2/vtlb.cpp): `vtlb_VMap`, fastmem mapping/protection functions, `mmap_MarkCountedRamPage`, `mmap_ClearCpuBlock`, `PageFaultHandler::HandlePageFault`, `vtlb_AddLoadStoreInfo`, `vtlb_BackpatchLoadStore`, `vtlb_ClearLoadStoreInfo` | Host RAM/fastmem protection and guest D are separate; SMC fault clears physical range; host-IP backpatch record creates code thunk, clears guest block and records faulting PC. |
| S9 | [iCOP0.cpp](../pcsx2/x86/iCOP0.cpp): `recTLBWI`, `recTLBWR`, `recMTC0`, `recERET`; S4 `recCall` and continuation | TLB writers call interpreter helpers; both constant/nonconstant EntryHi generators call `WriteCP0EntryHi`. A helper call alone is not proof that continuation assumptions are valid. |
| S10 | [iR5900.cpp](../pcsx2/x86/ix86-32/iR5900.cpp): `recResetRaw`, `recResetEE`, `recSafeExitExecution`, `recExecute`; [SaveState.cpp](../pcsx2/SaveState.cpp): `PreLoadPrep`, `PostLoadPrep`; [VMManager.cpp](../pcsx2/VMManager.cpp): `ClearCPUExecutionCaches` | Bump arena exhaustion rebuilds dispatchers and clears block/backpatch state. Provider resets can defer until JIT exit. State load clears execution/protection tracking and rebuilds active TLB map. |
| S11 | [Memory.cpp](../pcsx2/Memory.cpp): `memSetPageAddr`, `memClearPageAddr`; [Sif0.cpp](../pcsx2/Sif0.cpp): `WriteFifoToEE`; [SPR.cpp](../pcsx2/SPR.cpp): SPR DMA memcpy paths | Host aliases carry VPN/PFN, not guest D. EE-RAM DMA can write through host pointers; SIF0's commented clear does not prove missing coherence because host protection may notify. Complete writer proof remains required. |
| S12 | [TLB tests](../tests/ctest/core/ee_active_tlb_asid_tests_20261018.cpp), [cache tests](../tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp) | Helper/fixture source for ASID/global/D/PageMask, physical overlap/link dispatch, presence and reset mode. These are not generated-code/runtime evidence. |
| S13 | Official PCSX2 release `v2.4.0`: [BaseblockEx.h](https://github.com/PCSX2/pcsx2/blob/v2.4.0/pcsx2/x86/BaseblockEx.h), [iR5900.cpp](https://github.com/PCSX2/pcsx2/blob/v2.4.0/pcsx2/x86/ix86-32/iR5900.cpp) | Version-pinned upstream comparison: physical block key, bump arena/full reset, outgoing-link TODO and comment allowing return into invalidated code are present in these files. This does not describe current upstream master. |
| S14 | QEMU `v9.2.0`: [TCG design documentation](https://github.com/qemu/qemu/blob/v9.2.0/docs/devel/tcg.rst), sections CPU state optimisations, Direct block chaining, Self-modifying code and translated code invalidation, Exception support, MMU emulation | Documents state-qualified TB reuse, same-page direct-chain restriction, per-page TB/reverse-chain lists, runtime software-MMU translation and host-PC exception-state reconstruction. These are documented architecture facts, not measured PCSX2 results. |

## A. Measure causes precisely

Required cause taxonomy: CPU instruction store/SMC; EE DMA or external producer;
TLBWI; TLBWR; EntryHi/ASID change; permission-only D change; V/PFN/PageMask or
overlap-winner change; scratchpad; guest cache operation; savestate/load/reset;
settings/RAM/dispatch-mode change; host protection fault; fastmem backpatch;
manual integrity/write-watch; and code self-check failure. Record unknown cause
explicitly until its call path is classified. A bounded summary that groups
several of these is a useful first checkpoint, but does not establish that the
full causal inventory has been implemented.

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1. Actual retirement and mutation accounting; S1/S4/S5/S6/S8 | Avoid future speculative edits by attributing real cost; counters themselves change only bounded metadata | Cause must propagate through nested helpers and distinguish request, nonempty clear, distinct block retirement, dispatch store, link patch and host protection | Misattribution selects ineffective or unsafe optimization; counting same retired bytes twice invents a live-set estimate | 2.17M requests known; actual eviction cost **unknown** | op=0 except enabled diagnostics at events; fixed cause arrays/histograms; moderate plumbing | L1 overlapping/current-compiling/pending-link/idempotent patch fixtures and L2 callers required; coordinated metrics work **in progress** | **Implement foundation.** Require old/new snapshots before prioritizing retention; preserve original guest VPC and cause before physical conversion. |
| A2. Reset reason, live/dead/emitted bytes, compile/recompile identity and timing; S4/S10 | Reduces unnecessary arena resets only through later informed design | Count host bytes owned by blocks plus separately owned thunks/alignment/stubs; bounded repeat-ID sampling includes VPC/context/source, not PFN alone | High water mistaken for live bytes; apparent repeat rate merges semantically different code | Ten exhaustion resets and 188k–202k compiles/fill known; reusable repeats and reclaimable bytes **unknown** | op=0; event timers gated; bounded sample memory configurable; moderate | L1 reset/accounting lifecycle, L2 nested cause/time windows, L4 same phase required; metrics **in progress**, identity/timing completeness must be checked | **Implement incrementally.** Label block-body live estimates separately from total arena occupancy and thunk bytes. |
| A3. Distinct PFNs, write/protection/backpatch, phase/cycles and logging overhead; S1/S8/S10/S11 | Identifies host vmapping/protection/backpatch edits and repeated work | Fixed windows; per-cause PFN cardinality or bounded estimate clearly labeled; sample only representative events | Per-event tracing dominates workload or alias requests masquerade as distinct work | Sync cumulative 13.58s known; projection, invalidation, compilation, execution shares **unknown** | op=0 when disabled; bounded bitmap/sketch/windows; event clocks add enabled overhead | L1 saturation/reset, L2 fault nesting, L4 paired trace-on/off and guest-cycle rates | **Prerequisite.** Capture guest cycles/real seconds and reproducible image/workload stage; never infer FPS from clear counts. |

## B. Avoid work when nothing relevant changed

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| B1. Exact projection-input no-op cache; S1/S2 | Rebuilding/diffing temporary maps; avoids no code edits beyond existing effective equality behavior | Cache all 48 entries' semantically used raw fields plus active ASID and backing/lifecycle generation; invalidate on reset/load/RAM changes; compare before side effects | Stale projection or D permission after omitted field/reset; raw padding comparison can create false changes | 266k syncs / 29.1M unchanged page visits; whole-input equality frequency **unknown** | op=0; O(48) signature comparison at sync, small fixed snapshots; low/moderate | L1 V/D/PFN/PageMask/ASID/global/SPR/slot changes and reset; L2 real sync side effects; scoped projection work **in progress** | **Implement narrow exact cache.** First preserve full rebuild as reference; measure cache hits and projection time. No-op architectural instruction still performs required nonmapping effects. |
| B2. D-only `HasSameBacking`; S1/S2/S4/S7 | vTLB/fastmem unmap/map, rec alias edits, physical eviction, host SMC reprotection caused solely by D | Every generated/interpreter store and RMW checks current D before any access with side effects; publish permission map before resume; SPR exception preserved | Missing Mod exception, unauthorized write or RMW read; host SMC silently relaxed | Implemented before v26, helper source only; D-only count and runtime savings **unknown** | Fine-mode existing per-store helper, no extra native op; active map memory unchanged; modest | S12 `PermissionOnlyUpdatesRetainBackingAndPublishBothDirtyDirections` L1 source; L2 actual sync + L3 emitted stores/COW pending | **Retain experimental conservative path.** Both directions need real-store tests including aligned quad and merged stores. |
| B3. Entry-indexed reverse ranges and affected-VPN diff; S1/S2 `BuildEEActiveTLBPageMap` | Full O(P) projection allocations/scans after one slot update | Track old and new slot ranges; recompute every overlapped contender with first-slot priority, global/ASID, V/D, PageMask, SPR; ASID changes may affect many entries | Leaving lower-priority mapping installed or failing to expose one; invalid half/large-page offset wrong | 208k TLB writes known; affected span/overlap distribution **unknown** | op=0; interval index or 48-entry range vectors O(48 + changed pages), additional ownership metadata; high correctness complexity | L1 randomized 48-slot oracle against full selector, L2 actual synchronization, L4 native overhead | **Defer behind B1 metrics.** Prefer scanning 48 compact ranges over maintaining complicated per-VPN contender lists initially. |
| B4. Compact sorted page vectors, range projection, cached ASID projections, preallocation; S2 unordered map | Hash-node allocation, pointer chasing and rebuilding unchanged map; stable runs can avoid vmapping edits | Sort/diff must preserve first-slot selection and 4 KiB semantics; cache ASID view keyed by complete TLB generation; large-page runs split at overlaps/SPR | Cached ASID reuse maps wrong PFNs; vector merging loses D or ordering | High unchanged-page visits motivate hypothesis; allocator/projection-only cost **unknown** | op=0; sorted O(P log P) or range merge, vector capacity reuse; ASID caches can multiply P and RSS; moderate/high | L1 selector equivalence all masks/overlaps; L2 allocation counters; L4 sync/host edits | **Evaluate after B1.** Start reserve/reuse or range iterator; bounded cache only if observed ASID reuse justifies memory. |
| B5. Unchanged host backing/protection dedup and conservative code-presence filter; S1/S4/S8 | Repeated `vtlb_VMap`, `MemProtect`, recLUT alias edits and searches for never-compiled PFNs | Compare exact backing/alias/protection state; protection ownership separate from D; filter covers metadata and actual source, excludes unknown/non-RAM conservatively | False-negative code presence loses SMC; dedup protection retains writable code page or wrong mapping | 4096-byte bitmap covers 128 MiB potential RAM; skip frequency and host call costs **unknown** | op=0 except events; bitmap lookup/range scan; 4 KiB memory plus mapping state; low/moderate | S12 presence tests L1 source; L2 real map/protect sequences + L3 SMC pending | **Keep filter, measure skips.** Do not clear an ever-seen bit on block deletion without owned exact refcounts. Dedup only exact already-installed host state. |
| B6. Within-sync physical-clear dedup; S1/S2 | Duplicate physical block search/patches within one synchronous update | No guest execution between clears; new set every sync; non-RAM/SPR retain original behavior | Dedup across later writes/remaps suppresses required invalidation | Measured 177 / 2,206,093 (~0.008%): tiny observed opportunity | op=0; first 16 PFNs inline, heap spill for large masks; low | S12 dedup L1 source; prior Windows measurement pertains older tested build | **Keep bounded existing code; deprioritize.** Do not turn into cross-sync invalidation suppression. |

## C. Separate virtual eligibility from physical instruction content

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| C1. Dormant multiversion blocks keyed by VPC, source PFNs/ranges/content epochs, compilation mode and relevant context/data assumptions; S3–S7 | Physical code destruction, recompile/emission and link repatching on alias-only turnover | D source completeness; D data dependency solution; I safepoints; F every entry/link validates; stable ownership and bounded variants; eligibility detached before remap publication | Stale instruction/data host pointer, wrong target/EPC/BD or ASID-reuse misdispatch | Most clears across different syncs, but identical safe-reuse rate **unknown**; no observed FPS gain | Per entry O(K) guards or validated dispatch, O(E + variants + code) memory; high | L1 identity discrimination, L2 remap/reactivation, L3 data remap/delay faults and L4 hit rate; **design only** | **Ambitious candidate after blockers.** Pilot one-PFN guest-mode blocks; retain conservative invalidation for untracked blocks. |
| C2. Per-VPN mapping epochs/tagged dispatch plus per-PFN content epochs; S3/S5/S8 | Code bytes and patch sites when eligibility alone changes; dirty invalid entries can wait until used | Epochs cover all writers and mapping changes, ordered before entry; direct and patched links enter validation; content and mapping epochs separate | ABA on overflow/load permits stale block; mapping epoch mistaken for content identity | Guard hit/entry rates and epoch churn **unknown** | O(1) for one-source-page block + dependency guards; a dense 64-bit epoch per 128 MiB PFN is 256 KiB, per all 4 GiB VPN is 8 MiB before tags; sparse alternative adds lookup cost | L1 forced small-width wrap, reset and ASID reuse; L2 links/publication; L3 real SMC | **Prerequisite prototype.** Overflow triggers fully unwound invalidation/reset before value reuse; savestate uses fresh host generation, never restores old host epochs as proof. |
| C3. Lazy invalidation/detach and read-only validation; S1/S3/S5/S6 | Immediate block removal, links and dispatch patching on cold aliases | Invalid virtual dispatch cannot reach stale host entry; content invalidation must stop all incoming entries including inactive variants; dirty queue drained before possible execution | Lazy stale target executes before validation; unbounded retired metadata/RSS | Cold invalidated block fraction **unknown** | Entry check or dispatcher validation; dirty lists O(changed dependencies), bounded queues and fail-closed overflow; high | L1 queue overflow/repeated write, L2 all link routes, L3 write during executing block | **Defer.** First use logical detachment with existing physical eviction; only retain after C1 preconditions. |
| C4. Exact-byte comparison/hash-based content identity; S4 `recRAMCopy`, manual checks | Recompile after same bytes restored; avoids false-positive page invalidation | Compare every actual source range; hash only candidate prefilter, exact compare or established collision-safe proof; include context/data assumptions; concurrent writers synchronized | Hash collision or torn snapshot executes changed code; equal bytes mistaken for equal VPC semantics | Writes restoring old bytes and hash amortization **unknown** | Hash/compare O(source bytes) on validation, copies O(source bytes), no mandatory per-op hash; moderate/high | L1 adversarial collisions/restore-old/cross-PFN ranges, L3 writer timing | **Defer measured experiment.** Epoch first; exact compare only on mismatch/candidate lookup, never hash each guest op. |
| C5. Physical-only sharing, ASID-only versions, or fetch-PFN-only guard | Would avoid many cache writes if assumptions were silently discarded | Requires equivalence of VPC branch/EPC/BD, data mappings, modes and versions; not established | Wrong guest semantics with identical bytes/PFN; ASID reuse stale data or instruction code | No safe benefit evidence | Cheap key conceals missing checks; potentially unlimited corruption | Minimal falsifiers: same PFN two VPCs; same ASID new PFN; unchanged code PFN remapped constant data VPN | **Reject as proposed.** A complete proven equivalence key becomes C1, not this shortcut. |

## D. Complete instruction, data and producer dependencies

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D1. Split ordinary guest-mode blocks at each 4 KiB source boundary; special boundary branch/delay handling; S4 scanner | Makes dependency proof simpler and prevents unrelated-PFN extent eviction; enables safe future reuse | Branch plus delay slot handled architecturally: JR/JALR old target, JAL link timing, likely annulment, precise fault/EPC/BD; no speculative instruction fetch through invalid next page | Wrong delay-slot execution/fault or link register; second PFN not invalidated | Existing noncontiguous source hole proven by metadata model; occurrence rate **unknown** | More short blocks/dispatches near boundaries, no universal instruction guard; modest source metadata; moderate/high semantic risk | L1 split classification; L3 last-dword JR/JALR/JAL/likely, both PFNs and inaccessible next page; source boundary work **in progress** | **Correctness prerequisite.** Narrow interpreter/specialized boundary fallback is acceptable; blanket interpreter mode is not. Native split policy unchanged. |
| D2. Explicit source-range edges and reverse PFN→stable block handles; S4/S5/S6 | Precise retirement of only dependent blocks; avoids contiguous extent assumptions and global validation scans | Record all fetched instruction bytes before publication; stable IDs because `BaseBlockArray` memmoves; mutation/removal updates all reverse edges; branch delay source included | Second PFN write misses block; stale reverse pointer after array insertion or reuse | Actual write fanout and edge count **unknown** | op=0 at guest instruction level, O(affected edges) write lookup, O(E) memory; high | L1 discontiguous second-PFN and overlapping dependencies/array movement; L2 real clear callbacks; L3 real SMC | **Prefer long-term exact foundation.** D1 can bound E and reduce initial implementation surface. Bitmap alone is not a reverse map. |
| D3. Runtime translation for managed constant data accesses; S7 | Makes retained code independent of constant host pointer/handler identity; avoids recompile on unrelated data remap | All constant read/write widths and handlers use current vmap; fine-mode D before side effects; fastmem patches still use current effective address and own metadata | Stale MMIO handler/paddr, stale direct pointer or missed read-only fault | Number/hotness of constant managed accesses **unknown** | Added lookup/branch per affected access, no extra data edges; may lose constant-address fast path; moderate | L3 unchanged code + constant data VPN remap to RAM/MMIO/unmapped/read-only, interpreter parity | **Smallest reuse pilot choice.** Restrict to guest-managed addresses; retain native constants until general benefit proved. |
| D4. Registered constant-data VPN/handler dependencies with entry and continuation guards; S7/S9 | Preserves const fast loads/stores while allowing unrelated remaps to retain code | Track every embedded pointer/handler including backpatch specialization; mapping/mode/protection changes invalidate/guard; mid-block helper boundary; address equivalence exact | Entry fetch guard passes while stale data operation executes | Data edge fanout and const advantage **unknown** | O(Kdata) entry checks or reverse invalidations; O(Edata) metadata, high auditing burden | L1 dependency registration completeness, L2 data-only remap invalidation, L3 patched pointer/handler | **Alternative to D3, defer.** Prefer runtime lookup initially; forbid retention of unclassified embedded mappings. |
| D5. Complete instruction-writer barriers: EE stores, manual/fastmem SMC, DMA/IOP/DEV9 via real EE transfer, debugger/cheats/patches, savestate/direct RAM writers; S8/S10/S11 | Correct barriers allow precise content epochs instead of broad remap destruction | Every producer reaches physical range notification or active host SMC protection before stale code can reenter; ordering covers bursts/misalignment/quadwords/host concurrency; VU/texture only if actual EE-RAM sharing | External write leaves dormant code valid; protection on wrong alias misses notification | Producer frequencies **unknown**; paths inspected do not establish universal coverage | Barrier event O(changed PFNs + dependent edges); protected pages may fault; metadata O(PFN/E); high audit scope | L3 each real producer, partial/cross-page/bulk writes; L2 savestate; source path audit **in progress** | **Hard blocker.** Explicitly list unknown producers; do not infer missing clear from commented calls, or coverage from one fastmem test. |

## E. Granular, idempotent invalidation

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| E1. Physical byte ranges versus 4 KiB/host-page/64 KiB/PageMask clears; S1/S5/S8 | Avoids touching blocks whose instruction bytes do not overlap W; avoids resetting entire dispatch extent unnecessarily | Distinguish notification granularity from exact known written bytes; D2 reverse index; use wide range arithmetic; overlapping blocks/current compilation handled | Missed tail/overflow overlap; narrow host fault incorrectly assumes exact guest write range | False-positive blocks per clear **unknown**; physical clear request count cannot estimate | op=0; interval filtering O(affected edges), per-edge ranges; moderate/high | L1 huge/overflow/cross64KiB/overlap/current compiler; L3 real write extents | **Prerequisite after metrics.** Keep conservative host-page invalidation when only fault-page identity is known; never assume guest PageMask is instruction dependency size. |
| E2. Dirty generations/queues, compare unchanged write bytes, lazily validate; S4/S8 | Repeated invalidation and patch stores, especially data writes on code-containing PFN | Barrier before execution; dirty queue overflow falls back conservatively; same-value optimization must not suppress MMIO/store effects or races; restored bytes require full validation | Lost SMC on torn/128-bit/burst/concurrent write; dirty bit reset too early | Repeated same-range/same-value write frequency **unknown** | Write-time checks or entry validation; O(dirty pages) bounded queues, compare O(W); high | L1 duplicate events/overflow/restore-old; L3 misaligned/RMW/quad/DMA bursts and synchronization | **Defer read-only compare pilot.** Epochs conservative first; no inline comparison on all stores without measured payoff. |
| E3. Separate map/protect invalidation from content change; S1/S8 | Avoids advancing content version/removing code for a host alias or guest D edit alone | C/D/I/F entry and data assumptions solved; protect-state changes still retain SMC traps and owned backpatches | Mapping event incorrectly treated as harmless while embedded pointer changed | Historical TLB event volume high; harmless subset/live-byte share **unknown** | op=0 at guest level except future guards; event cause tag small; moderate | L2 host unmap/reprotect vs real bytes, L3 code/data remap/store protection | **Implement cause separation now, behavior later.** Content epoch should represent byte writes, not TLB lifetime; retain conservative evictions until proof. |
| E4. Idempotent link/dispatch patching and backpatch provenance dedup; S5/S6/S8 | Avoid writing already-correct patch target or dispatch word; avoids stale repeated backpatch work | Expected old/new patch ownership and generation valid; patch format/reachability checked; stale event cannot borrow new block's host address | Patch changes wrong recycled owner; x86 displacement overflow or instruction race | Repeated equal patch frequency **unknown**; baseline records only requests | op=0; load/compare at patch event, fixed counters; owner fields O(L); low initially/high ownership | L1 repeated patch/pending/new/remove cases; L3 fault after recompile and reset; scoped patch accounting **in progress** | **Implement idempotent helper without reclamation.** Source/link cleanup requires separate F1 proof. |

## F. Links, backpatches and ownership

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| F1. Source+target owner/generation records; erase outgoing retired-source records; S6 TODO | Historical patch records and needless future patch writes into dead code | Stable owner IDs; source code remains until safe boundary; surviving source links to removed target must still route compile/guard; all jump creation sites registered | Surviving incoming link omitted; freed patch address later overwritten by target publish | Outgoing stale-record count and patch volume **unknown** | op=0; O(L) fields + adjacency, O(source links + target fanin) retirement; high | L1 delete source/target in both orders, recompile target, duplicate links, array moves; L3 fault/helper retirement | **Prerequisite.** Add accounting first, then owned cleanup; never infer source owner solely from current target physical PC. |
| F2. Same-page/same-context direct links, guarded links and dispatcher fallback; S5 `EERecLinkStaticBlock` | Preserves fast native links; avoids unnecessary unlink/repatch on context-eligible paths | Same-page is sufficient only when all block data/context assumptions match and mapping cannot change mid-block; every newly patched link follows policy | Guard bypass after target compile; current block remaps own VPN then jumps stale local code | Existing fine-mode cross-VPN dispatcher restriction source-confirmed; link distribution/hit cost **unknown** | Native op=0; fallback adds dispatch, guarded edge O(K) or one branch; small metadata; moderate | S12 cross/pending/same/native/fixed targets L1 source; L3 remap while direct link exists | **Keep conservative restriction.** No broad direct-link expansion before C/D/I. Same-page policy is not proof of safe dormant reuse. |
| F3. Per-variant indirection slots/jump islands rather than direct patch fans | Updates one slot on deactivate/replace; avoids code-page writes across incoming L sites | Slots identify VPC+validated variant, are atomically published with generation; every path uses guard; host branch range and slot lifetime known | Stale slot or bypass calls reclaimed/wrong context code | Incoming fanin and W^X patch cost **unknown** | At least one extra dependent load/indirect branch at affected transfer; O(entries) slots, I-cache stub overhead; high | L1 slot generation/alias/replace, L3 emitted code/fault, L4 branch predictor/native cost | **Evaluate guest-mode only after link metrics.** Avoid replacing native direct links without measured general win. |
| F4. Host-IP backpatch and thunk ownership; S7/S8/S10 | Avoids dropping useful fault learning on unrelated code reset; allows local cleanup instead of global reset | Every record/thunk belongs to stable block/allocation generation; returning helper/exception frames tracked; fault PC learning includes relevant context | Host fault patches new allocation at reused IP; thunk returns to freed code | Repeated backpatch amplification source-plausible, **unmeasured** | Fault-path owner lookup, O(backpatches + thunk bytes), no universal guest op; high | L1 stale-IP handle/generation, L3 real exception/backpatch + arena retirement, L2 state load | **Hard prerequisite for G.** Separate guest-PC fault learning from host-IP metadata only with correct context/reset semantics. |

## G. Retain and recycle host code

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| G1. Segregated code slabs/regions with deferred retirement; S4 bump allocator/S10 | Full arena wipe, dispatch/helper regeneration and recompile of surviving regions | F owned links/thunks/backpatches; D complete deps; persistent dispatch/helpers outside reusable region; all JIT frames unwound before reuse; W^X and Windows VM semantics supported | Return or fault IP lands in reused code; helper overwritten; host protection failure | Ten full resets known; reclaimable live/dead bytes and region lifetimes **unknown** | op=0 or region-entry pin; slack/fragmentation and region metadata, VM-call cost; high | L1 ownership/retire queues, L3 nested helpers/events/faults, L4 Windows + normal games | **Defer implementation until ownership and live-byte evidence.** Regions simpler than arbitrary individual free lists initially. |
| G2. Size-class/free-list/per-page slab allocation | Reuses dead bytes, reduces high-water advance; per-page grouping may align lifetimes | Same G1 safety; size-class fit/host reachability; blocks crossing pages have explicit owner/deps; allocation failure conservative | Free-on-clear reuses still-executing bytes; coalescing corrupts ownership | Allocation size distribution, fragmentation **unknown** | Compile allocation work; class slack and per-allocation metadata; high | L1 randomized allocator after proven safe retirement; L3 active-frame reuse falsifier; L4 W^X/Windows | **Alternative after G1 evidence.** No immediate individual free on invalidation. |
| G3. Bounded generations, clock/LRU, hot/cold regions and lazy cold retirement | Avoids global eviction of useful translations; retains frequently reactivated variants | Stable generation handles, bounded capacity and eviction queue; eligibility detached before retirement; recency data does not alter semantics | Generation ABA, stale link survives eviction, unbounded inactive versions | Hot reuse distance/working set **unknown**; emitted bytes/fill not working set | Recency update per entry if exact LRU; sampled/clock reduces writes; O(blocks/regions) age metadata; high | L1 capacity/generation overflow/hot-cold stress, L2 victim detach, L3 safe reuse, L4 hit rate | **Evaluate approximate clock over exact LRU.** Do not add a metadata write on every guest op to save speculative code writes. |
| G4. Code compaction/relocation | Recovers fragmented holes without eviction | Complete relocation information for all branches, absolute constants, RIP-relative data, host return addresses, profiler ranges and exception/fault IPs; safepoint; W^X | One unrelocated pointer/return IP produces host crash or silent wrong access | Fragmentation itself **unmeasured** under current bump allocation | O(S + relocations + L) copy/repatch; temporary space and relocation tables; very high | L1 exhaustive relocation oracle, L3 all instruction/fault forms, L4 host platforms | **Defer strongly.** Evict whole retired regions first; current raw emission does not establish relocation support. |
| G5. Larger arena as benchmark; blind wrap/ring overwrite/free-on-clear | Larger arena delays resets; unsafe variants would avoid allocator checks | Benchmark must report RSS, cycles, emitted bytes and compile rate; unsafe overwrite lacks lifetime proof | Memory growth conceals unchanged churn; ring/free corrupts active code | Reset volume known; larger-cache speed/live-set response **unknown** | More RSS and longer fill interval; wrap/free unsafe irrespective of low cost | L4 controlled temporary size sweep only after native safety; none run | **Benchmark only for size. Reject blind wrap/free-on-clear.** Increased capacity is not a coherence solution. |

## H. Reduce unavoidable recompilation/operation cost

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| H1. Partial block/suffix repair; S4 compiler/register analysis | Re-emits only changed range, preserves stable prefix code | Exact entry register/constant assumptions, scheduler/cycle charge, branch-likely/delay state and all links/deps; extra code area or atomic safe patch; no active frame crosses changed suffix | Wrong register state, double/missing cycles, imprecise exception, fallthrough into retired suffix | Mutation range/compile-time share **unknown** | Complex extra entries/state maps; O(changed suffix) compile but patch/lifetime work high | L1 compiler-state equivalence, L3 prefix→repaired suffix/fault/delay parity | **Defer strongly.** Shorter source-complete blocks reduce repair scope safely first. |
| H2. Cold quick tier / hot optimized tier, stable code templates/patterns | Less initial emission/compile time; cold code uses smaller arena footprint | Both tiers preserve exact semantics/context/deps; promotion handles owner/link replacement; templates parameterize every embedded address/state assumption | Template reused with stale handler/context; promotion leaves stale sites | 188k–202k compiles/fill known; cold execution fraction and compile CPU **unknown** | Hotness writes per entry/sampling, tier duplication/code+metadata; high | L3 differential tiers and promotion under remaps/SMC, L4 compile/time/code bytes | **Defer to measurements.** Avoid permanent interpreter routing; consider emission-size wins only after profile. |
| H3. Precise memory side exits instead of unconditional `FLUSH_INTERPRETER`; S4 memory prologue/S7 | Reduces register flush emissions/runtime work and generated bytes on successful mapped accesses | Reconstruct precise GPR/FPR/PC/BD/EPC/BadVAddr and cycle state before fault; D check before RMW/MMIO side effects; fault/backpatch recovery maps; all widths/alignment/delay slots | Silent guest exception corruption even when normal loads appear correct | Flush on memory-class source path known; cost/hotness **unknown** | Guard/fast access + uncommon spill side exit; deoptimization metadata and slow-path code; very high | L3 every fault class, delay-slot/likely/RMW/128-bit/interpreter parity and host faults; L4 native performance | **Separate later project.** Keep correctness-first flush until side-exit reconstruction proved; native path changes need general measured win. |
| H4. Store permission check specialization/hoisting | Fewer per-store helpers and generated bytes | D and effective mapping stable over hoisted interval; mid-block context changes end interval; merged/quad stores check correct address before any side effect | COW/read-only write slips through; RMW MMIO touched before exception | Current fine-mode helper exists; per-store share **unknown** | Inline active-map tag check may be cheaper, hoist guards add epoch checks; modest/high | L3 D transitions and mid-block remap then store, special stores and faults | **Defer until I and measurements.** Never specialize permission only from compile-time D. |

## I. Synchronization, exceptions and reset lifetime

| Mechanism / current path | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| I1. Translation/context-changing opcode safepoints; S9 `recTLBWI/WR`, `recMTC0`, S4 scanner | Allows future retained code without global purges, stops stale continuation | Both generated/REC_SYS paths, constant/nonconstant EntryHi, Status/Config/ERET and helper exceptions classified; branch delay semantics and pending targets preserved; next fetch validated | Helper remaps data/code then returns into precompiled stale access or same-page link | 208k writes + 57.9k ASID changes known; actual in-block dangerous continuations **unknown** | Extra dispatcher exit only on relevant opcode, no ordinary native guest-op guard; modest metadata, high semantic proof | L1 opcode classification; L3 TLB/EntryHi then load/store/branch and delay-slot behavior; scoped source implementation recorded in v27; executable evidence absent | **Hard correctness prerequisite.** Block-end/event tests are not equivalent to validation before the next instruction. |
| I2. Full lifecycle generation reset/load/RAM/mode/provider switch; S8/S10 | Permits local lifetime decisions without global stale-host references | State load discards generated ownership/tags/queues; synchronize VU/GS/CPU producers; restored ASID map rebuilt; ExposedRam/config/mode changes invalidate cached projection and pointer assumptions | Host pointers/epochs survive restored memory or new RAM size; native lookup sees guest links | Reset/exhaustion measured; other lifecycle event rates **unknown** | Event-only generation bump and clear, O(metadata); low incremental/high audit | L1 forced wrap + provider/internal-reset distinction; L2 savestate/RAM/settings/interpreter↔JIT; L3 execution after restore | **Prerequisite.** Preserve HDD-enabled savestates. No cache-mission change to disk rollback policy. |
| I3. Reference-model/property mutation tests across 48 entries; S2/S12 | Avoids unsafe broad edits by proving selector equivalence before mutation reduction | Reproducible seeds; oracle independent enough to catch shared bug; compare observed translations/permissions/side effects, not only helper return | Both model and optimization share wrong overlap/mask semantics | Covers large state space, performance benefit not applicable | Test-only costs; bounded sequences and shrunk repro cases | L1 seeded no-op/D/V/PFN/global/ASID reuse/overlap/masks/wired-random/SPR; L2 real sync side effects; L3 generated exceptions | **Implement as design support.** Native 64 KiB dispatch and special RAM modes remain mandatory L4 regressions. |
| I4. Concurrency/quiescence and RCU-style epochs | Defers destructive edits/reclamation until all potential users finish | Identify every executing thread, helper return, host exception, VU/IOP/DMA producer; release/acquire publication or established thread confinement; no raw pointer survives epoch | Free while a handler/thread retains IP; dirty queue visible after code entry | Actual cross-thread code-user set **not established** | Epoch pin/read per entry/batch and retire queue; O(retired objects), high | L3 paused helper/fault + producer write and attempted reclaim, L2 shutdown/load | **Do not assume RCU solves ownership.** A single verified CPU-unwound boundary may be simpler; establish thread model first. |

## J. Architecture comparisons and rejected shortcuts

| Mechanism / source comparison | State avoided modifying | Preconditions / dependencies | Failure if wrong | Frequency / benefit evidence | Added op / memory / complexity | Tests / status | Decision / next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| J1. Preserve present native 64 KiB physical LUT/direct links versus fine 4 KiB guest-mode metadata; S3/S5 | Keeps ordinary PS2 dispatch fast and avoids universal tags/indirection | First guest-mode transition purges native-assumption links; resets preserve/restore correct policy; guest mappings and native fixed aliases distinguished | Stale native link bypasses remapped guest VPN or reset mode wrong | Normal-game speed **unmeasured here**; no permitted regression | No new native op; scoped guest metadata/guards only; moderate | S12 dispatch reset/link fixtures L1 source; L3 transition/remap; L4 BIOS/retail required | **Preserve architecture boundary.** Guest-mode experiment cannot justify native slowdown without measurements. |
| J2. Official PCSX2 v2.4.0 and local `origin/master` physical-key/bump-arena comparison; S13 and source below | No claimed savings; exposes inherited ownership/allocation assumptions | Compare exact versions; neither dated release nor fork ref establishes current master; guest-MMU additions change assumptions | Transplanting native-era assumptions into managed guest mappings; confusing snapshot with current upstream | No upstream runtime measurement | Source inspection only | L0 targeted release/fork source inspection | **Use inherited behavior as baseline.** Version-pinned upstream source confirms these foundations; it supplies no proof that retained guest-MMU variants are safe. |
| J3. QEMU documented state-qualified TB reuse, per-page reverse lists and same-page direct chaining; S14 | Can avoid code destruction on mapping-only changes and broad block scans while invalidating owned chains | QEMU documentation also specifies software-MMU translation at every memory access; PCSX2 constant host-pointer specialization does not satisfy that prerequisite; PS2 VPC/delay/context proof still required | Copying only physically indexed TB idea retains stale constant data mapping or bypasses PS2 exception/context guards | Architectural precedent only; no PCSX2 performance extrapolation | Runtime data translation/cache lookup plus state/dependency guards and reverse lists; C/D/F costs apply | L0 documentation comparison; local L1–L4 gates still required | **Supports D3+C1 as a coherent combination.** Reject physical-key-only transplantation; assess guard/data-lookup cost locally. |
| J4. Blindly skip TLB clears, cache by PFN/ASID alone, force interpreter, or suppress guest faults | Would superficially reduce clear/emission counts | Preconditions contradicted by S4/S7/S9 or outside mission | Stale code/data, wrong EPC/BD or missed exceptions; interpreter hides JIT issue and sacrifices native speed | No defensible correctness/performance evidence | Attractive short code change conceals unbounded semantic risk | C5/D3/I1 falsifiers invalidate shortcut | **Reject.** Keep conservative invalidation on unresolved dependencies. |
| J5. Architecture-neutral map/protection/patch dedup versus wholesale new JIT | Avoids exact redundant host cache writes with small semantics surface | Equality and caller lifetime proven; counters distinguish skipped writes from requests | Omitted dependency turns dedup into stale read | No-op/backing opportunities plausible; actual saved work **unknown** | Event-only checks and bounded metadata; low/moderate | L1–L4 as B/E rows | **Prioritize bounded foundations.** Major retained-code design follows evidence, not global replacement. |
| J6. QEMU documented host-PC→guest-PC/state reconstruction on exceptions; S14 versus S4 flush | Could reduce unconditional spill/emission using precise side exits | Per-instruction metadata must reconstruct all PS2 architecturally visible state, including GPR/FPR/BD/EPC and effects before a fault; QEMU's target examples are not a PS2 implementation | Correct fast accesses conceal corrupt guest exceptions or delay slots | No local cost or correctness result | Deoptimization metadata and fault-path lookup, H3 costs; very high | L0 architectural comparison; L3 exhaustive emitted-code faults required | **Defer as H3.** Documentation demonstrates a design obligation, not permission to remove `FLUSH_INTERPRETER`. |

The local comparison used `git show origin/master:pcsx2/x86/BaseblockEx.h` and
`git show origin/master:pcsx2/x86/ix86-32/iR5900.cpp` at
`091ab68708a555198d80a7216965481c7ddd0d98` (`Patch: Clean up PatchCommand struct`).
The configured origin is `https://github.com/Masamune3210/pcsx2.git`.
At that exact ref `BaseBlocks::Remove` retains its outgoing-link TODO,
`BaseBlocks::New` repatches target-keyed records, `recRecompile` uses
`recBlocks.New(HWADDR(startpc), ...)`, and `recPtr >= recPtrEnd` triggers reset.
These are source facts about the available fork ref. For a real upstream and
external DRC comparison, three targeted files were also retrieved read-only from
official GitHub repositories at the version-pinned S13/S14 URLs on 2026-10-21:
PCSX2 v2.4.0 `BaseblockEx.h` (5,295 bytes), PCSX2 v2.4.0 `iR5900.cpp`
(73,349 bytes), and QEMU v9.2.0 `tcg.rst` (9,639 bytes). No repository or log
bulk download occurred. S13 confirms the inherited source foundations, not the
current upstream implementation. S14 provides a documented coherence design
whose runtime data translation and state identity must accompany physical-code
reuse. No external runtime performance, complete TB-key composition, current
upstream master behavior, or Dolphin implementation facts are asserted. Other
architectural alternatives remain locally reasoned proposals.

## Smallest safe architecture for substantial retained-code reuse

The first prototype should reduce recompilation under alias turnover without
requiring general code relocation or immediate memory reclamation. It remains
**blocked**, not implemented, until the following gates hold.

1. **Bound source identity.** In fine guest mode, ordinary compiled blocks fit
   one 4 KiB source page. Boundary branch/delay cases use the proven specialized
   fallback or explicit multiple-range dependencies. Do not retain an ambiguous
   contiguous `BASEBLOCKEX` extent. Publish dependency metadata before any
   callable entry; use stable block IDs rather than movable array addresses.
2. **Remove untracked data assumptions.** For this pilot, generated accesses to
   guest-managed constant data addresses perform runtime vTLB translation.
   Retain current permission checks before stores/RMW. All patched forms and
   MMIO/handler paths must follow the same rule. Native fixed-address fast paths
   remain on their current policy. Any block with an unclassified embedded
   translation remains on conservative eviction.
3. **End assumptions at context changes.** Translation/context-changing opcodes
   exit through an architecturally correct continuation before another generated
   fetch/data instruction runs. Resolve delay slots and pending branch targets
   explicitly. An entry guard cannot repair a stale instruction halfway through
   the old block.
4. **Use bounded variants and complete tags.** Candidate lookup starts at VPC;
   a variant records source PFN and exact range/content epoch, generation,
   compilation mode and every audited context assumption. ASID is an eligibility
   input, not a sufficient identity. Global entries can share a variant only
   where all compiled semantics agree. Content writes retire/mark every variant
   through reverse dependencies even while no virtual alias maps it.
5. **Detach eligibility without destroying bytes only after validation exists.**
   On remap remove/tag invalid virtual dispatch first. A cache miss checks the
   selected mapping, source version and mode/context, then binds a compatible
   dormant variant. All entries and all patched links route through a valid
   guard or a dispatcher that repeats these checks. Initially prohibit managed
   direct links that bypass validation; keep native linking unchanged.
6. **Keep byte lifetime conservative.** Retained variants use the current bump
   arena and existing fully unwound full resets initially. This can reduce
   emission if reuse exists, while avoiding the independent G reclamation proof.
   Bound variant metadata and dormant code count/bytes; on capacity pressure
   conservatively detach/retire or perform the existing safe reset. Eviction of
   metadata is not permission to recycle code storage.
7. **Treat reset/overflow as invalidity.** A host lifecycle generation dominates
   all handles. A counter about to wrap forces an unwound global invalidation
   before value reuse. State load discards host generations/dependencies and
   reconstructs guest mappings from restored architectural state. Restore must
   never resurrect a stale host entry because an old numeric epoch matches.

The minimum hot guest-mode entry cost is one current mapping/variant check plus
one-source content/mode/context validation; additional dependencies add loads and
branches. It is **not** a claimed one-comparison solution: data/context and link
proofs determine the final checks. The pilot must measure guard instructions,
branch misses, hit rate, metadata bytes, actual compile time/bytes avoided and
whole-workload speed. If safe variants seldom recur, stop before adding G's
allocator/reclamation complexity. A slab/region generation is a separate next
prototype once live/dead byte evidence and F/I ownership exist.

## Dependency blockers and falsifying tests

Each failed test blocks retained reuse; performance evidence cannot override it.
The test should exercise production behavior at the indicated level.

| Blocker | Smallest falsifier | Required observation / level |
| --- | --- | --- |
| Incomplete source bytes | Branch/JR/JALR at VPC offset `0xffc`; next VPC page maps noncontiguous PFN; alter only delay-slot PFN | Every dependent block loses eligibility; reentry observes changed instruction. Repeat JAL/JALR link timing, likely annulment and absent next page. L3 |
| VPC/context identity | Same bytes/PFN under two VPCs with relative branch/JAL targets; same VPC under ASID A/B with differing PFNs; reuse ASID after remap | Branch targets, EPC and BD match interpreter; no PFN-only or ASID-only hit. L3 |
| Stale constant data pointer/handler | Keep code mapping unchanged; remap data VPN used by constant read/write to another RAM PFN, then MMIO, D=0, invalid and absent | Current translation/handler and precise exception before effects, including already backpatched site. L3 |
| Mid-block mapping change | TLBWI/TLBWR or constant/nonconstant MTC0 EntryHi followed immediately by constant load/store or branch into changed code VPN | Dispatch/revalidation occurs before stale access/fetch; current ASID permission and pending target correct. Include update in delay slot or classify unsupported path explicitly. L3 |
| Guard bypass through links | Compile source before target, patch later; remap target and source separately; delete/recompile each owner; same-page and cross-page variants | Every surviving link selects current validated target, stale-source records never patch new owner. L2/L3 |
| Untracked writer | Compile, dormancy by unmapping alias, then actual EE store, host/manual SMC, DMA/IOP transfer to EE RAM, debugger/cheat/patch, bulk restore | Relevant source epoch changes or variant retired even while dormant; cross-page/misaligned/128-bit/bursts included. Each real producer L3; state load L2/L3 |
| Content snapshot race | Change bytes while validating or draining dirty queue; write old bytes back; repeat event and force queue overflow | Either a coherent validated snapshot or fail-closed retirement; no lost invalidation. Demonstrate actual producer synchronization. L3 |
| Host-code lifetime | Pause generated helper or fault thunk with return into retired region, request retire/reuse, then resume | Reuse deferred until unwound; no stale link/backpatch IP reaches reused address. L3; Windows host-fault path required |
| Generation/load ABA | Small-width test epoch wraps; state load restores same guest bytes/ASID after host arena reset; RAM/mode changes | Old host handles rejected even if numeric content/map tag matches. L1 model + L2 lifecycle + L3 reentry |
| Projection equality/precedence | Seeded 48-slot writes: identical TLBWR, hidden higher/lower slots, D/V/global, ASID reuse, large masks, SPR, wired/random changes | Optimized effective projection and side-effect schedule match full selector/reference; unchanged backing does not suppress changed permission. L1/L2 |
| Native regression | Native BIOS/OSDSYS/retail boot, SMC-sensitive homebrew, interpreter/JIT switch and savestate, supported RAM modes | Current 64 KiB behavior, exceptions, links and normal-game speed preserved. L3/L4 |

## Ranked follow-up queue and acceptance measurements

1. **Obtain actual mutation and phase costs.** Complete coordinated A/E metrics
   checkpoint, inspect final symbols/revision, then obtain authorized Windows
   results with matched image clone, trace settings and guest stage. Historical
   2.17M requests and ten resets make this urgent; neither number tells how many
   useful live blocks were evicted. Report real seconds, guest cycles, sync
   phase times, actual removed blocks/body bytes, emitted bytes, repeat compiles,
   patch stores, host map/protect calls, backpatch activity, reset reason and RSS.
2. **Finish bounded no-op projection and idempotent patch foundations.** Their
   correctness surfaces are narrow and guest-op cost can remain zero. Measure
   exact projection-cache hit rate and skipped actual patch stores. Do not count
   unchanged page visits as whole no-op hits. D-only/presence-filter behavior
   requires L2/L3 verification before describing it as a validated improvement.
3. **Close noncontiguous source and mid-block safepoint holes.** These outrank
   speculative performance because the v25 source dependency hole is explicit.
   Complete source review and prospective assertions, then L3 branch/delay/data
   remap tests. Until then retain all conservative mapping invalidations.
4. **Establish ownership and complete producer barriers.** Inventory embedded
   data translations and every real EE-RAM instruction writer; add stable link/
   backpatch/variant owner handles before deleting outgoing records or reusing
   host bytes. Measure stale-source record volume to choose cleanup scope.
5. **Prototype the bounded dormant-variant design only if baseline repeat
   compilation justifies it.** Compare actual compile time/emission avoided to
   new entry/data lookup cost. Require all falsifiers and native performance.
   If unsafe or unproductive, keep foundations and report exact failing gate.
6. **Choose region/clock reclamation only if live/dead/fragmentation evidence
   justifies it.** Ten historical resets motivate the question, not the answer.
   Report peak live body+thunk/stub bytes, dead bytes, owner fanout and recycle
   latency. No free-on-clear, blind ring overwrite or unsupported relocation.
7. **Then optimize memory exception exits/tiering/range repair independently.**
   Profile compile versus execution cost first. Keep D/BD/EPC precision and
   native 64 KiB speed as hard gates. Revisit compact projections/entry reverse
   ranges if measured projection time remains material after B1.

Within-sync dedup remains correctly bounded but ranks below these items because
its measured saving was only ~0.008%. All other relative performance priorities
are provisional until actual eviction/time/reuse metrics arrive. Any source-only
checkpoint must retain the L0/L1-source versus executed L2–L4 distinction, name
its final revision, and state that FPS, native speed and boot correctness remain
unmeasured. No merge into `testing` is justified by this matrix alone.


## Final source status for this mission checkpoint

The coordinated implementation now includes bounded actual eviction/mutation and
compile/source-repeat diagnostics; exact link/dispatch patch-store deduplication;
a normalized 48-slot projection memo and production diff planner; fine-mode
branch-slot interpreter boundaries and TLBR/TLBWI/TLBWR/EntryHi continuation
boundaries; reset/load/RAM projection invalidation; and 34 prospective C++ cases.
The [v27 handoff](PSBBN_CODEX_EE_JIT_COHERENCE_INVARIANTS_20261021_v27.md)
contains the revision ledger, telemetry caveats and exact coverage levels.

This changes B1/E1 and scoped D/I foundations from implementation candidates to
source checkpoints, **not runtime-validated wins**. The remaining `recDI` eager
following-instruction case prevents a complete single-source-page claim. D-only
production-store proof, constant data translation ownership, manual/external
writer barriers, outgoing-link/backpatch ownership, savestates and native speed
remain unverified. No retained variants or host-byte recycling are enabled.
Queue items 1–3 therefore mean collect executable evidence and close these exact
gaps, rather than repeat completed source implementation. All L2/L3/L4 acceptance
gates and rejected shortcuts above still apply.
