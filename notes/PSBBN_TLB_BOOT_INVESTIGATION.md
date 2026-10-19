# PSBBN / EE TLB and HDD Boot Investigation

## Runtime after null-ppmap guard: host crash passed, first animation (2026-10-19)

**User-built and executed `PCSX2 v2.9.92-124-gcb4d9cd02`** (EE interpreter). The observer guard in `cb4d9cd` is confirmed successful at runtime: 8 `PSBBN-RA-PHYS` records, all `vtlbAvailable=0`, no native crash through **294s**. The formerly fatal `2AB0F2BC` prologue was crossed; at **176.883–176.976s**, ASID04 stored/restored RA `00403F58` at `7FFF7390` (archPhys `001DB390`), and at **213.366–213.367s**, ASID57 stored/restored RA `004093B4` at `7FFF7C70` (archPhys `01BB0C70`). Host VM paused and shut down normally around 294–295s.

**Visible change:** savestate images 004–019 show static PlayStation 2 logo, 020 black, 021–028 a **moving ring of blue-white orbs** at ~222–292s; no completed BB Navigator menu yet. Guest remains active through transition. Diagnostic reported 4397 TLBL (`code=08`), 596 TLBS (`0C`), 26 Mod (`04`), 45 syscall (`20`) records plus capped overflows, and 4311 `branch delay!!` lines. Counters are rate-limited; these do not independently prove a hang.

User supplied `emulog(7).txt` (SHA256 `74851f351a6f0ab40f2c1ebb0039354eef92d384f6e8ece3d1f255810f128d03`, 61,659,844 bytes) and `sstates(6).zip` (SHA256 `a8c6813e6bbc572a12d60c564499bb25f30d3f512d73c27f774591a5d33d579c`, 28 states, 80,487,276 bytes). Full extracted evidence summary in [ps2bbndecomp research note](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_POST_PPMAP_GUARD_RUN8_20261019_v1.md). Raw new attachments not yet in Git. Next: establish whether orbit animation progresses to BB Navigator UI, or identify true guest-side barrier. Do not re-open solved null-ppmap fault or modify JIT until interpreter is validated.


## Codex native observer ppmap crash fix applied (2026-10-19)

