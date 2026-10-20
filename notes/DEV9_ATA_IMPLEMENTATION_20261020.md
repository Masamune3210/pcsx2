# ATA corrective implementation, 2026-10-20

Source changes implement the [fix plan](DEV9_ATA_FIX_PLAN_20261020.md), following
source findings F1-F14 and the ATA-4 comparison. They are **not runtime-validated**:
the user prohibits compilation, so no CMake configure, compiler, C++ test, Windows
build, sanitizer, or PSBBN boot was run. No user image was opened or modified.

## Implemented corrections

| Finding / gap | Implementation and reasoning |
| --- | --- |
| FIFO wrap / invalid lengths | Checkpoint `58dc04a4fe`; second wrapped copy targets ring start, invalid counter/residual pairs cannot become negative copy sizes, shared rebasing prevents signed overflow. See FIFO rationale. |
| F1 bounds / error address | Complete-sector capacity is exclusive; subtract start before comparing count. Empty/sub-sector and invalid CHS starts fail; valid final sector is accessible. Crossing EOF transfers the valid prefix, then IDNF identifies the first invalid sector with residual count. Command snapshots avoid address drift as task registers advance. Addressable capacity matches IDENTIFY limits, preserving existing LBA48 extensions. |
| F2 read-after-write / F3 flush / F9 queue | All ATA queue observation and file access use ioMutex. Reads drain all accepted writes; holding the lock across host IO includes a dequeued final write. FLUSH/cache-disable/cache-off completion performs fflush plus fsync/FlushFileBuffers; cache-off writes remain BSY until completion. |
| F4 SEEK | Valid address succeeds, invalid address reports IDNF. |
| F5 reset / interrupted DMA | SRST is asserted until release. Reset synchronizes with the worker, cancels unfinished buffers/read continuations and SPEED/IOP FIFO ownership, clears offsets/callbacks/HOB, and preserves accepted write entries. DMA callbacks require a live transfer and positive bounded residual before copying. Close joins/drains the worker, flushes, and frees buffers; destructor uses Close. |
| F6 geometry | CHS conversion and task-file progression are implemented. INIT validates nonzero sectors and all four head bits; invalid translation rejects media access until repaired. IDENTIFY refreshes geometry immediately. Tiny images with no complete default cylinder report no default CHS geometry; LBA remains usable, and INIT can select a smaller geometry. |
| F7 IRQ / F8 HOB | Interrupt events latch even while nIEN masks the line; device selection gates external assertion. Status clears pending IRQ; alternate status preserves it. Control explicitly clears/sets HOB and Device/Head clears HOB. |
| F10 initialization | Register, offset, pointer, size, mode and sparse-cache state have explicit initial values before reset helpers access them. |
| F11 parameters / F12 selection | Unsupported features and unadvertised transfer modes ABRT; PIO mode selection preserves DMA selection. Multiple sizes are supported powers of two through 128; zero retains the existing disable extension. Absent device commands/data cannot access the image. Invalid command sequencing cannot replace a live buffer. During DRQ only the device-select bit may change; active address bits stay frozen. DATA is 16-bit; unsupported widths consume no word. |
| F14 backing failures | Seek/read/write/flush errors produce ATA errors rather than aborting the emulator. First deferred write fault remains sticky until reopen; incomplete transfers are canceled, accepted queued entries still attempted. Error LBA and active transfer residual count are reported where identifiable. Native flush failure cannot identify an exact failed sector. |
| Missing PIO writes | Real DATA word path implements30/31/C5 and later34/39. SectorCount0 means256 (65536 for EXT). Initial DRQ is polled; subsequent block requests and final completion interrupt, with no per-word IRQ. Completed blocks acquire queue ownership before the next request; SRST discards only the unfinished block. Cache-off blocks are durable before requesting the next block. |
| Missing aliases / verification | Defined21/41/C9/CB aliases decode explicitly. READ VERIFY reads the backing image and uses normal error handling. All23 mandatory bytes for the compared LBA-only profile now dispatch, plus conditional91; dispatch presence is not conformance validation. |
| Basic Power Management | E0/E1/E2/E3/E5/E6 expose active/idle/standby/sleep and standard standby timer values. CHECK POWER leaves timer/state unchanged; sleep requires reset. Standby/sleep command completion drains and flushes writes. Timing uses elapsed IOP cycles sampled by DEV9async, whose original caller passes constant1. No physical host spin-down is attempted. |
| SMART / identity | Autosave reads Sector Count. Enable/autosave persist in a validated versioned `.ata-settings` sidecar through synced temporary-file replacement; pre-commit failure rolls back the setting and ABRTs. Image bytes and hddid format stay unchanged. Unsupported logging/self-test claims are removed, extended flush follows LBA48, obsolete PIO word51 is valid, and >2TiB capacity clamps before narrowing. |

