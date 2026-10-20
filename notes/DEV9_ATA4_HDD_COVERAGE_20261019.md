# DEV9 ATA command coverage and regression plan

Baseline: `0dcd36309877d5da59344893d61c7a374a7851e3`.
Source-only inspection; no compilation or runtime tests. Findings and evidence
are in [the source audit](DEV9_ATA4_HDD_SOURCE_FINDINGS_20261019.md).

## Dispatcher inventory

The direct cases in `ATA_Command.cpp:7-118` are enumerated below. A handler's
presence is not proof of protocol conformance. This is an implementation
inventory, not a claim that every missing optional/obsolete opcode is required
for the selected PS2 HDD profile.

| Commands | Implementation at baseline | Limits / findings |
| --- | --- | --- |
| `00` NOP | Always ABRT | Handler present; no queued command support |
| `10` RECALIBRATE | Sets minimum address and completes | No physical media action |
| `20` READ SECTORS | Synchronous file read then PIO input | F1/F2/F5/F7; CHS rejected |
| `30` WRITE SECTORS | No dispatcher case | Known missing PIO data-out engine |
| `40` READ VERIFY SECTORS | Bounds check, no image read | Does not exercise host read errors; F1 |
| `70` SEEK | Handler present | Inverted bounds predicate, F4 |
| `90` EXECUTE DEVICE DIAGNOSTIC | Synthetic success/signature | Initialization/reset gaps, F5/F10 |
| `91` INITIALIZE DEVICE PARAMETERS | Stores geometry | Zero geometry/head decode, F6 |
| `B0` SMART | Subcommand dispatcher | Several unsupported/stubbed paths below |
| `C4` READ MULTIPLE | PIO read with interrupt grouping | Uses 512-byte staging; F1/F2/F5/F7 |
| `C5` WRITE MULTIPLE | No dispatcher case | Known run11 guest command; ABRT |
| `C6` SET MULTIPLE MODE | Stores arbitrary byte | No maximum validation, F11 |
| `C8` READ DMA | Synchronous read, SPEED FIFO output | F1/F2/F5; no CHS |
| `CA` WRITE DMA | Buffers full command, queues at completion | F1/F3/F5/F9/F14 |
| `E1`, `E3` IDLE IMMEDIATE / IDLE | Immediate completion; timer logging | No standby/power-state machine established |
| `E7` FLUSH CACHE | Queue check / deferred completion | Queue versus in-flight gap, F3/F9; durability unproven |
| `EC` IDENTIFY DEVICE | Regenerates 512-byte data | F6/F11 plus advertised-feature mismatches below |
| `EF` SET FEATURES | Cache enable/disable, transfer mode | Unsupported selectors may report success; F11 |
| `24`, `29`, `25`, `35`, `42`, `EA` EXT commands | Gated on image-dependent `lba48Supported` | Later 48-bit feature family; not baseline ATA-4 proof |
| `8E` Sony command | Separate vendor subcommand dispatcher | Only feature `EC` supplies SCE ID data |

All other opcodes reach `HDD_Unk`, which sets ABRT/ERR and completes without
data (`ATA_Command.cpp:121-129`). Retry/no-retry aliases and the wider ATA-4
optional command set need a standard-backed profile before implementation.
Do not mistake Sony `8E` / feature `30` for standard opcode `30`.

SMART details (`ATA_CmdSMART.cpp`): `D8`/`D9` toggle operations, `D2` changes
autosave, `D3` is a save stub, `DA` reports synthetic health, `D4` updates a
self-test counter but does not provide a completed self-test log; `D0` READ DATA,
`D1` READ THRESHOLDS and `D5` READ LOG explicitly abort. Sony security/read-ID/
write-ID subcommands also abort (`ATA_SCE.cpp:13-24`); external `.hddid` contents
are loaded by `ATA::Open` and returned by the supported SCE IDENTIFY path.

## IDENTIFY and protocol observations

- Words 47/59 advertise multiple transfers, while WRITE MULTIPLE is absent.
- Word 80 is `0x70`, advertising ATA/ATAPI-4, -5 and -6, not a strict ATA-4-only
  profile. Word 81 is `0x18`; its nearby comment is not an independently checked
  specification reference. LBA48 depends on image size (`ATA_State.cpp:98`).
- Word 83 bit 13 and word 86 bit 13 advertise FLUSH CACHE EXT unconditionally
  (`ATA_Info.cpp:236-240,270-274`), but opcode `EA` aborts if `lba48Supported`
  is false (`ATA_Command.cpp:86-90`). This is a direct advertisement/dispatch
  mismatch even without deciding which HDD profile should be emulated.
- Words 84/87 advertise SMART error logging and self-test, but SMART READ LOG
  aborts and the self-test data storage is commented out. Feature flags should
  describe behavior actually available, not only the existence of a handler.
- LBA28 count zero is deliberately expanded to 256; LBA48 zero/zero expands to
  65536 (`ATA_Command.cpp:150-175`). These source branches were inspected, not
  executed. Other commands' zero parameters must not use this rule by accident.
- PIO Data accepts a logged 8-bit access by consuming a whole 16-bit word; the
  8-bit bridge returns only the low byte (`ATA_State.cpp:338-343`; `DEV9.cpp:862-864`).
  There is no 8-bit transfer-mode implementation here. Actual SPEED access-width
  expectations need hardware/driver evidence before classifying this as a defect.
