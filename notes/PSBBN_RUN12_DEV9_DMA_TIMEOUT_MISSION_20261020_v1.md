# Mission: diagnose PSBBN run12 DEV9 DMA timeout and IOP transfer-completion failure

## Run12 source-fix checkpoint (2026-10-20)

**New evidence and change:** [8 KiB SPEED FIFO single-pass deadlock finding](PSBBN_RUN12_SPEED_FIFO_DEADLOCK_FINDING_20261020_v1.md). The 0x3000/0x7000/0x8000 IOP DMA8 requests exceed the FIFO's 0x2000 capacity, while prior DEV9runFIFO only drained once, refilled once, and returned without consuming the refill. The IOP DMA completion IRQ is gated on all requested bytes arriving. The first oversized request at 58.6989s stalls ~30s before the guest's recovery/reset. Source fix `5a8117f325e0` introduces a progress-checked drain loop and removes two invalid single-FIFO sized asserts. Prospective production-path test `baac0b5561c7` exercises exact guest request lengths with disposable image. **Neither fix nor test has been compiled/executed**. These findings supersede the prior mission's "no code changes" opening status, but do not establish that every subsequent error is resolved. Codex should review this checkpoint and finish focused verification rather than restarting broad source investigation. The HDD-enabled savestate ban remains explicitly reverted.


**Status:** Open; Codex investigation and tests requested. **Branch:** `note-working/dev9-ata4-hdd-audit-20261019`. **Date:** 2026-10-20. **Priority:** High, root-cause debugging; preserve existing ATA-4 HDD changes.

## Objective

Determine why PlayStation BB Navigator's Linux guest reaches a root ReiserFS mount but then fails ATA DMA reads at ~30-second timeout intervals, eventually repeatedly faulting in userspace while the PS2 logo remains on screen. Investigate the **full HDD -> ATA -> SPEED FIFO -> IOP DMA8 -> interrupt -> guest-driver path**. Find a reproducible discrepancy, establish causality, and apply a narrowly justified correction *only after* the expected behavior is evidenced. Add regression coverage without risking existing PSBBN images.

This is **not** a task to implement another ATA command, bypass guest error handling, silence the timeout, fabricate successful I/O, or change the EE TLB as a first response.

## Current experiment (run12)

- PCSX2 `v2.9.92-158-g922ad14d3`; EE interpreter; HDD `bbntest.raw`, 40 GiB / 83,886,080 512-byte sectors. Previous run on this image means it is **not** a controlled clean-image A/B experiment.
- User-supplied host log `emulog(10).txt`: SHA256 `109e894e5e42e9467aa43587fa7f471ab17df221f454733283b20ac4cacd0c27`, ~5.8 MiB, 87,250 lines.
- User-supplied `sstates(9).zip`: SHA256 `e8ddeee0a77cb60661d6c7ebce69d241a924372363dc96f18e01f2dfefe25233`; 38 `.p2s` captures with session prefix `PSBBN-auto-1792494497980514`, numbered 001..038 (outer ZIP uses Deflate64). **Neither uploaded evidence file is in Git; do not invent a repository path or commit extracted copyrighted firmware.**
- Snapshots show early animation states 001–002; PlayStation 2 logo from state 003 onwards, visually unchanged through 038. Yet guest Linux memory records successful root mount:

```text
reiserfs: checking transaction log (device 03:01) ...
Using r5 hash to sort names
reiserfs: using 3.5.x disk format
ReiserFS version 3.6.25
VFS: Mounted root (reiserfs filesystem) readonly.
Freeing unused kernel memory: 68k freed
```

- Guest message ring later records `hda: timeout waiting for DMA` **three times**. Host independently prints exactly three `ATA DMA force break` events:

