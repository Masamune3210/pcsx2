# EE JIT coherence: DI boundary and embedded data dependencies (v30)

Repository: `Masamune3210/pcsx2`
Existing branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Previous mission results: `notes/PSBBN_CODEX_EE_JIT_COHERENCE_INVARIANTS_20261021_v27.md`, writer audit v28 and mechanism matrix v29.

**Status: Source committed and reviewed, NOT compiled, tested, booted or benchmarked.** User cannot access the Windows test computer right now. This note supersedes the v27 ranked queue specifically for the DI source-boundary item; it does not mark other coherence gates complete. Keep the native PS2 64 KiB dispatcher, retail-game compatibility and savestates as acceptance gates. Do not merge into `testing`.

## What changed and why

### 1. Close recDI's eager second-page source dependency

`pcsx2/x86/iCOP0.cpp::recDI()` preserves an old compatibility behavior: it compiles the instruction *after* DI before emitting DI's EIE-clearing operation. The original comment names Jak X, several SpongeBob games and other titles. When DI occupies guest page offset `0xFFC`, its eager compilation reads `nextPC = DI_PC + 4` from a different guest 4 KiB page. A fine-mode translation may map this next page to a noncontiguous physical PFN or leave it unmapped. `BaseBlocks` metadata represents the block as a single physically contiguous range, so recDI could silently add an untracked physical instruction source (or dereference an inaccessible page). v27 already identified this hole.

Implemented:
- `EERecNeedsRuntimeDINextInstruction(fineMode, nextPC, inDelaySlot)` in `pcsx2/x86/EERecCoherence.h`, and `recNeedsDIBoundaryContinuation()` in `iR5900.cpp`. Only returns true for **fine dispatch**, a DI not already in a branch delay slot, and a next PC at the start of a guest 4 KiB page. No native mode change.
- `recDI` emits a flushed host call to `EEInterpretDINextInstructionFromJIT(nextPC)` and finishes the block with `g_branch=2`. It does NOT eagerly compile the next virtual instruction or physically extend the metadata past DI. Regular same-page recDI retains the original emitter/compatibility ordering.
- The new helper in `pcsx2/Interpreter.cpp` briefly selects the existing EE interpreter, fetches and executes the deferred opcode at guest runtime, then calls the interpreter's existing `COP0::DI()` to change EIE **after** that opcode. Guest TLB faults escape via the same private `fastjmp` cancellation path as the existing cross-page branch bridge and return to JIT dispatch after the interpreter CPU provider is restored. An untaken ordinary branch executes its delay slot before DI; an untaken likely branch annuls it; a taken branch already executes the slot in its interpreter branch helper. Branch-slot EPC/BD is intended to use the interpreter's native exception machinery. Event scheduling is deferred until the JIT epilogue. Shared bridge flag renamed `s_jitBoundaryInterpreterStep` to describe both clients.
- No guessed guest physical mappings, fake instruction words, new global interpreter mode, or userland-specific addresses.

Source edits: `EERecCoherence.h`, `iR5900.h`, `iR5900.cpp`, `iCOP0.cpp`, `Interpreter.cpp`. The key implementation checkpoint starts at `597238198ba0`; bridge and emitter checkpoints `df32e4731896` and `608eebfa190f`; branch-delay ordering `52bf75e5ea47`; last helper rename `5ce71b6c0c1e`.

**Important remaining validation**: The bridge is an architectural candidate, not proven behavior. It must pass emitted-code tests for a DI at 0xFFC, both guest next-page PFNs, missing next page, a memory fault within the next opcode/slot, JAL/JALR, ordinary/likely branches, precise exception EPC/BD and timing/interrupt ordering. The source-only test fixture calls the bridge directly and does not execute the emitted x86 or JIT epilogue.

### 2. Inventory constant data translations without changing generated code

`pcsx2/x86/ix86-32/recVTLB.cpp` embeds compile-time selected host data pointers or handler addresses in all three constant specializers:
- `vtlb_DynGenReadNonQuad_Const` (8/16/32/64-bit and SSE scalar);
- `vtlb_DynGenReadQuad_Const` (128-bit);
- `vtlb_DynGenWrite_Const` (8/16/32/64/128-bit including SSE).

Each has a comment assuming any COP0 TLB change clears its owner block. This is a **hard dependency barrier** to dormant code reuse. An unchanged instruction PFN is NOT proof its embedded data host pointer remains valid. There are also handler/MMIO and host SMC permission considerations.

Added `EERecConstantAddressUsesGuestTLB(address)` classifier and trace-only `recRecordConstantDataLookup(address, store, direct)` callback. At the three const specializers, it records the selected *compile-time* direct or handler mapping if `PSBBNTlbTraceEnabled()`. Reporting at existing JIT lifetime/reset summaries:

