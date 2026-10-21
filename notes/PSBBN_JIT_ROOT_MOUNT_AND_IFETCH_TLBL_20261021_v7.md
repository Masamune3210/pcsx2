# PSBBN: first confirmed JIT root mount; next instruction-fetch fault (v7)

## Branch and source head
- Repository: `Masamune3210/pcsx2`.
- Existing branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
- Latest source commit: `3efa5335f0515219fe8cb6db92423bdb6d92fb23`. **NOT compiled or run**. The previous built and tested revision was `35c2004ce107e809d62e857c619033bbe7e7d24e` (the previous notes-only branch head, with functional source at `246eaff5`).
- Original JIT memory-access repair remains intact; interpreter mode and DEV9 ATA branch changes remain intact.

## New uploaded evidence
- Windows EE JIT log: `emulog(20261021-014858).txt` (239,505 lines, PCSX2 v2.9.92-197-g35c2004ce).
- PSBBN printk ring: `psbbn-kernel-log(1).txt`. Ring contains an earlier 19:48 observer session that ended in `journal-2005`, then a newer 20:47 session that **successfully mounted root**.
- Earlier accidentally selected log was unrelated ARMSX2-NX on Switch.
- Latest guest boot: Sony Linux 2.4.17_mvl21, APA partitions hda1/hda2/hda4/hda5/hda6/hda7 from `bbntest.raw`; journal replayed 3 transactions; `VFS: Mounted root (reiserfs filesystem) readonly.`; `Freeing unused kernel memory: 68k freed`. This definitively clears the initial JIT journal barrier.

## MMU evidence in Windows log
- At line ~41698: first KSEG2 store miss `C0005000` from `801A4934` in the memset routine. Recompiler now calls `cpuTlbMiss` with `TLBS` (Cause 0x0c), faulting `EPC=801A4934` and `BadVAddr=C0005000`; guest TLBWR installs valid mapping for `C0005000`, followed by ERET back to `801A4934`.
- Lines ~43230-43233: four-phase exact-PC witness at SW `800B1578` / LW `800B1580`. Address `C00052DC` is mapped/writable to physical `002EF2DC`; pointer `C0005290` written and **read back identically**, unlike previous zero result.
- 190 `cpuTlbMiss` diagnostic lines in this boot. The previous silent zero/discard defect is decisively fixed for this path.
- After root mounting / early userspace, PCSX2 emits `ReportErrorAsync: R5900 Exception: Jump to unmapped recLUT page (PC: 0x0fb643c0)` (Windows log ~line 239490, t=25.2092). The preceding JIT ERET from kernel code `8001F42C` returned to `0FB643C0` with user mode status `00010C11`, SP `7FFF7F20`. Active last observed TLB entry was `EntryHi=10000001` so cannot assume code `0FB643C0` was mapped.
- The user EPC may be legitimate code requiring demand paging or a corrupted control-flow target; **not yet determined**. The new instruction-fetch exception path is necessary to distinguish these correctly.

## New source fix: guest instruction-fetch TLBL

Change `pcsx2/x86/ix86-32/iR5900.cpp::recError()` used by `_DynGen_UnmappedRecLUTPage()`:
- When recLUT dispatches to an unmapped fetch in an address whose translation depends on guest TLB (`KUSEG` or `KSEG2/3`), and the existing vTLB entry is the default unmapped *virtual* handler ID 0, the JIT now issues a real `TLBL` through `cpuTlbMissR`, then immediately exits the translated dispatcher.
- `recError()` receives `cpuRegs.pc` pointing at the not-yet-fetched instruction, while `cpuTlbMissR()` subtracts 4 internally. Set the temporary PC to `fetch_pc+4`, call `cpuTlbMissR(fetch_pc,0)`, resulting in precise `EPC=fetch_pc` and `BadVAddr=fetch_pc`, with BD clear.
- Remains a fatal reported recLUT error for direct-mapped kernel segments and for vTLB entries already mapped to some other handler or direct memory. Do not hide stale recLUT mappings or bus errors behind artificial guest TLB misses.
- Emits up to 32 `EE JIT instruction-fetch TLBL pc=...` records (plus existing cpuTlbMiss diagnostic).
- There is no guest memory, disk or PSBBN-specific address patch.

## Remaining work
1. Build commit `3efa5335` under Windows MSVC x64. This new source code is not runtime validated. Continue **cold boot** with the same image and host settings.
2. Verify root mounts and observe whether guest Linux handles `TLBL` at `0FB643C0`: guest exception entry, TLB refill, and ERET. If guest signals the task or shows kernel faults, collect `EPC/Cause/BadVAddr/EntryHi`, user process mapping and signal context to establish whether the address was legitimate.
3. If a `TLBL` repeats indefinitely for this PC, do **not** blindly map it. Inspect guest VMAs, ELF entrypoint, signal frame and branch/return provenance. Treat the potential bad return address as a distinct next bug.
4. Measure performance of the earlier correctness-first guest register flush before JIT memory-class opcodes. Optimize only after guest semantic correctness is established.
5. Ghidra metadata: unchanged. Prior symbols `ReiserfsJournalInit@800B1348`, `ReiserfsAllocateListBitmaps@800ADBA0`, `ReiserfsGetListBitmap@800ADC70`; existing runnable script `ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java` in the ps2bbndecomp branch. No new binary symbols inferred.

## Verification disclaimer
The *data-fault* repair is verified by live Windows boot, journal readback and root mount. The *instruction-fetch* repair is committed but not yet built or run.
