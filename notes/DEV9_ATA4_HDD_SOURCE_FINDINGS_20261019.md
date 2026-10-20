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

## Checkpoint 2: reset, register and host-safety findings

### F5 — High: reset leaves active transfer state behind

Evidence: `ResetBegin/ResetEnd` and diagnostics reset status/signature/geometry
(`ATA_State.cpp:294-332`; `ATA_CmdExecuteDeviceDiag.cpp:7-54`), but do not clear
`dmaReady`, `rdTransferred`, `wrTransferred`, `pioPtr/pioEnd`, the PIO callback,
or the partial `currentWrite` allocation. The same reset is reached by SRST
(`ATA_State.cpp:490-494`) and HDD hard reset (`DEV9.cpp:753-756`). `ATAreadPIO`
checks only the retained pointer/end, not DRQ, so data remaining before reset
can still be read after reset clears DRQ (`ATA_CmdPIOData.cpp:42-55`).

New DMA commands do not reset their byte offsets: read readiness only sets
DRQ/dmaReady (`ATA_CmdDMA.cpp:7-14`); write readiness replaces `currentWrite`
without freeing a partial prior allocation (`:30-45`). DMA copy lengths subtract
the retained offset from the new command's length (`:84-85,111-112`). If an old
partial offset exceeds the new request length, `std::min` produces a negative
`int`, which is converted to a huge `size_t` for `memcpy`. A retained offset
equal to the new length instead completes a zero-byte copy; a write can then
queue a new uninitialized buffer. This is a source-derived host-memory hazard;
no crashing sequence was executed here.

Concrete planned case: begin a 32-sector DMA command, transfer 8192 of 16384
bytes, assert/release SRST, then issue a one-sector command. The stale 8192-byte
offset yields `512 - 8192 = -7680`. Cover both directions, equal-length offsets,
PIO reset mid-sector, hard reset, close/reopen, and a fresh IDENTIFY after reset.
Check no old data/callback/IRQ or partial host buffer survives. Test under memory
sanitizers when compilation is permitted.

Correction rationale: reset needs one explicit transfer-cancellation contract,
separating partial unaccepted transfers from completed cached writes that must
be preserved. Clamping a negative copy length alone would conceal stale state.
The bridge's FIFO and IOP transfer state also need coordinated cancellation.

### F6 — High: invalid geometry can cause division by zero in IDENTIFY

Evidence: `HDD_InitDevParameters` copies `regNsector` to `curSectors` without
validation (`ATA_CmdNoData.cpp:44-51`). `CreateHDDinfo` divides by `curSectors`
while calculating current geometry (`ATA_Info.cpp:55-57`). The sequence
INITIALIZE DEVICE PARAMETERS (`0x91`) with Sector Count zero, then IDENTIFY
(`0xEC`), therefore reaches integer division by zero. This command's geometry
parameter is not the transfer count handled by zero-as-256 conversion.

The same initializer derives heads with `(regSelect & 0x7) + 1`; the declared
head field is four bits (`ATA.h:75-81`). Requesting 16 heads encodes low nibble
15, which is reduced to 8 heads. CHS data access itself is explicitly rejected
by `HDD_GetLBA` (`ATA_State.cpp:576-584`); successful parameter initialization
does not establish working CHS reads/writes.

Correction rationale: reject invalid geometry before storing it and decode all
four head bits. Planned validation: sectors zero and valid geometry, heads
1/8/9/16, followed by IDENTIFY; assert no host fault and consistent words 54-58.

### F7 — Medium: masked PIO events are discarded rather than latched

Evidence: PIO readiness sets `pendingInterrupt` only inside
`if (regControlEnableIRQ && sendIRQ)` (`ATA_CmdPIOData.cpp:18-23`). With nIEN
set, a command can become data-ready with no pending event. Unmasking only
reasserts INTRQ when `pendingInterrupt` is already true
(`ATA_State.cpp:475-487`). DMA and non-data completion latch the pending event
regardless of masking (`ATA_CmdDMA.cpp:25-27`; `ATA_CmdNoData.cpp:11-13`).

Correction rationale: separate the internal command event from whether it is
driven onto INTRQ, consistently across protocols. Planned validation: nIEN set,
issue IDENTIFY/PIO read, poll alternate status without consuming data, clear
nIEN, inspect IRQ; normal status should acknowledge, alternate status should
not. Check multiple-block boundaries and device selection separately.

### F8 — Medium: HOB cannot be cleared by writing Device Control zero

Evidence: Device Control sets `regControlHOBRead` if bit 7 is one but has no
assignment when it is zero (`ATA_State.cpp:496-497`). Reads of Sector Count and
LBA bytes continue returning the high-order shadow (`:349-380`) after Control
`0x80` followed by `0x00`. Writes to feature/count/LBA and Command clear HOB;
Device/Head writes do not (`:426-473,500-503`).

Correction rationale: reflect the written HOB selector, then review which
command-block writes must clear it. Planned validation: distinct high/low
register values, Control `0x80` then `0x00`, and each task-file write. This is
primarily a later 48-bit-addressing issue, not evidence for ATA-4 run11 failure.

### F9 — High: queue-empty observation races with the consumer's plain tail

Evidence: `SimpleQueue::tail` is an ordinary pointer (`SimpleQueue.h:23-24`).
`Dequeue` writes it on the worker (`:63-73`), while `IsQueueEmpty` reads it on
the command thread (`:78-80`). FLUSH CACHE calls `IsQueueEmpty` without taking
the worker mutex (`ATA_CmdNoData.cpp:34`). The atomic `head` and per-entry
`ready` protect publication, but do not make unrelated concurrent access to
`tail` atomic. `IO_Write` performs dequeue outside `ioMutex`
(`ATA_Transfer.cpp:37-44,84-87`).

Correction rationale: completion/queue observation needs a synchronization
contract, not just another unsynchronized emptiness check. This data race is
distinct from the logical in-flight-write gap in F3. Planned validation: Thread
Sanitizer plus deterministic flush/dequeue interleavings. No sanitizer was run.

## Outstanding audit work

- Command acceptance while BSY/DRQ and multiple-mode validation.
- DMA bridge stop/restart and the historical force-break message's actual source.
- IDENTIFY advertised features, command coverage and ATA-4 versus later features.
- Sparse-file correctness, host I/O errors and save-state coverage.
- Existing test coverage and a focused regression plan before implementation.

No proposed correction above has been implemented or tested. Production fixes
need focused tests; under the current no-compilation instruction, compiled
verification and guest/runtime validation remain unrun.
