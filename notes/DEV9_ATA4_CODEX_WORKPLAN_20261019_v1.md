# Codex task: end-to-end DEV9 HDD and ATA protocol correctness

## Current override: do not block HDD savestates (2026-10-20)

**User-directed reversion completed.** All restrictions introduced by `SaveState_CheckHDDStateSupport` were removed from `pcsx2/SaveState.cpp`, `pcsx2/SaveState.h` and `pcsx2/VMManager.cpp`, byte-for-byte restoring those files to the pre-audit state. Commits: `74a6308c057f`, `6980793d5957`, `8a8c4d4b9873`. This explicitly **overrides** earlier instructions in this workplan advocating a fail-closed HDD-enabled save/load policy.

Do not reapply an HDD savestate prohibition, preflight guard or other loss of functionality. Keep manual, scheduled diagnostic, and resume savestates usable. HDD image rollback is **not** implied; the user understands the risk of restoring against changed external disk contents. No unrelated ATA source or test behavior was reverted. No compilation or runtime verification performed.



**Task mode:** implement and test, not just summarize. **Branch:** `note-working/dev9-ata4-hdd-audit-20261019`. **Starting point:** existing audit note and `AGENTS.md` at repo root. **Current state:** production corrections are now implemented in the 2026-10-20 checkpoints below. Historical audit sections describe the original baseline; compiled/runtime validation remains prohibited and unrun.

## Why this work exists

Current user scope: PS2 host support goes up to ATA-4. Continue new command work
within ATA-4; planned 27/37 HPA EXT additions were stopped before code/test edits.
Preserve existing later extensions and inherited work. Do not infer PS2 host
support from a later virtual-drive feature or a later draft. Standard ATA-4
security/queued DMA/firmware download remain optional, unsupported and unclaimed.

Continued command review corrected IDENTIFY ASCII byte ordering and the complete
standalone 90h Device/Head signature, with two source-first prospective cases
(77 ATA total). See [reasoning](DEV9_ATA_IDENTIFY_DIAGNOSTIC_FIX_20261020.md).
Independent review checked the production changes and caught a test padding-count
mistake, which was corrected. Checkpoint `7541ee7878` was pushed. No compilation
or executable tests were run. Focused non-HPA review of command acceptance,
PIO/DMA completion, reset, power, SMART and parameter handling found no additional
established defect in this pass; that is source review, not runtime conformance.

PS2 BB Navigator v0.32, SCPN-60160, boots Sony Linux 2.4.17_mvl21 from an HDD image. In the running EE-interpreter experiment, Linux *successfully* mounts `/dev/hda1`, but later gets ATA errors while mounting `/dev/hda4` and fails its ReiserFS journal replay. The host logged 27 `DEV9: ATA: Unknown cmd c5` in ~230–232s. Command `0xC5 WRITE MULTIPLE` is handled by fallback `HDD_Unk`, which explicitly sets ABRT/ERR and never drives the DATA register DRQ phase. This explains a concrete class of Linux `no DRQ after issuing MULTWRITE` errors. The guest has also logged `hda: timeout waiting for DMA`; host emitted `ATA DMA force break` earlier (~177s), so there is more to review than command `0xC5`. The screen progresses through blue-white animated orbs and later stays black while kernel and userspace still execute. The latter black-screen symptom and ATA errors are temporally associated but not yet proven one-to-one.

**Do not confuse this with JIT's separate earlier `journal-2005` root mount panic**. JIT got as far as finishing installation on a clean HDD, then failed in guest ReiserFS bitmap-slot initialization. The interpreter's later `journal-837` I/O failure is distinct. A full change must be verified from cloned/disposable drive images rather than assuming existing test HDD files are undamaged.

Research source: [PSBBN live interpreter run11 report](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_INTERPRETER_LIVE_RUN11_ATA_WRITE_MULTIPLE_20261020_v1.md). The raw live log and 163 states are conversation attachments, **not present in this repository**. Use the report as a guide, not as an executable test fixture.

## Technical model and boundaries

