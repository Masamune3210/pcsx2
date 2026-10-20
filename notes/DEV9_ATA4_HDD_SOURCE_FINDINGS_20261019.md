# DEV9 ATA-4 source audit checkpoints

**Baseline findings, retained as evidence.** Corrective production changes are now
checkpointed at `58dc04a4fe` / `874309ca1c`; see the
[implementation ledger](DEV9_ATA_IMPLEMENTATION_20261020.md) for F1-F12/F14,
reset/persistence decisions and validation limits. F13 has a separate preflight
safety policy rather than full disk snapshot/restore. No compiled or guest tests
have run. The reproductions below remain required future validation.

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

## Checkpoint 3: initialization, coverage and persistence limits

### F10 — High: constructor reads register fields before initialization

Evidence: `ATA::ATA()` has no member initializer list and calls reset immediately
(`ATA_State.cpp:24-28`). `regStatus`, `regError` and `regSelect` have no in-class
initializers (`ATA.h:73-97`). Reset first performs `regStatus |= BUSY` and clears
READY (`ATA_CmdExecuteDeviceDiag.cpp:7-10`); diagnostics later modifies regError
and `SetSelectedDevice` modifies regSelect with a bitwise operation. These read
indeterminate scalar values. `new ATA()` in `DEV9.cpp:102` does not zero those
members because ATA has a user-provided default constructor. The `memset` of
`dev9` initializes the containing bridge, not the separately allocated ATA.

`pioPtr/pioEnd` and `HddSparseStart` also lack initializers
(`ATA.h:32,144-145`). A pre-command Data read compares the former
(`ATA_CmdPIOData.cpp:45`); the first sparse-cache update compares the latter
(`ATA_Transfer.cpp:303-304`). These are additional initialization gaps, not
proof of a particular startup status or reproducible crash frequency.

Correction rationale: initialize register and transfer state before any reset
helper reads it; define first-open and reopen cache-location state. Planned
validation: construct in nonzero-filled storage, inspect reset signature and
pre-command Data access, exercise the first sparse write, and run initialization
sanitizers. This is separate from clearing already-valid state on reset (F5).

### F11 — Medium: parameter acceptance exceeds advertised capabilities

Evidence: SET MULTIPLE MODE stores any Sector Count byte
(`ATA_CmdNoData.cpp:179-187`), even 255 while IDENTIFY word 47 advertises a
128-sector maximum (`ATA_Info.cpp:114-115`). Zero disables READ MULTIPLE through
the explicit check in `ATA_CmdPIOData.cpp:92-95`; zero here must not be expanded
to 256. Nonzero multiple counts need ATA-4 validation, including the standard's
permitted block sizes, rather than unconditional success.

SET FEATURES accepts MDMA mode 7 and UDMA mode 7 from the low three bits
(`ATA_CmdNoData.cpp:135-164`), while IDENTIFY advertises MDMA 0-2 and UDMA 0-6
(`ATA_Info.cpp:166-169,286-289`). Unknown feature selectors merely log and then
complete successfully (`ATA_CmdNoData.cpp:172-176`).

Correction rationale: reject unsupported parameters without replacing the prior
valid setting. Planned validation: advertised min/max and one-past-max transfer
modes; multiple count zero, supported sizes, 129/255 and unsupported intermediate
sizes; unknown feature selector must not silently claim implementation.

### F12 — Medium: ordinary commands and Data reads ignore device selection

Evidence: most task-file reads return zero for absent device 1
(`ATA_State.cpp:344-380,395-396`), but Command writes always dispatch
(`:500-506`), and `PreCmd` only checks READY (`ATA_Command.cpp:132-147`). There
is no selected-device check in PIO/DMA data commands. Data reads dispatch to
`ATAreadPIO()` before any selection check (`ATA_State.cpp:338-343`). An
ordinary read or write directed at absent device 1 can therefore operate on
device 0's state/image despite absent-device status reads returning zero.

The command-write path also bypasses the BSY/DRQ write guard
(`ATA_State.cpp:419`); `PreCmd` does not check those bits. An ordinary new
command can replace an active DRQ transfer. This amplifies F5's lifetime issue.
Special broadcast/diagnostic/reset commands need their own acceptance rules.