```
EE-JIT-CONSTDATA reason=... otherReadHandler=... otherReadDirect=... otherWriteHandler=... otherWriteDirect=... fineManagedReadHandler=... fineManagedReadDirect=... fineManagedWriteHandler=... fineManagedWriteDirect=...
```

All counters are **number of constant specializations compiled during this cache interval**, not executed memory operations, runtime hotness, distinct guest VPNs, potential safe reuse hits or actual invalidated blocks. They reset with other per-cache lifetime stats. `fineManaged*` means fine mode and a TLB-managed address (KUSEG or KSEG2/3); `other*` includes native and fixed KSEG0/1 addresses. This creates no guest instruction, JIT entry guard, mapping change, extra runtime data access or altered generated memory opcode. Instrumentation is disabled when COP0 trace is off.

Source: `recVTLB.cpp`, `iR5900.cpp`, `iR5900.h`, `EERecCoherence.h`. Counter checkpoints `fe79712bd2eb`, `213a21b65153`, `c5922539dae7`, and classification case `3f5c863072f1`.

## Source-level regression coverage added (8 cases, unexecuted)

- `EERecCoherencePolicy.DelayedDIFetchCrossingGuestPageUsesRuntimeInterpreter`: fine/native, boundary/nonboundary, no nested slot bridge.
- `EERecBoundaryBridge.DIContinuationExecutesMappedNextInstructionBeforeDisabling`: mapped next opcode and cycles.
- `DIContinuationUnmappedFetchKeepsEIEAndPreciseEPC`: fetch TLBL, EPC/BD, deferred DI.
- `DIContinuationUntakenBranchExecutesItsDelaySlot`: ordinary branch's deferred slot.
- `DIContinuationUntakenLikelyAnnulsSlot`: branch-likely annulment.
- `DIContinuationFaultedMemoryReadDoesNotApplyDI`: data TLBL in following instruction.
- `DIContinuationBranchSlotFaultPreservesBDAndPriorEIE`: delayed-slot EPC/BD before EIE clearing.
- `EERecCoherencePolicy.GuestTLBManagedConstantDataAddressesNeedDependenciesBeforeReuse`: managed versus fixed segments.

Test files: `tests/ctest/core/ee_rec_boundary_bridge_tests_20261021.cpp` (full `core_test` fixture) and `ee_rec_coherence_policy_tests_20261021.cpp` (native helper tests already registered by v27). No CMake/project registration changes required for these existing files. No test was actually executed.

## Latest source review

Fetched and verified exact symbols, three constant callsites (around `recVTLB.cpp:456/605/811`), JIT report/reset wiring, and all eight case declarations. Confirmed `iFlushCall(FLUSH_INTERPRETER)` marks the compile-time PC flushed, so the later `g_branch=2` epilogue should not overwrite the interpreter helper's runtime PC before virtual dispatch. Verified `GetInstruction().flags & IS_BRANCH` exists for untaken-branch classification. No compiler or dynamic validator was available in this pass; assertions above remain subject to executable verification.

## Next ranked work

1. Once Windows access is available, build branch in MSVC AVX2 and run native helper and `core_test` suites with all eight new cases. Real **emitted DI x86** plus mapped/unmapped next page test is a required separate integration proof, as are native retail-game tests for normal DI timing.
2. On a controlled PSBBN boot, compare `EE-JIT-CONSTDATA`, actual `blocksEvicted`, `retiredHostBytes`, `compileUs`, `projectionRebuildsSkipped`, `liveHostBytes`, reset reasons/count and FPS, using matching guest stages and trace settings. No gains are claimed.
3. For true dormant-code reuse, choose a **measured** strategy for fine-mode managed constant data loads/stores: runtime vTLB lookup for these cases (costs per access), OR complete registered page/handler dependencies with guards and invalidation. Test RAM -> RAM, RAM -> MMIO, RAM -> unmapped, D-only, host fastmem backpatch and mid-block remap. Keep native constant specializations unchanged unless general evidence supports a change.
4. Finish real EE instruction writer barriers (CPU/manual-mode SMC, other producers/DMA, cheat/debugger, raw host writes), generated-code link/backpatch ownership and safe unwind epochs before implementing per-PFN content epochs, dormant variants or arena reclamation. Continue previous v27/v28/v29 gates.
5. Keep recDI compatibility with the existing games named in its source comments. Do not fold the interpreter bridge into the normal hot path, disable all physical invalidations, or enlarge code cache as a purported root cause.

No provable PSBBN guest-binary symbols, signatures or Ghidra datatypes were recovered during this host EE JIT work. Existing guest metadata and its Java application script remain unchanged.