PS2-side controller is DEV9/SPEED; PCSX2 attaches an **emulated ATA drive backed by a disk image**. It does **not** pass commands directly to any physical ATA disk. The image does not furnish IDENTIFY DEVICE, DMA-mode or write-cache feature bits. Those are emulated hardware model decisions that must be mutually consistent and valid for guest drivers. User notes that PS2 hardware supports up to ATA-4. Check host/controller capability separately from guest-visible virtual disk revision and Sony-specific extensions; do not hardcode ATA-4 version/UDMA ceilings as a universal fix without evidence, and do not claim the image or host physical drive supplies capabilities. Implement advertised commands or narrow false claims with clear reasoning.

Before editing, trace only the directly relevant files:

- `pcsx2/DEV9/ATA/ATA.h`: state machine, buffers, modes, queue, read/write methods.
- `pcsx2/DEV9/ATA/Commands/ATA_Command.cpp`: task-file command decode, ABRT.
- `pcsx2/DEV9/ATA/Commands/ATA_CmdPIOData.cpp`: PIO data-in exists; data-out absent.
- `pcsx2/DEV9/ATA/Commands/ATA_CmdDMA.cpp`: read/write DMA, FIFO callbacks.
- `pcsx2/DEV9/ATA/Commands/ATA_CmdNoData.cpp`: SET MULTIPLE, FLUSH, FEATURES, reset related.
- `pcsx2/DEV9/ATA/ATA_State.cpp`: host-visible registers, status, HOB, sector arithmetic, reset, asynchronous command pump, file open/close.
- `pcsx2/DEV9/ATA/ATA_Transfer.cpp`: backing file, sparse image, cache and worker, ownership.
- `pcsx2/DEV9/ATA/ATA_Info.cpp`: IDENTIFY response.
- `pcsx2/DEV9/DEV9.cpp` and `DEV9.h`: SPEED FIFO bridge, DMA8, IRQ and address decode.
- Relevant tests in `tests/ctest` and existing test harness/build setup. No recursive repo-wide scan unless direct dependencies require it.

## Confirmed and high-confidence defects already observed

| Priority | Code / affected behavior | Established evidence and required action |
| --- | --- | --- |
| P0 | `ATA_Command.cpp` lacks `0xC5 WRITE MULTIPLE` and `0x30 WRITE SECTORS` | Commands fall into `HDD_Unk`/ABRT. Implement genuine PIO data-out protocol with task-file behavior, not DMA alias; add tests |
| P0 | `ATA_State.cpp::Write` has no `ATA_R_DATA` case | Writes to DATA are discarded; its top-level `BSY/DRQ` guard currently rejects all task-file writes other than command/control, even the DRQ-enabled data phase. Gate writes by register and phase correctly |
| P0 | `DEV9.cpp::IOPWriteFIFO` wrap branch | Second `memcpy` destination is `dev9.fifo + base` again, not start of FIFO, corrupting a wrapped IOP->FIFO DMA write. Add wraparound signature test and fix |
| P1 | `ATA_State.cpp::HDD_CanAccess` end of disk | `maxLBA = size/512 - 1` inclusive, but `posEnd = start + sectors` is exclusive and compared against inclusive max; likely rejects valid final-sector access. Fix with verified boundary contract for 1 and N sectors |
| P1 | `ATA_Transfer.cpp::HDD_ReadAsync` array lifetime | `delete readBuffer;` appears despite `new u8[]` allocation and other `delete[]` usages. Correct independently and test buffer growth; audit related arrays |
| P1 | ATA/SPEED async read/write and reset | On disk I/O errors `IO_Read`/`IO_Write` call `abort()`; worker flush/write lifecycle and transitions need correctness under shutdown, reset and host errors. Preserve guest data and surface ATA status instead of terminating host |
| P1 | `ATA_CmdDMA.cpp` and `DEV9.cpp` FIFO | Host log reports DMA force break before PIO errors. Inspect direction, residual counters, FIFO wrap, generation/reset, IRQ scheduling and length checks; separate root cause from rejected PIO commands |
| P2 | `ATA_Info.cpp`, `ATA_CmdNoData.cpp` | IDENTIFY words 47/49/53/59/63/80..88 advertise PIO multiple and ATA revisions/modes; `SET FEATURES` accepts modes broadly. Audit claims vs emulated implementation; preserve explicitly supported Sony behavior |
| P2 | `ATA_State.cpp` | CHS unsupported; SRST, nIEN, HOB, status-vs-alt-status and missing-command policy need source tests. Avoid unnecessary broad behavioral changes absent evidence |

