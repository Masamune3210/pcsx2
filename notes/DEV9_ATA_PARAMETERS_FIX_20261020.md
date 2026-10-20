# ATA parameter validation and IDENTIFY consistency (2026-10-20)

Scope: `ATA_CmdNoData.cpp`, `ATA_CmdSMART.cpp`, and `ATA_Info.cpp` on the existing
`note-working/dev9-ata4-hdd-audit-20261019` branch. The virtual drive retains its
large-image/LBA48 support and advertised UDMA modes 0–6. The disk image is not a
physical drive capability source.

## Regression cases specified before implementation

- SEEK: an in-range address succeeds; an invalid address sets ERR/IDNF.
- INITIALIZE DEVICE PARAMETERS: all four head bits participate, so maximum
  head 15 selects 16 heads. Zero sectors aborts without division by zero;
  an invalid translation clears IDENTIFY word 53 bit 0 and blocks media access
  until a valid initialization. Initialization accepts DRDY clear and clears
  stale command errors. Valid initialization refreshes current cylinders
  immediately, before any later IDENTIFY command.
- SET MULTIPLE MODE: accept 2, 4, 8, 16, 32, 64, 128, and the existing zero-disable
  extension. Reject other counts while preserving the previous block setting.
- SET FEATURES: abort unsupported outer feature codes and invalid mode values;
  accept default PIO 00, flow-controlled PIO 0–4, MWDMA 0–2, and UDMA 0–6.
  Reject default PIO 01 because IDENTIFY does not advertise IORDY disabling.
  A PIO change preserves the independent selected DMA mode. Selecting MWDMA
  or UDMA disables the other DMA family while retaining the PIO selection.
- SMART ATTRIBUTE AUTOSAVE: use Sector Count 00/F1, regardless of Sector Number;
  reject other values without changing the autosave flag.
- IDENTIFY: words 84/87 do not claim unimplemented SMART logging/self-test;
  FLUSH CACHE EXT follows LBA48 support; word 51 never reports reserved PIO
  timing codes; large sector counts clamp before narrowing to words 60–61.

These cases are delegated to the shared public-register regression fixture.
The parameter cases were written in `tests/ctest/ata/ata_image_tests.cpp` before
the production changes. Source review confirms that the fixture drives the
actual task-file registers and checks the resulting status/IDENTIFY words.
The explicit user instruction prohibits compilation, so compiled red/green
test execution, the project test suite, Windows MSBuild, and PSBBN runtime
validation are **not run**. Test source is evidence of intended coverage only.

## Specification rationale

References are to the retrieved T13/1153D revision 18 working draft, also used
by `DEV9_ATA4_COMMAND_SET_COMPARE_20261019.md`:

- §8.16.4/.6/.7/.8 defines head encoding, unsupported translation ABRT,
  acceptance regardless of DRDY, and invalid-translation identity/access state.
- §8.37.6 requires ABRT for unsupported features or invalid parameters;
  §8.37.10 and Table 20 require independent PIO/DMA selections, supported
  modes, and mutual exclusion between MWDMA and UDMA.
- §8.39.4/.6/.8 defines valid multiple-block sizes and advertised maximum.
  Zero-disable remains an explicitly preserved compatibility extension.
- §8.41.2 uses Sector Count for ATTRIBUTE AUTOSAVE.
- §8.12.24 permits only PIO timing values 0–2 in word 51. Later SMART logging
  and self-test capability bits are not justified by stub bookkeeping and an
  aborted READ LOG; the underlying SMART core remains available.

Write-cache disable durability and asynchronous completion belong to the I/O
changes. Power-state/timer behavior and CHS address conversion belong to the
shared state/dispatch changes. The subsequent SMART persistence implementation
is described below.

## Implementation and actual verification

The scoped production changes above are applied. `curCHSValid` is shared with
the state implementation: reset restores it and media bounds checking rejects
access while it is false. Invalid initialization retains safe previous numeric
geometry while invalidating it; a valid request regenerates IDENTIFY/current
cylinders. Error exits return immediately, avoiding a second no-data completion
interrupt after `CmdNoDataAbort()`.

The LBA28 capacity fix performs `std::min<u64>` before casting to `u32`; the
previous `std::min<u32>` narrowed image sizes before comparison and could wrap
for images of at least 2^32 sectors (2 TiB). Existing LBA48 capacity and UDMA
0–6 support remain intact. This extra source finding is independent of any
physical PS2/controller capacity limit.

Executed: scoped `git diff --check` (exit 0), direct production diff review,
and inspection of the shared register-driven regression source. These are
source checks only. No executable regression, compiler, emulator, guest Linux,
or Windows runtime was run. In particular, the >2 TiB identity edge case and
DRDY-clear initialization still need executable coverage/validation, alongside
the other specified cases above.

## Subsequent SMART power-cycle persistence

SMART enable and ATTRIBUTE AUTOSAVE settings now belong to an adjacent metadata
file named by appending `.ata-settings` to the **complete image pathname**;
the image extension remains intact. Relative image names are made absolute at
open so later working-directory changes cannot redirect a settings update.
Guest image sectors and the existing Sony `.hddid` file are not modified.

The format is exactly eight bytes: ASCII `PCSX2A` (six bytes), version `01`, then
flags (bit 0 = SMART enabled, bit 1 = autosave enabled, all other bits zero).
An absent file gives the previous defaults (both true). Existing settings are
read into a fixed eight-byte buffer with an extra EOF check; malformed,
unsupported, unreadable, or oversized files fail image open rather than silently
resetting saved settings. SMART state survives hard/software reset, and each
image open loads the corresponding metadata rather than inheriting another
image's state.

Changing a setting writes the complete record to sibling `.ata-settings.tmp`
using exclusive creation (`wbx`). A pre-existing temp entry causes ABRT and is
left untouched. Write, stdio flush, native file sync (`fsync`/`_commit`), close,
and replacement failures before commit abort the ATA command and restore its
previous in-memory setting. The temporary file created by this operation is
removed on such failure. The existing settings record remains intact.
Every SMART helper now honors a failed `PreCmd()` so a previously latched HDD
I/O fault cannot be overwritten by synthetic SMART success or metadata changes.

Atomic sibling rename is the commit point. Windows uses replacement with
`MOVEFILE_WRITE_THROUGH`; POSIX additionally attempts directory `fsync` after
rename. A directory-sync failure produces a bounded diagnostic and retains the
committed setting: reverting memory after successful rename would disagree with
the metadata already visible on disk. Host-filesystem guarantees and a failed
directory sync can limit crash/power-loss persistence. This metadata boundary
is distinct from flushing pending guest HDD data.

Regression source was written first in the shared ATA fixture for enable/disable
round trips, exact autosave records, malformed metadata rejection without image
changes, and preservation/ABRT on an existing temp entry. Scoped whitespace and
source review ran; **no compilation or executable tests ran**, including native
Windows replacement behavior and injected filesystem failure paths.
