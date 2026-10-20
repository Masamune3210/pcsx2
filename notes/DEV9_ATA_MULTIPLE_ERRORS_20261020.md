# READ/WRITE MULTIPLE errors, 2026-10-20

Primary source: local T13/1153D revision 18, sections 8.25.6/.8 and 8.47.6/.8.
This is a focused correction to existing PIO multiple commands; no compilation,
configuration, C++ test execution, or guest boot was performed.

## Two different error paths

Invalid host addresses can be rejected before command execution. Sections
8.25.6 and 8.47.6 explicitly permit either IDNF or command aborted for an address
outside the user-accessible range. The emulator now chooses ABRT before DRQ for
an invalid entire READ/WRITE MULTIPLE range (`C4/C5`, and existing extended
`29/39` equivalents). Range validation uses a temporary count, leaving the native
request snapshot intact. For a crossing range, the task file reports its first
invalid address. No data from the command is accepted or returned. The existing
single-sector-command valid-prefix policy (`20/30/24/34`) remains separate.

This avoids the previous clipping behavior, which silently shortened a negotiated
multiple block before reporting IDNF. A block has the negotiated multiple count,
or the actual remainder of the original request; a storage boundary must not
invent a different block size.

A real backing-image read failure during execution has different requirements.
Section 8.25.8 says device errors are posted at the beginning of the affected
block/partial block, DRQ remains one, and the block is transferred, including
corrupted data if any. Further blocks are transferred only for a correctable data
error. The old generic host-error helper instead returned UNC with no DRQ or data.

For `C4/29` only, synchronous read completion now captures a failed backing read
under the I/O mutex and prepares the original block containing the first failed
sector. Earlier complete blocks are transferred normally. UNC/ERR is asserted
before DRQ/IRQ for the affected block. Complete sectors actually read retain their
bytes; the incomplete sector and unread remainder are zero-filled, including any
partial bytes that `fread` copied into its final incomplete sector. This is
deterministic error data, never successful replacement data: UNC/ERR stays set.
Transfer terminates after that entire block, or the original command's partial
tail. Subsequent blocks are not offered. The first failed sector is retained in
the task file; section 8.25.8 otherwise leaves post-error-block contents undefined.

The generic error helper must not cancel this prepared block. Its captured read
failure is consumed only for `C4/29`; actual write errors still take precedence.
The new `pioReadErrorBlock`/`pioReadErrorSector` fields are transient command state
and are cleared on ordinary command setup, reset, close, generic error completion,
and completion of the errored read block.

## WRITE MULTIPLE and other additions

Section 8.47.8 places device write errors after the attempted device write of the
transferred block and prohibits later blocks. The existing uncached-write path
accepts a complete host block, attempts its queued write/flush, and reports a host
failure before another block is requested. The cached path may acknowledge data
before a later backing write/flush failure; its existing sticky device fault and
flush failure policy remains necessary. This patch does not convert a write fault
into UNC (bit 6 means write protection in the ATA-4 write error table).

The new diagnostic READ/WRITE BUFFER callbacks have separate ownership and are
cleared by cancellation/error paths. Their prerequisite/unsupported-command ABRT
paths do not require a READ MULTIPLE error block. SMART background verification
uses its own buffer, serializes shared file access, and records failures in SMART
state without setting guest task-file UNC/ERR. No additional concrete error-phase
regression was found in those additions during this focused source review.

## Prospective checks

Three prospective source cases were added. The native invalid-range and error-block
cases were written before the production changes; extended coverage followed:

- Native multiple reads/writes crossing within a block, crossing after a complete
  block, and starting outside the image all abort without DRQ or disk changes.
- Truncated reads fail in the first, middle, or final partial block. They assert
  ERR/UNC at block start, preserve preceding valid bytes, transfer zero-filled
  failed sectors, generate only block interrupts, and offer no subsequent block.
  A prior successful read seeds the buffer to detect stale-byte exposure, and a
  31-byte partial sector checks `fread`'s incomplete-item behavior.
- POSIX sparse-image coverage exercises actual advertised `29/39` support,
  invalid extended ranges, and an extended read failure midway through a block.

Only source diff/whitespace inspection was performed. These cases are prospective,
uncompiled and unrun; Windows and runtime protocol conformance remain unverified.

Independent review found and corrected one error distinction: unsuccessful INIT91
requires IDNF until a valid translation is supplied, including in-range LBA multiple
commands. That state is handled before invalid-range ABRT, with no DRQ. The existing
INIT regression now checks20/30/C4/C5 for IDNF and absence of ABRT. Both multiple
preflight rejection paths clear internal sectors-left state. Review was source-only.
