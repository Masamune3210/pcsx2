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
Branch created for systematic source audit. No production behavior changed yet. Source and test review pending.

## Next step
Inspect only `pcsx2/DEV9/ATA/**`, its `DEV9.cpp` bridge, HDD file I/O and existing DEV9 tests. Record definite defects with code addresses and prioritized reproduction; implement independently verifiable corrections with tests. Run Windows MSBuild `Release AVX2|x64` and PSBBN cold boot only when candidate changes are ready; until then verification is source review only.