**Important correction:** `0xC6 SET MULTIPLE MODE` and `0xC4 READ MULTIPLE` are **already handled**; the TODO comment listing them is stale. The issue is *WRITE* MULTIPLE, including its IRQ cadence and sector batching.

Other audit candidates: `IO_SparseCacheAssertFileZeros` seems to read into `hddSparseBlock` when a separate temp buffer was allocated; verify before changing, this is debug-only. `Close()` expects an empty write queue and aborts otherwise, which interacts with command abortion and safe shutdown. `HDD_WriteDMA` can buffer large commands in full before queue flush; examine counts/ownership with reset. Check `ATA_ReadPIO` behavior on multi-block tails and whether status/data-phase cleanup is correct.

## Required execution sequence

**Phase 1, map & test harness.** Confirm state transitions for `DATA` and both DMA paths. Identify minimal build/test route without a full emulator boot, preferably a harness that instantiates ATA against a small temporary image; run existing tests first. Record actual test/build environment. Create a maintained ATA command coverage matrix (supported, unsupported, advertised, tested, observed Linux uses). Inspect DISK *content* integrity by comparing hashes of affected sectors on **disposable** images.

**Phase 2, data-integrity correctness.** Fix FIFO wrap and array-mismatch immediately with isolated regression tests. Fix final-LBA boundary only after demonstrating expected last-sector read/write behavior. Add tests for corruptions at wrap boundary; no silent write drops or host aborts.

**Phase 3, ATA PIO writes.** Implement `0x30` and `0xC5` with a genuine host-to-device DATA register path (`ATA_R_DATA`, 16-bit words; handle 8-bit or invalid-width access according to documented emulation policy). Use 512-byte sector buffers and correct 1..256 sector-count decoding for 28-bit commands; negotiated multi-sector block size from `0xC6`; finish each DRQ block; raise ATA INTRQ at appropriate command/DRQ phases; handle final partial block. Confirm LBA progression, aborting bad addresses, reset during transfer, error latching, `FLUSH CACHE`, write-queue persist and re-opened disk contents. Do not send spurious IRQ on every word.

**Phase 4, DMA and SPEED.** Audit existing queued DMA write/read transfer sizes, outstanding IOP DMA pointers, fifo counters, direction, force-break handling, interrupts, `SRST`, cycle scheduling and teardown. Reproduce both `ATA DMA force break` and no-DRQ errors where possible. Implement only verified fixes, each with a regression test.

**Phase 5, ATA-4 and Sony compatibility.** Compare selected capability claims with realistic drive identity and implemented commands; distinguish PS2 controller ATA-4 limits and attached virtual drive's own features. Maintain Sony HDD 0x8E (security control) semantics and real BI/console behavior; do not replace Sony-specific paths with a generic drive identity just for spec tidiness.

**Phase 6, full verification and handoff.** Report production build results and automated tests. Windows user can build `PCSX2_qt.slnx` in Visual Studio/MSBuild `Release AVX2|x64`; if Codex's environment cannot build this target, mark this as **not run** and supply exact files/commands for the user. Once the user can cold-boot a clean HDD clone, check Linux `/dev/hda1` root mount, subsequent `/dev/hda4` mount, absence/presence of 0xC5 aborts, reiserfs journal I/O and later Navigator UI.

## Definition of done

1. Real ATA PIO writes work with persisted sector data; invalid/incomplete commands cannot silently corrupt an image. Tests exercise single and multi-block writes, LBA bounds, IRQ/DRQ transitions and reset.
2. FIFO wrap fix validated; separate DMA/force-break assessment includes which code path was responsible and whether fully corrected.
3. Document supported/unsupported ATA command set and IDENTIFY model accurately, with an ATA-4 host vs virtual drive distinction.
4. Leave touched files scoped to DEV9 ATA/SPEED and its tests/docs except for necessary build-registration changes.
5. Update `notes/DEV9_ATA4_HDD_AUDIT_20261019.md` and this file with findings, decisions, changed files, commit hashes and **actually executed** tests, outstanding work and a next step. Do not falsely claim PSBBN works before a real boot.
6. Commit logical changes to the **existing branch**; preserve branch history and all preexisting PSBBN TLB fixes. If only partial work is finished, report it candidly and preserve a usable checkpoint.