Correction rationale: enforce command/device/protocol acceptance centrally,
with explicit exceptions. Planned validation: select absent device and issue
IDENTIFY/read/write; verify the actual image is unchanged and no master event
is consumed. Issue ordinary commands during BSY/DRQ and verify the original
transfer remains consistent. No illegal-host-sequence compatibility policy was
changed in this audit.

### F13 — High for recovery: DEV9/ATA state is not serialized

Evidence: no DEV9 state entry appears in `SaveState.cpp:689-704`; no DEV9 call
appears in its internal freeze path; no freeze/DoState/StateWrapper routine is
defined under `pcsx2/DEV9`. ATA owns private task-file state, buffers, callbacks
and the write queue; the bridge separately owns FIFO counters/data and the IOP
DMA pointer (`DEV9.h:39-80`). They are not the generic IOP hardware-memory block.
`SaveStateBase::vmFreeze` also does not save them (`VMManager.cpp:1870-1902`).

Consequently, loading old CPU/IOP state does not restore the corresponding
DEV9/ATA transfer state, nor rewind accepted writes in the external HDD image.
A save-state archive alone is not a coherent HDD rollback/checkpoint. This is a
source-observed coverage gap; no load-induced corruption was demonstrated here.

Correction rationale: define save/load support and disk-image consistency
explicitly before using save-state rewind to diagnose a journaling filesystem.
A future implementation must serialize logical state, not FILE pointers,
threads, mutexes or callback addresses, and must specify treatment of queued and
in-flight writes. Planned validation: save/load idle and mid-PIO/DMA using a
disposable HDD copy, compare device registers/data and disk generation. Until
then, use cold boots with separately preserved evidence images for comparisons.

### F14 — High for host failures: image I/O failures abort the process

Evidence: failed seek/read in `IO_Read` calls `abort()`
(`ATA_Transfer.cpp:63-76`); failed seek/write/fflush does the same in `IO_Write`
(`:94-99,159-177`). Sparse-cache reads/writes also abort on host I/O failure
(`:249-257,334-340`). Thus short I/O, an externally truncated image, full disk,
or a backing-storage error is not converted into an ATA error completion.
With write cache enabled the guest may already have received a successful
write completion, making deferred error handling a separate protocol concern.

Correction rationale: propagate host errors through a defined device error and
shutdown policy without reporting successful flush/persistence. Planned
validation: inject seek failure, short read/write, fflush failure and full-disk
failure, with cache on/off and during `0xE7`; assert no success IRQ hides the
failure and preserve the original image for postmortem inspection. No fault
injection was performed in this pass.

## Coverage and next correction order

See [command coverage and regression plan](DEV9_ATA4_HDD_COVERAGE_20261019.md)
for dispatcher coverage, IDENTIFY inconsistencies, bridge/sparse limitations,
and specific required test cases. This first source pass is complete; it is
not an exhaustive ATA-4 conformance assessment. External standard clauses and
actual guest command traces are still needed for a full conformance matrix.

Suggested correction order, keeping commits independently reviewable:

1. Establish deterministic initialization and reset/command-cancellation state
   (F10/F5/F12), including safe DMA copy-length invariants.
2. Repair capacity/partial-transfer semantics and first-failing-LBA reporting
   together (F1); fix SEEK and geometry validation separately (F4/F6).
3. Define ordered write/read/flush handling with synchronized queue observation
   (F2/F3/F9), including host error and durability policy (F14).
4. Add the missing PIO data-out engine and `0x30`/`0xC5` commands, covering both
   persistence and protocol transitions; do not just add dispatcher cases.
5. Reconcile interrupt/HOB handling and advertised command/parameter support
   (F7/F8/F11), then define save-state support (F13).

No proposed correction above has been implemented or tested. Under the user's
no-compilation instruction, compiled regression tests, memory/thread sanitizers,
Windows builds and guest/runtime validation remain unrun. This does not block
source review, but it prevents claiming a working fix or a repaired PSBBN boot.
