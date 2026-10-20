# DEV9 ATA-4 source audit checkpoints

Date: 2026-10-19 (America/Chicago)

## Scope and evidence

Baseline: `0dcd36309877d5da59344893d61c7a374a7851e3` on
`note-working/dev9-ata4-hdd-audit-20261019`. Line references below refer to that
baseline. This is source inspection, not a compiled test or emulator run. No
production behavior has been changed. No compilation, BIOS, HDD image, PSBBN
boot, or Ghidra extension build was used. Runtime reproductions below are plans,
not results. Findings do not establish the cause of the recorded ReiserFS panic.

Reason for this document: preserve independently reviewable evidence and proposed
validation in pushed checkpoints before making behavioral changes. The known
missing `0xC5` WRITE MULTIPLE / `0x30` WRITE SECTORS path is already established
in the [branch handoff](DEV9_ATA4_HDD_AUDIT_20261019.md); it is not a new finding.

## Checkpoint 1: command, bounds and write ordering

### F1 — High: capacity bounds exclude the last valid sector

Evidence: `ATA_State.cpp:624-647` computes `maxLBA = imageBytes / 512 - 1`,
then compares the **exclusive** transfer end `posStart + count` to that
**inclusive** last LBA. A request touching the last valid sector is marked in
error and shortened by one sector. `HDD_CanSeek()` passes count zero, so the
initial seek check can succeed before the transfer check fails.

For an eight-sector image, LBA 7 / count 1 becomes count 0 with IDNF; LBA 6 /
count 2 becomes count 1 with IDNF. LBA 6 / count 3 should transfer two valid
sectors before reporting LBA 8; it is shortened to one. These examples were
calculated using a Python transcription of the bounds arithmetic, **not by
executing the production implementation**.

Consequences extend beyond an early error: PIO stage 2 unconditionally exposes
512 bytes (`ATA_CmdPIOData.cpp:115-121`) even after count became zero; `IO_Read`
reads zero sectors in that case (`ATA_Transfer.cpp:70-72`). Thus the PIO buffer
can expose old/uninitialized read-buffer contents instead of the final sector.
DMA write allocates a zero-length buffer after that same truncation
(`ATA_CmdDMA.cpp:33-39`); its zero-length completion needs explicit coverage.

Correction rationale: keep sector capacity and end-exclusive limits in the same
coordinate system; distinguish a valid start with remaining capacity from an
invalid start. Do not simply change one comparison without tracing zero-length
transfer behavior. Also audit error-address reporting: `HDD_SetErrorAtTransferEnd`
adds `nsector` **and another one** (`ATA_Transfer.cpp:494-504`). Bounds correction
and error-LBA correction must agree on the first inaccessible sector.

Planned validation: sentinel-pattern image; first/penultimate/final/one-past-final
LBA; count 1, count 2, and zero-as-256; PIO read, DMA read/write and READ VERIFY;
assert exact data length, no stale data, unchanged adjacent sectors, and error
task-file address. Include an image with an incomplete trailing sector.

### F2 — High: cached writes can be bypassed by a following read

Evidence: `PostCmdDMADataFromHost` enqueues the write and reports completion when
the cache is enabled (`ATA_CmdDMA.cpp:48-73`). `HDD_ReadSync` pauses `ioWrite`,
waits for the worker to be idle, then calls `IO_Read` directly
(`ATA_Transfer.cpp:429-473`). It neither drains `writeQueue` nor overlays queued
data. `IO_Read` reads only the backing file (`ATA_Transfer.cpp:59-81`).

Admissible ordering: the emulator queues write A and signals the worker; before
the worker starts A, the next read acquires `ioMutex`, clears `ioWrite`, sees the
worker idle and reads A's sector from the old file. The paused writer is resumed
only after that read. If the worker was already writing A, the idle wait protects
that write; it does not protect additional queued entries. This is a missing
read-after-write ordering guarantee, not evidence of a measured timing frequency.

Correction rationale: command completion with write caching may precede media
completion, but subsequent reads must observe accepted writes in command order.
Serialize a read behind all earlier writes, or provide coherent queued-write
forwarding. The chosen approach must preserve last-write-wins for overlapping
writes and avoid deadlocks during close/flush.

Planned validation: hold the writer before dequeue, complete a cached DMA write,
issue a PIO read and a DMA read of the same LBA, then release the writer. Both
must return the new bytes. Repeat with two overlapping queued writes and with
one dequeued write plus another still queued.

### F3 — High: FLUSH CACHE can complete while a dequeued write is unfinished

Evidence: `HDD_FlushCache` checks only `writeQueue.IsQueueEmpty()` and completes
immediately if empty (`ATA_CmdNoData.cpp:28-41`). `IO_Write` dequeues an entry
before its seek/write/fflush (`ATA_Transfer.cpp:84-99,159-178`). Dequeue advances
`tail` before returning (`SimpleQueue.h:63-73`), so the queue can be empty while
the worker owns an unfinished final write. The fast completion path does not
check `ioWrite` or wait for the worker to become idle. The deferred `awaitFlush`
path does inspect worker flags in `Async` (`ATA_State.cpp:519-550`); the defect
is the fast path bypassing that check.

Correction rationale: empty pending queue is not equivalent to completed I/O.
Flush completion needs a barrier for both queued and in-flight prior writes.
Separately, normal persistence calls `fflush`, not a general OS durability
primitive; power-loss durability is not established by draining this queue.
`FlushFileBuffers` appears in a Windows sparse-cache probe, not as a universal
flush-command implementation (`ATA_Transfer.cpp:198-206`).

Planned validation: block the final write after dequeue and before file write;
issue `0xE7`; verify no completion IRQ until that write finishes. Repeat with an
already empty/idle queue and multiple queued writes. A separate durability test
must establish the supported Windows/POSIX flush contract.

### F4 — Medium: SEEK handles the bounds predicate backwards

Evidence: `HDD_SeekCmd` enters its ERR/IDNF branch when `HDD_CanSeek()` returns
**true**, and sets SEEK when it returns false (`ATA_CmdNoData.cpp:98-115`).
PIO/DMA command paths use `!HDD_CanSeek()` for their failure branches. For an
LBA-mode valid start, standalone SEEK therefore fails; for an out-of-range
LBA-mode start, it reports success. CHS has a separate unsupported path and
must not be conflated with this inversion.

Correction rationale: align standalone SEEK's predicate with its documented
meaning and other callers, preserving error/seek-lock handling. This is separate
from adding write commands and has no demonstrated connection to run11.

Planned validation: valid LBA 0 and final LBA succeed; one-past-final LBA returns
ERR/IDNF; inspect regular versus alternate status seek-lock behavior.

## Outstanding audit work

- Reset and aborted-transfer lifetime, command acceptance while BSY/DRQ.
- PIO interrupt masking, data-register access and multiple-mode validation.
- DMA bridge stop/restart and the historical force-break message's actual source.
- IDENTIFY advertised features, command coverage and ATA-4 versus later features.
- Sparse-file correctness, host I/O errors and save-state coverage.
- Existing test coverage and a focused regression plan before implementation.

No proposed correction above has been implemented or tested. Production fixes
need focused tests; under the current no-compilation instruction, compiled
verification and guest/runtime validation remain unrun.