| Host time | Host line | Observation |
| --- | --- | --- |
| 58.6989s | log excerpt | `HDD_ReadDMA` request, IOP DMA8 size `0x3000`, begins the suspected outstanding transaction |
| 88.6765s | line 50484 | first `ATA DMA force break` (~30s after request) |
| 88.6782s | subsequent request | retry with IOP DMA8 request `0x7000` |
| 118.4130s | line 57521 | second force break (~30s later) |
| 118.7912s | subsequent request | retry with IOP DMA8 request `0x8000` |
| 148.6084s | line 76087 | third force break (~30s later) |
| ~148.945s | log | persistent guest exception from PC `0x0FB6800C` while accessing `BadVAddr=0xFFFF8378` |

- Host emits many `DEV9 read16 10000040: ffff` reads around retries, including immediately before/after the latter two force breaks. An earlier checkpoint counted ~34,124 such `0xFFFF` reads overall. **Do not equate these read values with a proven DMA payload error** without decoding guest recovery behavior and the DATA port's expected state.
- The later repeated fault has `Cause=0x00000008` (TLB exception code 2), ASID `0x01`, Status `0x00010C11`; repeated PC `0x0FB6800C`, `BadVAddr=0xFFFF8378`. It begins after third DMA failure. Whether it is causal, downstream, or independent remains **unknown**.
- No `Unknown cmd c5`, `no DRQ after issuing MULTWRITE`, or `journal-837` messages were found in the *new host log*; absence there does not establish successful execution of WRITE MULTIPLE or a successful `/dev/hda4` mount. Prior interpreter run11 instead got as far as later ReiserFS `/dev/hda4` journal replay error with the *old* ATA implementation, then orb animation/black screen. A direct regression comparison is not yet controlled.

## Start here, and avoid prior investigation churn

Read branch `AGENTS.md`, `notes/DEV9_ATA4_HDD_AUDIT_20261019.md`, `notes/DEV9_ATA_IMPLEMENTATION_20261020.md`, and *this mission* before any wider browsing. Existing ATA-4 command audit, tested-by-source-only changes, and savestate reversal are settled context; do not redo command inventories.

### Exact production path to trace

1. `pcsx2/DEV9/ATA/Commands/ATA_CmdDMA.cpp`: `HDD_ReadDMA`, `DRQCmdDMADataToHost`, `ReadDMAToFIFO`, `PostCmdDMADataToHost`; inspect sector counts, DRQ/deassertion, pending ATA INTRQ and bytes delivered.
2. `pcsx2/DEV9/ATA/ATA_Transfer.cpp`: `HDD_ReadSync`, image read availability, buffered/queued write coherence and host I/O errors; distinguish successful file read from successful guest transfer.
3. `pcsx2/DEV9/DEV9.cpp`: `HDDWriteFIFO` (ATA -> ring), `DEV9runFIFO`, `IOPReadFIFO` (ring -> IOP), `FIFOIntr`, `DEV9readDMA8Mem` and `psxDMA8Interrupt()` ordering; inspect SPEED `SPD_R_XFR_CTRL`, `SPD_R_IF_CTRL`, FIFO read/write counters, `dma_iop_ptr`, `dma_iop_size`, `dma_iop_transfered`, `fifo_generation`, `dmaReady`, `SPD_IF_ATA_DMAEN`, `SPD_IF_READ`.
4. IOP DMAC channel 8 call/return and interrupt acknowledgement/timeout handling in the adjacent IOP/DEV9 call sites, only when needed. **Locate the emitter of literal `ATA DMA force break`**; verify whether it is generated by the *guest* ATAD driver/BIOS rather than by PCSX2 host code. Do not assume the text comes from the host emulator's own DMA abort path.
5. Investigate why the guest falls back to high-volume ATA DATA-register `0xFFFF` reads. Record status/alt status/error and command at the transition; determine whether PIO fallback, bus contention, nIEN, disabled DRQ, register selection, or an invalid phase explains the reads.

## Mandatory diagnostic design

First reconstruct a transaction timeline with **one stable correlation ID per ATA command and related IOP DMA8 request**, including retries/reset generations. Use bounded logging or an opt-in in-memory ring buffer to avoid inflating runtime and changing timing as much as possible. Snapshot for each relevant transition:

