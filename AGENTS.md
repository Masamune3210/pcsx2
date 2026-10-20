# Codex instructions: DEV9 ATA HDD audit branch

**Active branch:** `note-working/dev9-ata4-hdd-audit-20261019`

**First action:** Read `notes/DEV9_ATA4_HDD_AUDIT_20261019.md` and `notes/DEV9_ATA4_CODEX_WORKPLAN_20261019_v1.md` before browsing code. This is an existing, active branch. Stay on it unless the user requests otherwise. Preserve all baseline changes inherited from `note-working/psbbn-tlb-boot-20261017`.

## Goal

Perform a *thorough, end-to-end* correctness/data-integrity audit and implementation hardening of PCSX2's **image-backed virtual ATA HDD** and PS2 DEV9/SPEED interface. The triggering observation is PlayStation BB Navigator's Linux 2.4.17_mvl21 guest failing a ReiserFS journal replay during a later mount with `DEV9: ATA: Unknown cmd c5`. Do **not** solve this by making one switch-case change, changing guest Linux, or hiding disk errors.

This is a **virtual drive, not host ATA pass-through**. A disk image provides data sectors, **not** a host HDD capability table. `IDENTIFY DEVICE` describes the *emulated drive* and must be consistent with implemented commands, selected geometry/addressing, and transfer modes. Do not constrain image size or erase supported advanced ATA features *merely* because the PS2 host interface is often described as ATA-4; separately analyze what the PS2/SPEED controller supports, what Sony's physical HDD reports, and what the virtual drive claims. Respect ATA-4 semantics where relevant while documenting ATA-5/6 extensions already exposed. No fabricated passthrough capability checks.

## Preserve and protect

- Avoid touching EE interpreter/JIT, guest vmlinux, kernel patches, HDD image formats, unrelated emulator subsystems or older PSBBN TLB debug paths without proven necessity.
- Never edit/repair a user's installation image to make tests pass. Use **new disposable test images** and verify data readback from disk (and ideally after reopen).
- Do not mask ATA failures, bypass `ReiserFS`, reroute 0xC5 to DMA, or report write completion if data was dropped. No unverified timing hacks.
- Snapshot/clone any stateful image before destructive tests. Honor work already committed to the branch; do not force-push or rewrite history.
- Do not claim Windows or PSBBN runtime validation unless actually executed. The user's Windows build is VS/MSBuild `Release AVX2|x64` with `PCSX2_qt.slnx`; CMake-only verification is not a substitute.
- Avoid noisy register-level logging in normal builds; regression tests and bounded diagnostics should be sufficient.

## Immediate, source-confirmed defects to investigate

1. ATA 0xC5 WRITE MULTIPLE is absent from `ATA_Command.cpp::IDE_ExecCmd` and aborted via `HDD_Unk`; ATA 0x30 WRITE SECTORS likewise absent. The `ATA_R_DATA` *write* case is missing in `ATA_State.cpp::Write`, whose pre-switch BUSY/DRQ guard currently drops precisely the writes needed during PIO data-out. Implement the **entire PIO data-out state machine**, not a switch-only workaround.
2. `DEV9.cpp::IOPWriteFIFO`: wrapped second `memcpy` targets `dev9.fifo + base` again instead of the start of the FIFO. Verify with a wrapped-transfer regression that exercises unique patterns.
3. `ATA_State.cpp::HDD_CanAccess`: inclusive last-LBA vs exclusive end-of-range appears off by one; test sector at EOF-1, crossing EOF, size=1, empty files and count=0 semantics. A false diagnosis here would risk the end of an image, so establish exact expected behavior before changing.
4. `ATA_Transfer.cpp::HDD_ReadAsync`: `delete readBuffer` appears paired with `new u8[]` (should be array deletion). Audit other allocation/free ownership and interrupted transfer paths.
5. `ATA_Transfer.cpp`: backing-file read/write failures frequently call `abort()`; inspect robust guest-visible ATA errors and lifecycle. Review `IO_Thread`, write queue, `Async`, flush and reset ordering for races, short reads/writes, write acknowledgment vs durable completion and data loss.
6. `ATA_Info.cpp::CreateHDDinfo`: cross-check ATA IDENTIFY words 47, 49, 53, 59, 60–61, 63–64, 80–88 and 100–103 against implemented virtual capabilities; `SET MULTIPLE MODE` **is implemented** (0xC6), as is `READ MULTIPLE` (0xC4). Do not treat stale TODOs as authoritative.
7. `DEV9.cpp`: FIFO/DMA ring wrap, start/stop, transfer lengths, IRQ order, direction, abort/force break and SPEED adapter flags. Guest previously logged `ATA DMA force break` **before** the 0xC5 abort, so treat DMA as an independent issue, not as automatically solved by PIO writes.
8. Verify ATA task-file/status/alternate-status semantics, HOB, nIEN, SRST, DRQ/BSY/ERR/ABRT, SEEK/LBA/CHS and partial final transfer behavior; assess save/load states and interrupted HDD I/O. Identify unsupported standard commands as intentional vs implementation gaps.

## Implementation, tests and handoff requirements

- Trace the full path **IOP DEV9 register -> ATA command/data -> PIO/DMA buffers -> queued file I/O -> guest IRQ/status**. Maintain a command coverage matrix with ATA number, code handler, advertised support, result, and test.
- Prioritize issues that cause **silent data loss or incorrect writes**, then observed BB Navigator blockers and invalid interrupts/timeouts, then further protocol compliance. Fix verified issues and add automated tests, preferably isolated/disposable-image fixtures with real sector persistence and queue flushing. Avoid overly large unreviewable commits; stage logically.
- Tests must cover PIO single/multiple-sector writes/reads, 256-sector count encoding, 128-sector negotiated multiple chunks and non-divisible tails, IRQ/DRQ phases, LBA boundary, aborted/invalid mode, reset mid-transfer, repeated reads after writes, DMA FIFO wrap, read/write error propagation and reopen persistence. If a test cannot be implemented in this environment, record the missing verification explicitly.
- Record discoveries, design decisions, source locations, changed files, tests actually run, and **remaining** risks in `notes/DEV9_ATA4_HDD_AUDIT_20261019.md` and `notes/DEV9_ATA4_CODEX_WORKPLAN_20261019_v1.md`; keep both current after substantive work. Include reproduction instructions for the user on Windows.
- Do not stop after an audit checklist if straightforward verified defects can be fixed. Do as much safely as possible in this coding session. If blocked on a test/build, continue independent source review and bounded unit tests, then document the obstacle.
- One terminal command per line. Preserve the existing branch, write focused commits, and provide a final handoff with commit hashes, actual verification, and the next test for the user.

The user's longer-running PSBBN forensic evidence is summarized in `notes/PSBBN_TLB_BOOT_INVESTIGATION.md`. Do **not** scan its ~88 KiB of historical handoff unless deeper context becomes necessary: the two audit notes above contain current relevant facts.
