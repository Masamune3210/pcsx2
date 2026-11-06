# Memory-card image idle writeback branch

Branch: `fix/memcard-image-idle-writeback-20261106`
Base: `a821f07110a711569d8fb387c6bbca3fe16461cd`
Last updated: 2026-11-06

## Why
Direct memory-card image reads, read/modify/writes, and erases block emulation on high-latency rclone/WinFsp mounts. Folder cards already batch writes; file cards currently do not.

## Decisions
- File-backed card images load into a per-slot memory buffer on first guest access, not at VM initialization.
- All guest reads use the buffer. Save and erase retain existing PS2 bit-clear/erase behavior.
- Every guest write checks the backing file's size and timestamps before mutating RAM.
- Before a batch is written, read and hash the complete backing file to detect changes missed by timestamp granularity.
- Write back only dirty 528-byte pages after two seconds without card access, with a 15-second maximum dirty duration.
- Flush on close/eject. Stop writing on conflicts or I/O failure, reporting and attempting to preserve an image copy under the local PCSX2 cache directory.
- Never reject intermediate PS2 filesystem states just because a game is mid-format/save; sanity checking verifies safe image shape and bounds instead.
- Keep the existing non-ECC conversion path and legacy checksum behavior.

## Current state / verification
- Initial implementation committed on this branch only; do not merge into testing before compilation and an end-to-end save/load test.
- Static review only. No local Windows/PCSX2 compiler or rclone mount available in this environment.
- Important limitation: deferred flush occurs on the emulation thread and can still cause one stall per batch. It removes repeated tiny filesystem reads/writes, but fully asynchronous writeback is a possible subsequent improvement.
- Metadata verification on every write may still be expensive on some remote mounts. Existing Win32 write-share denial only protects other writers using the same local mount, not remote-side changes. Remote VFS caching may also hide direct remote edits.

## Next
1. Compile on Windows (Release and Debug).
2. Exercise file-card reads, writes and block erases, then idle and shutdown flushes.
3. Compare timing against a local card and an rclone-mounted card; verify failure recovery and direct remote modification handling.
4. Revisit writeback scheduling if the once-per-batch stall remains noticeable.