- guest command opcode, LBA28/48, count, transfer mode, selected device, task-file registers and ATA BSY/DRQ/ERR/ABRT / pending INTRQ;
- image read bytes requested/read and checksum/sample, `readBuffer` lifetime, residual sector count and `rdTransferred`;
- `SPD_IF_CTRL`, `SPD_XFR_CTRL`, `SPD_DMA_CTRL`, DMA direction flags, FIFO occupancy/counters and generation, IOP request size/offset/pointer validity;
- bytes entered and left FIFO; whether 16-bit/word boundaries and partial blocks cause a no-progress condition; exact place and cycle of ATA vs SPEED vs IOP DMA8 completion interrupt;
- state at guest timeout / reset / force break, and whether an unfinished request survives incorrectly into retry.

Have the trace *discriminate* between: (a) image bytes unavailable; (b) ATA readiness never asserted; (c) FIFO producer blocked; (d) FIFO consumer not enabled; (e) request fully copied but DMA8 IRQ lost; (f) premature ATA completion/deassertion; (g) guest abort of still-progressing transfer; (h) stale generation or post-reset counters. Clearly report the first unmet invariant, not merely the final hang.

If the 58.7s DMA transaction cannot be identified deterministically from existing log, add **instrumentation only** and request a captured run. Avoid guessing the LBA or new guest-side driver behavior. Include enough logging to distinguish *why* progress stopped and confirm whether the 0xFFFF storm is PIO fallback.

## Regression, evidence and acceptance

- Extend existing `tests/ctest/ata` and `tests/ctest/core/dev9_fifo_tests_20261020.cpp` with *production-path* tests for: IOP transfer not started before ATA DRQ vs started late; IOP request exhausted with bytes still in FIFO; ATA data exhaustion before IOP request completes; bounded FIFO ring fullness/emptiness; partial last block; ATA completion and IOP DMA8 interrupt sequencing; guest reset/cancel/retry during active DMA; absent/masked IRQ; insufficient file data; transfers `0x3000`, `0x7000`, `0x8000` bytes. Use disposable patterned disk images and compare exact bytes, not just state flags. Do not fabricate synthetic SPEED behavior as a substitute for integration coverage.
- Add a compact reproduction recipe and expected/observed register timeline. Preserve evidence provenance and clearly mark what is measured, inferred or unverified.
- **User instruction for this branch: do not compile or run C++/Windows builds until separately authorized.** Writing tests and source-only/static checks is allowed, but a test file is NOT a passed test. Do not claim regression fixes successful without execution and appropriate guest/hardware observation.
- Do not change unrelated EE interpreter/JIT, vTLB, recompiler, guest kernel, MagicGate, filesystem images, ATA-5+ command scope, or PSBBN savestate cadence to hide failures.
- **Critical prior decision:** user explicitly reverted the HDD-enabled savestate ban. NEVER reintroduce `SaveState_CheckHDDStateSupport` or disable scheduled/manual/resume savestates without a new explicit user request. Diagnostic captures remain valuable; users know their external HDD image is independent of savestates.
- Preserve Sony ATA / security command behavior and existing ATA-4 fixes, unless a regression is demonstrated. Do not make destructive tests against `bbntest.raw` or `bbntest2.raw`; create disposable clones and carry any `.ata-settings`, `.ata-smart`, `.ata-hpa` sidecars required to reproduce virtual drive state.

**Definition of done:** a causal explanation tied to specific source lines and guest-visible evidence; a focused correction if actually established; prospective regression tests exercising the real implementation; source-only verification reported accurately; updated branch handoffs with decisions, test status, unresolved uncertainties and the next user Windows/PSBBN step. A PCSX2 build or a Linux mount alone is not proof of ATA correctness.

## Current status / next action

Mission created from run12 evidence. No new instrumentation, emulator behavior change, build or guest run has been performed by this mission setup. **Next:** Codex traces the DMA handshake through production source, determines the provenance of `ATA DMA force break`, and prepares targeted opt-in transaction instrumentation or a justified fix, with a source-checked regression and narrow handoff.
