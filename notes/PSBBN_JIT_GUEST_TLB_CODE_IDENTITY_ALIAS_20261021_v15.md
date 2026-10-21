# PSBBN EE JIT: stale KUSEG/KSEG2 instruction identity aliases (v15)

## Where to continue

Repo `Masamune3210/pcsx2`; **existing** branch `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`. Source commit: `b4930a94f7dcb5e70f877ab229abedcc6593dec4`. Do not create a nested branch. **Not compiled or runtime verified**.

PS2 retail/BIOS compatibility and performance take priority. The original 64 KiB `recLUT/hwLUT` boot identity mapping and native fast dispatch are intentionally preserved. This changes only the fine 4 KiB table used when a guest TLB-based instruction mapping requires it.

## Uploaded diagnostic

`emulog(20261021-032722).txt`, PCSX2 `v2.9.92-219-g4f3966782`, 75,833 lines. This is the first boot after the first-fetch null-pointer guard and cross-page delay-slot interpreter fallback.

- BIOS, HDDLOAD/KELF pass. Journal pointer `C0005290` is stored and read back correctly at `C00052DC`, JIT TLB writable.
- Fine-grained dispatch activates at user PC `0FB643C0` (line 44267).
- Cross-page branch fallback activates **3 times** for `JR` at `0FB73FFC` whose delay slot resides at unmapped `0FB74000`; no host null dereference is logged (lines 44634/44657/44743).
- At line 47512, many `TLB Miss, pc=0x401500 addr=0x401500..0x4015b0 [load]` follow a userspace execution path into `0x00401500` despite missing guest virtual translation. Line 47559 onward logs unrecognized MIPS opcodes including `0x72746e65`, `0x72676f72`, `0x736e6f69`, `0x74636574`, whose byte patterns correspond to ASCII text rather than intended MIPS instructions. This is a strong symptom of instruction dispatch into non-code physical backing.
- Lines 47569–75820: **28,239** `cpuTlbMiss` entries for data load fault `BadVAddr=00006F60` at JIT sampled next PC `0FB62D44`, guest faulting PC `0FB62D40`, `Status=00010C11`. The loop was paused. No `R5900 Recompiler: Instruction fetch has no physical backing` error, and no `EE JIT repaired instruction alias` record. Previous host null accesses are not observed.
- No Sony Linux printk ring supplied with this upload. The PCSX2 log itself has no `VFS: Mounted root` record, so do not make a new mount claim from this log alone. Prior Windows runs had confirmed mounting.

## Root-cause finding in PCSX2 source

`pcsx2/x86/ix86-32/iR5900.cpp::recReserveRAM()` filled all 1,048,576 fine 4 KiB recLUT/hwLUT entries by cloning the original 64 KiB boot alias table:
```cpp
recLUTPages[block * 16 + sub] = recLUT[block];
hwLUTPages[block * 16 + sub] = hwLUT[block];
```
The native table identity maps the first exposed RAM through KUSEG (low addresses) and KSEG2/3 (C/D segments) for legacy PS2 compatibility. That is reasonable for the **native 64 KiB dispatch**, but it is not a valid starting state for **TLB-managed 4 KiB dispatch**. Unmapped user PCs such as `0x00401500` could hit a direct code-block table entry and be compiled using physical RAM at the same numeric address, without any guest TLB mapping. Thus a valid physical pmap pointer hid the missing guest virtual translation; `recRecompile`'s null-physical guard from v14 would not catch it.

This bug existed before `b4930a94` even though `recUnmapTLBPage` already invalidated stale *previously mapped* guest TLB aliases. Pages never touched by a guest TLB entry retained their incorrect boot identity alias forever.

## Committed fix

In `recReserveRAM`, initialize the fine-grained table from fixed native aliases, then call the existing `recUnmapTLBPage(virtualPage)` for **every KUSEG and KSEG2/3 virtual page**. That maps them to the existing `recLutUnmapped` JIT handler rather than to physical RAM. KSEG0/1 fixed direct mappings remain. After this initialization, `BuildEEActiveTLBPageMap(..., currentASID)` reapplies only actual valid guest TLB mappings to the fine table, with physical code-block identity.

The original 64 KiB recLUT/hwLUT arrays are completely unchanged. Guest memory and BIOS/PSBBN binary images are not modified. A JIT fetch from an unmapped user page should now reach the already implemented guest `TLBL` path rather than silently decoding physical data. Initializing these fine entries occurs at reserve/reset, not on the regular runtime JIT dispatch hot path.

## Follow-up and verification

1. Build the branch at `b4930a94` with MSVC x64 AVX2. Cold boot the same PSBBN HDD image under EE JIT. Confirm BIOS/HDDLOAD remains functional and early journal readback still works.
2. After 4 KiB dispatch activates, determine whether an attempt to execute guest `0x00401500` generates a `TLBL` handled by Linux, or whether no such bogus code fetch occurs. Specifically check **no MIPS decode of ASCII words**, no repeated `0x00006F60` faults, and actual advancement toward PSBBN userspace.
3. If new instruction-fetch `TLBL` loops for `0x00401500`, inspect guest ASID/EntryHi/Lo, VMA/executable permissions, and the underlying return/branch target. Never force-map a guessed page or NOP invalid instructions.
4. Check potential compatibility/performance costs. Native fast dispatch remains original; fine-table fill now invokes an existing page-unmap routine on about 786k managed 4 KiB pages *once during initialization*; measure startup overhead. Perform ordinary PS2 game/BIOS/savestate regression tests prior to any merge.
5. Separate issues remain: cycle-derived approximation of EE CP0.Random under JIT; expensive full register flush around translated memory ops; precise exception and delay-slot semantics. They are not proved corrected by this commit.

No new metadata about guest binary functions or datatypes was proved. Ghidra Java metadata script remains `ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java` in `Masamune3210/ps2bbndecomp`.