- READ MULTIPLE stages one sector at a time and groups IRQs modulo the selected
  multiple count (`ATA_CmdPIOData.cpp:124-141`). DRQ/BSY changes occur within the
  last-word read call. Source structure alone does not prove a guest-visible
  extra DRQ edge or a broken multiblock transfer.
- Ordinary data commands never implement CHS translation; identify/geometry
  words do not establish CHS support (F6). Completion task-file count/address
  semantics also need a clause-by-clause ATA-4 check before modification.

## Bridge, disk I/O and save-state boundaries

- The path is IOP DMA8 MADR/BCR/CHCR -> SPEED FIFO -> ATA command buffer -> image
  worker (`IopDma.cpp:152-181`; `DEV9.cpp:232-434,1066-1117`). No PC PCI IDE
  bus-master PRD table is present or expected in this path; do not introduce a
  PRD requirement merely because standard PC IDE implementations use one.
- FIFO capacity is 16 * 512 bytes. Both bridge counters are signed `int` and
  grow with transfers; reset writes clear them independently
  (`DEV9.h:72-80`; `DEV9.cpp:688-703`). Long-run overflow, mismatched directions,
  and independent counter resets need separate invariant tests. No overflow was
  exercised; no wraparound policy has been chosen.
- Bridge DMA pause logs that it is unimplemented (`DEV9.cpp:648-649`). HDD and
  IOP DMA enables gate further movement but do not supply a complete ATA
  cancellation/reset contract. ATA completion clears `dmaReady`; the header's
  auto-clear comment for `SPD_IF_ATA_DMAEN` is not implemented by those completion
  handlers. Hardware evidence is needed to specify that bit's transitions.
- The literal `ATA DMA force break` is absent from this baseline's `pcsx2`
  sources. Its original build/source/log context remains unresolved. Do not
  attribute that historical message to this FIFO code without locating it.
- Sparse writes split at filesystem blocks; zeroing punches a hole only after
  the cached entire block is all zero, with normal-write fallback on failure
  (`ATA_Transfer.cpp:101-168,313-388`). Cache loading caps reads at image size
  (`:184-195`). These are reviewed safeguards, not tested persistence guarantees.
  Include cache location initialization (F10), partial final blocks, fallback,
  zero/nonzero neighbors and reopen checks in platform tests.
- Windows uses FSCTL_SET_ZERO_DATA, Linux uses fallocate KEEP_SIZE/PUNCH_HOLE,
  macOS uses F_PUNCHHOLE. No platform path was executed here. Neither a punched
  hole nor a successful `fflush` alone proves power-loss durability (F3/F14).
- Save-state CPU/IOP rewind does not serialize this device or roll back the
  HDD image (F13). Preserve separate disk copies for any future guest experiment.

## Focused regression plan

No dedicated DEV9/ATA/HDD test cases were found under `tests/ctest`. The core
target includes EE TLB, patch and GS swizzle sources, and links the emulator
library (`tests/ctest/core/CMakeLists.txt:1-19`). `add_pcsx2_test` declares tests
`EXCLUDE_FROM_ALL` (`tests/ctest/CMakeLists.txt:5-17`). A general emulator build
would not prove the proposed ATA cases ran, even when builds are permitted.

| Area | Required cases | Required observation |
| --- | --- | --- |
| Initialization/reset | Nonzero storage; soft/hard reset mid-PIO and partial DMA; close/reopen | Defined signature, canceled partial transfer, no stale pointer/offset/callback |
| Bounds/CHS | First/final/beyond-end; count zero; incomplete trailing sector; CHS request | Exact bytes and error address; no zero-length/stale-data transfer |
| PIO writes | `30` and `C5`; one/many sectors; partial last multiple block | Correct word counts, initial/next DRQ and IRQ rules, actual persisted bytes |
| Multiple mode | Disable; supported maximum; excessive/unsupported counts | Correct acceptance/ABRT and unchanged prior setting on error |
| DMA/FIFO | Both directions; circular wrap; stop/restart/reset; mismatched lengths | Bounded copy lengths; no stale completion; exact data and IRQ |
| Write coherence | Worker held before dequeue and after dequeue; overlapping cached writes | Later PIO/DMA reads see latest accepted data |
| Flush/cache | Empty queue idle versus in-flight; multiple writes; disable cache | Completion only after all prior writes finish; explicit durability contract |
| Host failures | Short read/write, seek/flush failure, truncated image, full disk | Device error policy, no silent persistence success or uncontrolled abort |
| Sparse persistence | Zero and nonzero neighbors; cross-block; partial tail; fallback; reopen | Exact image contents and unchanged size on each platform |
| Registers/IRQs | nIEN/unmask, normal/alternate status, HOB set/clear, device 1 | Correct event retention, acknowledgment and register visibility |
| Advertisements | IDENTIFY after each mode/feature change; each advertised command | Flags and active settings match implemented behavior |
| Save/load | Idle and mid-transfer with disposable image copies | Defined disk/device consistency or explicit unsupported policy |

These must exercise production ATA/bridge behavior and real temporary-file
contents; a Python model or source-text assertion is not a substitute. Schedule
compiled tests/sanitizers and Windows/PSBBN integration only when the user lifts
the no-compilation constraint. Guest boot success is a separate gate from unit
test success, and neither has been claimed in this source pass.
