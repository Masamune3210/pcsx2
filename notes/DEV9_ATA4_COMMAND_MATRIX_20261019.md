# ATA-4 complete command-family matrix

Primary reference: [T13/1153D revision 18, 19 August 1998 working draft](https://www.mit.edu/afs/sipb/contrib/doc/specs/protocol/ata/d1153r18.pdf). This is the retrieved draft, not a claim of final published-standard certification. SHA256: `9ac718ef8b9dbc309a7dc597288141acbd0e4715937c10e9ca4691cd94326ce9`.

Original production baseline: `0dcd36309877d5da59344893d61c7a374a7851e3`. See [comparison and evidence](DEV9_ATA4_COMMAND_SET_COMPARE_20261019.md) for capability flags, detailed subcommands, PS2 software expectations, limits and priorities. [All 256 command bytes](DEV9_ATA4_OPCODE_INVENTORY_20261019.csv) are classified independently from the modern SDK names.

Current implementation update: `874309ca1c` adds the corrections listed below.
The CSV now records current source dispatch and preserves its original result
in `baseline_comparison_result`. See the [implementation ledger](DEV9_ATA_IMPLEMENTATION_20261020.md).
All entries remain uncompiled and unexecuted.

## Reading the comparison

M = mandatory General; M-PM = mandatory basic Power Management; CHS = geometry-dependent; O = optional General; P = prohibited for this non-PACKET profile. CFA/SEC/HPA/Q/RM/SMART depend on the corresponding feature, with qualifications below. Protocol text comes from each command section. Handler presence is source evidence only: no compiled tests or device execution occurred. Finding references F1-F14 refer to the earlier source audit.

| Clause | ATA-4 command family | Bytes | Requirement | Protocol | Current branch |
| --- | --- | --- | --- | --- | --- |
| 8.1 | CFA ERASE SECTORS | `C0` | CFA | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.2 | CFA REQUEST EXTENDED ERROR CODE | `03` | CFA | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.3 | CFA TRANSLATE SECTOR | `87` | CFA | PIO data in command (see 9.7). | ABRT; feature not advertised / optional not implemented |
| 8.4 | CFA WRITE MULTIPLE WITHOUT ERASE | `CD` | CFA | PIO data out command (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.5 | CFA WRITE SECTORS WITHOUT ERASE | `38` | CFA | PIO data out command (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.6 | CHECK POWER MODE | `E5` | M-PM | Non-data command (see 9.9). | Implemented CHECK POWER; preserves state/timer; unrun |
| 8.7 | DEVICE RESET | `08` | P | Device reset (see 9.4 ). | ABRT; correct for fixed non-PACKET profile |
| 8.8 | DOWNLOAD MICROCODE | `92` | O | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.9 | EXECUTE DEVICE DIAGNOSTIC | `90` | M | Device diagnostics (see 9.5 ). | Diagnostic retained; reset/lifetime/init corrections; unrun |
| 8.10 | FLUSH CACHE | `E7` | O | Non-data command (see 9.9). | Queue/in-flight ordering and native durable flush implemented; unrun |
| 8.11 | GET MEDIA STATUS | `DA` | RM | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.12 | IDENTIFY DEVICE | `EC` | M | PIO data in (see 9.7). | Geometry/mode/SMART/PM/flush capability claims aligned; unrun |
| 8.13 | IDENTIFY PACKET DEVICE | `A1` | P | PIO data in (see 9.7). | ABRT; correct for fixed non-PACKET profile |
| 8.14 | IDLE | `E3` | M-PM | Non-data command (see 9.9). | Idle transition and standard standby timer implemented; unrun |
| 8.15 | IDLE IMMEDIATE | `E1` | M-PM | Non-data command (see 9.9). | Idle transition implemented; unrun |
| 8.16 | INITIALIZE DEVICE PARAMETERS | `91` | CHS | Non-data (see 9.9). | Validated translation and CHS access implemented; unrun |
| 8.17 | MEDIA EJECT | `ED` | RM | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.18 | MEDIA LOCK | `DE` | RM | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.19 | MEDIA UNLOCK | `DF` | RM | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.20 | NOP | `00` | O | Non-data (see 9.9). | Present intentional ABRT; no queued commands |
| 8.21 | PACKET | `A0` | P | Packet (see 9.11). | ABRT; correct for fixed non-PACKET profile |
| 8.22 | READ BUFFER | `E4` | O | PIO data in (see 9.7). | Implemented E4 diagnostic PIO data-in; one sector, no media writes; unrun |
| 8.23 | READ DMA | `C8 C9` | M | DMA (see 9.10). | C8/C9 decode; DMA bounds/reset/coherence corrections; unrun |
| 8.24 | READ DMA QUEUED | `C7` | Q | DMA QUEUED (see 9.12). | ABRT; feature not advertised / optional not implemented |
| 8.25 | READ MULTIPLE | `C4` | M | PIO data in (see 9.7). | Whole-range preflight; actual failure returns one full ERR/UNC block with DRQ; unrun |
| 8.26 | READ NATIVE MAX ADDRESS | `F8` | HPA | Non-data command (see 9.9). | Implemented F8 native max for ATA-4 profile; LBA48 profile defers HPA; unrun |
| 8.27 | READ SECTOR(S) | `20 21` | M | PIO data in (see 9.7). | 20/21 decode; data-in bounds/coherence/task-file corrections; unrun |
| 8.28 | READ VERIFY SECTOR(S) | `40 41` | M | Non-data (see 9.9). | 40/41 decode; reads backing image with range/error checks; unrun |
| 8.29 | SECURITY DISABLE PASSWORD | `F6` | SEC | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.30 | SECURITY ERASE PREPARE | `F3` | SEC | Non-data (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.31 | SECURITY ERASE UNIT | `F4` | SEC | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.32 | SECURITY FREEZE LOCK | `F5` | SEC | Non-data (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.33 | SECURITY SET PASSWORD | `F1` | SEC | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.34 | SECURITY UNLOCK | `F2` | SEC | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.35 | SEEK | `70` | M | Non-data (see 9.9). | Valid SEEK succeeds; invalid address IDNF; unrun |
| 8.36 | SERVICE | `A2` | Q | PACKET or READ/WRITE DMA QUEUED (see 9.11 and 9.12). | ABRT; feature not advertised / optional not implemented |
| 8.37 | SET FEATURES | `EF` | M | Non-data (see 9.9). | Validated selectors/modes; 66/CC reset reversion; safe PIO2/MDMA0 defaults; unrun |
| 8.38 | SET MAX ADDRESS | `F9` | HPA | Non-data command (see 9.9). | Implemented F9 limit, adjacency, NV quota/reset and metadata; no truncation; unrun |
| 8.39 | SET MULTIPLE MODE | `C6` | M | Non-data (see 9.9). | Validates 2..128 powers of two; retains zero-disable extension; unrun |
| 8.40 | SLEEP | `E6` | M-PM | Non-data command (see 9.9). | Sleep command and reset recovery implemented; durable entry; unrun |
| 8.41 | SMART | `B0` | SMART | Subcommand-specific (8.41.1-8.41.7) | D0 data, D3 persistence, D4 real bounded offline collection; core settings/status; unrun |
| 8.42 | STANDBY | `E2` | M-PM | Non-data command (see 9.9). | Standby state and standard timer implemented; durable entry; unrun |
| 8.43 | STANDBY IMMEDIATE | `E0` | M-PM | Non-data command (see 9.9). | Standby state and durable entry implemented; unrun |
| 8.44 | WRITE BUFFER | `E8` | O | PIO data out (see 9.8). | Implemented E8 diagnostic PIO data-out; one sector, reset-safe; unrun |
| 8.45 | WRITE DMA | `CA CB` | M | DMA (see 9.10). | CA/CB decode; bounded writes/reset/ordering/error corrections; unrun |
| 8.46 | WRITE DMA QUEUED | `CC` | Q | DMA QUEUED (see 9.12). | ABRT; feature not advertised / optional not implemented |
| 8.47 | WRITE MULTIPLE | `C5` | M | PIO data out (see 9.8). | Real PIO writes; whole-range preflight; accepted block/reset and write-error handling; unrun |
| 8.48 | WRITE SECTOR(S) | `30 31` | M | PIO data out (see 9.8). | Real30/31 PIO data-out, count/progress/persistence; unrun |

## Conditional requirements and source conflicts

- The 12 unconditional General families come from §6.3.1 (printed p21); all six basic Power Management commands and the standby timer are required by §6.8 (p27). A cleared synthetic power-management feature bit does not make their absence conforming.
- INITIALIZE DEVICE PARAMETERS is mandatory with CHS. §8.16.2 (p108) supplies an exception for >8 GB LBA-only drives; the broad §6.3.1 summary must be read with this exception. The baseline populated CHS fields while rejecting CHS access; the corrective implementation now translates CHS and validates geometry.
- Five optional General families are DOWNLOAD MICROCODE, FLUSH CACHE, NOP, READ BUFFER and WRITE BUFFER (§6.3.1). Optional does not mean a successful stub is sufficient once a command is implemented; FLUSH must meet §8.10 and PS2SDK independently issues it.
- CFA requires all five CFA commands when claimed (§6.13). Codes 87/C0 are vendor-specific if CFA is absent (Table E.1); no missing-CFA defect is asserted for this fixed non-CFA profile.
- Security Mode requires all six standard F1-F6 commands when claimed (§6.10). HPA requires both F8/F9 (§6.12). Standard security remains unclaimed. ATA-4 profiles now claim HPA with F8/F9 implemented; LBA48 profiles defer HPA and keep those feature bits clear. Sony 8E/F1-F5 is separate.
- GET MEDIA STATUS is required with Media Status Notification (§8.11). MEDIA EJECT is required with the applicable removable feature on a non-PACKET device (§8.17); MEDIA LOCK/UNLOCK are mandatory for traditional Removable Media but optional for Notification (§8.18-8.19). Neither profile is currently advertised.
- SERVICE is conditional on overlap/queued operations. Informative Table E.3 marks it N for non-PACKET, but normative §8.36.3/.6 explicitly includes READ/WRITE DMA QUEUED. It must not be declared intrinsically prohibited for every non-PACKET drive.
- SMART is an optional feature, currently advertised; §6.11.5 requires D2/D8/D9/DA, not every later SMART selector. D0/D4 are optional; D3 is optional and not recommended (§8.41.7.2). Detailed comparison is in the evidence report.

## Full opcode-space reconciliation

Table E.1 has C=51 defined codes plus F=2 CFA/vendor-dependent codes, R=128 reserved, E=40 retired, V=27 vendor-specific and O=8 obsolete: 256 total. The 48 families above account for 53 defined bytes; five families have two defined encodings (20/21,30/31,40/41,C8/C9,CA/CB). Hosts are advised against the alternate encoding, but it remains defined in the command section.

For the compared >8 GB LBA-only profile, all 18 unconditional families / 23 bytes
now have dispatch cases; including CHS command91 gives19 families /24 bytes.
The baseline lacked11 encodings (`21 30 31 41 C5 C9 CB E0 E2 E5 E6`), now present.
Power state/timer behavior and real PIO writes are implemented. These are source
coverage counts, not executable test passes or conformance certification.

Obsolete 10 RECALIBRATE is implemented for compatibility; 11-1F and 71-7F are retired. Those ranges must not be masked into current commands without an explicit compatibility policy. Existing later LBA48-gated opcodes occupy bytes reserved in ATA-4; their later profile must be checked separately rather than deleted merely to make an ATA-4 table look tidy.

## Verification limits

All 48 families and all 256 opcode cells are accounted for against the retrieved draft. This is a complete command-set source comparison for the stated profile, not electrical bus certification, an exhaustive protocol test, proof of every Sony physical-drive firmware feature, or a claim that PSBBN now boots. Production corrections are checkpointed; compilation, sanitizers, Windows tests and guest execution were not run.

## Subsequent command implementation

READ/WRITE BUFFER and SMART D0/D3/D4 are now implemented, together with SET
FEATURES 66/CC reversion behavior and safe transfer-mode defaults. See
`DEV9_ATA_BUFFER_COMMANDS_20261020.md`, `DEV9_ATA_SMART_COMMANDS_20261020.md`, and
`DEV9_ATA_COMMAND_SEMANTICS_20261020.md`. These are source changes; compiled
regressions and runtime conformance remain unrun. Unclaimed CFA, queued DMA,
firmware download and standard security remain separate optional profiles. HPA is now implemented for the ATA-4 profile as described below.

## HPA and multiple-error protocol update

F8/F9 are implemented and claimed for native images which do not enable LBA48.
The accessible limit affects IDENTIFY and all host sector bounds without changing
native backing-file size. Later 27/37 HPA and HPA security extensions remain
unsupported and unclaimed; large-image data commands/capacity remain available.
See `DEV9_ATA_HPA_COMMANDS_20261020.md` for exact chosen-draft semantics and metadata.

C4/29 real backing read failures now deliver valid preceding blocks and one full
ERR/UNC block, with unread bytes zero-filled and no subsequent blocks. Invalid
whole multiple-command ranges ABRT before execution; failed INIT translation
continues to require IDNF. C5/39 retain accepted-block persistence/write-fault
handling. See `DEV9_ATA_MULTIPLE_ERRORS_20261020.md` for clauses and distinctions.
All 75 prospective ATA cases and seven FIFO cases remain uncompiled/unexecuted.
