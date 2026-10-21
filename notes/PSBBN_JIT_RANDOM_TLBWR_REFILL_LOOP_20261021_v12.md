# PSBBN JIT: alternating userspace TLB refills caused by fixed CP0.Random (v12)

## Where to continue
- Repo: `Masamune3210/pcsx2`.
- Existing branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`. Do NOT spawn a nested branch.
- Windows run: `emulog(20261021-024506).txt` from `v2.9.92-209-gd59d8949c` and `psbbn-kernel-log(4).txt`.
- Source commits created after this run: `fa1b7c993bf4a1839583eec8ee3cb858c77511af` (Random), `040735b00fef56c72c4fe1ebc3ed1b040b9ec57e` (avoid double virtual translation of physical rec code-check addresses), `fff797905a6db9f87d19748b9dc60c294f46a9d2` (explicit <algorithm> include).
- **Latest functional changes unbuilt/unrun under Windows**. Do not assert the loop is resolved yet.

## Evidence and advance
1. The demand-driven native-first 64KiB -> guest-code 4KiB dispatcher successfully completed BIOS, HDDLOAD/KELF authentication, Sony Linux boot and root ReiserFS mount. The 21:44:33 observer session's printk ring records `VFS: Mounted root (reiserfs filesystem) readonly.`, `Freeing unused kernel memory: 68k freed.`; no current `journal-2005`.
2. At Windows log line 44298, `EE JIT enabled 4KiB guest TLB instruction dispatch at 0fb643c0`. The guest successfully fetches subsequent instructions through `0fb643d4`. This verifies dynamic 4KiB code dispatch works at least for this page; not full userspace or native-game regression testing.
3. At line 44299, `LW` instruction `8f84806c` (from guest `0fb643d4`, sampled next PC `0fb643d8`) accesses unmapped `0fb8941c` and raises TLBL, EPC=`0fb643d4`. Guest refill maps data virtual page `0fb89000` with `EntryHi=0fb88001`, `EntryLo1=0007ba9e`.
4. Host later fetches `0fb643d4` again, but its instruction page is gone, because that preceding `TLBWR` overwrote the code-page entry. Linux refills code-page `0fb64000` with `EntryHi=0fb64001`, `EntryLo0=0007b85a`, evicting data page. Alternating misses then repeat at precisely 27 guest cycles per fault while the emulator's `CP0.Random=0x2F` and `Wired=0` stay fixed.
5. The Linux handler consistently uses `TLBWR` at guest `0x8000004c`, TLB slot **47**. The logged slot toggles between `0fb88001` and `0fb64001`. Total `cpuTlbMiss pc:fb643d8` samples: 37,255; emulation paused near 26.19 seconds. These are *not* failures in the HDD or ReiserFS.
6. Prior global 4 KiB dispatcher at build `b035160c8` regressed BIOS/HDDLOAD; demand-driven mode restores those paths and honors PS2-first compatibility requirement.

## Source finding: missing advancing Random register
- In `pcsx2/R5900.cpp` EE reset initializes `cpuRegs.CP0.n.Random=47`.
- In `pcsx2/COP0.cpp::MTC0`, writes to `Wired` reset `cpuRegs.CP0.n.Random=47`.
- In `pcsx2/COP0.cpp::TLBWR`, previous implementation simply read `Random & 0x3f` and wrote that TLB index. No previous observed PCSX2 code advanced Random over the non-wired range.
- This is a general EE CP0 model defect, not Linux-specific behavior. The hardware Random register is not a constant. TLBWR must not perpetually replace the same slot.

## Experimental source fix
`COP0.cpp` now has an `UpdateTLBRandom()` helper that selects within `[Wired,47]` using the emulated guest cycle counter, updates `CP0.Random` before every `TLBWR`, and supplies the same current value to `MFC0 Random`. It incurs no per-instruction JIT host cost and introduces no additional unsaved C++ epoch state:
```
wired = min(CP0.Wired & 0x3f, 47)
available = 48 - wired
CP0.Random = 47 - (cpuRegs.cycle % available)
```
**Accuracy caveat:** this is a cycle-derived approximation of hardware Random's per-instruction decrement/phase, not yet hardware-cycle-perfect. In particular, immediate reads after `MTC0 Wired` may not match an exact phase. It is superior to the previous constant and makes general TLBWR selection functional. This approximating approach **must be validated for native retail games and refined if timing/phase matters**; do not merge merely because Linux advances.

`iR5900.cpp` also repairs a latent address double-translation introduced by the 4KiB change: `memory_protect_recompiled_code` had set `lpc=inpage_ptr=HWADDR(startpc)` (already physical) then compared `PSM(HWADDR(lpc))`. The second `HWADDR` was incorrect. The change restores both sides to `PSM(lpc)`. This matters for normal PS2 self-modifying code/manual block checking.

## Next Windows validation
1. Compile current head on Windows/MSVC Release AVX2 x64. If compiler error, fix the actual compile issue, do not patch guest bytes or force-map faulting addresses.
2. Cold boot on same `bbntest.raw`. Confirm BIOS/OSDSYS/HDDLOAD work, that ReiserFS root still mounts, then that adjacent code/data TLB pages at `0fb64000` and `0fb89000` occupy *different* unwired TLB slots and execution progresses past `0fb643d4`.
3. If still faulting, use precise `EPC/BadVAddr/EntryHi`, both TLB slot values and the index selected at each TLBWR; inspect whether guest `Random` is changing and whether JIT block mapping follows.
4. After functionality, compare normal retail title compatibility, savestates, timing and performance. The original 64KiB JIT dispatch must remain native default.
5. No new Ghidra metadata was proven. Existing ReiserFS Ghidra Java metadata script `ApplyPSBBNReiserfsJournalMetadata_20261019_v4.java` remains in `Masamune3210/ps2bbndecomp`.
