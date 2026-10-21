# PSBBN EE JIT: experimental precise guest TLBL/TLBS recovery (2026-10-21 v5)

## State / branch

Existing branch `note-working/psbbn-jit-reiserfs-mmu-witness-20261020` now has two implementation commits:
- `4884a15d025a264905282253030795e7d69a374a` updates `pcsx2/x86/ix86-32/iR5900.cpp` to capture architecturally current guest state before recompiled memory-class instructions and provide `recHandlePreciseTlbMiss()` to abort host execution after guest exception injection.
- `68e68e11ff7283bdf1b4305366866b34c2ee791c` updates `pcsx2/vtlb.cpp` so a JIT miss with the precise memory context armed calls the new exception-bailout function instead of reporting and returning zero/dropping stores.
- Previous `f558bd9df8684ea7244f98a90e454a69b6c4ade6` documents the exact four-phase witness from uploaded Windows `emulog(20261021-004822-1).txt`: source store `C0005290` into `C00052DC`, same virtual address read back as zero, no active TLB mapping or direct vTLB mapping in JIT.

## Why this path is necessary

The observed Linux KSEG2 access must trigger a guest TLB refill exception. Explicit host mapping of `0xC00052DC` would conceal the underlying MMU defect. The guest code and disk contents are not being patched.

Upstream JIT `vtlb_Miss` logged but did not deliver `cpuTlbMissR/W`, and the virtual handler returned a fabricated zero/dropped the write. The interpreter path already correctly raises the guest exception. The JIT did not have a reliable guest PC/register snapshot when an indirect memory handler ran inside a translated block.

## Implementation details

- In `recompileNextInstruction`, on MIPS primary memory-class opcodes `0x20..0x3f`, `0x1a`, `0x1b`, `0x1e`, `0x1f`, emit `iFlushCall(FLUSH_INTERPRETER)` *before* emitting the translated opcode. This flushes guest GPR/XMM/constant state, opcode and PC. Mark a runtime `g_recPreciseMemoryFaultArmed` guard and the exact branch delay state.
- `recompileNextInstruction` already increments the compilation `pc` before normal opcode emission. For branch-delay-slot compilation it does not; explicitly use `pc+4` for a delay-slot fault because `cpuTlbMiss()` subtracts four before calculating EPC and sets BD based on the passed delay-slot indicator.
- On a JIT vTLB miss with guard armed, call `cpuTlbMissW/R(addr, cpuRegs.IsDelaySlot)` then `recExitExecution()` (fastjmp back to the EE core). This prevents guest execution continuing after a failed memory operation. The guest TLB-refill exception handler then has the chance to map and retry the original instruction.
- Clear the context on normal instruction completion, when a fault is injected, and at `recExecute` entry. JIT misses outside a known synchronized memory instruction retain the previous diagnostic fallback instead of unsafely injecting a fault with stale registers.
- `vtlb.cpp`'s JIT extension is guarded by x86-64 platform macros; other architectures retain their existing implementations. The interpreter branch remains unchanged.
- The existing PCSX2 `PauseOnTLBMiss` setting is not used as an enable/disable switch for this correctness path.

## Verification status

**Implementation committed, compilation and runtime UNVERIFIED in this environment.** No Windows executable was built, and root mounting has not been confirmed. This correctness-first implementation flushes registers at each memory-class instruction and may substantially slow down JIT code. It should remain on this investigation branch until correctness and speed are measured. Do not represent this as a final optimized fix. Benchmark it against earlier EE JIT and interpreter runs, then investigate a lazy/exception-side state spill to eliminate full guest-state flushes on the common mapped memory path.

## Next required Windows test

1. Rebuild `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`, keeping default EE JIT, matching HDD image, and existing Sony Linux printk-ring observer. Start from cold boot rather than a legacy post-panic savestate.
2. Confirm a JIT access to `0xC0005000` produces `cpuTlbMiss` and the PSBBN exception/refill witness with `BadVAddr=C0005000` (or the first unmapped KSEG2 address), then an appropriate TLB write and successful retry. No more infinite zero-on-read without guest exception.
3. Confirm root `/dev/hda1` ReiserFS mounts, and then whether the next interpreter issue `/dev/hda4` / ATA WRITE MULTIPLE remains resolved by the preserved DEV9 ATA branch. Report real runtime result rather than assuming success.
4. If the guest hangs in repeated refill or crashes, inspect exact `EPC`, `Cause.BD`, `Status.EXL`, `BadVAddr`, `EntryHi`, plus JIT guest `PC` and active ASID. Verify host reg flushing and branch-delay handling first.
5. Measure EE speed: if significantly worse, optimize around a conditional slow handler/deoptimization pathway, while preserving equivalent precise exception semantics.

## Ghidra database

No new metadata for the Sony binary was inferred from this implementation. Previously verified structure/function metadata and `ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java` remain under `Masamune3210/ps2bbndecomp`; no duplicate script necessary.