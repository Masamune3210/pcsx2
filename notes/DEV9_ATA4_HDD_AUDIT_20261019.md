# DEV9 HDD / ATA-4 Audit
Date: 2026-10-19

## Purpose
Audit PCSX2 DEV9 ATA emulation end-to-end against baseline ATA/ATAPI-4 behavior and PS2's actual HDD command use. Source branch: `note-working/psbbn-tlb-boot-20261017` at `cee2b8475a29805ae8e996cf13d00ad57ede2ae6`. Keep the active long PSBBN EE-interpreter build untouched while auditing.

## Established from PSBBN run11 (do not rediscover)
The guest Linux 2.4.17_mvl21 mounts root /dev/hda1, then later fails a ReiserFS journal replay while mounting /dev/hda4 after IDE errors. Host log contains 27 `Unknown cmd c5`; ATA 0xC5 = WRITE MULTIPLE, absent from current command dispatcher, and `HDD_Unk` aborts it. No PIO data-out implementation exists in `ATA_CmdPIOData.cpp`; 0x30 WRITE SECTORS also absent. 0xC6 SET MULTIPLE MODE and 0xC4 READ MULTIPLE are present. An earlier `ATA DMA force break` is an independent lead. The current run11 is live; raw evidence is not in the repo. Detailed [research report](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_INTERPRETER_LIVE_RUN11_ATA_WRITE_MULTIPLE_20261020_v1.md).

## Audit scope
- ATA task-file registers, CHS/LBA28, command dispatch, IDENTIFY DEVICE advertised features/geometry and transfer modes.
- ATA-4 PIO read/write sectors and multiple DRQ batching, data register byte/word access, interrupts, abort/error and reset sequencing.
- Bus-master DMA read/write start/stop, PRD handling, forced breaks, completion interrupts and error propagation.
- HDD image reads/writes, bounds, buffered write queue, flush, filesystem persistence, synchronization, and partial/short I/O.
- DEV9/SPEED bridge register semantics and save/load state correctness.
- Command coverage matrix vs ATA-4, Sony-specific commands separated; avoid falsely advertising ATA-5+ features when emulating ATA-4 target.
- Tests for real sector persistence, transfer word counts, multiblock boundaries, zero sector-count semantics, DRQ/IRQ transitions, reset and invalid LBA.

## Current state
First source pass completed against baseline `0dcd36309877d5da59344893d61c7a374a7851e3`. No production behavior changed. See [checkpointed source findings](DEV9_ATA4_HDD_SOURCE_FINDINGS_20261019.md) for fourteen findings with evidence, priorities, reproduction plans and validation limits, and [command coverage/regression plan](DEV9_ATA4_HDD_COVERAGE_20261019.md) for the protocol inventory. User instructions: **no compiling**, document reasoning for changes, and push checkpoints to this branch. The Ghidra extension is deferred.

## Next step
Follow the correction order and test plan in the source findings. Keep fixes independently reviewable, document their reasoning, and push checkpoints. Stay within `pcsx2/DEV9/ATA/**`, its `DEV9.cpp` bridge, HDD I/O and focused tests, tracing integration callers only as needed. **Do not compile:** this current user instruction takes precedence over earlier build suggestions in the notes. Compiled regression tests, Windows MSBuild `Release AVX2|x64` and PSBBN cold boots remain unrun; source inspection does not establish a working fix or the cause of the guest panic.