## State of verification at handoff

### Subsequent command-comparison checkpoint (2026-10-19)

- User requested a full ATA-4 command-set comparison and continues to prohibit compilation. The narrower current task is comparison, not implementation of every workplan phase in this turn.
- Full source comparison completed in `DEV9_ATA4_COMMAND_SET_COMPARE_20261019.md`, `DEV9_ATA4_COMMAND_MATRIX_20261019.md` and `DEV9_ATA4_OPCODE_INVENTORY_20261019.csv`. Initial primary-document access was blocked at checkpoints `1da1c4a40f` / `18f7055445`; the exact MIT SIPB mirror later succeeded. T13/1153D rev18 working-draft SHA256 is `9ac718ef8b9dbc309a7dc597288141acbd0e4715937c10e9ca4691cd94326ce9`. All 48 families / 256 bytes are classified, with feature conditions and five legal alias pairs.
- New standard-backed gaps: mandatory Power Management lacks `E0/E2/E5/E6` and a standby state/timer implementation; alternate encodings `21/31/41/C9/CB` are defined, not retired. Alongside missing `30/C5`, there are 11 absent mandatory encodings for a >8 GB LBA-only profile. `91` is separately CHS-dependent (§8.16.2). These counts measure dispatch presence only.
- SMART autosave must read Sector Count, not `regSector` (§8.41.2); SMART core mandates D2/D8/D9/DA, while D0/D4 and D3 are optional. D1 is obsolete and D5/D6 reserved in ATA-4; later logging claims need a later-profile check. SET FEATURES must ABRT unsupported/invalid inputs (§8.37.6), keep an independent PIO/DMA selection (§8.37.10), and flush on cache disable (§8.37.9).
- Preserve existing virtual-drive extensions while assessing consistency. Physical-drive evidence is supplementary; neither an image nor a host disk supplies the virtual drive's IDENTIFY fields.
- Retraction of the comparison checkpoint's FIFO note: a direct `git show` of production baseline `0dcd36309877d5da59344893d61c7a374a7851e3` confirms `DEV9.cpp:339` targets `dev9.fifo + base` again for the wrapped second copy. The historical handoff was correct; the contrary checkpoint note was a source-reading error. The wrap defect remains unfixed and runtime wrap testing is still unrun.
- No production changes or compiled tests in this command-comparison checkpoint. Remote documentation commits were merged without rewriting branch history.

- Source paths checked from GitHub branch. The two PIO write omissions and FIFO wrap destination are direct source observations.
- The ATA end-of-range calculation and array delete mismatch have not been exercised under sanitizers or built tests yet; classify as source-verified suspicious/defective patterns until demonstrated.
- **Zero production fixes or tests** have been applied by this Codex-setup task.
- The previous live interpreter run was still ongoing at upload; no final outcome is claimed.

### Implementation resumed, 2026-10-20

User instruction: fix the documented bugs, retaining no compilation and checkpoint
pushes. The first production checkpoint fixes FIFO wrap, invalid copy lengths and
counter rollover, adds a reset cancellation API, and saves seven prospective FIFO
regressions. Detailed decisions and unrun validation are in
`DEV9_FIFO_FIX_20261020.md`; the scoped implementation sequence is in
`DEV9_ATA_FIX_PLAN_20261020.md`. This supersedes the earlier comparison-only task
mode. No compiled tests or guest boots have been run. ATA/IO work remains ongoing.

### ATA corrective checkpoint, 2026-10-20

Implementation phases2-5 now have source changes and39 prospective disposable-image
ATA cases, plus seven registered FIFO cases. The full delta, rationale, review
findings and exact remaining verification are in
`DEV9_ATA_IMPLEMENTATION_20261020.md`, with focused I/O and parameter notes.
This supersedes historical claims of zero production changes. Source-only bridge
constant/whitespace checks passed; no configure/compiler/tests/Windows/guest runs.
The save-state preflight policy remains a separate next checkpoint; full disk
snapshot restore and undocumented Sony authentication are not implemented.

### Final lifecycle and handoff checkpoint, 2026-10-20

