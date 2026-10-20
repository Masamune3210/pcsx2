# Full ATA-4 command-set source comparison

Status: **command-set source comparison complete** against the retrieved
T13/1153D revision-18 working draft: [all 48 defined command families](DEV9_ATA4_COMMAND_MATRIX_20261019.md),
[all 256 command bytes](DEV9_ATA4_OPCODE_INVENTORY_20261019.csv), feature-set
requirements, legal alternate encodings, and SMART/SET FEATURES/Sony subcommands.
No compilation, device execution or production changes. This is not a claim of
final published-standard certification or runtime conformance.

For a >8 GB LBA-only fixed non-PACKET HDD, 18 unconditional command families
expand to 23 defined opcode bytes. The dispatcher has 12; **11 are missing**:
`21 30 31 41 C5 C9 CB E0 E2 E5 E6`. Six whole required families are absent,
and four others lack an alternate encoding. If CHS requirements apply, add `91`
(19 families / 24 bytes, 13 dispatched). Present handlers can still be defective.

Production baseline: `0dcd36309877d5da59344893d61c7a374a7851e3`; previous source
audit checkpoint: `26e221cbd8a8887032d5841bb02edcae40b0bd85`.

## Comparison profile and evidence rules

The intended default is a fixed ATA HDD behind PS2 DEV9/SPEED, compatible with
Sony's 40 GB HDD software.

The attached drive is virtual and image-backed, not host ATA pass-through. The
image supplies sector data, not IDENTIFY capabilities. Physical Sony hardware
evidence informs compatibility but does not define the virtual drive's feature
table. Existing advanced features must be assessed on their own merits rather
than removed solely because the controller is described as ATA-4. The comparison
answers three independent questions:

1. Which commands ATA/ATAPI-4 requires for a fixed ATA device, and which become
   required only when an optional feature set is implemented.
2. Which commands actual PS2 software uses, including Sony's vendor extensions.
3. Which capabilities the emulated device advertises and actually implements.

An opcode handler is not a passing command implementation. Neither a driver's
opcode constants nor its generic protocol lookup table proves physical support.
ATA-4 does not mean implementing all ATAPI/CFA/removable-media commands. Conversely,
disabling an advertisement is not sufficient when real guest software uses the
command. LBA48 and later command families must be compared separately.

## Retrieved primary reference

