# PSBBN run12: oversized IOP DMA8 request stalls the single-pass SPEED FIFO bridge

**Date:** 2026-10-20 · **Status:** source-derived root-cause candidate repaired; regression **NOT COMPILED / NOT RUN**. This note is a focused update to [run12 DMA mission](PSBBN_RUN12_DEV9_DMA_TIMEOUT_MISSION_20261020_v1.md). Do not treat this as guest runtime validation.

## Evidence from the user's uploads (not committed)

`emulog(10).txt` SHA256 `109e894e5e42e9467aa43587fa7f471ab17df221f454733283b20ac4cacd0c27` (87,249 lines); `sstates(9).zip` SHA256 `e8ddeee0a77cb60661d6c7ebce69d241a924372363dc96f18e01f2dfefe25233`, 38 Deflate64 `.p2s` entries. PCSX2 `v2.9.92-158-g922ad14d3`, EE interpreter, `bbntest.raw` (not a controlled fresh-disk clone).

Most successful-looking preceding DMA8 requests were `0x2000` or less. At **58.6919s**, the guest requested `0x2000`; the host log subsequently showed `SPD_R_XFR_CTRL 8bit read 6` then `... read 86`. At **58.6989s**, the guest requested **`0x3000`**, with `DEV9: HDD_ReadDMA` and `SPD_R_XFR_CTRL 8bit read 6`, but no `... read 86` before the ~30-second delay. The next relevant sequence is a guest-commanded ATA reset at **88.3967s** followed by guest text `ATA DMA force break` at **88.6765s**. Same pattern after retry sizes **`0x7000`** at **88.6782s** and **`0x8000`** at **118.7912s**, with resets at **118.2148s** and **148.3940s** and force-break messages at **118.4130s** and **148.6084s**, respectively. Thus the observed `force break` diagnostic follows, rather than initiates, each reset; its exact source remains unproven. No second IOP DMA8 request follows a stalled oversized request until guest recovery.

User savestates independently show root ReiserFS `/dev/hda1` mounted, then three `hda: timeout waiting for DMA` printks; screenshot persists at PS2 logo, followed by an unresolved guest TLB exception. The upload's absence from Git does **not** prevent analyzing or preserving these measurements. No copyrighted guest RAM or firmware was committed.

## Specific source defect

- `pcsx2/DEV9/DEV9.h`: `SPD_DBUF_AVAIL_MAX = 0x10`, physical FIFO bytes `0x10 * 512 = 0x2000` (8192).
- Old `pcsx2/DEV9/DEV9.cpp::DEV9readDMA8Mem` and `DEV9writeDMA8Mem` asserted `size <= 0x2000`, even though guest IOP DMA8 requests are larger. Those assertions did not limit the received DMA size in non-assert builds.
- Old `DEV9runFIFO` called HDD -> FIFO once, IOPReadFIFO once, HDD -> FIFO refill once, **then returned**. For a healthy 12 KiB read, the first 8 KiB reaches IOP, another 4 KiB enters the FIFO, but no second IOPReadFIFO is called. `FIFOIntr` only calls `psxDMA8Interrupt()` when `dma_iop_transfered == dma_iop_size`, so the stranded remainder cannot signal IOP completion absent another `DEV9runFIFO` trigger.
- Reverse direction IOP -> FIFO -> HDD was likewise limited to one pump, despite being able to receive an IOP request bigger than the FIFO.
- Guest-visible consequence is strongly consistent with the exact **8 KiB versus 12 KiB** boundary and the ~30-second retries. However, neither the old nor new build has an actual DMA-byte counter trace in the uploaded log, so **an unrelated additional stall is not ruled out**.

## Source change and prospective test

**Commit `5a8117f325e0`**: `DEV9runFIFO` now makes progress-checked FIFO passes until the pending IOP request is consumed or no byte counters advance, checking reset-generation and pointer validity before each pass. Stalled/empty/full FIFO exits without spinning. It does not fake an IRQ, change ATA command semantics, alter filesystem or bypass guest timeouts. Removed both invalid FIFO-sized assertions; a DMA8 request can legitimately span multiple ring fills. This is an intentionally narrow behavioral change.

**Commit `baac0b5561c7`**: adds `DEV9FIFO.AtaReadDMA8DrainsRequestsLargerThanThePhysicalFIFO` in `tests/ctest/core/dev9_fifo_tests_20261020.cpp`. It creates a disposable 128-sector patterned raw image, instantiates the real ATA backend, and exercises READ DMA via **the production ATA -> DEV9runFIFO -> IOP DMA8 path** for **`0x3000`/`0x7000`/`0x8000`**. Checks exact output bytes, completion pointer, byte counts, FIFO occupancy, and ATA status. This test is registered in existing `core_test`; no extra CMake target or synthetic copy of the FIFO code was added. It is **not compiled or executed** and may need test-fixture corrections when the user authorizes compilation.

### Verification status

- **Performed:** independent source inspection of FIFO capacity, single-pass bridge, `FIFOIntr` completion condition and IOP DMA8 entry points; scanned exact uploaded log occurrences and retry/reset ordering; committed actual source fix and prospective regression.
- **Not performed:** C++ compilation, any test execution, Visual Studio build, hardware reference run, PSBBN emulator run, real measured post-fix IRQ/DRQ timing, disk-image hash verification.
- **Risk:** synchronous iteration over a whole pending IOP request may alter timing relative to real SPEED hardware; the existing DEV9 model already completed ordinary DMA transfers synchronously, but larger DMA behavior must be validated. The test's `psxDMA8Interrupt()` execution in `core_test` is unverified until compiled/run. The log's `0xFFFF` ATA DATA reads and later guest TLB exception have not been proven caused solely by this stall.

## Next steps for Codex/user

1. **Before broad new edits**, review the pump for termination, reentrancy, IRQ order, reset/cancellation and direction-change correctness, including possible IOP DMA8 callbacks causing a new request while the bridge is pumping. Verify current `core_test` fixture initializes sufficient IOP memory to receive `psxDMA8Interrupt()`; if not, adjust test scaffolding without duplicating production logic.
2. Await user's authorization to compile. Run focused `core_test` production-path FIFO regression first; inspect Byte/IRQ statuses at exactly `0x2000`, `0x3000`, `0x7000`, `0x8000`; add complementary multi-FIFO write test if needed.
3. Next guest validation: compile the audit branch and cold-boot a **clone** of the same image with matching metadata sidecars. Check if a `0x3000` DMA completes without the 30-second wait, whether the root/other partitions mount, and if logo progresses beyond static PS2. Save the full log and scheduled savestates for later forensic analysis.
4. If the timeout persists, add bounded per-command FIFO/IOP counter tracing at the request/start, each pump exit, ATA completion, `FIFOIntr`/DMA8 IRQ, and reset. Treat subsequent TLB exceptions and `0xFFFF` reads as secondary until evidence establishes linkage.

**Hard constraints:** user knows savestates do not roll back external HDD; all manual/scheduled/resume savestates remain enabled. Never add back the HDD savestate ban. No unrelated EE/JIT/guest, disk contents, Sony security or ATA-5 extensions were modified.
