# DEV9 ATA host protected area commands, 2026-10-20

The virtual drive now implements the ATA/ATAPI-4 Host Protected Area command pair,
READ NATIVE MAX ADDRESS (`F8`) and SET MAX ADDRESS (`F9`), for the image profiles
which do not enable LBA48. The image's native size remains unchanged. Guest SET MAX
changes an exclusive count of accessible sectors; it does not truncate, erase,
resize or rewrite guest disk sectors. Hidden sectors become accessible again when
the guest expands the limit. Existing LBA48 image support remains unchanged.

This is a bounded ATA-4 HPA profile. Large-image profiles retain the existing
LBA48 data commands, keep HPA support/enabled bits clear, and abort `F8/F9`.
Later READ NATIVE MAX ADDRESS EXT (`27`) and SET MAX ADDRESS EXT (`37`) remain
unimplemented; they need an independent later-standard audit. This restriction
avoids advertising a later HPA feature combination or leaving a large image behind
a legacy limit which those legacy commands cannot remove. SET MAX password,
lock/unlock and freeze extensions remain unclaimed and unimplemented.

## Normative source and behavior

Primary reference is T13/1153D revision 18, §§6.12, 6.12.1, 8.26 and 8.38,
retrieved as `d1153r18-mit.pdf`; SHA256
`9ac718ef8b9dbc309a7dc597288141acbd0e4715937c10e9ca4691cd94326ce9`.
Local searchable reference: `/workspace/scratch/ata4-reference/d1153r18.txt`.
The exact revision matters: despite calling Sector Count bit0 **VV**, §8.38.4
states that **one preserves the limit across power-up or hardware reset**, while
zero restores the most recent nonvolatile limit on those events. Later revisions
must not silently replace this revision's bit meaning.

- Every F9 requires a successful immediately preceding F8. Task-file writes do
  not interrupt that sequence, but every issued command consumes it. A failed F8
  cannot authorize F9; reset also clears it.
- F8 returns native capacity independently of the active accessible limit. LBA
  F9 accepts an inclusive last address, converts it to a sector count, and rejects
  a count beyond native capacity with ABRT. LBA0 is a legal one-sector limit.
- CHS F8/F9 use the native default head and sector fields explicitly required by
  §§8.26.5 and 8.38.4, even after INITIALIZE DEVICE PARAMETERS changes the current
  translation. Native geometry is 16 heads and 63 sectors. CHS F9 requires native
  head 15/sector 63; cylinder values above 16383 abort. The specific §8.38.8 formulas
  use default words 1/3/6 to derive the resulting capacity and current 55/56 to
  report current geometry. CHS native queries abort without a complete default
  cylinder. LBA queries remain available for tiny images.
- A second nonvolatile F9 after power-on or hardware reset fails with IDNF.
  A failed metadata save does not consume that quota. Software reset preserves
  the active volatile limit and quota. Hard reset restores the last nonvolatile
  limit and reopens the quota; reopen is a new virtual power cycle.
- Sector/DMA/verify I/O above the accessible limit uses IDNF; invalid multiple-command ranges ABRT before DRQ.
  IDENTIFY reports accessible capacity while native capacity remains available
  for F8 and profile selection. Failed F9 leaves IDENTIFY unchanged.

The draft has a small-capacity geometry conflict: §8.38.8 explicitly keeps native
words 3/6 unchanged while calculating word 1 by integer division; a legal tiny LBA
limit can therefore yield zero cylinders with nonzero native heads/sectors.
General §6.2.1(10) instead requires all CHS fields zero when one default field is
zero. The implementation follows the more specific SET MAX update rules for a
native image which originally had complete cylinders. Images already smaller than
one default cylinder retain their original all-zero default geometry. This choice
is explicit rather than an invented lower bound on valid LBA limits.

## Persistent metadata

The full image path plus `.ata-hpa` contains a fixed 40-byte version 1 record. It
stores magic `PCSX2HPA` (bytes 0–7), version 1 (byte 8), zero reserved bytes 9–15,
little-endian native sector count (bytes 16–23), little-endian nonvolatile accessible
sector count (bytes 24–31), zero reserved bytes 32–35, and CRC-32/ISO-HDLC of bytes 0–35
in little-endian bytes 36–39. Exact length, magic, version, reserved values, checksum,
native image size and nonzero/in-native limit are checked before image open
completes. Missing metadata selects native capacity; invalid or unreadable metadata
refuses open. Copy/move this sidecar with an image to retain its nonvolatile HPA.
A sidecar from an image of another size is rejected rather than silently clamped.

HPA and SMART metadata share the renamed `SaveATAMetadataSidecar` helper: exclusive
`.tmp` creation, checked write/flush/close plus host file sync, then atomic replace.
The HPA handler commits that record before changing visible capacity or quota.
Save failure before replacement leaves those states unchanged. A directory-sync
failure after successful atomic replacement is reported as a durability limitation;
the committed state remains consistent with the new record. The fixed temporary
name intentionally fails safely on a pre-existing temporary entry.

## Source changes and prospective checks

`ATA.h` carries separate native/accessibility helpers and HPA state. `ATA_State.cpp`
loads checked metadata during open and applies reset persistence. `ATA_Command.cpp`
tracks command adjacency and dispatches the pair. `ATA_CmdNoData.cpp` validates and
completes both non-data commands. IDENTIFY, generic command refreshes and I/O bounds
use the accessible count; native size still drives backing-file layout and LBA48
profile selection. SMART offline scans intentionally retain the native extent:
they are device-side verification, rather than a host sector-read command.

Thirteen source-first disposable-image cases cover native/current distinction,
last-visible-sector reads, PIO/DMA/read-verify rejection beyond the limit, unmodified
image bytes/length, adjacency and failed-F8 consumption, invalid limits preserving
identity, software/hardware reset, nonvolatile quota/reopen, CHS after translation
change, tiny limits, blocked atomic metadata saves, corrupt/trailing metadata,
changed native size, not-applicable register bits and the deferred LBA48 HPA profile.

No CMake configuration, compilation, executable C++ test, sanitizer, Windows build
or guest boot was run. Test sources preceded production edits, but no red/green
execution is claimed under the user's explicit prohibition. Source whitespace and
focused source inspection are the available verification; actual protocol/runtime
validation remains required when authorized.
