# Image-backed SMART commands, 2026-10-20

This checkpoint implements SMART READ DATA (B0/D0), SMART SAVE ATTRIBUTE VALUES
(B0/D3), and a real SMART EXECUTE OFF-LINE IMMEDIATE (B0/D4) routine for the
virtual image-backed drive. Primary reference: T13/1153D revision 18,
`/workspace/scratch/ata4-reference/d1153r18.txt`, §§6.11, 8.41.4, 8.41.5 and
8.41.7. The reference is a working draft, not physical Sony-drive evidence.

D0 returns one PIO sector, with the ATA-4 checksum and reserved bytes zero. There
are no invented physical temperature, wear, reallocation or spin-up attributes.
Bytes 0-1 hold vendor data revision 1; the legacy 30 physical attribute entries
remain empty. Offline capability byte 367 is 01h: the routine exists and suspends
on host commands, resuming at an idle, awake pump. SMART capability word 368 is
0002h: event autosave exists, with no power-mode saving guarantee. Later SMART
logging, self-test and error-log capabilities remain unadvertised. D1 remains an
obsolete unsupported operation and D5 remains unsupported; neither receives a
fabricated sector.

D4 immediately completes its non-data command and schedules read verification of
the image's complete-sector extent. Each eligible Async call reads at most 128
sectors (64 KiB), with an emulated IOP cycle budget corresponding to a maximum
8 MiB/s. The total-time word estimates that schedule, clamps to 1..65535 seconds,
and excludes host-command stalls and host filesystem latency. Scan I/O holds
`ioMutex`, waits for accepted queued writes and normal reads/writes, and uses its
own stack buffer. The routine changes no guest taskfile registers or IRQ state.
It verifies readability, not content integrity against an external expected hash.
A readable image containing already-corrupted guest data can therefore pass.

ATA-4 marks Sector Number not applicable for D4. Previously accepted later
selectors 1/2/127/129/130 had fake success behavior and no actual self-tests. D4
now ignores Sector Number and always starts/restarts this ATA-4 offline routine;
it does not claim to implement those later self-test or abort-selector protocols.

An interrupting command suspends the scan (offline status 04h); a later idle,
awake pump resumes it. Standby/Sleep suppress scan I/O. SMART disable and reset
abort it (05h); another D4 restarts from sector zero. Never-started is 00h,
completed is 02h, and backing-read or result-persistence failure is 06h. Running
uses vendor status 40h. A backing read failure latches the virtual-drive reliability
condition, exposing F4h/2Ch through RETURN STATUS, the fatal offline status, failure
count and first unread sector through READ DATA. This reliability latch persists;
a later successful read does not erase historical fault evidence.

## Separate SMART state metadata

The existing `.ata-settings` version 1 file remains exactly eight bytes and its
meaning is unchanged. New `.ata-smart` metadata is exactly 512 bytes, using the
same checksummed sector as READ DATA. Its vendor section is:

| Bytes | Meaning, little-endian integers |
| --- | --- |
| 386-396 | ASCII `PCSX2VSMART` |
| 397 | Format version 1 |
| 398 | Bit 0: reliability condition latched |
| 400-403 | Started verification count, saturating |
| 404-407 | Completed read-verification count, saturating |
| 408-411 | Failed read-verification count, saturating |
| 412-415 | Suspend/abort event count, saturating |
| 416-423 | Complete sectors read in current/last scan |
| 424-431 | First unread sector of last failed scan; all ones if none |
| 432-439 | Image extent in sectors when the scan started |
| 440-443 | Metadata-save failure count, saturating |

The completed count measures completed reads; a subsequent failure to persist the
result still changes status to 06h, latches the reliability condition and increments
the separate metadata-save failure count. Unsaved state naturally cannot survive
a process exit. No automatic retry loop hides a persistent metadata failure.

Open validates exact length, format, checksum, supported capability/status fields
and reserved standard bytes; malformed existing state fails Open without touching
guest image sectors. A loaded running/suspended scan becomes aborted, rather than
pretending an old process is still running. Missing metadata starts an honest
never-scanned state.

D3 saves accumulated counters, progress and fault state regardless of D2 and
reports DF/ABRT if the save fails. Offline completion also saves regardless of D2,
as §8.41.4 requires. Reset/close are vendor autosave events when D2 is enabled and
SMART remains enabled; no automatic metadata write executes in Standby/Sleep.
SMART disable stops monitoring and further saving, consistent with §8.41.1. Metadata uses
exclusive temporary creation, file flush/sync, atomic replacement and directory
sync on POSIX. A pre-existing `.tmp` is preserved and fails the command. The
shared helper retains the existing settings-file behavior, including reporting a
post-rename directory-sync durability limit without falsely rolling back an
already-committed setting.

## Integration and verification limits

The parent integrates `LoadSMARTState(hddPath)` into Open after the existing
settings load; `SMART_Interrupt()` before accepted command dispatch;
`SMART_Async(cycles)` at Async's idle end; and `SMART_Cancel()` into reset/close.
The hooks and state operate on the emulation thread; only backing-file reads share
the worker's `ioMutex`. The read chunk bounds work and memory, but synchronous
host filesystem calls cannot guarantee a two-second response bound on stalled or
remote filesystems. Solving host filesystem stalls requires separate asynchronous
I/O cancellation architecture; no runtime timing guarantee is claimed here.

Eleven prospective disposable-image regressions cover D0 sector checksum and
truthful fields; bounded scan/taskfile/IRQ isolation; persisted completion with D2
disabled; real truncated-image read failure and RETURN STATUS; D3 persisted reset
state; existing temporary-file preservation; standby/resume; disable/reset;
autosave enabled/disabled; corrupt length; and a full-length checksum-corrupt
sidecar. The first nine were written before production edits; the last two extend
source coverage during review. Existing tests still cover settings-version
compatibility and D2's Sector Count input.

Actual checks in this checkpoint: source inspection against the cited draft and
`git diff --check`; declaration/definition, fixture and metadata-layout source
checks. No CMake configuration, compilation, C++ test execution, sanitizer,
Windows build or guest run was performed, per user instruction. The prospective
C++ cases have not been observed failing or passing. Their source presence is
not runtime conformance evidence. Run the ATA image harness on disposable images
when execution is authorized, followed by the user's Windows Release AVX2|x64
build and cloned-image guest validation.