**Applied to this branch:** [`cb4d9cd02f306e244446c47f032bf2231dbf812f`](https://github.com/Masamune3210/pcsx2/commit/cb4d9cd02f306e244446c47f032bf2231dbf812f), changing **only** `pcsx2/COP0.cpp::PSBBNTraceReturnSlotTranslation`. This is the exact research patch `ps2bbndecomp/patches/pcsx2/20261019-guard-optional-ppmap-return-observer.patch`. It checks optional `vtlb_private::vtlbdata.ppmap` before calling `vtlb_V2P(slot)`, emits `vtlbAvailable=0` and unavailable sentinel instead of dereferencing a nonexistent Goemon LUT. Do not interpret `samePhys=0` as a real discrepancy when no lookup exists. It does not allocate the optional LUT, change guest CPU state, or alter general TLB/Dirty-bit semantics.

**Cause and evidence:** The debugger's previous host AV read at `0x001FFFDC` matches accessing null `ppmap[0x7FFF7]` for the adjusted frame's RA slot `0x7FFF7390`. Codex's extracted-production-source ASan reproduction failed at that exact address before the fix, then **9/9** synthetic observer cases passed after it. Independent savestate reader tests passed **4/4**. This is strong source-level host-crash evidence, not a new Windows debugger stack.

**Verification still required:** User must pull and compile PCSX2 with VS/MSBuild `Release AVX2|x64`, then cold-boot PSBBN with EE interpreter. Expect `PSBBN-RA-PHYS ... vtlbAvailable=0` rather than a host AV when the tracked SW RA executes. Prior user-confirmed `EEActiveTLBNative` 12/12 tests remain valid and need not be rerun for this observer-only change. If a host crash persists, capture its *new* host fault address/stack. PSBBN guest GP/RA provenance, Mod retry/COW, and boot hang remain unresolved; JIT is still deferred.


## Native EEActiveTLB regression tests: 12/12 PASS (2026-10-19)

**User-verified, compiled execution:** In the native Visual Studio/MSBuild `Release AVX2|x64` test harness, the user successfully ran `bin/tests/EEActiveTLBNative-x64-Release AVX2.exe --gtest_filter=EEActiveTLB.*`. GoogleTest reported **12 tests run, 12 passed, 0 failures** (3 ms test suite, 4 ms total); all individual `EEActiveTLB` tests passed.

The native harness comprises `tests/ctest/core/ee_active_tlb_asid_tests_20261018.cpp`, bundled GoogleTest sources, and a test-local `_cpuRegistersPack` definition (`EEActiveTLBNativeGlobals.cpp`) so it does not need to link production `R5900.cpp`. This is the **first actual compiled/executed validation** of the production header-inline `BuildEEActiveTLBPageMap` and `EEActiveTLBStoreNeedsModification` behavior on Windows. Prior notes saying the 12 tests were unexecuted have now been superseded for this test target.

**Coverage:** ASID isolation, global/invalid selection, larger pages, scratchpad, distinct even/odd Dirty permissions, protected shared PFN across three ASIDs, independent COW replacement PFN/writable permission, permission-only TLB rewrite and read-only global behavior. This is **unit-level proof, not full interpreter end-to-end proof**: the suite does not directly execute `vtlb_memWrite`, `cpuTlbModification`, guest EPC/BD exception dispatch, Linux fault repair/retry, or a complete PSBBN boot.

**Next gate:** Run a fresh interpreter-only PSBBN boot after rebuilding with diagnostic null-dereference guard commits [`8dde4ac`](https://github.com/Masamune3210/pcsx2/commit/8dde4ac9992c558401ea198f288be037132cc8b2) and [`d614b1f`](https://github.com/Masamune3210/pcsx2/commit/d614b1f88a054fbea2b69db8cc0a80fd3f4e52d6). Confirm whether `PSBBN-RA-PHYS-SKIP reason=eeMem-null` occurs and whether PCSX2 remains running, and compare guest fault progress to the previously captured first TLB Modification event around 170.5199 s. A test-pass does not establish a host crash fix. JIT remains explicitly deferred.


## Native EEActiveTLB linker dependency resolved in source (2026-10-19)

The user's first native MSVC `Release AVX2|x64` build of `EEActiveTLBNative` failed with `LNK2001` (`?\_cpuRegistersPack@@3UcpuRegistersPack@@A`) from `ee_active_tlb_asid_tests_20261018.obj`, followed by `LNK1120`. Root cause: `R5900.h` defines translation-unit-static references `cpuRegs`/`fpuRegs` to the global `_cpuRegistersPack`. The real emulator defines that global in `pcsx2/R5900.cpp`, which should not be linked wholesale into the standalone selector test. 

PCSX2 commits [`a614be3`](https://github.com/Masamune3210/pcsx2/commit/a614be326302357f6118773323263ae44cf75275) and [`ae7ec73`](https://github.com/Masamune3210/pcsx2/commit/ae7ec73e7f07a2a44426c517ceaf89d498cf06a2) add `tests/ctest/core/EEActiveTLBNativeGlobals.cpp` containing just an aligned zero-initialized `cpuRegistersPack _cpuRegistersPack{}`, and include it **only** in `EEActiveTLBNative.vcxproj`. Production `R5900.cpp`, production TLB/exception code and CMake test targets are unchanged. This is an **isolated test linking dependency**, not an attempt to mock actual guest state for integration tests.

The user's MSVC link error is explained and its missing definition is now present. **Rebuild and execution of all twelve tests have not been observed**. Next: pull, rebuild `EEActiveTLBNative`, run with `--gtest_filter=EEActiveTLB.*`, and retain any new compiler/linker/test diagnostics. 


## Native Visual Studio EEActiveTLB test target added (2026-10-19)

User's PCSX2 checkout uses `PCSX2_qt.slnx` and native MSBuild outputs (`build/obj-pcsx2-x64-Release AVX2`), **not a CMake-configured build directory**. Earlier directions to look for `CMakeCache.txt` in the existing `build` directory were inapplicable. Preserve the user's native `build` folder; do not overwrite/reconfigure it for CMake. The CMake `core_test` target is excluded from the default build and has no corresponding native .exe.

PCSX2 changes on branch `note-working/psbbn-tlb-boot-20261017`: new `tests/ctest/core/EEActiveTLBNative.vcxproj` (`181233d`) and solution integration `PCSX2_qt.slnx` (`f27be10`). This is a **standalone native MSBuild console GoogleTest executable** compiling the same 12-source-case file `ee_active_tlb_asid_tests_20261018.cpp` with bundled `gtest-all.cc` and `gtest_main.cc`. It exercises production **header-inline selector and Dirty-bit predicate** without linking the whole emulator. It **does not** execute integrated guest fault delivery, RAM-store cancellation or Linux COW; these still require runtime tests.

The VS solution lists `EEActiveTLBNative` under the `tests` folder, excluded from Build Solution by default. After pulling, choose `Release AVX2|x64`, right-click the test project and Build. Executable: `bin/tests/EEActiveTLBNative-x64-Release AVX2.exe` (if it compiles), run with `--gtest_filter=EEActiveTLB.*`. The native project exists in GitHub and its referenced paths/solution entries were statically checked; **MSVC compile and twelve tests remain unverified**. The CMake tests stay unchanged for users of that build system.


## Native PSBBN observer crash addressed (2026-10-19)

User's Visual Studio debugger captured `0xC0000005` reading host `0x00000000001FFFDC` in `PSBBNTraceReturnSlotTranslation` (`pcsx2/COP0.cpp` old line 226). The PSBBN diagnostic dereferenced `eeMem->Main` without checking if the memory base was present; the low host fault address is consistent with `eeMem == nullptr`. PCSX2 commit [`8dde4ac`](https://github.com/Masamune3210/pcsx2/commit/8dde4ac9992c558401ea198f288be037132cc8b2) now guards the diagnostic's EE RAM reads and emits one bounded `PSBBN-RA-PHYS-SKIP` marker when the base is null. **This is a host diagnostic fix, not an EE TLB or guest Linux correction.** The reason the pointer was unavailable is not established; Windows rebuild/retest pending. Prior post-Dirty runtime evidence of actual guest TLB Modification exceptions remains valid. See [Post-Dirty runtime and native debugger diagnosis](PSBBN_POST_DIRTY_RUNTIME_AND_TEST_BUILD_20261019_v1.md) for full evidence and next steps.


## EE interpreter Dirty-bit enforcement implemented, JIT explicitly deferred (2026-10-19)

**Latest implementation reference:** [PSBBN_INTERPRETER_DIRTY_MOD_IMPLEMENTED_20261019_v1.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_INTERPRETER_DIRTY_MOD_IMPLEMENTED_20261019_v1.md). The user explicitly directed fixing the **EE interpreter first**, postponing JIT work until interpreter correctness is established.

**Source code on PCSX2 PSBBN branch:** `0273f9d`, `eba103e`, `237fd77`, `c86de77`, `cba0adb`, `54179ea`, `af17acf`, `c656aab`, `73f1b4a`. The active TLB page model retains `EntryLo.D`; writable changes participate in synchronization. Interpreter stores of **8/16/32/64/128 bits** through `vtlb_memWrite` and `vtlb_memWrite128` check a valid matching D=0 page **before write**. They deliver `cpuTlbModification` / `EXC_CODE_Mod`, using existing CP0 fault register, exception-vector, EPC and branch-delay handling, then cancel the faulting instruction. Reads remain mapped; unmapped entries retain existing TLB miss logic; scratchpad and direct-mapped kernel aliases retain their behavior. No JIT implementation made.

**Regression suite:** 6 newly added `EEActiveTLB` GoogleTests for even/odd D halves, the three-ASID COW shared stack PFN from sixth boot, D-only permission rewrite, read-only global translation, scratchpad exemption, plus a new COW repair test (commit `486f5d7`) checking that ASID05 receives a new writable PFN while ASID06 retains the original D=0 page; 12 tests total. **12/12 cross-file static integration checks passed. The new unit tests were committed but NOT actually compiled or executed.** Windows/MSVC build and a PSBBN cold boot have NOT been run for this revision. Do not claim the COW defect or logo is runtime-resolved until the user tests it.

**Next step:** build PCSX2 on Windows, execute EEActiveTLB tests, then **cold boot with interpreter**. Check that writes through `D=0` raise Mod (`Cause.ExcCode=1`), preserve physical RAM and let Linux's COW handler retry after installing a writable mapping. Compare the previous ASID05 zero-GP, ASID08 zero-RA, user SIGSEGV and kernel-idle failure. Do not load corrupted old savestates. The *JIT* requires its own independently tested write checks and precise exception delivery **later**, not as part of this interpreter checkpoint.


## Sixth-run ASID08 zero return plus physical mapping witness (2026-10-19)

**New independent evidence:** [PSBBN_SIXTH_BOOT_RA_ZERO_AND_SIGNAL_STORM_20261019_v1.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_SIXTH_BOOT_RA_ZERO_AND_SIGNAL_STORM_20261019_v1.md). Direct analysis of all 19 user-uploaded `PSBBN-auto-1792408073646415-001..019.p2s` (all ZIP CRC checks pass) and `emulog(4).txt` SHA256 `e2f241b804dadf0131b0207b69c69421fccb5623a2a95ef8708e487447601fab`, PCSX2 binary `v2.9.92-84-gc4a8e1006`. **Do not conflate this with older fifth-run frame logs.**

At **167.4450s**, `ASID08` saved valid `RA=0x2AB4C21C` at stack `0x7FFF7120` (`2AB0F2D8: sw ra,0x40(sp)`). At **167.5202s**, same ASID/stack slot reached `2AB0F3A4: lw ra,0x40(sp)`, immediately followed by `PSBBN-FAULT pc=0 bad=0 ra=0 asid=08`. This is a **new zero-return symptom**, missed by the older `PSBBN-RA-WRITE` observer restricted to GP-adjacent nonzero values. Only seven logged virtual stores to `0x7FFF7120`; **none between this ASID08 save and restore**. Independently, ASID09 accesses `0x2AD00420` at 167.5196/167.5216; ASID07 signal cascade repeats EPC `0x2AB21114` at 167.5869–167.7443 (4096 Linux IHIN events) with no direct original fault record due quota. Slot015 has PID12/13 live; slot016 PID12 is TASK_ZOMBIE exit `0x0B` and kernel idle, unchanged logo. Raw `eeMemory.bin` physical `0x001DB120` changes `0x2AC24400` (slot015) to `0x10008270` (slot016), but **do not attribute the physical PFN to ASID08 at a different instruction time without a live translation witness**.

**Already implemented, diagnostic only:** PCSX2 commits `a7b778d`, `8387bb7`, `6a00420`. New `PSBBN-RA-PHYS` at the verified save/restore logs phase before/after, ASID, slot, architectural 48-entry TLB-selected physical address/physical word, live `vtlb_V2P` mapping/physical word, and RA. Independent of the older limited `PSBBN-RA-WRITE` range. **16/16 static integration checks passed; Windows/MSVC compilation and actual runtime unverified**. Uses existing COP0-trace opt-in only; no emulated RAM mutation. A mismatch between architectural physical page and live vTLB is a specific mechanism for correction; do not force a good RA/GP by patching guest state.

**More fundamental architectural issue already verified separately:** [PSBBN_SIXTH_BOOT_COW_DIRTY_GAP_20261019_v1.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_SIXTH_BOOT_COW_DIRTY_GAP_20261019_v1.md). User-stored `EntryLo.D=0` pages sharing a stack PFN across ASIDs 05,06,08 are incorrectly mapped writable because the current active-TLB selector drops Dirty. **TLB Modification (ExcCode 1) guest traps are required**, with COW and interpreter/JIT parity. This is a general emulation defect, independently of identifying which store first caused this run's crash. Do not mark resolved by extra tracing alone; a correct architectural fix with regression tests is still pending.


## Sixth-run independent COW/Dirty-bit crosscheck (2026-10-19)

**Read [PSBBN_SIXTH_BOOT_COW_DIRTY_GAP_20261019_v1.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_SIXTH_BOOT_COW_DIRTY_GAP_20261019_v1.md) before changing MMU semantics.** New direct independent scan of 19 states, specifically **015**: `0x7FFF7190` (GP stack save/restore slot) matches three **non-global, valid, Dirty=0** TLB entries: index26 ASID05 `EntryHi=7FFF6005`, index35 ASID08 `EntryHi=7FFF6108`, index45 ASID06 `EntryHi=7FFF6006`. Each has `EntryLo1=000076DA` and maps stack address to **physical `0x001DB190`**. PID12 and PID13 distinct page directories both hold PTE **`0x001DB69B`** for this page, consistent with Linux fork/COW sharing. The earlier RA stack slot `0x7FFF7120` is within the very same guest/physical page.

Direct code audit establishes a **general emulator defect**: `EEActiveTLB.h::BuildEEActiveTLBPageMap` projects valid/ASID but **drops EntryLo.D**, `COP0.cpp::SyncActiveTLBMappings` installs `memSetPageAddr()`, and `vtlb_VMap()` makes a host pointer mapping without guest write-permission checking. A D=0 guest **store should raise TLB Modification (ExcCode 1) before touching physical memory**; otherwise Linux cannot safely handle COW, and stack contents can leak across processes. This is a stronger architectural mechanism than generic memory corruption. **Specific causation of the 167.5139s GP restore is still unproven**; capture015 TLB state precedes the fault, and physical page after process death may have been reclaimed. The existing sixth-run GP save/restore tracer is pushed but unbuilt. Required implementation: write-protect active TLB mappings, deliver precise guest TLB Mod for interpreter **and JIT**, invalidate mapping/code caches as Dirty changes, test global and ASID alias cases; no guest-specific memory/register patch.


## Sixth cold boot: GP restore-zero SIGSEGV before bad-RA hypothesis (2026-10-19)

**Latest canonical analysis:** [PSBBN_SIXTH_BOOT_GP_RESTORE_FAULT_20261019_v4.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_SIXTH_BOOT_GP_RESTORE_FAULT_20261019_v4.md). Log `emulog(4).txt` SHA256 `e2f241b804dadf0131b0207b69c69421fccb5623a2a95ef8708e487447601fab`, binary `v2.9.92-84-gc4a8e1006`. New 19/19 Deflate savestates CRC-verified, prefix `1792408073646415`; capture 015 **166.7700s** and 016 **176.8090s** straddle first fatal signal.

**NEW rootward observation:** At **167.5138s**, ASID05 guest `pc=2ABB7248`, `sp=7FFF7180`, `gp=2AC24400`. Guest instruction at PC is **`8FBC0010: lw gp,0x10(sp)`**. Next `pc=2ABB7258` reports **GP=00000000**, and **`8F828060: lw v0,-0x7fa0(gp)`** attempts **0xFFFF8060** and triggers first `PSBBN-IHIN` at **167.5140s**; recursive signals n=4096 by 167.7443s. Code bytes recovered from 015/016 EE RAM **physical 0x003C6000**: function PIC prologue `2ABB7038 lui gp,7`, `2ABB703C addiu gp,gp,-0x2c38`, `2ABB7040 addu gp,gp,t9`, `2ABB7044 addiu sp,-0x190`, **`2ABB7048 sw gp,0x10(sp)`**. With `t9=2ABB7038`, computed **GP=2AC24400**. Exact failed GP stack slot is **0x7FFF7190** (NOT older RA slot `7FFF7120`). GP zero is proven at dereference; reason the saved slot is zero not yet established. Earlier GP-valued RA restoration is **absent** from sixth run; ASID04,08,09 function saves/restores valid RA. Do not claim same failure mode on every run.

**Diagnostic-only PCSX2 code pushed:** `6ac781b` GP tracing APIs; `8fca7e9` GP-frame host state; `b6d4b3c` GP-save, competing-store, and GP-restore tracer; `13a0149` interpreter pre/post-execution hook; `6ce74e8` independent quota for 32 anomalous GP restores per scheduled savestate. Tracking is ASID-keyed and stack-slot-derived, covers interpreter guest stores in user/kernel modes, preserves watches across autosaves, no guest-memory changes or extra INI setting. **21/21 static source checks passed; Windows/MSVC compile and new runtime unverified.** Next run: capture `PSBBN-GP-SAVE`, `PSBBN-GP-STORE`, `PSBBN-GP-MISMATCH` or `PSBBN-GP-RESTORE`, plus one post-signal state. If saved GP was correct and no guest writer, audit VTLB translation/page selection and kernel context/stack restoration rather than replacing GP or adding mappings. Goal remains fixing semantics in EE JIT, not interpreter-only workaround.


## Fifth cold boot: distinct ASID07 saved-return corruption (2026-10-19)

**New canonical forensic report:** [PSBBN_ASID07_RETURN_STACK_WITNESS_20261019_v3.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_ASID07_RETURN_STACK_WITNESS_20261019_v3.md). Fifth-run `emulog(3).txt` SHA256 `130c52f5a5daefa0ba3d037dec0e2dc8d920ecc7e41100c5b4f1ce70254e6ad5`, binary `v2.9.92-79-g36534d4aa`; 19 individual `PSBBN-auto-1792392525200064-001..019.p2s` savestates. Capture015 `158.2122s PC=800E38F0`, capture016 `168.2456s PC=800185F0`, initial fatal return `163.9573s` between.

**Crucial distinction:** only one original `PSBBN-FRAME-SAVE`: `161.2532s pc=2AB0F2D8 savedRA=00403F58 slot=7FFF7390 asid=04`. Fault at `163.9573s pc=2AB0F3A4 opcode=8FBF0040`: `newRA=2AC24400 oldRA=2AB0F36C sp=7FFF70E0 EntryHi=2ABCC107`, hence **ASID07 slot `7FFF7120`**. Kernel exception EPC `2AC24400` immediately follows and recursive signal frames reach 4096 by 164.3309s. There is **no captured ASID07 prologue**, and the ASID04 frame **is not the same stack slot**; do not claim its saved RA was overwritten in ASID07. All **127** original `PSBBN-FRAME-STORE` reports targeted **ASID04 slot `7FFF7390`**, mainly ordinary GP saves into a reused stack address. Original witness catches bad restore but not creation of failing frame.

**Already pushed PCSX2 diagnostic-only source:** `f38866a` entry/epilogue provenance + exact `7FFF7120` store watch independent of tracked-frame records; `e7bc0f5` interpreter hook for enter/save/exit; `9df80f1` include kernel-mode stores because Linux can modify userspace memory; `3300692` separate ASID07 (96/interval) and other ASIDs (24/interval) log budgets with CP0 Status. Frame records persist across autosaves and retire on matching epilogue. No guest-memory mutation, new settings, or speculative pointer/mapping repair. **19/19 static source checks passed**; Windows/MSVC compile and runtime of latest code unverified. Next run should capture `PSBBN-FRAME-PATH`, `PSBBN-STACK-FOCUS`, `PSBBN-RA-WRITE`, and one subsequent autosave. Then decide if cause lies in caller/task restore, write to stack, or stale TLB physical mapping. Do not assume any one of these yet.


## Fourth cold boot: RA restored from GP-valued stack slot (2026-10-19)

**Latest canonical forensic report:** [PSBBN_POST_ASID_RA_FRAME_PROVENANCE_20261019_v2.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_POST_ASID_RA_FRAME_PROVENANCE_20261019_v2.md). New `sstates(3).zip` SHA256 `da3cdea68bfcd9a2d2efb48dbfde9446d7a426239033f5df31fd94088415fac2` includes **four separate runs** (21+21+20+25 savestates): **only** prefix `1792385099348561` (25 snapshots) is current; accompanying pasted log SHA256 `8fe14374e4ad3e846745ea1504906fed0c35408812c2a7729d293f380545a7c9`, binary **v2.9.92-71-gcea5363cb**. New slot014 at 151.2639s (kernel) and slot015 at 161.2651s (back idle) straddle failure.

**Direct cause of invalid return PC:** at **157.4604s**, `PSBBN-RA-WRITE pc=2ab0f3a4 opcode=8fbf0040 oldRA=2ab0f36c newRA=2ac24400 sp=7fff70e0 gp=2ac24400 asid=07`. Thus guest `lw ra,0x40(sp)` loads GP value from **0x7FFF7120**, then `jr ra` leads to `EPC=2AC24400` on the next `PSBBN-IHIN` signal-delivery trace. Recovered actual function bytes in new slot015 `eeMemory.bin` at physical offset `0x01E033A4` establish **entry `0x2AB0F2BC: addiu sp,-0x50`, save `0x2AB0F2D8: sw ra,0x40(sp)`, exit `0x2AB0F3A4: lw ra,0x40(sp)`, `0x2AB0F3B4: jr ra`**. The frame layout agrees! Its PIC gp reconstruction gives `0x2AB0F2B0+0x00115150=0x2AC24400`. The saved RA slot is either wrong at function entry or overwritten later; current evidence cannot distinguish. **Do not modify emulator guest registers or invent VMA mappings.**

**Diagnostic-only additions already committed to PCSX2 branch:** `542c7dd` declaration, `bc02dbd` bounded 64-frame/128-line per-autosave stack provenance observer, and `40cffe8` interpreter pre-execution integration. **`9cd11c5`** preserves watched frame slots across autosaves, rotating only the logging budget; a function can span multiple capture intervals. Commit `11b33e2` invalidates stale frame records on stack-slot reuse; 16 static integration checks passed. No MSVC build or runtime for this new observer yet. New `PSBBN-FRAME-SAVE` captures original RA at **prologue**; new `PSBBN-FRAME-STORE` records ordinary guest stores overlapping that saved RA word for same ASID. Uses existing COP0 trace opt-in and 10s scheduled savestates, no new envvar or INI. 12 static consistency checks pass; Windows build and **new** diagnostic runtime unverified. Next cold boot only needs to include the prologue through the first invalid return, not another prolonged idle session.


## Third cold boot: unmapped-PC return and RA provenance (2026-10-19)

Latest forensic report in [ps2bbndecomp/docs/PSBBN_POST_ASID_RA_ORIGIN_20261019_v1.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_POST_ASID_RA_ORIGIN_20261019_v1.md). Inputs `sstates(2).zip` SHA256 `a3fe167b28ded2d179049ede5f7fbb9034f035d1a9c4969286452d7e5d25f7c2`, `emulog(1).txt` SHA256 `e4f0c90a811b8e18be63f819cef39284b3f0cadd5a52e1de4fb94c3c2ad2a9d7`. The ZIP includes 21+21 older entries; **only 20 new prefix `1792383328013097`** for this run. PCSX2 binary **`v2.9.92-65-ga4886a00e`** ran interpreter with 10s autosaves.

**Before** the recursive NULL-pointer signal frames, at **136.9811s**, `PSBBN-FAULT pc=2ac24400 bad=2ac24400 code=08 asid=07 ra=2ac24400`; kernel `PSBBN-IHIN` immediately reports `EPC=2ac24400`. This is an instruction-fetch/load TLB miss, not a signal number. Slot13 PID12 `mm=0x801D1120 pgd=0x803FE000` has its highest userspace data VMA ending **0x2AC23000**, and **PTE for 0x2AC24400 is zero** (PGD[0xAB] -> 0x803E3000, PTE[0x24]=0). The previous mapped page has PTE `0x0021E68B`. The offending `$ra` equals the attempted PC, a value previously seen as a module GP base but GP at this exact fault was not logged. This makes a bad RA restoration or wrong register-indirect jump a **specific hypothesis**, not yet a proven emulator bug. Then at **136.9824s**, `PC=0x2AB291B0 BadVAddr=0` recurs with stack consumption (IHIN reaches 4096 by 137.3471s), and PID12 dies SIGSEGV, kernel idle by slot14. `0x2AD00420` access is a *later* event at 208.3197s after state20, not this cascade's first fault.

**Code instrumentation added on PSBBN branch (diagnostic-only):** `9a84711` header API; `11cfe1c` tracks `$ra` writes into the unmapped nearby address range with separate 64-event budget per autosave; `c275fb5` + `6891d43` interpreter `execI()` hooks before/after instruction, preserves original opcode despite delay-slot recursion; `2004a38` adds GP/T9/call args to original pre-exception `PSBBN-FAULT`. The new `PSBBN-RA-WRITE` event includes source guest PC/opcode, old/new RA/GP, T9, SP, next guest PC and EntryHi. Existing Trace.Enabled + EE.cop0 and autosaves remain the only opt-ins. No register/mapping/syscall/guest RAM modification. **Windows build/runtime for this latest diagnostic revision not yet verified.** Do not guess or overwrite the supposedly bad pointer. Goal is identifying the *origin* of the return target before proposing a semantic fix.


Status: **note-working; local interpreter traces acquired with additional experimental patches; general syscall fix and EE JIT MMU parity pending**
Created: 2026-10-17
Repository: `Masamune3210/pcsx2`
Base: `testing` at `82ec795368b6e57c46471a95929f33dd0f4f7eee`
Branch: `note-working/psbbn-tlb-boot-20261017`

## Cold boot after ASID correction: new SIGSEGV and signal storm (2026-10-19)

**First read:** [new 21-state forensic report](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_POST_ASID_SIGSEGV_STORM_20261019_v1.md), using uploaded `sstates(1).zip` SHA256 `0037c4920fb5c80374608bfc7b65931b0a5201dcc887c4fe4816ec3a5a596051` and `emulog.txt` SHA256 `b63b1a421ae02da1dd422214891e9e5be85e1cce41c47d95e4b13fe0a27e6939`. New prefix `1792381284664639`; old prefix from previous session is ALSO in the archive, do not mix sets. New run: PCSX2 `v2.9.92-60-g3ede3fd38`, EE interpreter, 10s scheduled savestates, 21 captures, unchanged centered PS2-logo screenshot throughout slots 3–21.

The previous self-referential node loop is **absent**: by slot21 `[0x2AC1A774+0x408]=0x2AD00010` and `[0x2AD00010+0x408]=0x2AC1A774`. Instead, after PID12/13 `init` are live at slot13, by slot14 they have left the task list and the emulator is permanently in kernel `cpu_idle`. Retained task memory at `0x803EA000` confirms parent PID12 `state=TASK_ZOMBIE (4)`, `exit_code=0x0B` = SIGSEGV, `exit_signal=0x12` = SIGCHLD.

Using PID12's `mm=0x801D1120,pgd=0x803FE000` and PID13's `mm=0x801D11C0,pgd=0x803D3000` at slot13, **neither** VMAs nor PTEs cover `0x2AD00420` (both PTEs zero, mapped data region ends at `0x2AC23000`). User code `0x2AB21114: lw v0,0x410(s0)` would dereference `0x2AD00420` when `s0=0x2AD00010`. PID13's preserved kernel stack contains both addresses. Approximately 4,000 repeated signal contexts (8,130 occurrences of sentinel pointer in repeated frame records) and IHIN count reaching 4096 at 142.33s indicate a **signal-delivery storm**, not a stable user infinite loop or just unsupported-CACHE warnings.

The 0008 Linux syscall guard's 13 exact kernel signature words **all still matched** in independently checked snapshots 11–15 and 21; no evidence the fingerprint disappeared when the userspace fault occurred. This does **not** establish the origin of the invalid pointer or every syscall return. Note that there is no VMA at slot13, but a mapping could have been created/removed between samples. Do not fabricate a VMA, hardcode a pointer, or merge another speculative semantic fix.

**New opt-in instrumentation committed on this same test branch:**
- `7a4f75a` declarations in `COP0.h`;
- `80361ee` fixed-size 256-unique-key + 384-emission / interval interpreter observer in `COP0.cpp`, logging earliest user-mode address exceptions and O32 `sigreturn` attempts (4119) with exact pre-exception PC/BadVAddr/ASID, register values and repeat counts;
- `464efa6` hook before `cpuExceptionInternal` mutates `EXL/EPC`;
- `dba6212` starts a new bounded fault window each scheduled savestate using the existing COP0 trace opt-in, no new INI or envvar.

Nine static source-integration checks passed. **MSVC compilation and another runtime collection of this instrument are not yet verified.** This instrumentation does not alter TLB, memory, syscall or user-space semantics; it prepares specific evidence to locate the first fault and avoid masking it with later repetitive signal frames. The user can continue with current autosave INI and COP0 trace settings after a build. A precise general emulator correction still depends on understanding where the `0x2AD00010` pointer originated.

## vTLB physical lookup coherence follow-up (2026-10-19)

**Code commit:** `66831a0b766ecfa32d1052c96e05168c0fea5255` in `pcsx2/vtlb.cpp`. The existing `vtlbdata.ppmap` cache (guest VPN to physical page, used by `vtlb_V2P`) was populated on `vtlb_VMap()`, but `vtlb_VMapUnmap()` previously removed only the primary `vmap` and fastmem mapping, leaving `ppmap` pointing at the old ASID's physical frame. The ASID projection correction makes unmap/remap a normal event, so stale auxiliary PFNs would defeat consistent translation. `vtlb_VMapUnmap()` now restores the table's initialized identity sentinel on unmapped low-address pages. `vtlb_VMapBuffer()` similarly discards stale physical PFNs when installing special buffer mappings such as EE scratchpad. Ordinary `vtlb_VMap()` continues assigning its new PFN. High kernel direct-mapped pages keep their existing direct-map behavior. **No MSVC build or runtime of this follow-up is claimed.** This is general vTLB mapping coherence, not a guest-specific data patch.

## General EE TLB ASID mapping correction IMPLEMENTED (2026-10-18)

**Current code branch:** `note-working/psbbn-tlb-boot-20261017`. This is real emulator source code, not another proposal or a PSBBN address-band-aid. Root evidence in [21-state ASID/link-loop report](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_AUTOSAVES_ASID_LINK_LOOP_20261019_v1.md).

**Files and behavior implemented:**
- `pcsx2/EEActiveTLB.h` contains the **production pure selector**, constructing the current ASID's valid 4KB mappings from architectural 48-entry `tlbs`, also including global entries, variable PageMask halves and special scratchpad pages. When two entries match the same ASID/VPN (architecturally undefined case), it gives deterministic priority to the first TLB slot. No hardcoded Linux, PSBBN, ASID or virtual address signatures.
- `pcsx2/COP0.cpp` stores the current flat-vTLB materialization and diff-reconciles it when ASID/TLB state changes. Superseded virtual pages are unmapped and the recompiler's guest instruction blocks invalidated via `Cpu->Clear`; newly valid pages are installed through existing `memSetPageAddr/ vt...MapBuffer`, which also update fastmem. Scratchpad contiguous 16KB buffer fastmem mapping is preserved rather than forcing 4KB slowmem mapping.
- `WriteCP0EntryHi()` compares ASID low eight bits and resynchronizes only when ASID changes, shared by interpreter `MTC0` register 10 and `TLBR`, and by **both** constant/dynamic JIT `recMTC0` paths in `pcsx2/x86/iCOP0.cpp`. `TLBWI/TLBWR` retain cache-tag cleanup but no longer erase mappings belonging to other ASIDs before the replaced entry is installed.
- `vtlb_Reset` and `vtlb_Shutdown` clear tracked translations. `PostLoadPrep()` in `pcsx2/SaveState.cpp` **always** rebuilds from the restored TLB/EntryHi ASID, even when all 48 TLB entry bytes happen to match the previous state. The obsolete old-entry backup has been removed.
- `tests/ctest/core/ee_active_tlb_asid_tests_20261018.cpp` has six GoogleTest cases against the same pure selector used by production; CMake includes the test source. Cases cover ASID-separated same VPN/different PFN; global; invalid entry; 16KB halves; scratchpad and partial-global; and the code/node/sentinel ASID discrepancy captured in PSBBN.

**Concurrent branch checkpoint:** Commit `0104f002` is now on this same branch and applies the previously local PSBBN late-trace plus experimental kernel-specific `0008` syscall guard. It predates the latest ASID corrections; `0008` is consequently **present on this PSBBN test branch**, but must still NOT be called a general syscall fix or merged into `testing`. Commit `66831a0` additionally updates `vtlb_VMapBuffer`/`vtlb_VMapUnmap` to invalidate the parallel virtual-to-physical LUT for scratchpad/unmapped pages, which must remain coherent under the active-ASID selector. These changes have not undergone a full Windows build in this session. Do not reapply the already-committed local 0008/late-trace patch after pulling.

**Verification:** 14/14 targeted source integration checks passed on current GitHub file contents (including JIT, resync on save restore, scratchpad fastmem, CMake). **The six GoogleTests have been authored but NOT executed; full MSVC/Windows build NOT run here; PCSX2 boot under this change NOT tested; JIT exception-delivery parity is NOT proven.** Do not merge into `testing` or call PSBBN supported yet.

**Next test when user builds locally:** `git pull origin note-working/psbbn-tlb-boot-20261017`; build PCSX2 and, when available, the `core_test` test target (filter `EEActiveTLB.*`); boot the **same PSBBN HDD image** cold with EE interpreter first and 30s/selected scheduled savestates. Compare whether PID12 still reaches the self-link loop, and whether the TLB active-ASID trace resolves. Then repeat with EE recompiler to locate remaining exception-delivery differences. Archive any compile errors or regressions before additional code changes. Avoid reapplying already-committed local 0008/late-trace changes over merged files; reconcile any other local patches carefully.

**Separate remaining tasks:** trace how the user's node link became self-referential (candidate store `0x2AB21398` only verified statically); generalize BIOS syscall ownership instead of 0008's kernel fingerprint; implement JIT guest TLB miss/invalid/refill exception parity and CACHE IHIN coherence. None of those has silently been declared fixed by ASID mapping work.

## 21 savestates expose genuine userspace infinite loop and ASID-blind MMU (2026-10-19)

**Latest canonical evidence:** [PSBBN user-RAM/ASID forensic report](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_AUTOSAVES_ASID_LINK_LOOP_20261019_v1.md), from 21 original scheduled `.p2s` snapshots and the matching user's log. User build `v2.9.92-36-g9cd9f9199`; autosave succeeded at 10-second intervals (`900000` configured maximum). All 20 screenshots in slots 2..21 are binary-identical. Userspace task PID12 `init` enters at slot13; subsequent EE PCs repeatedly fall into the code sequence `0x2AB210E8..0x2AB21168` and only 24..29 RAM pages change per interval.

**Proximate stalled behavior:** In slots 13..21 `$s0=0x2AC1A774`, `$s1=0x2AD00010`, `[s0+0x408]=s0`, `[s0+0x410]=1`, `[s1+0x408]=s0`. The raw linked userspace code `bnel v0,zero` / delay-slot `lw s0,0x408(s0)` / `bne s0,s1` traverses **the same node forever**. The writer and origin of the self-link are unknown; **never patch the link in guest memory** as an emulator fix.

**Independent architectural correctness bug:** A saved non-global TLB entry `0x2AB20118` (ASID `0x18`) maps the executing code page, but current CP0 `EntryHi=0x105` (ASID `0x05`) in six snapshots. `COP0.cpp::MapTLB()` maps valid entries unconditionally through `memSetPageAddr(vaddr,paddr)` without filtering ASID; `Memory.cpp` uses a flat `vtlb_VMap` indexed only by VPN. This does not implement the architected ASID address space separation for overlapping guest VPNs. This could cause cross-ASID stale mapping and data corruption, but **there is not yet proof it specifically caused the self-link**.

**New static writer-site finding:** The same linked userspace module has `0x2AB21268: sw zero,0x408(s0)`, `0x2AB21390: sw v0,0x408(s0)`, and `0x2AB21398: sw s0,0(v1)`. The saved GP-relative load/add sequence `0x2AB21384..0x2AB21388` resolves `v1=0x2AC1AB7C=s0+0x408` in the failing state. **If executed with these pointers**, the last store can create the observed self-link directly. This is a precise diagnostic/watchpoint site, **not yet a proven runtime writer** or justification to patch userspace. Full crosswalk is in the linked research report.

**ASID cross-contamination fully checked:** Code at `0x2AB21168` (TLB slot4) and `$s1=0x2AD00010` sentinel (slot2) are valid **only under non-global ASID 0x18**. The `$s0=0x2AC1A774` node and `[s0+0x408]` field (slot35) are valid **only under non-global ASID 0x05**. There is no alternate matching entry among all 48. Saved current ASID alternates 0x05/0x18 while this function keeps reading from both sets, a concrete mismatch with architectural translation. Source `MapTLB()` is ASID-blind and `vtlbdata.vmap` keys only by virtual page. The visible loop mixes two normally inaccessible address spaces. **Do not treat this as merely a cosmetic stale-entry observation**; prioritise MMU ASID ownership. Self-link writer site still needs runtime confirmation after MMU repair.

**Next engineering work:** Build a deterministic two-ASID/same-VPN/different-PFN guest regression, plus global and invalid-entry cases; then fix MMU mapping ownership, ASID switching and cache invalidation for both interpreter and JIT. After validating, trace the writer of the self-link and compare new boot snapshots. Do not merge speculative semantic edits into `testing`. Old PID14 zombie claim belongs to a previous distinct run: **PID14 does not appear in the newest 21-state sequence**. The experimental Linux-syscall fingerprint `0008` is a separate, incomplete issue.

## Optional scheduled savestates for diagnosing later PSBBN failures (2026-10-19)

**Code commit:** `6d05802d58e80759056309e607810aa89c095042`, `pcsx2/VMManager.cpp`, branch `note-working/psbbn-tlb-boot-20261017`.

Configure via **base `PCSX2.ini`**, not environment variables:

```ini
[EmuCore/PSBBN]
AutoSaveIntervalSeconds = 15
AutoSaveMaxCaptures = 48
```

- `AutoSaveIntervalSeconds` defaults to `0` (off), accepts `1..3600`. `AutoSaveMaxCaptures` defaults to `48`, accepts `1..4294967295` with no separate arbitrary ceiling. Invalid values disable the scheduler with a console warning. Read on **each new VM initialization** via the base settings getters; change the INI and start a new VM to apply it.
- Wall-clock timer uses `std::chrono::steady_clock`. The poll runs at `VMManager::Internal::VSyncOnCPUThread()`, so captures happen on the VM CPU thread at the first VSync after their due time. The scheduler does **not** run during pause, GS dump playback or memory-card busy state, and doesn't produce catch-up bursts. It depends on guest VSync; if the VM stops VSync entirely there may be no additional snapshots.
- Captures go to the existing `EmuFolders::Savestates` directory, named `PSBBN-auto-<wall-clock-microseconds>-001.p2s` etc., with a unique boot-session prefix. No normal manual slots or existing files are overwritten.
- Each save uses PCSX2's existing synchronous state capture and asynchronous ZIP write. It defers the next capture while a save worker is active; stops after the configured number; logs PC and output path; reports save failures. Compression is the existing savestate setting: **user plans Deflate**, but this branch does not forcibly change that setting.
- The source-level checks confirmed `Host::GetBaseIntSettingValue`, interval and maximum entries, reset-on-boot, VSync poll, non-overlap guard, standard save path, and absence of environment-variable access. **Windows/MSVC build and live automated capture remain unverified.**
- This is instrumentation, not a boot fix. We need state transitions and task exit/parent relationships (especially the likely userspace child `PID 14`, zombie, code `0x0B`), not merely an increased quantity of uncorrelated logs. Keep the saved BIOS/kernel/IOP memory intact when sharing. Analyze with `ps2bbndecomp/tools/inspect_psbbn_savestates_20261018_v1.py`.

**Branch boundary:** No code merges into `testing`; no change to `ghidra-emotionengine-reloaded` required. Never assume a snapshot finished compressing merely because its initial console message appeared.

## User-confirmed visible result after 0008 (2026-10-19)

The locally tested experimental Linux O32 syscall guard (0008) stopped the recorded spurious BIOS SetGsCrt interception and preserved the genuine stage2 BIOS call. **The user explicitly confirms that the screen did not change: the centered PlayStation 2 logo remains.** This is a negative *visible boot outcome*, even though the syscall classification experiment corrected an observable erroneous side effect. Do not ask the user to reconfirm or repeat the 0008 baseline. Investigate the remaining boot blocker independently, without assuming IHIN warning suppression will fix it or accepting the Sony-kernel fingerprint as a permanent syscall solution. The EE recompiler MMU/exception parity remains required for eventual support.

## CACHE IHIN interpreter observer and JIT gap (2026-10-19)

**Source review and implementation committed:** PCSX2 EE `CACHE 0x0B` is **IHIN (instruction-cache hit invalidate)**, documented in the EE Core Instruction Set Manual v6.0, page 308. The uploaded capture10 logged 14,202 generic unsupported-CACHE warnings during 138.4629–142.0333s, but the kernel continued executing to at least 187.4967s. The warnings are not evidence of a permanent CPU hang.

The EE interpreter fetches via `memRead32(pc)` and has no separate emulated instruction-cache lines. In this coherent-instruction-fetch model, IHIN has no distinct line to invalidate; previous missing `case 0x0b` generated a warning for every operation. The new `Cache.cpp` case recognizes IHIN and calls a **bounded diagnostic observer**. The observer in `COP0.cpp` uses existing COP0 opt-in tracing, resets on VM/TLB observer reset, and captures exact interpreter PC, effective address, instruction opcode and CP0 data (first32 + powers of two to65536; at most43 records). No guest register, TLB, cache tag, or memory value is changed by this observer.

Commits: `14dec034` (header), `6faf3112`/`f365bdd3` (observer), `1480554f` (IHIN dispatch). **9/9 static source checks passed; no Windows/MSVC build or subsequent runtime of this latest change has been verified.**

**Do not misstate the JIT status:** `pcsx2/x86/iR5900Misc.cpp::recCACHE()` remains a no-op. Real instruction-cache coherence for recompiled guest code and the TLB/MMU exception paths still require architecture-level implementation and regression tests. A one-line `Cpu->Clear(guest_vaddr)` without mapping/alias reasoning is not an acceptable completion.

Research details and next implementation criteria: [EE_CACHE_IHIN_ANALYSIS_20261019_v1.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/EE_CACHE_IHIN_ANALYSIS_20261019_v1.md). The original capture10 log is still a conversation attachment, with compressed hash recorded in that branch.

## Latest cross-repository checkpoint: 0008 interpreter run (2026-10-19)

**Research/output source:** [PSBBN capture10 analysis](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/POST_0008_RUNTIME_CAPTURE_20261019_v1.md), derived from the user's 26,256,899-byte pasted runtime log (SHA256 `62ffce05973be78c910c73d3b10580dd5712d2ec5970cac8227c35b18431cc07`). This commit updates branch documentation only; **it does not apply experimental 0007 or 0008 code to the PCSX2 source on this remote branch**. The log reports base revision `4621a080b` despite the local working tree including further patches; do not infer which patch code was committed from that version string.

The locally executed experimental `0008` Linux-O32 guard logs 16 `abi=linux-o32` syscalls, including Linux `open` at `0x0FB77E2C`, `v0=4005`, `v1=2`. Unlike capture9, the new capture has no mistaken host `SetGsCrt` after Linux startup. The legitimate earlier BIOS `SetGsCrt` at `0x0102E424` still occurs. Heartbeats continue to 187.4967 seconds, and there are 14,202 `Cache mode b not implemented` warnings starting at 138.4629s. Neither a completed PSBBN boot nor the final screen outcome was established from this report.

**Interpretation and branch policy:**
- The BIOS-v1 vs Linux-v0 syscall ABI collision and erroneous host hook are established. Patch `0008` suppresses it in one tested Sony kernel by inspecting 13 hardcoded kernel words. It is **an experiment, not the root-cause/general design**, and must not be merged into `testing` as-is.
- Native syscall *handler ownership* must eventually determine when BIOS host-side interception is legitimate; preserving user-mode genuine BIOS requests matters.
- A proper EE JIT TLB exception path remains a separate requirement. Interpreter progress is not full compatibility and no demonstrated JIT fix exists.
- Investigate the issuing guest PC/sub-op for CACHE mode B with bounded instrumentation before changing any cache semantics. Warning count alone does not establish causality.
- **Canonical future changes/research** belong on the decomp research output branch until a general emulator correction is designed and specifically authorized for this test branch. Do not duplicate or silently reapply patches already present in a local checkout.
- The complete raw capture is currently conversation-attached rather than committed to GitHub; its analysis and checksum are recorded in `ps2bbndecomp`.

## Objective

Investigate and eventually support Sony's PlayStation Broadband Navigator (PSBBN) in PCSX2, with correct EE TLB/MMU behaviour, HDD/DEV9/ATA operations, and boot/loader behaviour. Initial target: an English-localized BB Navigator environment from CosmicScale's **PSBBN Definitive Project**, but keep original Japanese PSBBN as an independent reference.

This branch is a parking place for reproducible research, instrumentation, implementation, and regression tests. **Do not substitute emulator-specific shortcuts for root-cause fixes without documenting them.**

## Relevant software

- [CosmicScale/PSBBN-Definitive-Project](https://github.com/CosmicScale/PSBBN-Definitive-Project)
  - English (and other language) localization and enhanced PSBBN v0.32 environment.
  - Full PSBBN/HOSDMenu install uses Sony PSBBN with patched system components, including `osdboot.elf` (CRC security check bypass) and **OSDMenu MBR**, a homebrew replacement for Sony's original MBR loader.
  - Installer operates on a PC-attached drive; Windows launcher uses WSL. **It formats the selected drive.** Never point it at a production HDD/image.
  - Current installation guide specifies minimum 32 GB drive; this is a *physical-hardware installer requirement*, not proof of any specific PCSX2 image size requirement.
  - Documentation: [README](https://github.com/CosmicScale/PSBBN-Definitive-Project/blob/main/README.md), especially “Install PSBBN and HOSDMenu,” “PSBBN,” “OSDMenu MBR,” and “Known Issues.”
- Original Japanese BB Navigator should be captured/tested separately where possible; the modified Definitive boot chain is not a byte-for-byte substitute for Sony's original one.
- Current fork has ongoing MagicGate/KELF and HDD work. Do not assume those paths are complete solely because individual decrypt/authentication tests succeed.

## Prior investigations and upstream references (as of 2026-10-17)

| Source | Evidence / significance | Status / caution |
| --- | --- | --- |
| [PCSX2 issue #11910](https://github.com/PCSX2/pcsx2/issues/11910) | PCSX2 lacks full guest-managed EE TLB support; PS2 Linux and PSBBN depend on it. | Open. |
| [PCSX2 issue #11746](https://github.com/PCSX2/pcsx2/issues/11746) | PSBBN boot failing in DEV9/ATA path; reported `ATA failed, -502` on a decrypted KELF-based boot attempt. | Open; older report, not a current reproduction on this fork. |
| [PCSX2 issues #11892](https://github.com/PCSX2/pcsx2/issues/11892), [#11918](https://github.com/PCSX2/pcsx2/issues/11918), [#11909](https://github.com/PCSX2/pcsx2/issues/11909) | Investigate unsupported HDD commands, HDD↔IOP DMA semantics, pending ATA IRQ handling. | Separate possible blockers; revalidate before attributing failures. |
| [PCSX2 PR #14847](https://github.com/PCSX2/pcsx2/pull/14847) | Proposed changes to guest ASID handling, refill/modified/address exceptions, delay-slot state and BIOS syscall interception; author reported reaching PS2 Linux interactive shell. | **Closed, NOT merged.** Review identified correctness/maintainability problems and rejected hacky logic. **Study as a hypothesis/reproducer only; do not blindly cherry-pick.** |
| [CosmicScale issue #388](https://github.com/CosmicScale/PSBBN-Definitive-Project/issues/388) | Maintainer noted in Dec 2025 that PCSX2 could not run PSBBN; commenters identified TLB and MagicGate/MBR boot requirements. | Historical compatibility evidence, not proof about the latest local fork. |

## Correctness-first investigation plan

### 0. Establish reproducible baseline
- Record exact fork commit, BIOS/ROM version, PCSX2 settings, HDD image geometry and format, PSBBN/Definitive Patch version, bootloader, and guest console region.
- Work on a **copy** of the HDD image; preserve original snapshots/hashes.
- Capture full log plus failing guest PC, COP0/TLB state, IOP/ATA operation and interrupts at first divergence.
- Compare original PSBBN and Definitive Project separately if both are available; identify patches that alter security, kernel, MBR, or launch sequence.

### 1. Distinguish boot stages
1. BIOS/OSDSYS HDD detection and system update/MBR selection.
2. MBR execution and HDD reads, ATA commands, DMA, and interrupts.
3. `osdboot.elf` or equivalent chain, KELF authentication/decryption where applicable.
4. PSBBN Linux 2.4.17-derived kernel loading and MMU/TLB setup.
5. Kernel transition to userspace and application/process switching.
6. PSBBN UI, channels, HDD filesystem, launch/return-to-menu behavior.

Record first *observable* failure at the correct stage. Avoid assuming every earlier successful return implies later successful boot.

### 2. EE MMU/TLB fundamentals to audit against hardware semantics
- Guest TLB entries: VPN2, ASID/global, even/odd page halves, PFN, page mask, permissions/valid/dirty, cached/uncached treatment.
- Guest `EntryHi`/`EntryLo`/`PageMask` and TLB instructions (`TLBR`, `TLBWI`, `TLBWR`, `TLBP`), including replacement and ASID/global effects.
- Correct virtual-to-physical translation across kernel and userspace address segments.
- Separate address-space mappings across process switches; no stale VTLB/recompiler mappings.
- Exceptions: TLB refill, invalid, modified, address error; `BadVAddr`, `Context`, `EntryHi`, `Cause`, `EPC`, `Status`, delay-slot (`BD`) semantics and exception vectors.
- JIT/recompiler fastpaths, TLB invalidation, exception dispatch, and interpreter/recompiler equivalence.
- BIOS syscall HLE dispatch must not collide with legitimate Linux/PSBBN syscalls. Fix specific dispatch correctness, not via a Linux-process string or heuristic special case.

### 3. Prove each layer
- Run isolated guest TLB instruction/exception microtests first, including ASID context switching, global entries, dirty/invalid mappings and branch delay slots.
- Run compatible non-Linux PS2 regressions and existing emulator test suites.
- Try unmodified PS2 Linux boot (when available) as an independent MMU/userland exercise, with a trace to its first failure.
- Then try PSBBN; separate DEV9/ATA and MagicGate failures from MMU/TLB failures.
- Ensure passing outcomes reproduce after a cold boot with no savestate shortcuts.

## Guardrails

- This branch has **no implementation yet**. PSBBN and PS2 Linux are **not** claimed working.
- Do not merge broad PR #14847 wholesale; only adopt individually verified concepts backed by hardware/architectural evidence and regression tests.
- Prefer fixing shared emulator inaccuracies, not modifying Sony software just to avoid them; modified Definitive Project boot stages must be called out.
- Keep detailed logs only in local test artifacts as appropriate; do not commit BIOS files, licensed system executables, keys, or disk images.
- Rebase on `testing` when beginning implementation, preserving an identifiable experimental baseline.

## Interpreter Random/Wired compile correction (2026-10-18)

User's local Windows/MSVC compile of the `0006` interpreter Random/Wired candidate reported `C2065 cpuRegs undeclared`, `C2737 wired const must be initialized`, and `C2530 random reference must be initialized` at `pcsx2/COP0.h:22–25`. The actual cause is that the `__fi COP0_AdvanceRandom()` inline **function body lived in COP0.h**, which can be included before `cpuRegs` is declared. The latter two messages are cascading parser errors, not separate uninitialized variables.

Fix: **move the exact same 13-line inline implementation into `pcsx2/Interpreter.cpp`** (immediately after `static int branch2`), and remove it entirely from `COP0.h`. `Interpreter.cpp` already sees `cpuRegs`; keeping `static __fi` preserves the interpreter hot-path inlining instead of adding an out-of-line call per guest instruction. `COP0.cpp` still holds the `MTC0 Wired` reset/read-only `Random` behavior and `R5900.cpp` still initializes Random to 47. The runtime model and TLB behavior are not intentionally changed by this placement fix.

The same placement correction is committed to `ps2bbndecomp/patches/pcsx2/0006-interpreter-random-wired-candidate.patch`. Its five hunk pre/post counts and source-context matches were mechanically checked against the original PCSX2 `testing` files (zero errors). **No local Windows/MSVC build was performed in this session; a user rebuild remains necessary.** Do not reapply the complete 0006 patch over a branch already containing it; update the branch instead.

## PSBBN bounded observer build fix (2026-10-18)

The opt-in TLB diagnostic observer from `ps2bbndecomp/patches/pcsx2/0001-psbbn-bounded-tlb-diagnostics.patch` has been applied to this branch. MSVC reported:

```text
pcsx2/COP0.cpp:87: error C2065: 'VTLB_PAGE_BITS': undeclared identifier
```

**Root cause:** `VTLB_PAGE_BITS` is declared inside the `vtlb_private` namespace in `pcsx2/vtlb.h`, which `COP0.cpp` already includes. The observer's raw vmap index referenced the constant without that namespace.

Applied exact correction:

```cpp
const auto mapping = vtlb_private::vtlbdata.vmap[0xc0005000u >> vtlb_private::VTLB_PAGE_BITS];
```

The same correction was committed to `Masamune3210/ps2bbndecomp` on `note-working/psbbn-ghidra-analysis-20261018` in the original diagnostic patch, so new checkouts will not reintroduce it. All adjacent `VTLBVirtual` uses were checked against `vtlb.h` and have valid signatures (`isHandler(u32)`, `assumeHandlerGetID()`).

**Verification:** Source/header checked; no PCSX2 Windows/MSVC build run in this session. User should rebuild the same branch and report any next compiler diagnostics. The fix changes a compile-time name lookup only, not TLB behavior or observer semantics. Do not infer successful PSBBN boot from compilation.

## First real PSBBN 0.32 boot trace (2026-10-17)

**Observed:** Original Japanese PSBBN 0.32 installer reported completion of BB Unit initialization and installation. Installer disc was subsequently removed; the recorded restarted boot stayed on the PlayStation 2 logo. Evidence below is from the user's approximately 1,057,412-line PCSX2 trace of that session, **not yet a controlled fresh-power-on trace**. The drive being tested in the recorded boot was a disposable 40 GiB `bbntest.raw` image. The BIOS was USA v1.60 (2002-02-07), a potential separate compatibility variable.

### Ordered boot evidence

1. Initial OSDSYS HDD detection attempt at about `653.7648` logged `HDL:read 0 sec`, `ATA: C_READ_SECTOR`, `cannot alloc memory`, then `new power off`. This was **earlier in the session, while the installer disc was involved**, so do not conflate with the later boot path.
2. After the disc was removed at approximately `1162.872`, later restart at `1243+`:
   - `1245.1040`: `loadmodule: fname rom0:HDDLOAD args 28 arg -osd`
   - `1245.6353`: `HDL:read 0 sec`
   - `1245.6875`: `HDL:securiyt decript.` (verbatim guest message)
   - `1247.3923`: `HDL:END ver.1.00`
   - `1247.6971`: loads `rom0:UDNL` with `img0:` argument (IOP image boot stage).
   - `1249.8776`: `Input ELF format filename = pfs0:/p2lboot/osdboot.elf`
   - `1250.3924`: `Loaded, pfs0:/p2lboot/osdboot.elf`
   - Subsequent IOP reboots and HDD/PFS activity continue through `1253+`.
   - `1251.7260`: `pfs: warning: null buffer returned` (potential separate issue, not yet causally connected).
3. `1253.0593` through `1253.0618`: 50 logged EE **store** TLB misses at the same `pc=0x801a4930`, addressed sequentially `0xc0005000`, `0xc0005004`, ... `0xc00050c4`. This address range is in guest TLB-mapped KSEG2. The log does not prove which guest mapping or instruction should have been used.

**Interpretation:** This run moves well past mere BIOS HDD recognition and visibly loads `osdboot.elf` from the installed HDD. Do **not** state that it necessarily boots the final PSBBN Linux kernel or that TLB is the sole cause of the hang. The final TLB messages are a strong direct lead.

### Concrete emulator-source finding

Checked `pcsx2/vtlb.cpp` on fork `testing`, around lines 514–550, at the time of this audit:

- `vtlb_Miss(u32 addr, u32 mode)` on the **interpreter** path (`Cpu == &intCpu`) invokes `cpuTlbMissW` or `cpuTlbMissR`, then `CancelInstruction()`.
- On the **recompiler** path it constructs the `TLB Miss, pc=...` message and, when `PauseOnTLBMiss` is enabled, pauses/raises a host debugger report. Without that setting, it logs and returns **without delivering an equivalent guest TLB exception** at that point.
- The logger uses `static int spamStop` and emits at most **50 errors** in a normal non-development build. Therefore the 50 visible misses are a logging cap, **not evidence the misses stopped**.

Relevant exact source: [fork vtlb.cpp on testing](https://github.com/Masamune3210/pcsx2/blob/testing/pcsx2/vtlb.cpp).

### Minimal discriminating next tests

1. Preserve an untouched backup/hash of the installed test HDD. Record full executable hash, BIOS, region, settings, and boot origin.
2. Run **from a full cold boot with no disc**, on a copied HDD image, first using existing EE recompiler then **EE interpreter**, with identical settings except the engine. Avoid introducing new emulation hacks for this comparison.
3. Enable **Pause on TLB Miss** for the recompiler and capture, at the **first** miss, guest PC/instruction or recompiled block, general registers, COP0 `EntryHi/EntryLo0/EntryLo1/PageMask/Index/Status/Cause/EPC/BadVAddr`, and emulator VTLB state. Check whether the expected guest TLB entry was installed and whether its ASID/global, valid and dirty flags permit the write. Note: a single logged PC can be block-level/stale for recompiler mid-block faults, per existing `vtlb.cpp` comment.
4. If interpreter proceeds beyond this point, compare first divergent guest operation and exception behavior. If interpreter also fails, check missing/invalid guest mappings, earlier exception registration, invalid page mask handling, and BIOS-vs-original-Japanese region mismatch.
5. Revisit `pfs: warning: null buffer returned` only if trace/differential tests connect it to the boot failure. Do not assume the log proves a broken HDD filesystem.

**State:** Documentation only. No fixes, workarounds, emulator binaries, keys, BIOS, or game content committed.

### Interpreter differential: repeated TLBWR (2026-10-17)

Switching **EE recompiler off** replaces the visible recompiler store-miss sequence with an apparent fast repetition of:

```text
[ 1802.0182] COP0_TLBWR 0:0,fb88101,0,0
```

The exact same five fields repeat hundreds/thousands of times in the user's reported log excerpt. This output is printed unconditionally on the interpreter-side `R5900::Interpreter::OpcodeImpl::COP0::TLBWR()` code path, `pcsx2/COP0.cpp`, **even though the line is routed through a development-console warning and not itself an error return**.

The fields come from source as `Random:PageMask,EntryHi,EntryLo0,EntryLo1`. So the observed state is `Random=0`, `PageMask=0` (4 KiB pages), `EntryHi=0x0FB88101`, and both `EntryLo` registers `0` (neither page valid). TLBWR picks `j=Random&0x3F`; all reported calls target index 0 with those values, and `MapTLB` maps only if an EntryLo valid bit is set. An invalid TLBWR *can be normal* (invalidation or flush), so this alone does not establish the causal failure.

**Do not assume** this is the same fault/PC as the recompiler's stores at `pc=0x801A4930` and `addr=0xC0005000...`. The interpreter trace currently lacks guest PC, COP0 `EPC`/`Cause`/`Status`/`BadVAddr`, `Wired` and associated transitions. There may be repeated guest-exception entry, incorrect `Random` register evolution, or an independent initialization/invalidation loop. Need evidence, not a workaround.

Relevant exact code:
- [COP0.cpp TLBWR / WriteTLB / MapTLB](https://github.com/Masamune3210/pcsx2/blob/testing/pcsx2/COP0.cpp)
- [R5900.cpp cpuTlbMiss and cpuException](https://github.com/Masamune3210/pcsx2/blob/testing/pcsx2/R5900.cpp)

**Next diagnostic:** From the earliest repeated TLBWR in the *interpreter* run, capture guest PC and decoded nearby instructions, `Random`, `Wired`, `EntryHi`, both `EntryLo` values, `Index`, `Status`, `Cause`, `EPC`, `BadVAddr`, plus the preceding TLB exceptions and changes. Verify whether Random should move and whether the guest code is attempting to initialize/flush entries or service recurring faults. Collect only bounded samples, not another million lines.

### Recovered PSBBN v0.32 osdboot ELF and second stage (2026-10-17)

Using the user's extracted `hdd0:/__system/p2lboot/osdboot.elf` (KELF) **plus the full verbose PCSX2 MagicGate log**, reconstructed the authenticated plaintext directly from the decrypted BIT and `SCMD 0x8E` output. **No guesswork and no console keys needed to reproduce from this log.**

Original KELF: 1,097,904 bytes, SHA256 `164a607b2496c6079f9d299584be1820aef59141985b1ede05207855698d376f`.
Parsed PS2SDK `SecrKELFHeader_t` (LE): content_size=0x10BD00, KELF_header_size=0x03B0, unknown5=0x0B00, flags=0x022C, BIT_count=0, mg_zones=1 (Japan). Decrypted BIT table at ~1248.435 in user's trace has block_count=53, table length 856 bytes, content length 1,096,960 bytes. Descriptors completely cover content without gaps: 29 flagged=3 blocks of 6,128 total protected bytes, 24 flagged=0 unprotected blocks of 1,090,832 bytes. The logged verified content plaintext totals exactly 6,128 bytes for this KELF. Applying it to protected blocks, and retaining unprotected bytes directly, produces an actual `ELF32 MIPS little-endian EE` recognized by `file` and `readelf`.

Recovered EE ELF: size=1,096,960, SHA256 `ce10a21c5c741fcc4b0a4f4561e8df516558a1b6d1a3ffa7d09af3a794c72bed`; entry=0x00800000; main PT_LOAD=0x00800000, size=0x10A9BC; secondary PT_LOAD=0x0090AA00. Matches the guest `loadelf` output in the original trace. Do not load the original KELF directly into Ghidra.

The EE ELF's `.text` is only 0x258 bytes. Disassembly reveals an additional **XXTEA-style self-decrypting second stage** using a key stored in its own `.data` (NOT a MagicGate key); delta 0x9E3779B9 from `.sdata`, ciphertext [EE 0x00800290,0x0090A990), 272,832 LE u32 words, six XXTEA rounds. Bootstrap copies this plaintext to 0x01000000 and jumps to it.

Recovered raw second-stage EE image: 1,091,328 bytes, SHA256 `423e87063db94a01c425bfb67719a2410c1d27fa36fbe08fbadb1c3f5bf85eff`. **Ghidra import as raw R5900 little-endian, guest base 0x01000000**.

Two complete, valid IOP IRX module ELFs (MIPS little endian e_type 0xFF80, with relocation sections intact) were carved from stage 2:
- stage2 offset 0x63B30, mapped position 0x01063B30, size 275,200 bytes, SHA256 `8e1751b9421c9f05d59716c5a2ff1398320122a5c3e682028da5b397c460989a`.
- stage2 offset 0xA6E30, mapped position 0x010A6E30, size 310,832 bytes, SHA256 `bcf8b05b3263c4427638a9859f299bd61cff186217d9e8f164ac2688d1577e4d`.
Import these IRXs using the separate IOP loader (not EE); both contain text pointing to `IOPBTCONF`.

PS2 Linux strings are visible in the second-stage image, including the original memory-card startup warning.

**Still unresolved:** the repeatedly faulting guest PC `0x801A4930` (physical alias 0x001A4930) is outside the recovered stage1 main LOAD, and is not inherently at the raw stage2 base. Map the code's actual load chain before assigning that PC to a named binary. Do not assume completing KELF/XXTEA extraction solves the TLB issue.

A separate pure-Python reproducibility helper `recover_psbbn_osdboot.py` was generated in this conversation, taking the extracted KELF and the PCSX2 log and generating the four files; code and commercial binary dumps have **not** been committed.

### User Ghidra analyses and next-stage kernel discovery (2026-10-17)

The user supplied four Ghidra 12.1.3 `.gzf` projects, preserving completed analysis. The packed Ghidra DBs were inspected for persisted metadata and symbols; source binaries were cross-checked independently with ELF parser/LLVM disassembly. These are **not** headless Ghidra analysis results or a decompilation audit.

Import architectures were correctly set:
- `osdboot.decrypted.elf`: `r5900:LE:32:default` (ELF loader).
- `osdboot.stage2.bin`: `r5900:LE:32:default` (raw binary loaded at `0x01000000`).
- `osdboot.stage2.iop1.irx` and `osdboot.stage2.iop2.irx`: `MIPS:LE:32:default` (IOP IRX ELF loader).
Both IRX projects record `Relocations applied=82`, `partial=11`, `failed=0`, `unknown=0`, `unsupported=0`, `import tables=4`, `export tables=0`. `readelf` independently finds 86 `rel.text` plus 7 `rel.rodata` entries = 93 records per IRX, so 82+11 accounts for all relocation entries. Review the 11 partial cases before assuming perfect cross-references.

**Unexpected shared loader:** Both IOP IRXs have identical 0x1640-byte `.text` layout, with only SIX bytes different between text sections; their 0x50-byte `.rodata` sections match byte-for-byte. The executable ELF entry is 0xE00 in both. The substantive differences are in large `.data` payloads (0x416D0 and 0x4A200). These appear to be *shared IOP loader/bootimage variants with different embedded payloads*, not unrelated IRX drivers. Treat that interpretation as a working model until their runtime unpacking is traced. The Ghidra EE raw stage2 project detects some apparent R5900 functions *inside the embedded IRX regions*; these must not be conflated with independently analyzed IOP functions.

**Important discovery: next kernel pathname in stage2.** Offset `0x103970` of the raw second stage (guest address `0x01103970`) contains the literal null-terminated `pfs0:/p2lboot/vmlinux`. Raw disassembly at guest `0x01000460`–`0x01000468`:

```asm
0x01000460: lui   v0, 0x0110
0x01000464: addiu v0, v0, 0x3970
0x01000468: sw    v0, 0x14(sp)
```

This assigns a default boot-path pointer (conditional on zero preexisting pointer in preceding code). It is therefore an **active code reference**, not merely a stray unused embedded filename. Additional strings near `0x01104CD0` and `0x01104D28` describe kernel CRC and ELF validation errors; `0x01104298` describes `encpack_header decode error`. `0x01105EF0` references `hdd%d:__linux.%d`. They document a kernel-loading/configuration layer whose exact behavior remains to be reconstructed.

For Ghidra start at `0x01000460`, follow its enclosing function and calls to kernel path resolution, `encpack` validation, memory segment load, and transfer. Resolve use-sites of `0x01103970`, `0x01104298`, `0x01104CD0`. For a more meaningful address-to-code mapping of recompiler failure PC `0x801A4930`, **extract `pfs0:/p2lboot/vmlinux` from the HDD copy next** and inspect its actual file format; do not assume ordinary ELF until header verified. If absent, verify which PFS mount backs `pfs0:` and record file-access attempts.

A raw 4-byte-aligned scan found **zero exact `TLBWR` opcodes (0x42000006) within the decrypted first-stage ELF or the stage2 binary**. This does not prove a runtime mapping or that no instruction is generated/copied later. It supports looking for the repeated interpreter TLBWR in a subsequently loaded executable rather than attributing it directly to stage2.

**State:** source-level evidence only; no modifications to uploaded Ghidra archives, no emulator behavior changes, and no copyrighted binary artifacts committed.

### Original PSBBN vmlinux extraction resolves exact fault PC (2026-10-17)

User extracted three additional files from the BB Unit HDD image:
- `pfs0:/p2lboot/vmlinux`: 3,038,863 bytes, SHA256 `07e44b50e2558d188caa306f60c138ad6015bbabcd3bc32fba35eeb6e8a870fc`.
- `hdfill/hdfill.elf`: 541,248 bytes, SHA256 `ffb7f54fd437af7d68178803dfab9e6acea452d8a988e5dcbe708bfadcb00414`.
- `hdfill/HDFILLER.PAK`: 4,142,080 bytes, SHA256 `999b9838eecafe6645f559c12b358ec033cc48db26b9300944ca1573c888fb7c`.

**Critical finding:** `vmlinux` is an intact, unstripped ELF32 MIPS little-endian MIPS-III kernel, version **Linux 2.4.17_mvl21**, compiled by `inamoto@imo.sm.sony.co.jp` with gcc 2.95.2, build `#1 Tue Jun 17 18:36:14 JST 2003`. ELF entry=0x80010490 `kernel_entry`; LOAD at 0x80010000 (filesz 0x1E5000, memsz 0x20DB50). `.text` 0x80010000–0x801C5E5F, `.bss` 0x801F5000–0x8021DB4F, 9,385 total symbol-table entries; .mdebug is also present. **Import as ELF at linked addresses, not raw / arbitrary base**.

Recompiler's previously opaque `pc=0x801A4930` resolves *exactly* to symbol `__bzero` (at 0x801A48FC), which is a second entry to optimized `memset` (at 0x801A48E0). `0x801A4930` is loop header `addiu a0,a0,0x40`. Starting at 0x801A4934, sixteen 4-byte `sw a1,-0x40(a0)` through `sw a1,-4(a0)` instructions form a 64-byte block fill. Back edge: `0x801A4974 bne t1,a0,0x801A4930`. This **perfectly accounts** for recorded repeated store misses at `0xC0005000, 0xC0005004, ...`: Linux was attempting to zero KSEG2 memory lacking usable virtual mapping in PCSX2. The precise caller, expected page-table state and first failed write are not captured; don't conclude this is simply a kernel bug or solely a recompiler bug.

Searched the full kernel `.text` for instruction word `0x42000006` (TLBWR): exactly **two static sites**:
- `0x800258B8`, `update_mmu_cache+0x128`, function begins 0x80025790; adjacent `tlbwi` at 0x800258BC.
- `0x80025A08`, `except_vec0_r5900_spr+0x28`, function begins 0x800259E0; branch-delay-slot `tlbwr`, `eret` 0x80025A14. Earlier code explicitly stages `EntryLo0=0x80000006`, `EntryLo1=0x6`, then TLBWR. **Interpreter reports both entry-lo values=0**, so do not assume the repeated warning is from this particular site. The kernel may install copied vectors and relocated addresses at runtime.

Other relevant kernel symbols:
- `0x80025A20` `handle_tlbl` (refill/load), `0x80025BC0` `handle_tlbs` (refill/store), `0x80025D60` `handle_mod` (TLB modified exception).
- `0x80025350` `local_flush_tlb_all`.
- `0x801D0628` `start_kernel` (init text), `0x801D1C80` `setup_arch`.
- `0x8021DB48` `pgd_current` (.bss, size 8).
- `0x801E358C` `init_mm` (data, size 132).

**HDFILL side files:** `hdfill.elf` is itself KELF, not a plaintext ELF: header size=0x3A0, declared content_size=0x83EA0, and 0x3A0+0x83EA0=0x84240 exact file size; flags at offset 0x18=0x022C. Additional MagicGate data would be required for static ELF recovery (not needed for the kernel failure). `HDFILLER.PAK` starts with `fpkr` magic and is not a standard ELF or ZIP; content has repetitive/highly compressible areas. Its format and role are **not established**; don't interpret as executable or assume its purpose from filename. These two files are separate from the now-identified vmlinux TLB path.

**Actionable Ghidra metadata:** script `ghidra_scripts/AnnotatePsbbnVmlinuxTlb_20261017_v1.java` checks exact linked instruction bytes then adds comments/bookmarks at relevant kernel symbols and both TLBWR sites. Requires importing vmlinux correctly and analysis; do not automatically rename imported symbols or override resolved datatypes. Static verification only: no local Ghidra instance was run to compile/execute the script.

**Next needed diagnostic:** bounded logs of interpreter TLBWR with **true executing guest PC** and `EPC, BadVAddr, Cause, Status, EntryHi, EntryLo0, EntryLo1, Random, Wired` plus context around first event. Capture preceding writes to `0xC0005000`, not the million-line print flood. Check whether COP0 Random advances and whether page-table mapping for target virtual page was ever installed. Compare to original Linux refill path before implementing fixes.

### Ghidra vmlinux analysis archive and CCC import warnings (2026-10-18)

User's Ghidra import of original Sony vmlinux reported **4,769 imported functions**, **3,871 globals**, **161 skipped unmapped/unnamed functions**, **30 function-creation failures**, and **one unmapped label**. CCC's first ten discarded-address warnings include `profile_setup`, `trap_init`, `check_bugs`, `init_arch`, `init_generic_irq`, `null_rtc_get_time`, `paging_init`, `loadmmu`, `r5900_flush_cache_range_d64i64`, and `add_temporary_entry`.

User subsequently supplied `vmlinux.gzf` (12,020,937 bytes, SHA256 `a26d015f0695e0a5a9fffa5dee65d34eac74ee6dc2bafdd01aa8cfd5d12108d0`). The archive is Java serialization with one local ZIP entry, `FOLDER_ITEM`. Raw DEFLATE successfully recovered its **41,451,520-byte** Ghidra packed DB payload; verified CRC32 `e7f2c302`, exactly matching the ZIP data descriptor. No archive corruption is indicated. Packed DB strings include all ten CCC-warning symbols as well as kernel TLB function names, but packed string presence alone does **not** establish their final symbol/function status in Ghidra. No local Ghidra runtime was available for direct DB or script execution.

**Reason for warnings:** Verified CCC upstream source `chaoticgd/ccc/src/ccc/mdebug_importer.cpp`, around lines 575–604. Its `detect_fake_functions()` deliberately unassigns an **.mdebug function candidate's address when multiple function symbols collide at that address** and an external mdebug function record does not match its name. Therefore the warning is a *heuristic disambiguation*, not a Ghidra failure to load the ELF and not proof that the named kernel function doesn't exist. It might discard duplicate incorrect mdebug definitions even where a correct function name exists elsewhere. Avoid globally disabling the heuristic without verifying each duplicate. The user's installed CCC build matches this diagnostic site; the GitHub Java extension branch may be older than locally built CCC integration (runtime reports JSON v14), so don't blindly modify the older importer implementation.

Independently verified via `readelf -sW vmlinux` that each of the first ten warned names has a **real, section-allocated ELF `.symtab` STT_FUNC record**:

```text
0x801D0000 profile_setup                     0x38  .text.init
0x801D0860 trap_init                         0xAF4 .text.init
0x801D1358 check_bugs                        0xA0  .text.init
0x801D13F8 init_arch                         0x6F8 .text.init
0x801D2120 init_generic_irq                  0x48  .text.init
0x800213F0 null_rtc_get_time                 0x0C  .text
0x801D22A8 paging_init                       0x64  .text.init
0x801D2630 loadmmu                           0x5C  .text.init
0x80023B90 r5900_flush_cache_range_d64i64  0x408 .text
0x801D28C8 add_temporary_entry              0x104 .text.init
0x801D0038 name_to_kdev_t                    0x100 .text.init
0x801D0258 checksetup                        0xAC  .text.init
```

These should be Ghidra function labels at the corresponding addresses when the ELF itself is loaded. The native ELF `.symtab` has **4,711 size-positive FUNC rows at 4,711 unique addresses**, indicating no ambiguity for those addresses in that table (zero-size aliases are separate). **Do not recreate a second overlapping function for `__bzero`** at `0x801A48FC`: it is a zero-sized ELF function alias within `memset`. The sole logged unmapped symbol `_end` is an ELF `OBJECT` of zero size at `0x8021DB50`, which is precisely the **exclusive high boundary** of loaded `.bss`, not a lost executable or missing memory allocation. Do not extend a memory block solely to host `_end`.

**Actionable new script:** `ghidra_scripts/ReconcilePsbbnVmlinuxSymbols_20261018_v1.java`. Prompt user for exact original ELF `vmlinux` (NOT gzipped archive); verify its SHA256, MIPS ELF class, entry and four known instruction encodings before edits. Read the ELF `SHT_SYMTAB` directly, reconcile mapped `STT_FUNC` and `STT_OBJECT` symbols into Ghidra: create missing functions where no existing body overlaps, fill generic names, add import labels when a valid function already has a separate non-generic name, leave user-chosen function names and signatures alone, mark zero-size aliases as labels. In addition, add read-only comments/bookmarks for verified TLB routines, `__bzero` stores, `pgd_current`, and `_end` boundary. Print applied, already-present, skipped and overlap counts so any residual gaps can be investigated exactly. No unknown datatypes or speculative calling conventions are applied. **Not run in Ghidra yet**, needs user execution/console output for validation. This is an exact ELF `.symtab` reconciliation, not a fix to CCC's upstream duplicate-function scoring.

**Next check:** Run script on imported vmlinux Ghidra project, capture summary counts. Inspect any remaining skipped overlap/function-creation sites, especially `.text.init`, before altering the importer. For runtime PSBBN TLB issue, next gather true interpreter TLBWR guest PC+COP0 state and correlate with page table before modifying emulator logic.

## First next step

Collect a **non-destructive baseline run** of PSBBN (Definitive Project) on the current fork using a disposable HDD image, identifying whether the first failure occurs in HDD/ATA/MBR/KELF or at Linux EE TLB initialization. In parallel, inspect PCSX2's existing TLB logic and review closed PR #14847 *line by line* to extract hypotheses for testable fixes.
