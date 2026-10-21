# Current objective continued: standard-game protection, cache isolation, and VM lifecycle (v32)

Repository: Masamune3210/pcsx2
Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Read first: [v31 execution-profile objective](PSBBN_EE_EXECUTION_PROFILE_ISOLATION_20261021_v31.md), then this v32 checkpoint.
Do not create a new branch. Do not merge to `testing`. All C++/GoogleTest edits below remain **UNBUILT AND UNEXECUTED**.

## What was done this session

1. Compared the specific baseline `testing` implementations against current:
   - `pcsx2/COP0.cpp::MapTLB/UnmapTLB`: baseline directly adds/removes every selected guest TLB entry's virtual aliases, clearing `Cpu->Clear` on removal; current central `SyncActiveTLBMappings` projects all 48 entries under the selected ASID and reconciles per-page changes. This is a major **shared architectural change** inherited before v16, not a detachable two-line optimization.
   - `pcsx2/x86/ix86-32/iR5900.cpp::recClear`: baseline translates input VPC via `HWADDR` then directly visits `BaseBlocks` metadata; current uses `recClearPhysical`, actual physical code cache identity, a presence bitmap, cause telemetry, and a new physical invalidation routine. Crucially, the presence-bit filter was previously shared with retail games.
   - `pcsx2/vtlb.cpp::vtlb_VMapUnmap/vtlb_Reset`: baseline removes fastmem mappings and restores virtual handler, without a `ppmap` identity sentinel or explicit `ResetActiveTLBMappings`; current does both to prevent stale translated physical aliases. These changes require an architectural mapping test before deciding whether they are common hardware fixes or Extended-only.
   - `iR5900.cpp::recError`: baseline treats unhandled JIT LUT page fetch as host error/pause, while current delivers valid guest instruction TLB exceptions and may enable 4 KiB dispatch on demand. Forcing Standard back to the host error would break any ordinary title legitimately fetching mapped guest code. **Not gated**.
   - `BaseblockEx.h::BaseBlocks::Remove` and `PatchLink`: baseline writes every redirect displacement without checking; current computes identical displacement and avoids redundant patch writes. This is a shared mutation optimization; it remains common pending a direct-link/invalidation ownership review and generated JIT tests. Old `links` metadata may outlive its source.
2. Isolated two additional optional cache optimizations:
   - `iR5900.cpp::recClearPhysical` now bypasses a full physical invalidation **only under Extended**, and only when the generation-local conservative compiled-page bitmap says the page has never held any compiled block. **Standard always reaches `EERecClearPhysicalBlocks`** after the same bounds checks, regardless of the bitmap result. The bitmap marking machinery remains allocated and maintained in both modes for now. `EECanSkipPhysicalCodeClear` is a pure policy function in `EEExecutionProfile.h`; a direct unit test covers its four combinations.
   - `COP0.cpp::SyncActiveTLBMappings` now only suppresses duplicate physical clear requests within one sync under **Extended**. Standard always reaches `ClearActiveTLBCodePage` for mapping changes; no dependency on the dedup set in Standard. Historical v22 run saw only 177 avoided duplicate requests of roughly 2.2 million, so this is negligible for PSBBN performance.
   - Existing v31 gates remain: Extended-only full 48-slot TLB projection memoization and D-only permission transition shortcut; Standard recomputes projection and invalidates/reinstalls permission changes conservatively.
3. Fixed an actual profile lifecycle defect:
   - v31 placed `EEExecutionProfile::ResetAtShutdown()` only in the `Initialize` scoped failure guard `close_state`. `close_state.Cancel()` executes on successful initialization, so normal shutdown did **NOT** clear the latch. This could carry Extended selection from BB Navigator to the next ordinary game boot and reject its subsequent `SelectAtBoot`.
   - Now `VMManager::Shutdown()` resets the profile when a fully initialized VM actually tears down, and `VMManager::Internal::CPUThreadShutdown()` resets defensively when the CPU thread exits. The startup failure guard remains as before. `VMManager::Reset()` within one session deliberately does **NOT** toggle the policy, since changing policy while JIT code is live is unsafe.
   - Added a consecutive-VM-session unit test showing Extended from one boot must not leak into Standard of the next. This tests the pure lifecycle API, not actual VM teardown. VM teardown remains unexecuted.