See [I/O rationale](DEV9_ATA_IO_FIX_20261020.md),
[parameters and SMART metadata](DEV9_ATA_PARAMETERS_FIX_20261020.md), and
[FIFO rationale](DEV9_FIFO_FIX_20261020.md) for ownership/error decisions.
The metadata sidecar must accompany a cloned/moved image to preserve SMART
configuration. Its absence uses documented defaults; malformed existing metadata
rejects opening rather than silently resetting configuration.

## Verification and remaining limits

Added isolated `dev9_ata_test` using real ATA and common filesystem code, with a
narrow test DEV9 bridge, plus seven real FIFO cases in `core_test`. The fixtures
use disposable patterned files, whole-image readback/reopen, PIO block/count/IRQ
phases, EOF/CHS, SRST, DMA aliases/offsets, power states, modes, and metadata.
See [test instructions](../tests/ctest/ata/README.md). The bridge constant checker
compares 31 constants to production; it does not execute ATA code.

Executed: `git diff --check`, Python bridge-constant comparison, inspection of all
ATA queue call sites and removal of process-abort/scalar-delete patterns, plus
independent source review. That review identified cache-off DMA BSY, accepted PIO
block ownership, error residual-count, and ignored command-preamble returns; these
were corrected before checkpoint. No executable test outcome is claimed.

Remaining work requires validation: actual Windows `Release AVX2|x64` build,
compiled regressions/sanitizers, deterministic final-write-in-flight and host
write/flush-failure tests, SPEED DMA start/stop/force-break behavior, and cold boot
of a disposable PSBBN clone. The old force-break symptom is not established as
resolved by these changes. The dormant async-read helper has no production callers;
its allocation correction is not covered by the public DMA growth case.

F13 now has a [save/load preflight safety policy](DEV9_ATA_SAVESTATE_POLICY_20261020.md):
HDD-enabled capture/load is rejected before guest-state mutation, including the
VMManager reset-on-load-error path. Full HDD state plus image-generation/snapshot
restore is not implemented; old archives and later ELF profile changes remain
explicit policy limitations. Optional undocumented Sony
security/authentication subcommands remain unsupported rather than invented; SCE
IDENTIFY is preserved. None of these changes proves ReiserFS recovery or PSBBN UI
progression on an existing image.

Final lifecycle corrections: Close clears DRQ/IRQ/FIFO state and ATA writes require
an open image, covering the Ethernet-only shared decoder. FIFO cancellation advances
a generation counter; callback progress is discarded if a deferred host error reset
the bridge during completion. The regression tree now has 40 ATA cases and seven
FIFO cases; all remain uncompiled/unexecuted. Source-only checks and independent
review found no further established important issue in the reviewed paths.

The final command-error review uses UNC for failed reads and DF/ABRT for failed
host writes/flushes (ATA-4 bit6 means write protection for writes). This avoids
misreporting storage failure as protection. First-error addresses follow
§8.27.6/§8.48.6; transferred amount on error is indeterminate under the draft.

## Subsequent command completion

`693ff612a2` adds E4/E8 buffer transfers, D0 SMART data/D3 persistence/D4 real
background verification, and EF 66/CC reset-reversion control. A later checkpoint
adds ATA-4 HPA F8/F9 native/current limits with CRC-checked metadata, plus genuine
C4/29 read-error block delivery. These supersede the earlier ledger's blanket HPA
absence and its generic multiple-read failure behavior. Default selected transfer
modes are now PIO2/MDMA0, while supported higher modes remain available.

The command matrix and focused buffer/SMART/command-semantics/HPA/multiple-error
notes describe the current profile. There are 75 prospective ATA cases and seven
FIFO cases. Source review/checks only; all C++ execution, Windows and guest tests
remain unrun. Full disk snapshots, later HPA extensions, standard security and
undocumented Sony authentication remain outside this implementation.
