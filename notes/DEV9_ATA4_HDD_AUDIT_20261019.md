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
- Command coverage matrix vs relevant ATA-4 host semantics and the image-backed virtual drive model, Sony-specific commands separated; IDENTIFY DEVICE must accurately describe implemented emulated-drive features rather than reflecting a nonexistent physical host disk. Consider later ATA revision bits independently, not as an automatic reason to restrict capacity or feature emulation.
- Tests for real sector persistence, transfer word counts, multiblock boundaries, zero sector-count semantics, DRQ/IRQ transitions, reset and invalid LBA.

## Current state

Production fixes are checkpointed at `58dc04a4fe` (FIFO) and `874309ca1c`
(ATA state, PIO writes, I/O ordering/errors, power, parameters and SMART).
The final lifecycle checkpoint adds closed-image command rejection, FIFO
cancellation generation tracking, and HDD-enabled save/load preflight rejection.
See the [implementation ledger](DEV9_ATA_IMPLEMENTATION_20261020.md),
[current 48-family matrix](DEV9_ATA4_COMMAND_MATRIX_20261019.md) and
[256-byte inventory](DEV9_ATA4_OPCODE_INVENTORY_20261019.csv).
All 23 compared mandatory LBA-only encodings now dispatch. This is source coverage,
not runtime conformance or proof of PSBBN recovery.

The tree contains 40 prospective ATA disposable-image cases and seven registered
FIFO cases. Whitespace and source inventory/bridge checks plus independent review
were performed. **No compilation, CMake configuration, C++ tests, Windows build,
sanitizers or guest boot ran.** The Ghidra extension remains untouched.

## Next verification

When builds are authorized, follow the [regression instructions](../tests/ctest/ata/README.md),
then build the user's Windows `PCSX2_qt.slnx` in `Release AVX2|x64` and cold boot a
disposable PSBBN clone. Check root and hda4 mounts, journal replay, and remaining
DMA timeouts. Force-break hardware behavior and host write/flush-failure injection
remain unverified. The `.ata-settings` sidecar preserves SMART configuration and
must accompany a moved/cloned image for identical settings.

HDD-enabled save/load now fails before guest-state mutation; existing HDD states
cannot safely restore the external disk. See the [policy and compatibility limits](DEV9_ATA_SAVESTATE_POLICY_20261020.md).
Full device/image snapshot restore and undocumented Sony authentication remain
unimplemented. Disabling HDD is not a valid way to recover an old HDD guest state.

## Implementation checkpoint, 2026-10-20

The user has now requested fixes. The FIFO checkpoint corrects the wrapped IOP
write destination and guards invalid residual/occupancy lengths, signed counter
rollover, and second callbacks after ATA completion. It adds a bridge cancellation
helper for ATA reset integration and seven prospective production-function tests.
See [FIFO rationale](DEV9_FIFO_FIX_20261020.md) and the
[implementation plan](DEV9_ATA_FIX_PLAN_20261020.md). Whitespace/source inspection
checks passed; C++ tests are uncompiled and unrun under the no-compilation rule.
ATA state-machine and backing-file fixes are in progress separately.

## ATA implementation checkpoint, 2026-10-20

Production corrections now cover the PIO write state machine, accepted-block
persistence across reset, DMA bounds/lifecycle, EOF/CHS, coherent queued reads,
durable flush and guest-visible host errors, parameters, IRQ/HOB/selection,
mandatory aliases/power commands, SMART metadata persistence and IDENTIFY claims.
See [implementation ledger and remaining risks](DEV9_ATA_IMPLEMENTATION_20261020.md).
All23 compared mandatory LBA-only opcode bytes now dispatch; this is source
coverage, not runtime conformance. Added39 prospective ATA integration cases and
registered seven FIFO cases. Source checks/independent review only; **no compiling
or runtime execution**. Save-state safety policy is a separate pending checkpoint.

## Final lifecycle checkpoint, 2026-10-20

Closed-image writes are ignored even when Ethernet keeps DEV9 decoding enabled;
close clears transfer/IRQ/FIFO state. FIFO callbacks apply progress only if their
bridge generation remains current, preventing a deferred host error's cancellation
from being overwritten by a returning DMA callback. Added a prospective closed-image
regression, bringing ATA cases to 40. HDD save/load preflight rejects unsupported
restoration before capture/load mutation or the public load-failure reset path.
SaveState/VMManager edits are the necessary direct integration for finding F13;
there are no EE/JIT/kernel or image-format changes. Full F13 snapshot restore remains
a design gap, explicitly bounded by the preflight policy. No runtime validation.

Final error-code review also distinguishes read UNC from write DF/ABRT; write
bit6 would incorrectly imply write protection in ATA-4. This was checked against
the retrieved draft's read/write error-output clauses, not executed on hardware.
