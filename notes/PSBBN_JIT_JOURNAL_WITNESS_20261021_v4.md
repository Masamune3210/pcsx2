# PSBBN journal JIT fault: confirmed four-phase witness, 2026-10-21

## Scope and test provenance
- Source branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
- Windows PCSX2 `v2.9.92-191-gb59ccfd14` in uploaded `emulog(20261021-004822-1).txt`; this is precisely the build with corrected PC guards and FLUSH_EVERYTHING trace snapshots.
- Companion `emulog(20261021-004822).txt` is an unrelated ARMSX2-NX/Nintendo Switch startup log (no configured BIOS). Do not attribute it to this PSBBN boot.
- The Windows run uses HDD image `F:\Emulation\Emulation Files\pcsx2\bbntest.raw`, with EE/iR5900 recompiler enabled.

## Decisive runtime witness (Windows log lines 41939–41942)
```text
phase=0 guestPC=800b1578 ea=c00052dc v0=c0005290 v1=c0005000 archMapped=0 vmapHandler=00000000
phase=1 guestPC=800b1578 ea=c00052dc v0=c0005290 v1=c0005000 archMapped=0 vmapHandler=00000000
phase=2 guestPC=800b1580 ea=c00052dc v0=c0005000 v1=c0005000 archMapped=0 vmapHandler=00000000
phase=3 guestPC=800b1580 ea=c00052dc v0=c0005000 v1=00000000 archMapped=0 vmapHandler=00000000
```
Guest writes `C0005290` to `C00052DC` in `SW v0,0x2dc(v1)` at `800B1578`, then `LW v1,0x2dc(v0)` at `800B1580` returns zero. No architectural active-TLB entry and no vTLB direct mapping exist during either access. The first miss was logged earlier by `vtlb_Miss`; previous misses during journal memset start `C0005000` and reach `C00053DC` by cap. Guest kernel pointer assignments are not invented; they are real runtime registers.

## Cause and priority
- The current `pcsx2/vtlb.cpp::vtlb_Miss()` handles interpreter misses via `cpuTlbMissW/R` and `CancelInstruction`, but the JIT path only reports/pauses and returns. The JIT's unmapped virtual read handler returns zero; unmapped virtual write handler returns without writing.
- Thus the missing guest TLB refill exception + instruction retry is a confirmed architectural JIT correctness deficiency causing the observed zero read. Do not map or patch the guest `C0005xxx` region artificially; it is a KSEG2/MMU translation managed by Sony's guest Linux.
- Correct JIT exception delivery requires exact memory-op PC, delay-slot/BD state, guest GPR/XMM/constant register synchronization, and immediate bailout before subsequent guest operations. `cpuTlbMiss()` expects its input `cpuRegs.pc` to point *after* the faulting instruction and subtracts 4. Returning from the virtual read/write handler after reporting is incorrect.
- The Linux log still reports `journal-2005` and `VFS: Unable to mount root fs`; earlier successful interpreter boot versus failing JIT mode is the same divergence.
- Instrumentation fix in `b59ccfd14` is verified by the four-phase trace; no memory exception fix is proven until another Windows boot.

## Remaining validation
1. Implement a bounded/accurate JIT TLB exception path at a generic instruction/memory access layer, not guest-specific address patch.
2. Verify Linux takes TLB refill (BadVAddr / Context / EntryHi, EPC, vector), retries journal memset and SW/LW correctly, mounts /dev/hda1.
3. Check performance and optimize register state barriers, but never make invalid guest memory silently succeed.
4. Preserve DEV9 ATA-4 work and interpreter-only TLB Dirty-bit semantics.

## Ghidra metadata (confirmed in earlier analysis)
- `0x800B1348 -> ReiserfsJournalInit`
- `0x800ADBA0 -> ReiserfsAllocateListBitmaps`
- `0x800ADC70 -> ReiserfsGetListBitmap`
- `0x8002B300 -> KernelPanicHaltLoop`
- Existing runnable Java Ghidra script is maintained in `Masamune3210/ps2bbndecomp` under `ghidra_scripts/ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java`. No new Ghidra datatype changes inferred from this run.
