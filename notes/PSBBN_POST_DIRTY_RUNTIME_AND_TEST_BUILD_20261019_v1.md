# Post-Dirty interpreter runtime and missing core_test.exe (2026-10-19)

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


The current PCSX2 branch tip is `833a0d3008eb5742eac331815c14e1427b774f60`; user's new log `emulog(5).txt` reports `v2.9.92-107-g833a0d300`, so this run uses the committed Dirty-bit implementation.

## Unit tests are source-only until explicitly built

`tests/ctest/core/CMakeLists.txt` registers `core_test` with `add_pcsx2_test`, which declares it `EXCLUDE_FROM_ALL` in `tests/ctest/CMakeLists.txt`. The tests are *not* automatically generated by building PCSX2 or delivered in git. `ENABLE_TESTS` defaults ON, but a CMake cache may have it OFF.

From the **existing CMake build directory**, check that `ENABLE_TESTS:BOOL=ON` is present in `CMakeCache.txt`, then run:

```powershell
cmake --build . --target core_test --config Release --parallel
Get-ChildItem -Path . -Recurse -Filter core_test.exe -File | Select-Object -ExpandProperty FullName
```

Run the resulting executable with `--gtest_filter=EEActiveTLB.*`. If `core_test` is not a known target, reconfigure the existing build with `-DENABLE_TESTS=ON`, preserving its generator/compiler/toolchain options. Do not confuse the already-passed *static source-integrity checks* with executing the twelve C++ tests. No Windows test build has been witnessed yet.

## New interpreter cold run evidence

- User uploaded `emulog(5).txt` (SHA256 `02517f37e3f55d9ced841bbb8d7e590e04cfa8c29b80671f4f0d0562fbabf5e2`) and `sstates(4).zip` (SHA256 `c9427841483b705a403ea1db1c5fc80a2b74589eff9e31c5f397e87a1508c818`).
- The archive contains 103 savestates in five series, of which 16 are from this run. Captures 004–015, 49.2 to 159.5 s, are sampled in Linux's kernel idle loop `0x800185D8`/`0x800185F0` with PID1 blocked; this is *not proof the guest hung*. Capture 016 at 170.5308 s has `PC=0x0FB6D0B4`, ASID03, PID1 running, and 135 changed EE RAM pages compared with 015. All screenshots 004–016 still show the identical PlayStation 2 logo.
- Eight user-mode `PSBBN-FAULT code=04` entries establish the new TLB Modification fault path executes: first at 170.5199 s, `pc=0x0FB6F8D8`, `BadVAddr=0x2AC1D000`, ASID03; five more at 171.10–171.17 s in ASID03 (all `bd=1`), then ASID04 at 176.3970 and 176.8595 s (last `bd=1`).
- Snapshot 016, about 11 ms after the first Mod, has TLB slot 7 / ASID03 / `EntryHi=0x2AC1C003` mapping `0x2AC1D000` with `EntryLo=0x0007A6DE` (`V=1,D=1`) to physical `0x01E9B000`. A writable map exists after the Mod and guest execution advanced. This **supports** fault handling, but does not itself prove precise COW PFN replacement.
- Last log entry at 180.4631 s: `PSBBN-FRAME-PATH pc=2AB0F2BC`, ASID04, `gp=0x2AC24400`, `ra=0x00403F58`. This is *not* a fatal PC report and does not prove the full GP/RA corruption is fixed.
- About 440,576 `DEV9 read16 10000040` log lines came from HDD PIO traffic. Their volume may add overhead, but there is **no evidence** the DEV9 reads themselves caused a crash. The user reports a native PCSX2 process crash; no native exception/dump was included. The log itself ends without an explicit crash marker.
- MTVU was enabled and warns that saved states might not be stable.

Research detail and evidence limits are preserved in [PSBBN_POST_DIRTY_INTERPRETER_RUN_20261019_v1.md](https://github.com/Masamune3210/ps2bbndecomp/blob/note-working/psbbn-ghidra-analysis-20261018/docs/PSBBN_POST_DIRTY_INTERPRETER_RUN_20261019_v1.md).

**Next:** collect native host crash information (dump or WER error record) rather than attributing the termination to the final guest log PC; then check the guest Mod handler and retry in a bounded interpreter-only run, without conflating stagnant screen pixels with no CPU progress. JIT remains deferred.

## Native crash diagnosis and targeted fix (2026-10-19)

User ran the same build under the Visual Studio debugger. The native crash is `0xC0000005` reading host address `0x00000000001FFFDC`, with top frame `PSBBNTraceReturnSlotTranslation` in `pcsx2/COP0.cpp` at original line 226, invoked from `execI` line 248. The PSBBN-only observer derived a physical offset and directly read `eeMem->Main + offset` without verifying that `eeMem` remained allocated. `EEVM_MemoryAllocMess::Main` begins at offset zero, so the low faulting address is consistent with a null `eeMem` base. **Why the pointer is null at this stage is not yet established**; do not assert VM teardown or another lifetime transition without runtime evidence.

Committed diagnostic safety correction [`8dde4ac`](https://github.com/Masamune3210/pcsx2/commit/8dde4ac9992c558401ea198f288be037132cc8b2): `PSBBNTraceReturnSlotTranslation` returns before either `BuildEEActiveTLBPageMap` / `vtlb_V2P` or the raw physical RAM reads when `eeMem == nullptr`. It emits only one `PSBBN-RA-PHYS-SKIP reason=eeMem-null` marker to retain evidence. This changes **no guest TLB/exception/memory operation** and leaves JIT untouched. **Windows rebuild and re-run not yet verified**. If the guard triggers while the VM should be executing, investigate EE memory lifetime and VM stopping order, not the guest COW operation.

The previous report's 'no native crash information' is superseded by the user's Visual Studio exception and stack trace. Other late guest correctness issues remain unresolved. Rebuild PCSX2 from this commit before another interpreter-only test.

**Follow-up safety refinement:** PCSX2 commit [`d614b1f`](https://github.com/Masamune3210/pcsx2/commit/d614b1f88a054fbea2b69db8cc0a80fd3f4e52d6) snapshots the checked EE memory pointer into a local `sampledMemory` and uses that same checked pointer for both diagnostic reads. This prevents a null re-read of the global pointer between the check and the two copies; it does not guarantee safety against unsynchronized memory deallocation, which would require lifecycle investigation. Windows rebuild/runtime validation remains pending.