F13 now has a current-profile fail-closed save/load policy, documented in
`DEV9_ATA_SAVESTATE_POLICY_20261020.md`; full HDD/disk-generation serialization is
not implemented. Necessary direct SaveState/VMManager callers reject before state
mutation and avoid resetting the running guest on policy refusal. This intentionally
disables manual/autosave/resume archives while HDD is enabled. Older archive
provenance and a target ELF profile enabling HDD later remain explicit limitations.

Close now clears transfer/IRQ/FIFO state, and an absent image cannot receive ATA
commands through the still-enabled Ethernet decoder. FIFO generation checks keep
returning callbacks from adding stale progress after host-error cancellation.
There are 40 ATA and seven FIFO prospective cases. Current matrix/CSV reflect all
23 mandatory dispatch bytes while retaining original baseline comparison results.
Fresh whitespace, 256-byte/48-family source inventory, test registration and bridge
constant checks passed. No compilation/configuration/C++ execution/Windows/guest
runs occurred. Next validation is the documented disposable-image suite and Windows
`Release AVX2|x64` build, followed by a cloned-image PSBBN cold boot when authorized.

Error-code review before the final push: backing read errors use UNC; host write
and flush errors use DF/ABRT, avoiding ATA-4's write-protect meaning of bit6. Normal
next-address/residual-count reporting is an emulation policy where the draft marks
those normal outputs not applicable. First-error address and indeterminate transfer
amount follow the normative error-output clauses. No host-failure test was run.

Durable source handoff: pushed `58dc04a4fe`, `874309ca1c`, `2c39d25e37`; local and
remote source HEAD matched and the worktree was clean at final source verification.
Recorded fresh checks: full-range whitespace; 31 bridge constants; all 256 CSV
source-line references; 48 family rows; 40 ATA plus seven FIFO prospective cases.
No compilation or executable test outcome is claimed. The final documentation-only
checkpoint preserves this handoff; no production behavior changes after `2c39d25e37`.

## Remaining-command implementation, 2026-10-20

User requested currently unimplemented/incorrect commands. This checkpoint adds
E4/E8 diagnostic buffer commands, SMART D0 data/D3 real persistence/D4 real offline
collection, SET FEATURES 66/CC volatile reset reversion, and safe power-up PIO2 and
MDMA0 defaults while preserving higher supported modes and SRST UDMA selection.
See the three focused buffer/SMART/command-semantics notes for clauses and design.
There are now 59 prospective ATA cases plus seven FIFO cases. No configuration,
compilation, C++ execution, Windows build or PSBBN boot was performed. Fresh
whitespace/bridge/source inventory checks and independent code review were used;
no important defect was established by source review, which is limited evidence.
Further optional command families remain separate work; HPA is the next candidate.

## HPA and multiple-error checkpoint, 2026-10-20

The preceding remaining-command batch is pushed as 693ff612a2. This checkpoint
adds ATA-4 HPA F8/F9: native versus guest-accessible capacity, immediate successful
F8 prerequisite, volatile/NV persistence, per-power NV quota, CHS and checked
`.ata-hpa` metadata. It never truncates the image. HPA remains unclaimed for the
LBA48 profile pending later 27/37 support; existing large-image data support stays.
SMART's device-side verification intentionally scans native media, including HPA.

READ MULTIPLE now returns a complete ERR/UNC block for a real backing read failure,
including valid prior blocks and zero-filled unread bytes, then stops. Invalid
multiple ranges ABRT before DRQ; failed INIT retains its mandatory IDNF result.
See `DEV9_ATA_HPA_COMMANDS_20261020.md` and `DEV9_ATA_MULTIPLE_ERRORS_20261020.md`.
There are 75 prospective ATA cases plus seven FIFO cases. Independent source review
found one IDNF distinction, corrected with expanded regression source. Fresh
whitespace, bridge constants and command inventory checks passed. No compiling,
configuration, C++ execution, Windows/sanitizer or PSBBN validation occurred.
Remaining unclaimed families and runtime/design limits are explicit in the notes.

Remaining-command source handoff: `693ff612a2` and `87a3a63258` are pushed; remote
and local source HEAD matched, and the worktree was clean. Fresh source-only checks
covered whitespace,31 bridge constants,256 opcode references,48 families and75 ATA
plus seven FIFO prospective cases. Full compiled/runtime verification remains
unrun under the user's no-compilation constraint. The final documentation-only
checkpoint records this result; source behavior is unchanged after `87a3a63258`.
