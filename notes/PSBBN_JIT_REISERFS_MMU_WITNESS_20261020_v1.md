# PSBBN JIT ReiserFS / EE vTLB fault witness (2026-10-20)

## Branch purpose
Branch `note-working/psbbn-jit-reiserfs-mmu-witness-20261020` starts at `note-working/dev9-ata4-hdd-audit-20261019` (base `e31d514cd123e78de47d3295262b08d7695a8b11`). Preserve the ATA-4, FIFO, Linux printk, interpreter TLB and native observer work. Identify the first JIT memory-exception divergence responsible for PSBBN's early ReiserFS journal failure. **No production fix or guest workaround has been applied.**

## Established evidence and code audit
* Run9 and clean install run10 under EE JIT both reached Linux, detected `hda1 hda2 hda4 hda5 hda6 hda7`, then logged `journal-2005, get_list_bitmap failed for journal list 0` followed by root mount failure on `03:01`, and entered `0x8002B300` panic halt loop.
* Earlier EE interpreter successfully mounted `/dev/hda1` but eventually failed mounting a *different* partition `/dev/hda4` during ATA MULTWRITE/DRQ errors. Those are distinct failures. Runs were not from proven byte-identical immutable HDD baselines.
* The fresh-install JIT log emitted 50 TLB store misses `0xC0005000..0xC00050C4` sampled near guest `memset` at `0x801A4930`, just before the first panic capture; the sampled JIT PC is **not an exact faulting instruction PC**.
* Current fork `pcsx2/vtlb.cpp`: `vtlbUnmappedVWriteSm/Lg` call `vtlb_Miss(addr, 1)` and do not perform a backing write. In `vtlb_Miss`, the interpreter calls `cpuTlbMissW(addr, cpuRegs.branch)` and `Cpu->CancelInstruction()`. The recompiler instead may log the event or pause, **without delivering a guest exception or retrying that specific store**. The source itself notes that JIT mid-block exception handling cannot currently trust the PC because indirect memory ops do not flush it. This is a real source-level correctness gap, not yet a demonstrated cause of the exact ReiserFS bitmap-slot contents.
* Run10's saved actual compiled kernel bytes identify:
  * `0x800B1348` ReiserfsJournalInit; clears a `0x115C0`-byte journal, initializes bitmap slots.
  * `0x800ADBA0` ReiserfsAllocateListBitmaps; five 8-byte `JournalBitmapSlot` entries at `journal+0x290`; zeroes each `journalList` owner at `0x800ADC08`.
  * `0x800ADC70` ReiserfsGetListBitmap; cyclic slot index `journal+0x64`, compares owner at `0x800ADCE4`; returns NULL if still occupied at `0x800ADD1C`.
  * `0x800B156C` journal-init call to GetListBitmap, `0x800B1590` emits journal-2005.
* Runnable Ghidra metadata script already exists in `Masamune3210/ps2bbndecomp`, `ghidra_scripts/ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java`. No need to recreate it. Execution in Ghidra remains unverified.

## Required witness before changing JIT code
1. Preserve an after-install/pre-boot HDD baseline when possible. Use **separate byte-identical writable clones** for EE interpreter versus JIT, identical BIOS/build/DEV9 config and settings except EE mode. Avoid repairing any image or reusing an image after guest writes as if it were pristine.
2. In JIT, configure `PauseOnTLBMiss` to pause at the **first** suspect `C0005xxx` write. This is a diagnostic, not a fix. Record guest virtual address, store width/value, accurate guest instruction PC (current `cpuRegs.pc` can be stale inside a JIT block), CP0 Status/Cause/EPC/EntryHi/BadVAddr and which TLB mapping was active. Crucially identify the actual JIT load/store site before attributing the store to the journal.
3. Follow the compiled guest journal path with standard EE execution breakpoints at `0x800B1348, 0x800B1470, 0x800B156C, 0x800ADC70, 0x800ADCE4, 0x800ADD1C, 0x800B1590`. Stop **before** journal memory can be freed, not after the panic.
4. Record `sb`, `*(sb+0x11C)` journal pointer, `journal+0x64` slot index, and the five words `*(journal+0x290+8*i)` (owner) and `*(journal+0x294+8*i)` (bitmap pointer), for i=0..4, at corresponding interpreter and JIT milestones. Observe **guest physical mapping**, but never assume `ppmap` exists: the earlier optional-LUT dereference caused a native AV.
5. A nonzero owner during the first lookup can result from initialization stores not arriving, later overwrite, an incorrect journal pointer, an incorrect index, or a genuine earlier acquisition. Correlate the **first wrong value** with the *exact* executed load/store and TLB/ASID state. If the first lookup succeeds but a subsequent lookup fails, revise the reentrancy hypothesis.
6. Avoid simply inserting `cpuTlbMissW` into the JIT `vtlb_Miss` path. Correct delivery must restore per-instruction PC, branch-delay/EPC semantics, cancel the pending translated store, flush/register sync as necessary and restart at the correct guest exception vector. An ill-positioned exception would create a different bug.

## Diagnostic import
The accompanying `tools/PSBBN_Reiserfs_JIT_breakpoints_20261020_v1.csv` uses the **existing** PCSX2 Qt Debugger Breakpoints-view `Paste from CSV` format. It imports ordinary enabled, pausing EE execution breakpoints (not auto-logging instrumentation); the CSV importer explicitly does *not* import `instrumentationEnabled`, `logFormat`, or `continueOnHit`. Use the **EE** debugger and paste all CSV text (including header) using the context menu. Do not attempt to import it into IOP. The breakpoints can be configured individually for logging/continuation in the GUI if desired. PCSX2 source support inspected; GUI/runtime not tested here.

## Verification and next step
Source-only investigation; no PCSX2 compilation, native tests, Ghidra execution or guest boot performed in this work. Savestate binaries referred to in earlier handoffs are not mounted in the current conversation container; conclusions about runtime follow verified prior extraction notes, not a new snapshot read. Next step: capture the five slot owners **at `0x800ADD1C` before freeing the journal**, plus the exact virtual->physical mapping of the first store miss, then implement an architectural JIT MMU/exception correction supported by that witness.