4. Reviewed the branch commit history and source, not merely the last two Codex commits. Main comparison ranges remain `testing..current` (~269 ahead at v31) and `note-working/psbbn-tlb-boot-20261017..current` (142 ahead at v31), with unrelated DEV9/ATA and UI debug changes deliberately outside this EE policy effort.

## Isolation matrix as of v32

| Mechanism | Standard | Extended | Remaining risk |
|---|---|---|---|
| Boot/session selection, exact BB Navigator disc serial, manual INI override | Default | Auto for `SCPN-60160`; explicit `Extended` for HDD-only | Boot identity and settings integration not run |
| TLB full active projection generation | Always rebuild | Memoize identical input | Shared projection model differs radically from `testing` |
| D-only mapping permission transition | Remove/reinstall/clear | Permission-only shortcut | Generated store D-bit tests still pending |
| Within-sync physical invalidation dedup | Disabled | Enabled | No performance gains promised |
| Never-compiled physical-page negative invalidation filter | Disabled | Enabled | Filter soundness and noncontiguous source-page dependencies still require generated-code tests |
| Physical `recClearPhysical` and BaseBlocks invalidator | Shared | Shared | Larger ASID alias and SMC coherence differences from `testing` |
| On-demand 4 KiB guest instruction dispatch | Still demand-driven | Still demand-driven | Default games that really use mapped instruction pages must keep working |
| RecDI/branch interpreter boundary bridge | Fine dispatch only | Fine dispatch only | Untested guest EPC/BD, interrupt timing and Jak X compatibility |
| Constant data host pointer specialization | Shared | Shared + trace-gated accounting | Unsafe basis for dormant reuse until invalidation dependencies proven |
| JIT link patches and backpatch lifetime | Shared | Shared | Old source link ownership and savestate resets |
| Accurate guest TLB faults, Random, D permissions, external RAM writers | Hardware correctness candidate, common | Hardware correctness candidate, common | Tests/reference needed before selectively gating |
| DEV9 HDD/ATA/ISO/UI printk diagnostics | Separate subsystem | Separate subsystem | No EE profile coupling |

**Standard does not yet mean “byte-for-byte `testing`.”** It now excludes four optional shortcuts (memoization, D-only, dedup, never-compiled negative filter), but still includes deep shared EE behavioral changes. “Standard” should never be advertised as a production regression-free profile until executable A/B evidence exists.

## New regression tests (not run)

- `EEExecutionProfile.StandardNeverSuppressesPhysicalClearFromNegativePageFilter`.
- `EEExecutionProfile.ConsecutiveVMSessionsDoNotInheritEarlierBootSelection`.
- Existing v31 pure-profile selection, boot latch, and TLB planner tests remain.
- No host x86 generated-code or Windows MSVC tests were run in this session.

## Next objective and decisions

1. **Executable profile bootstrap test**: Build MSVC `Release AVX2|x64`, run `EEActiveTLBNative` and targeted `core_test` (profile, COP0 active-map, physical presence and DI bridge). Do not ask the user to test until at their computer. Separate “source reviewed” from “compiled” and “booted”.
2. **Actual VM lifecycle test**: Validate two consecutive boots in the *same process* BB Navigator/Extended -> retail/Standard. Check failed boot, soft reset, disc/ELF swap and savestate restore, not just standalone atomic policy functions. Ensure the boot log prints a newly selected Standard profile after teardown.
3. **Savestate policy**: currently host-only profile selection is NOT serialized in state metadata; loading under different session profiles is an unresolved compatibility case. Define safe metadata binding or explicit mismatch policy; all JIT/fastmem pointers must be rebuilt after load.
4. **Source-callsite safety audit**: compare `testing`, predecessor, current on code-cache link patching, physical JIT block identity, `PSM/HWADDR` virtual translation, `vtlb_VMap` and inter-CPU/DMA writers. Only gate further when proof shows it is experimental, not real hardware correctness. Provide a source-to-tests matrix and named commits.
5. **Hard-stop** on dormant code reuse or arena reclaim until all instruction and data page dependencies, exact write barriers, direct links, helper-return lifetimes, and savestates are proven.
6. **Runtime A/B**: compare Standard/Extended in SAME executable with ordinary game and PSBBN HDD clone; measure FPS and shared invalidation correctness, not just number of skipped calls. No compilation or launch was attempted here.

User action required **only later**, for Windows build and game/PSBBN testing. All work described here was completed by committed source changes and targeted GitHub review. Continue independently with next source-callsite audit or tests while user is away.
