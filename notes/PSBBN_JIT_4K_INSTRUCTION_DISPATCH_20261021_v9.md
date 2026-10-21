# PSBBN EE JIT: 4 KiB instruction-fetch TLB dispatch (2026-10-21 v9)

## Branch / state

- Repo: `Masamune3210/pcsx2`.
- Continue the **existing** branch `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`. No additional nested branch.
- Last uploaded Windows build: `PCSX2 v2.9.92-201-g1e7ddca45`; files `emulog(20261021-020139).txt` and `psbbn-kernel-log(2).txt`.
- Latest **source-only, unbuilt** mapping patch commits: `7cfa20c6b03123c76db8b88bb35e98a682c5b143` (4K recLUT); `835e501059ae3fa06c605f2cca5b3a8d91a4a31f` (TLB synchronization); `93c98945423c354244626e3a420e3eded735e7f5` (correct user/kernel mapped segment invalidation).

## Verified runtime progression

- Guest Sony Linux 2.4.17_mvl21 boots under **EE JIT**, identifies HDD partitions hda1/hda2/hda4/hda5/hda6/hda7 and **mounts ReiserFS root `/dev/hda1` read-only**. Kernel ring last messages: `VFS: Mounted root (reiserfs filesystem) readonly.` then `Freeing unused kernel memory: 68k freed`. This is a real JIT success; root journal bug is resolved by earlier precise JIT data TLB fault handling.
- Confirmed four-phase JIT journal field store/read: at `0x800B1578`, writes `C0005290` to `C00052DC`, then `0x800B1580` reads **`C0005290`** successfully. Mapping is `archMapped=1`, `archWritable=1`, physical `002EF2DC`.
- Newly implemented instruction-fetch `TLBL` exception handler (prior `3efa5335`, compile namespace fix `07c7bbcd`) actually fires for user PC `0FB643C0`. The user process stack is `7FFF7F20`; Linux's guest TLB handler probes and writes TLB slot 47. At final probe `EntryHi=0FB64001`, `EntryLo0=0007B85A` (V=1, D=0, user read-only/executable code page), `EntryLo1=00000010`; ERET retries `0FB643C0`.
- **Despite a valid architectural TLB mapping and Linux ERET, PCSX2 still invokes recLUT unmapped page handler at `0FB643C0` and pauses.** This is a separate instruction-dispatch alias defect, not journal, MagicGate, or ATA.
- The TLB fill points to valid physical RAM `0x01EE1000` for guest virtual page `0x0FB64000` (first 4K of an 8K pair). The old recLUT only maps 64K chunks established at EE boot and never reflects TLBWI/TLBWR mappings.

## New implementation (source-only, no Windows validation yet)

Files:
1. `pcsx2/x86/ix86-32/iR5900.cpp` retains legacy 64 KiB static recLUT/hwLUT boot aliases but expands them into new **4 KiB recLUTPages/hwLUTPages** (1M pointer entries, 1M u32 entries). Both host JIT dispatchers shift the virtual PC by 12 rather than 16; `PC_GETBLOCK` and `HWADDR` use the 4K tables. Each active guest page maps to its *physical* `recRAM` block identity so multiple aliases share compiled blocks.
2. The new `recMapTLBPage(virtualPage, physicalPage)` and `recUnmapTLBPage(virtualPage)` adjust one 4 KiB code alias at a time. Unmapped user (KUSEG) and KSEG2/3 translations return to the unmapped recLUT handler; KSEG0/1 boot direct aliases remain. Scratchpad/non-RAM physical pages are deliberately not aliased to main RAM.
3. `recReserveRAM` rebuilds new tables from boot aliases and reapplies live architectural TLB mappings so JIT cache resets don't forget Linux's address space.
4. Guest opcode fetches and JIT code-integrity reads previously using `PSM(virtualPC)` now resolve `PSM(HWADDR(virtualPC))`; leave `PSM(oldBlock->startpc)` unchanged because that argument is **already physical**. The recompiler already splits blocks at 4 KiB page boundaries.
5. `pcsx2/COP0.cpp::SyncActiveTLBMappings()` hooks TLB map/unmap and active-ASID selection changes to update recLUTPages after the established vTLB mapping + code invalidation, while `ResetActiveTLBMappings()` also drops tracked code aliases. Functions exist only in the x86-64 EE JIT path; interpreter semantics unchanged.

## Risk/validation status

**Not built or tested with Windows/MSVC, no boot verdict for this latest patch.** Expected additional static footprint is around 12 MiB for 4KiB pointer and hardware tables (plus existing 64KiB boot tables). Existing per-memory-instruction register flush from previous fixes remains a performance concern. Do not claim full Linux userspace boot until demonstrated.

Test next:
- Build existing branch head with Windows x64/MSVC; fix any C2065/link errors using current source, do not create new branch.
- Cold boot PSBBN with existing `bbntest.raw`. Verify ReiserFS still mounts, and then that `0FB643C0` dispatch executes translated user instructions (or that Linux signals it if guest actually can't fetch).
- If recLUT still trips, capture `EntryHi/Lo0/Lo1`, `PC`, and JIT physical alias for `0FB64000`; confirm GPR and JIT opcode fetch use `0x01EE1000`.
- Watch block-cache invalidation across ASID changes, 4KiB aliases, and self-modifying code; compare against interpreter boot beyond userspace startup.
- Do not force a physical page for this PC or bypass the guest TLB handler. Preserve later DEV9 ATA-4 implementation and root-mount repair.

## Ghidra metadata

No new Sony binary symbols, structures, signatures, or data redeclarations were proven here. Previously confirmed ReiserFS metadata and runnable `ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java` remain in the `Masamune3210/ps2bbndecomp` repo.