The comparison uses T13 project 1153D, ATA/ATAPI-4 revision 18, dated
1998-08-19, downloaded from [MIT SIPB's mirror](https://www.mit.edu/afs/sipb/contrib/doc/specs/protocol/ata/d1153r18.pdf).
SHA256: `9ac718ef8b9dbc309a7dc597288141acbd0e4715937c10e9ca4691cd94326ce9`.
The cover explicitly identifies it as an internal working draft, not an approved
completed standard. Clauses and **printed** page numbers below refer to this copy.
No copy of the PDF is republished in the repository.

Key references: §6.3.1 (p21) General commands; §6.8 (p27) basic Power
Management; §6.9-6.14 optional features; §8.1-8.48 command definitions;
§8.37 / Tables 19-20 SET FEATURES; §8.41 / Table 22 SMART; §9 protocols;
Annex E Tables E.1-E.3 (pp318-321) the complete command-space summary.

Retrieval history: initial T13/MIT requests were denied; the exact MIT SIPB URL
subsequently returned the complete document. The candidate T13 project URL
returned 404. [The reference index](https://github.com/kaitai-io/kaitai_struct_formats/issues/526)
helped locate the mirror but is not the normative source. Network-domain
additions were saved in the environment draft; successful MIT retrieval is
observed separately from draft saving and publication.

Normative command clauses control over informative Annex E where they differ:
§8.16.2 qualifies INITIALIZE DEVICE PARAMETERS for >8 GB LBA-only drives;
§8.36 explicitly includes queued-DMA SERVICE despite Table E.3's non-PACKET
marking. §8.12.45/.46 also contain broad reserved-bit sentences that conflict
with their subsequent explicit definitions for bits 1-4; those bits must not be
misclassified by reading only the broad sentence.

## Reproducible PS2 software expectations

PS2SDK source pinned at `329e2bb43287e16af16c4dde1c614e2662c24468`:

- [ATAD implementation](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/dev9/atad/src/ps2atad.c)
- [ATA opcode definitions](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/common/include/atahw.h)
- [ATAD interface](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/dev9/atad/include/atad.h)
- [ATAD build options](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/dev9/atad/Makefile)

SHA256 of the retrieved `ps2atad.c`:
`07b15b94282a3373769aabb62518ebd1e255469e2f575cb1bdaf83c3bd5ec7bf`.
These are primary sources for an open-source driver, not an official Sony drive
specification or an observed trace. The generic command table at lines 93-149
mixes multiple ATA generations and must not define the hardware profile.

| Command | Concrete PS2SDK command path | PCSX2 comparison |
| --- | --- | --- |
| `EC` IDENTIFY DEVICE | `ps2atad.c:922-929,1287-1291` initialization | Present; geometry safety and advertisement issues in prior audit |
| `EF/03` SET TRANSFER MODE | `:998-1008,1336` initialization | Present; accepts unsupported mode values |
| `B0/D8` SMART ENABLE | `:953-960,1342` initialization attempt | Present |
| `E3` IDLE / standby timer `FF` | `:911-919,1343-1344` initialization attempt | Present as logging/immediate completion, no implemented timer state |
| `C8` / `CA` READ / WRITE DMA | `:1041-1085` ordinary I/O | Present; reset/bounds/coherence gaps in prior audit |
| `E7` FLUSH CACHE | `:900-908` exported helper | Present; queue/in-flight write barrier gap |
| `B0/D3` SMART SAVE ATTRIBUTES | `:963-971` exported helper | Reports success as a stub |
| `B0/DA` SMART RETURN STATUS | `:974-994` exported helper | Synthetic status rather than actual backing-media health |
| `E1` IDLE IMMEDIATE | `:1517-1525` exported helper | Present, immediate completion |
| **`E0` STANDBY IMMEDIATE** | **`:1528-1544` shutdown callback, registered at `:341-342`** | **Absent; defaults to ABRT. New directly evidenced PS2 compatibility gap** |
| `8E/EC` Sony IDENTIFY | `:942-950` helper; authentication check conditional at `:1371-1377` | Present; separate vendor-ID response, not standard IDENTIFY |
| `8E/F1`, `8E/F2`, `8E/F3` then `8E/F4` | `:1121-1187` Sony password/unlock/erase helpers | All explicitly abort in `ATA_SCE.cpp` |
| `25` / `35`, `EA` EXT variants | Conditional on detected LBA48 capability | Present conditionally; later profile, not evidence of ATA-4 requirements |

`E0` is not merely a constant in a header: the driver actually issues it during
shutdown. Correct emulation need not simulate mechanical spin-down, but should
have a defined command result/state and ordering for accepted writes. Do not
implement a success-only stub that conceals write-flush errors.

The default PS2SDK UDMA selection considers modes through 4 from IDENTIFY word 88
(`ps2atad.c:1320-1336`); optional `ATA_ENABLE_MAXUDMA` permits through 6 but
defaults to zero (`Makefile:24-25`). This is driver/controller policy, not a claim
that ATA-4 itself defines all those modes. The AIF path selects PIO4.

Sony authentication is optional in this SDK: `ATA_SCE_AUTH_HDD` defaults to zero
(`Makefile:9-10`). Standard ATA security opcodes `F1`-`F6` must not be conflated
with Sony command `8E` plus feature selectors `F1`-`F5`. Interface `atad.h:63`
explicitly distinguishes the Sony-modified HDD functions.

## Exhaustive implementation-side opcode inventory

[The CSV inventory](DEV9_ATA4_OPCODE_INVENTORY_20261019.csv) has exactly one
row for every command byte `00`-`FF`, including unassigned-to-this-implementation
bytes. It was enumerated from `IDE_ExecCmd`, not counted from comments:

- 18 direct dispatch cases, including the Sony vendor command `8E`.
- 6 cases conditional on `lba48Supported`: `24`, `25`, `29`, `35`, `42`, `EA`.
- 232 bytes take the default `HDD_Unk` / ABRT path. **This is not a count of
  232 missing ATA-4 commands**: reserved, vendor, obsolete, ATAPI/CFA and later
  assignments still require classification against the original standard.

Every row is now classified from ATA-4 Table E.1 and mapped to its applicable
command/feature clauses. The independent modern PS2SDK-name column remains
supplementary: 188 bytes have names there, including later assignments to
ATA-4-reserved bytes. Source references are to the unchanged production baseline.
The table reconciles 51 C + 2 F (CFA/vendor-dependent) + 128 R + 40 E (retired)
+ 27 V + 8 O (obsolete) = 256. The 48 defined families occupy 53 bytes.

## Advertised feature families versus actual command paths

The following compares `ATA_Info.cpp` with the retrieved command/feature clauses.
Missing commands in an optional unadvertised family are not automatically
defects. Basic Power Management is an exception: §6.8 requires it for this
non-PACKET HDD, so clearing its feature bit does not excuse missing commands.
Later feature meanings must be evaluated under the later advertised profile,
not backdated into ATA-4.

| Family / identity field | Advertised by the virtual drive | Actual implementation / mismatch |
| --- | --- | --- |
| Addressing and data transfer | Word 49 LBA/DMA/IORDY bits set (`0B00`) | LBA reads/DMA present; CHS rejected; PIO write-sector command `30` absent |
| Geometry validity | Word 53 bits 0-2 set; words 54-58 populated | `91` accepts zero sectors/incorrect head mask; CHS access not implemented |
| Multiple transfers | Word 47 `8080`, word 59 valid/current count | `C4`/`C6` present; `C5` missing; excessive negotiated counts accepted |
| Multiword DMA | Word 63 mask `0007` plus selected mode | `C8`/`CA` present; SET FEATURES accepts modes beyond 0-2 |
| Advanced PIO | Word 64 mask `0003` | PIO input exists; PIO output absent; transfer mode validation too broad |
| Ultra DMA | Word 88 mask `007F` plus selected mode | Modes 0-6 advertised; selection accepts mode 7; controller and standard generation need separate analysis |
| ATA revision | Word 80 `0070`, word 81 `0018` | ATA-4/5/6 major bits set; do not interpret the nearby minor-version comment as a checked specification citation |
| SMART | Word 82 bit 0; word 85 follows enabled flag | Mandatory D2 autosave uses the wrong register; optional D0 data read aborts; later logging support is incomplete |
| SMART logging/self-test | Words 84/87 bits 0-1 set | READ LOG aborts; self-test bookkeeping is synthetic and log storage commented out |
| Write cache | Word 82 bit 5; word 85 follows enabled flag | `EF/02` and `EF/82` change flag; DMA queues accepted writes; read/flush ordering defects remain |
| NOP | Word 82 bit 14 | `00` intentionally returns ABRT; ABRT is not by itself proof that NOP is missing |
| Power management | Word 82 bit 3 clear; word 85 bit 3 clear | §6.8 requires six commands and timer; `E1`/`E3` are stubs, `E0`/`E2`/`E5`/`E6` abort; PS2SDK also issues E0 |
| Standard security | Word 82 bit 1 clear; enabled flag false | `F1`-`F6` all default ABRT; Sony security is a separate command namespace |
| Host protected area | Word 82 bit 10 clear; enabled flag false | `F8`/`F9` default ABRT; no HPA emulation established |
| Read look-ahead | Word 82 bit 6 clear | No look-ahead state; unsupported feature selectors nevertheless report success |
| READ/WRITE BUFFER | Word 82 bits 12/13 clear | `E4`/`E8` default ABRT; optional under §6.3.1, not missing mandatory commands |
| Download microcode | Word 83 bit 0 clear | `92` default ABRT; no firmware download engine |
| Queued DMA | Word 83 bit 1 clear, queue depth zero | `C7`/`CC` and `A2` default ABRT; no ATA queued-command engine |
| Advanced power management | Word 83 bit 3 clear | No APM state; `EF` unsupported-feature behavior needs review |
| Removable/media status | Word 82 bit 2 and word 83 bit 4 clear | `DA`/`DE`/`DF`/`ED` default ABRT; fixed-image profile |
| Packet device | Word 82 bit 4 clear; ATA fixed-device identity | `08`/`A0`/`A1`/`A2` default ABRT; ATAPI is not automatically required for an ATA HDD |
| CFA | Word 83 bit 2 clear; no CFA identity | `03`/`38`/`87`/`C0`/`CD` default ABRT; no CFA profile |
| 48-bit addressing | Words 83/86 bit 10 conditional on image size; words 100-103 conditional | `24`/`25`/`29`/`35`/`42` present conditionally; `34` WRITE SECTORS EXT and `39` WRITE MULTIPLE EXT absent; assess under later standard separately |
| FLUSH CACHE | Words 83/86 bit 12 set | `E7` present; in-flight write barrier/durability limitations |
| FLUSH CACHE EXT | Words 83/86 bit 13 always set | `EA` aborts on smaller images where LBA48 is disabled: direct advertisement/dispatch inconsistency |
| Sony HDD ID/security | Separate SCE response loaded/synthesized in `ATA::Open` | `8E/EC` present; other security/ID operations below abort |

Revision boundary: ATA-4 §8.12.47 defines UDMA 0-2 and reserves the higher mode
bits; §8.12.45/.46 reserve word 84/87 feature bits 0-13. Later code uses these
bits for logging/self-test and word 83 bits 10/12/13 for LBA48/flush claims.
Those later meanings must not become invented ATA-4 mandatory commands.
Table 12 in this draft reserves minor revision `0018`; decoding its later
assignment needs the later revision reference. None of these observations is
a recommendation to erase working extensions or reduce image capacity.

Relevant source regions: `ATA_Info.cpp:114-171,188-289,329-347`;
`ATA_State.cpp:98,555-650`; `ATA_CmdNoData.cpp:118-187`;
`ATA_Command.cpp:7-129`. The [earlier findings](DEV9_ATA4_HDD_SOURCE_FINDINGS_20261019.md)
provide transfer/reset/ordering evidence. No capability has been removed or
silently restricted as part of this comparison.

## Subcommand comparisons

### SMART (`B0`)

The wrapper requires cylinder signature `4F/C2` and enabled SMART except for
enable operations (`ATA_CmdSMART.cpp:11-23`). These are source conditions, not
runtime checks performed by this audit.

| Feature byte | ATA-4 requirement | Current path | Comparison |
| --- | --- | --- | --- |
| `D0` READ DATA | Optional (§6.11.5, §8.41.5) | Explicit ABRT (`:47-50`) | Not a missing mandatory ATA-4 core subcommand |
| `D1` READ THRESHOLDS | Obsolete (Table 22) | Explicit ABRT (`:43-46`) | No missing ATA-4 requirement |
| `D2` ATTRIBUTE AUTOSAVE | Mandatory if SMART (§8.41.2) | Changes bool for **`regSector`** 00/F1 (`:65-81`) | **Wrong register: §8.41.2.4/.8 requires Sector Count** |
| `D3` SAVE ATTRIBUTES | Optional, not recommended (§8.41.7.2) | Stub success (`:84-89`) | No stored attributes persisted; SDK helper does issue it |
| `D4` EXECUTE OFFLINE | Optional (§6.11.5, §8.41.4) | Synthetic counter/status (`:91-127`) | No actual test/log data; later self-test selectors need later specification |
| `D5` READ LOG | Reserved (Table 22) | Explicit ABRT (`:51-54`) | Correct reserved response for ATA-4 alone; later logging advertisement still inconsistent |
| `D8` ENABLE | Mandatory if SMART (§8.41.3) | Sets flag (`:130-135`) | Basic state present; power-cycle persistence not demonstrated |
| `D9` DISABLE | Mandatory if SMART (§8.41.1) | Clears flag (`:130-135`) | Basic state/gating present; power-cycle persistence not demonstrated |
| `DA` RETURN STATUS | Mandatory if SMART (§8.41.6) | Synthetic `smartErrors` health (`:137-150`) | No host I/O health/error propagation established |
| All other 247 byte values | See exhaustive partition below | Default ABRT (`:58-61`) | Includes ATA-4-reserved `D6` WRITE LOG; not automatically a missing ATA-4 command |

Table 22 accounts for all 256 feature bytes: seven defined subcommands, three
obsolete (`D1/D7/DB`), 214 reserved (`00-CF`, `D5-D6`, `DC-DF`), and 32 vendor
specific (`E0-FF`). Four of the seven defined subcommands are mandatory when
SMART is claimed (`D2/D8/D9/DA`); the other three are optional. In particular,
missing READ DATA or READ LOG must not be used to inflate ATA-4 mandatory counts.
The later error-logging/self-test advertisements are a separate issue.

§8.41.1.8 and §8.41.2.8 require persistence of SMART enabled/autosave state
across power cycles. The audited implementation stores these as ATA object
booleans; no persistence path for those flags was found in the audited source.
Persistence therefore needs an explicit virtual-drive policy and validation.

### SET FEATURES (`EF`)

§8.37.2 requires `03` for all devices, `02/82` when write cache is implemented,
and `31/95` for the applicable removable-media feature. Other subcommands are
optional. §8.37.6 requires ABRT for an unsupported selector or invalid parameter.

| Table 19 feature bytes | ATA-4 meaning / requirement | Current path and comparison |
| --- | --- | --- |
| `02`, `82` | Enable/disable write cache; required for this implemented cache | Sets bool; disabling waits through Async. §8.37.9 requires flushing to nonvolatile memory before completion; ordering/error/durability work remains |
| `03` | Set transfer mode; mandatory | Class/mode decoder present, but overaccepts values and clears both DMA selections on PIO selection |
| `05`, `85` | Enable/disable APM; optional feature §6.9 | Unadvertised/unimplemented; logs then returns success instead of unsupported ABRT |
| `31`, `95` | Disable/enable Media Status Notification; feature-conditional | Fixed target does not claim removable media; unsupported selectors nevertheless report success |
| `55`, `AA` | Disable/enable read look-ahead; optional | Not advertised or implemented; success without changing a feature |
| `5D`, `DD` | Enable/disable release interrupt; optional | No overlapping-command state; success without implementing selector |
| `5E`, `DE` | Enable/disable SERVICE interrupt; optional | No queued/overlap engine; success without implementing selector |
| `66`, `CC` | Disable/enable reverting to power-on defaults; optional | No corresponding state or reset policy; success without implementing selector |
| `01`, `81` | Retired | Unknown-feature path reports success |
| `04`, `33`, `44`, `54`, `77`, `84`, `88`, `99`, `9A`, `AB`, `BB` | Obsolete | Unknown-feature path reports success; not missing active commands |
| All other 228 values | Reserved in Table 19 | Unknown-feature path reports success instead of unsupported ABRT |

These groups partition all 256 feature bytes: 15 active selectors, two retired,
11 obsolete and 228 reserved. Only three selectors (`02/03/82`) have actual
outer-switch handlers. The remaining 253 take the silent-success behavior.

For feature `03`, 32 of 256 Sector Count encodings enter recognized classes;
224 enter the unknown-mode abort branch. Table 20 allows default PIO `00` and
conditionally `01` (disable IORDY), not arbitrary defaults `02-07`. PIO/MWDMA/UDMA
selection must respect supported modes. The drive's masks indicate PIO through
4, MWDMA through 2, and later UDMA through 6, while each class accepts mode 7.
ATA-4 itself defines UDMA 0-2 (§8.12.47); higher existing modes are later
extensions to assess separately, not features to delete automatically.

§8.37.10 requires one PIO mode and one DMA mode selected at all times. Both PIO
branches set `mdmaMode = udmaMode = -1` (`ATA_CmdNoData.cpp:140-152`), after
which the DMA copy methods' mode gates return zero. PIO selection must not be
treated as sufficient reason to erase the separately selected DMA mode.

SET MULTIPLE MODE also has an exact ATA-4 input set: §8.39.4 specifies
2/4/8/16/32/64/128, §8.39.6 requires ABRT for unsupported counts, and §8.39.8
requires support for the advertised maximum. This implementation accepts any
byte. Its zero-disable behavior is an existing compatibility behavior; it is
not a defined host input in this revision's §8.39.4, and zero must not be
expanded to a 256-sector multiple block by borrowing transfer-count semantics.

### Sony vendor command (`8E`)

| Feature byte | Current path | Evidence / gap |
| --- | --- | --- |
| `EC` IDENTIFY DRIVE | Returns 512 bytes of SCE ID | Actual PS2SDK helper exists; distinct from standard `EC` |
| `F1` SET PASSWORD | Explicit ABRT | Actual Sony helper issues it |
| `F2` UNLOCK | Explicit ABRT | Actual Sony helper issues it |
| `F3` ERASE PREPARE | Explicit ABRT | Actual Sony erase helper issues it |
| `F4` ERASE UNIT | Explicit ABRT | Actual Sony erase helper issues it after F3 |
| `F5` FREEZE LOCK | Explicit ABRT | Named in SDK protocol table; invocation not established here |
| `20` READ ID / `30` WRITE ID | Explicit ABRT | Named in SDK protocol table; invocation not established here |
| Other 248 values | Default ABRT | No claim of additional Sony command support |

Counts above partition selector-byte spaces; they are not test-pass counts.
The Sony namespace is outside standard ATA-4 and must not inflate an ATA-4
mandatory-command failure count.

## Physical hardware evidence and unresolved profile

A [first-hand unit report](https://github.com/CosmicScale/PSBBN-Definitive-Project/issues/563)
identifies a Japanese SCPH-20400 external enclosure / SCPH-20401 40 GB unit as
a Maxtor D540X-4D / 4D040H2, manufactured 2002-04-24. This is useful evidence for
that reported unit, not proof of all official Sony HDD models or firmware.

No manufacturer command-support table or decoded real-drive standard `EC`
IDENTIFY payload was obtained. An `HDD_ID.bin` or Sony `8E/EC` response does not
substitute for the standard `EC` capability words. PCSX2's default SCE model
string and its generated IDENTIFY flags cannot prove real hardware support.

The prior [run11 research report](https://github.com/Masamune3210/ps2bbndecomp/blob/5f11d2bf562e5f091fdec2271860f8e6994bda91/docs/PSBBN_INTERPRETER_LIVE_RUN11_ATA_WRITE_MULTIPLE_20261020_v1.md)
documents guest WRITE MULTIPLE attempts and `no DRQ` failures, including reported
27 host `Unknown cmd c5` entries. The raw log is not in this checkout; this audit
has read the report, not re-executed or independently recounted the raw evidence.

## Result and correction priorities

1. **Complete mandatory PIO data-out**: §8.47/§8.48 and §9.8 require real WRITE
   MULTIPLE and WRITE SECTORS transfer behavior. Add `30/31/C5` only with the
   Data-register write path, block counts, tails, error addresses, queue ordering,
   reset cleanup and actual sector persistence. `C6`/`C4` are already present.
2. **Complete basic Power Management**: §6.8 makes `E0/E2/E5/E6`, the IDLE
   behaviors and standby timer required. A virtual device need not consume less
   real electricity, but must expose correct command-visible states, timer,
   CHECK POWER MODE results, and SLEEP/reset behavior. A false support bit or
   success-only stub is not sufficient. PS2SDK supplies a concrete `E0` caller.
3. **Handle defined alternate encodings**: `21/41/C9/CB` are legal counterparts
   of existing handlers. The specification says hosts *should not* use them;
   it does not remove their definitions. Do not turn retired `11-1F/71-7F` into
   mandatory aliases or normalize arbitrary command ranges indiscriminately.
4. **Repair advertised/mandatory subcommands**: SMART autosave uses the wrong
   parameter register (§8.41.2). SET FEATURES must ABRT unsupported selectors
   (§8.37.6), preserve the independent DMA selection when selecting PIO
   (§8.37.10), validate modes and multiple counts, and complete cache disable
   only after flushing (§8.37.9). SMART flag persistence needs a defined policy.
5. **Reconcile identity with implemented behavior**: CHS validity/geometry,
   missing basic PM, existing flush/order/error issues, and later-feature
   advertisements must describe the virtual drive accurately. Unadvertised
   optional HPA/security/CFA/removable/queued/firmware features are not
   automatically missing required ATA-4 support. Sony security remains a
   separate, documented compatibility gap, not six missing standard commands.

Protocol gates for implementation: count zero means 256 sectors for ordinary
LBA28 read/write commands, not 256 multiple blocks; §8.47.8 requires a final
partial multiple block and first-error reporting. §9.7 PIO data-in and §9.8
PIO data-out have different interrupt sequences. Figure 13 shows the initial
data-out DRQ being polled, with later block/completion interrupt handling; do
not copy the input engine's initial IRQ blindly. The broad beginning-of-block
wording in §8.47.8 should be reconciled with that protocol and PS2 driver
expectations in focused tests before committing an IRQ design.

Further classification limits: Device Control bit 7 is reserved in ATA-4
(§7.9.6), so the existing HOB issue belongs to the later LBA48 profile, not a
new ATA-4-required register. §9 warns that issuing commands or resets during
BSY/DRQ may corrupt in-progress data; the previous audit's command-replacement
case is primarily a host-safety/recovery hardening concern, not proof that
ATA-4 mandates one particular response to an invalid host sequence. It never
justifies a host memory overrun. The actual FIFO wrap defect remains a
separate data-integrity issue from command coverage.

## Verification and remaining runtime work

- All 48 command families, all 256 opcode bytes, and all implemented
  SMART/SET FEATURES/Sony selectors are accounted for against the retrieved
  draft, with independent classification/count review.
- Present direct, image-gated and default-ABRT cases were checked against source;
  capability and subcommand gaps have explicit clauses below. This is not an
  executed conformance suite.
- Exact capability evidence for every Sony physical model/firmware remains
  unavailable. That does not prevent comparing the explicitly emulated feature
  model, and does not justify fabricating pass-through capabilities.
- Focused disposable-image, register/IRQ, power-state, persistence, error and
  reset tests remain the implementation gates. No compilation or runtime tests
  were run. Electrical cable timing/CRC certification is outside this source pass.

The earlier [coverage inventory](DEV9_ATA4_HDD_COVERAGE_20261019.md) and
[source findings](DEV9_ATA4_HDD_SOURCE_FINDINGS_20261019.md) remain valid context,
and are complemented by the standard-backed matrix and corrections here.
