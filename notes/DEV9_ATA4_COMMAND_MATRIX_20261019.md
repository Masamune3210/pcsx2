# ATA-4 complete command-family matrix

Primary reference: [T13/1153D revision 18, 19 August 1998 working draft](https://www.mit.edu/afs/sipb/contrib/doc/specs/protocol/ata/d1153r18.pdf). This is the retrieved draft, not a claim of final published-standard certification. SHA256: `9ac718ef8b9dbc309a7dc597288141acbd0e4715937c10e9ca4691cd94326ce9`.

Production baseline: `0dcd36309877d5da59344893d61c7a374a7851e3`. See [comparison and evidence](DEV9_ATA4_COMMAND_SET_COMPARE_20261019.md) for capability flags, detailed subcommands, PS2 software expectations, limits and priorities. [All 256 command bytes](DEV9_ATA4_OPCODE_INVENTORY_20261019.csv) are classified independently from the modern SDK names.

## Reading the comparison

M = mandatory General; M-PM = mandatory basic Power Management; CHS = geometry-dependent; O = optional General; P = prohibited for this non-PACKET profile. CFA/SEC/HPA/Q/RM/SMART depend on the corresponding feature, with qualifications below. Protocol text comes from each command section. Handler presence is source evidence only: no compiled tests or device execution occurred. Finding references F1-F14 refer to the earlier source audit.

| Clause | ATA-4 command family | Bytes | Requirement | Protocol | Current branch |
| --- | --- | --- | --- | --- | --- |
| 8.1 | CFA ERASE SECTORS | `C0` | CFA | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.2 | CFA REQUEST EXTENDED ERROR CODE | `03` | CFA | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.3 | CFA TRANSLATE SECTOR | `87` | CFA | PIO data in command (see 9.7). | ABRT; feature not advertised / optional not implemented |
| 8.4 | CFA WRITE MULTIPLE WITHOUT ERASE | `CD` | CFA | PIO data out command (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.5 | CFA WRITE SECTORS WITHOUT ERASE | `38` | CFA | PIO data out command (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.6 | CHECK POWER MODE | `E5` | M-PM | Non-data command (see 9.9). | Missing mandatory command; ABRT |
| 8.7 | DEVICE RESET | `08` | P | Device reset (see 9.4 ). | ABRT; correct for fixed non-PACKET profile |
| 8.8 | DOWNLOAD MICROCODE | `92` | O | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.9 | EXECUTE DEVICE DIAGNOSTIC | `90` | M | Device diagnostics (see 9.5 ). | Present; reset/initialization gaps F5/F10 |
| 8.10 | FLUSH CACHE | `E7` | O | Non-data command (see 9.9). | Present; ordering/durability gaps F3/F9/F14 |
| 8.11 | GET MEDIA STATUS | `DA` | RM | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.12 | IDENTIFY DEVICE | `EC` | M | PIO data in (see 9.7). | Present; capability/geometry inconsistencies |
| 8.13 | IDENTIFY PACKET DEVICE | `A1` | P | PIO data in (see 9.7). | ABRT; correct for fixed non-PACKET profile |
| 8.14 | IDLE | `E3` | M-PM | Non-data command (see 9.9). | Present stub; no timer or power-state model |
| 8.15 | IDLE IMMEDIATE | `E1` | M-PM | Non-data command (see 9.9). | Present stub; no power-state transition |
| 8.16 | INITIALIZE DEVICE PARAMETERS | `91` | CHS | Non-data (see 9.9). | Present; geometry divide-by-zero/head-mask gap F6 |
| 8.17 | MEDIA EJECT | `ED` | RM | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.18 | MEDIA LOCK | `DE` | RM | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.19 | MEDIA UNLOCK | `DF` | RM | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.20 | NOP | `00` | O | Non-data (see 9.9). | Present intentional ABRT; no queued commands |
| 8.21 | PACKET | `A0` | P | Packet (see 9.11). | ABRT; correct for fixed non-PACKET profile |
| 8.22 | READ BUFFER | `E4` | O | PIO data in (see 9.7). | ABRT; feature not advertised / optional not implemented |
| 8.23 | READ DMA | `C8 C9` | M | DMA (see 9.10). | C8 present; C9 aborts; DMA lifetime/coherence gaps |
| 8.24 | READ DMA QUEUED | `C7` | Q | DMA QUEUED (see 9.12). | ABRT; feature not advertised / optional not implemented |
| 8.25 | READ MULTIPLE | `C4` | M | PIO data in (see 9.7). | Present; bounds/coherence/IRQ gaps |
| 8.26 | READ NATIVE MAX ADDRESS | `F8` | HPA | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.27 | READ SECTOR(S) | `20 21` | M | PIO data in (see 9.7). | 20 present; 21 aborts; bounds/coherence/IRQ gaps |
| 8.28 | READ VERIFY SECTOR(S) | `40 41` | M | Non-data (see 9.9). | 40 present; 41 aborts; only range checked, no media read |
| 8.29 | SECURITY DISABLE PASSWORD | `F6` | SEC | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.30 | SECURITY ERASE PREPARE | `F3` | SEC | Non-data (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.31 | SECURITY ERASE UNIT | `F4` | SEC | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.32 | SECURITY FREEZE LOCK | `F5` | SEC | Non-data (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.33 | SECURITY SET PASSWORD | `F1` | SEC | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.34 | SECURITY UNLOCK | `F2` | SEC | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.35 | SEEK | `70` | M | Non-data (see 9.9). | Present; inverted seek predicate F4 |
| 8.36 | SERVICE | `A2` | Q | PACKET or READ/WRITE DMA QUEUED (see 9.11 and 9.12). | ABRT; feature not advertised / optional not implemented |
| 8.37 | SET FEATURES | `EF` | M | Non-data (see 9.9). | Present; unsupported selectors succeed and modes overaccepted |
| 8.38 | SET MAX ADDRESS | `F9` | HPA | Non-data command (see 9.9). | ABRT; feature not advertised / optional not implemented |
| 8.39 | SET MULTIPLE MODE | `C6` | M | Non-data (see 9.9). | Present; accepts unsupported block counts |
| 8.40 | SLEEP | `E6` | M-PM | Non-data command (see 9.9). | Missing mandatory command; ABRT |
| 8.41 | SMART | `B0` | SMART | Subcommand-specific (8.41.1-8.41.7) | Present partial; D2 reads wrong register; see subcommand table |
| 8.42 | STANDBY | `E2` | M-PM | Non-data command (see 9.9). | Missing mandatory command; ABRT |
| 8.43 | STANDBY IMMEDIATE | `E0` | M-PM | Non-data command (see 9.9). | Missing mandatory command; ABRT; used by PS2SDK shutdown |
| 8.44 | WRITE BUFFER | `E8` | O | PIO data out (see 9.8). | ABRT; feature not advertised / optional not implemented |
| 8.45 | WRITE DMA | `CA CB` | M | DMA (see 9.10). | CA present; CB aborts; reset/ordering/error gaps |
| 8.46 | WRITE DMA QUEUED | `CC` | Q | DMA QUEUED (see 9.12). | ABRT; feature not advertised / optional not implemented |
| 8.47 | WRITE MULTIPLE | `C5` | M | PIO data out (see 9.8). | Missing mandatory PIO data-out command; ABRT |
| 8.48 | WRITE SECTOR(S) | `30 31` | M | PIO data out (see 9.8). | 30 and 31 both absent; missing mandatory PIO data-out |

## Conditional requirements and source conflicts

- The 12 unconditional General families come from §6.3.1 (printed p21); all six basic Power Management commands and the standby timer are required by §6.8 (p27). A cleared synthetic power-management feature bit does not make their absence conforming.
- INITIALIZE DEVICE PARAMETERS is mandatory with CHS. §8.16.2 (p108) supplies an exception for >8 GB LBA-only drives; the broad §6.3.1 summary must be read with this exception. Current IDENTIFY populates CHS fields even though CHS access is rejected, so identity and command semantics need reconciliation.
- Five optional General families are DOWNLOAD MICROCODE, FLUSH CACHE, NOP, READ BUFFER and WRITE BUFFER (§6.3.1). Optional does not mean a successful stub is sufficient once a command is implemented; FLUSH must meet §8.10 and PS2SDK independently issues it.
- CFA requires all five CFA commands when claimed (§6.13). Codes 87/C0 are vendor-specific if CFA is absent (Table E.1); no missing-CFA defect is asserted for this fixed non-CFA profile.
- Security Mode requires all six standard F1-F6 commands when claimed (§6.10). HPA requires both F8/F9 (§6.12). Current drive does not advertise either feature. Sony 8E/F1-F5 is separate.
- GET MEDIA STATUS is required with Media Status Notification (§8.11). MEDIA EJECT is required with the applicable removable feature on a non-PACKET device (§8.17); MEDIA LOCK/UNLOCK are mandatory for traditional Removable Media but optional for Notification (§8.18-8.19). Neither profile is currently advertised.
- SERVICE is conditional on overlap/queued operations. Informative Table E.3 marks it N for non-PACKET, but normative §8.36.3/.6 explicitly includes READ/WRITE DMA QUEUED. It must not be declared intrinsically prohibited for every non-PACKET drive.
- SMART is an optional feature, currently advertised; §6.11.5 requires D2/D8/D9/DA, not every later SMART selector. D0/D4 are optional; D3 is optional and not recommended (§8.41.7.2). Detailed comparison is in the evidence report.

## Full opcode-space reconciliation

Table E.1 has C=51 defined codes plus F=2 CFA/vendor-dependent codes, R=128 reserved, E=40 retired, V=27 vendor-specific and O=8 obsolete: 256 total. The 48 families above account for 53 defined bytes; five families have two defined encodings (20/21,30/31,40/41,C8/C9,CA/CB). Hosts are advised against the alternate encoding, but it remains defined in the command section.

For a >8 GB LBA-only fixed non-PACKET profile, the 18 unconditional families expand to 23 opcode bytes. Direct cases exist for 12; 11 are missing: `21 30 31 41 C5 C9 CB E0 E2 E5 E6`. With CHS requirements included, add 91: 19 families / 24 bytes, 13 direct cases. These are dispatch counts, not passes.

Six entire required families are absent: WRITE SECTORS, WRITE MULTIPLE, STANDBY IMMEDIATE, STANDBY, CHECK POWER MODE and SLEEP. Four other families lack their alternate encoding: READ SECTORS, READ VERIFY, READ DMA and WRITE DMA. Present IDLE/IDLE IMMEDIATE do not implement the required power-state/timer behavior.

Obsolete 10 RECALIBRATE is implemented for compatibility; 11-1F and 71-7F are retired. Those ranges must not be masked into current commands without an explicit compatibility policy. Existing six LBA48-gated opcodes occupy bytes reserved in ATA-4; their later profile must be checked separately rather than deleted merely to make an ATA-4 table look tidy.

## Verification limits

All 48 families and all 256 opcode cells are accounted for against the retrieved draft. This is a complete command-set source comparison for the stated profile, not electrical bus certification, an exhaustive protocol test, proof of every Sony physical-drive firmware feature, or a claim that PSBBN now boots. Production files are unchanged; compilation, sanitizers, Windows tests and guest execution were not run.
