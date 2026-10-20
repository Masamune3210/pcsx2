# Full ATA-4 command comparison: evidence checkpoint

Status: **in progress**. The complete implementation-side inventory is available;
the mandatory/optional classification against the primary ATA-4 document is
blocked by network access. Do not treat a modern opcode-name table as an ATA-4
conformance table. No compilation, device execution or production changes.

Production baseline: `0dcd36309877d5da59344893d61c7a374a7851e3`; previous source
audit checkpoint: `26e221cbd8a8887032d5841bb02edcae40b0bd85`.

## Comparison profile and evidence rules

The intended default is a fixed ATA HDD behind PS2 DEV9/SPEED, compatible with
Sony's 40 GB HDD software. Three independent questions must be answered:

1. Which commands ATA/ATAPI-4 requires for a fixed ATA device, and which become
   required only when an optional feature set is implemented.
2. Which commands actual PS2 software uses, including Sony's vendor extensions.
3. Which capabilities the emulated device advertises and actually implements.

An opcode handler is not a passing command implementation. Neither a driver's
opcode constants nor its generic protocol lookup table proves physical support.
ATA-4 does not mean implementing all ATAPI/CFA/removable-media commands. Conversely,
disabling an advertisement is not sufficient when real guest software uses the
command. LBA48 and later command families must be compared separately.

## Primary standard access still required

The relevant document is T13 project 1153D, ATA/ATAPI-4 revision 18, dated
1998-08-19. Candidate primary-document locations identified through an existing
reference index are:

- [T13 project document](https://www.t13.org/documents/UploadedDocuments/project/d1153r18-ATA-ATAPI-4.pdf)
- [MIT SIPB document mirror](https://www.mit.edu/afs/sipb/contrib/doc/specs/protocol/ata/d1153r18.pdf)
- [Reference index identifying revision and locations](https://github.com/kaitai-io/kaitai_struct_formats/issues/526)

The document itself has **not** been retrieved. Earlier attempted T13 and MIT
requests returned HTTP 403 under this environment's restricted networking; a
denied request does not validate the candidate path. Network-domain additions
were saved to the environment draft for `www.t13.org`, `t13.org`,
`pdos.csail.mit.edu`, `www.mit.edu`, `www.seagate.com` and
`standards.incits.org`. Saving that draft does not apply network changes.
Publication and actual document retrieval remain unverified.

No normative command count, mandatory/optional label, reserved-opcode assignment,
or exact standard clause is claimed until the primary document is inspected.
This prevents importing obsolete/later command assignments from a current table.

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

## Completion gates

- Inventory all 256 command bytes and all implemented subcommand selectors;
  distinguish direct, image-gated and default-ABRT paths.
- Obtain and inspect the exact ATA-4 document. Classify every assigned command,
  aliases, reserved/vendor/obsolete ranges and optional feature requirements.
- Compare each feature requirement against real-drive capability evidence when
  available, the PS2 software paths above, and PCSX2's IDENTIFY words.
- Include SET FEATURES/SMART/Sony subcommands and transfer/reset/error semantics;
  a top-level opcode count alone is not a full comparison.
- Push the completed matrix and conclusions with explicit source/runtime limits.

The earlier [coverage inventory](DEV9_ATA4_HDD_COVERAGE_20261019.md) and
[source findings](DEV9_ATA4_HDD_SOURCE_FINDINGS_20261019.md) remain valid context,
but are not substitutes for the outstanding normative classification.
