# DEV9 ATA backing I/O correction (2026-10-20)

This note records the backing-I/O portion of the implementation. It is not a runtime pass report. The user prohibits compilation; no emulator build, compiled regression, Windows test, image modification, or PSBBN boot was performed for this change.

## Confirmed failures and implemented ordering

`HDD_ReadSync` previously paused the background writer, then read the file while accepted writes remained queued. An immediately following read could therefore return old bytes. It now acquires `ioMutex`, drains every accepted queued entry, and reads only after those writes finish. The worker holds that same mutex throughout actual file I/O, including the final dequeued write. Acquiring the mutex consequently waits for an in-flight write as well as protecting the queue. Async reads snapshot LBA and sector count on the emulation thread, and the worker drains earlier writes before reading.

The existing `SimpleQueue` reads its consumer-owned tail in `IsQueueEmpty`. Its use was unsafe when the caller and worker operated concurrently. The fix is scoped to ATA: all its queue enqueue/dequeue/emptiness operations must hold `ioMutex` (or occur after joining the worker). The generic queue used elsewhere is unchanged. This protocol depends on the integration changes in `ATA_State.cpp` and the DMA/PIO enqueue path.

Keeping the mutex during host I/O deliberately favors correctness and a simple ownership rule over latency. Emulation-thread operations that need the lock can stall until queued I/O finishes. Background writes remain on the worker, but this is not a claim of equivalent throughput.

## Durable completion and errors

`IO_Flush` performs `fflush` followed by `FlushFileBuffers` on Windows or `fsync` on POSIX. The native handle is obtained from the stream, including when sparse optimization is disabled. An empty queue alone does not establish either completion or durability. FLUSH CACHE, cache disable, and writes with caching disabled must complete through the durable-flush path after all queued/in-flight writes finish. Shutdown joins the draining worker before flushing/closing.

Host seek/read/short-write/flush failures no longer invoke `abort`. The worker records an error and the emulation thread reports ATA ERR/UNC for read failures and DF/ABRT for write failures and clearing the transfer phase. A short read records its first unread sector; a short write records the first uncompleted sector. A durability failure cannot identify an exact failed sector, so it retains the last attempted write range's starting LBA. The first deferred write fault remains sticky until reopen, including across guest reset. New commands and later flushes must not acknowledge success after such a fault.

A command acknowledged with the emulated write cache enabled cannot have that acknowledgement retracted if a later host write fails. Failed buffers are freed after the checked host attempt; the sticky error prevents subsequent success from silently hiding the failure. Already accepted remaining queued entries are still attempted and shutdown still drains them. This is error reporting, not rollback or a guarantee of recoverable data after a host storage failure.

## Allocation and sparse-file corrections

Read-buffer growth uses `delete[]` for the `new u8[]` allocation. `IsAllZero` reads words through `memcpy`, avoiding the previous unaligned access and strict-aliasing violation.

Sparse-cache loading reads real file bytes and restores the stdio position. This removes the mixed native `SEEK_HOLE`/`SEEK_DATA` and buffered-file-position shortcut, whose early returns could leave the native offset inconsistent. It also removes reliance on allocation queries to conclude that a cache block is zero. Sparse/cache failures retry the same bytes as ordinary checked writes; only failure of that required write latches a guest-visible write error. The fallback invalidates the sparse cache and subsequent chunks skip sparse operations. Native hole punching is preceded by `fflush`.

The optional debug zero checker now reads into its separate temporary buffer and compares those bytes, rather than overwriting the live sparse cache. Host I/O failures in the checker log and return instead of asserting.

## Integration interface

`ATA.h` adds `ioReadError`, sticky `ioWriteError`, event flag `ioErrorPending`, `ioErrorSector`, and snapshotted `ioReadSector`/`ioReadSectors`, all protected by `ioMutex`.

- `IO_Read`, `IO_Write`, `IO_Flush`: caller holds `ioMutex`; shutdown may call `IO_Flush` after joining the worker.
- `HDD_ReportIOError`: acquires `ioMutex` internally, so callers must release it first. It clears read/pending error state, retains the sticky write fault, cancels pending read/flush continuations, records the error LBA where supported, and posts ATA completion with error.
- `IO_SparseCacheLoad` now returns `bool` for checked optimization fallback.
- `Open` initializes completion/error/shutdown state. Reset must synchronize with the worker before changing transfer state and retain deferred write faults. `Async` must handle pending errors before continuing a read or declaring a flush complete, without generating repeated idle IRQs for the sticky fault. The command preamble rejects later commands on sticky write failure.

## Verification and remaining work

Executed: `git diff --check` and targeted source inspection of queue operations, error paths, allocation ownership, stream/native flushing, and callers. No compilation or runtime tests were executed. The separate ATA regression source is maintained by the regression task and must be built/run later.

Required runtime verification on disposable images: cached write immediately followed by a read before the next async tick; repeated overlapping writes/readback; FLUSH while the final dequeued write is in flight; flush with an empty queue; disabled-cache writes and cache-disable completion; shutdown/reopen readback; truncate/read failure and write/flush fault injection; reset during a queued read/write; buffer growth beyond 256 sectors; zero/nonzero sparse chunks and unsupported-hole-punch fallback. Windows native flush behavior and PSBBN recovery remain unverified.

Final error-code review: ATA-4 write Error bit6 means write protection, whereas
read bit6 means UNC. Host write/flush failure therefore reports DF/ABRT, not a false
write-protect indication; read failures retain UNC. Clauses8.27.6/8.48.6 allow the
first-error address with an indeterminate amount transferred. Residual count and
normal next-address progression are useful emulation choices, not additional
ATA-4 mandatory outputs. Host-failure injection remains unrun.
