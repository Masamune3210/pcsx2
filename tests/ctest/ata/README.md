# Disposable-image ATA regressions

`dev9_ata_test` builds the actual `pcsx2/DEV9/ATA/*.cpp` and command sources,
links the real `common` file API, and exercises `ATA::Open`, `Close`, `Read`,
`Write`, `Async` and the DMA data methods. It never accesses a user HDD image.
Each test creates a unique temporary directory, writes a known patterned image,
and removes it after closing the real I/O worker. Persistence checks compare the
entire backing file, including untouched neighboring sectors; several reopen it
through a fresh ATA instance.

The test include path selects `bridge/DEV9/DEV9.h` in place of the emulator's
DEV9 header. This supplies only HDD-enable configuration, register constants,
IRQ observation and empty SPEED FIFO callbacks. It does not replace ATA command
handlers, buffers, worker code, queues, filesystem operations or sparse writes.
Consequently these are ATA integration tests, **not** SPEED/IOP or emulator boot
tests. The separate `core_test` `DEV9FIFO.*` cases exercise production FIFO code.
The copied register constants have a source-only consistency check:

```sh
python3 tests/ctest/ata/check_bridge_constants.py
```

Coverage includes PIO `30/31/C5` writes and `20/21/C4` reads, exact IRQ block
cadence and DRQ lifetime, zero-count encoding for 256 sectors, 128-sector
multiple blocks with a 130-sector tail, final task-file progress, EOF/empty and
one-sector images, crossing EOF with valid-prefix transfer and first-invalid-LBA
error, SRST of partial data and accepted blocks, absent-device isolation,
closed-image command rejection, unsupported data widths, nIEN/pending IRQ/status acknowledgement, HOB clear,
CHS first/last sector roundtrips and invalid addresses after negotiated geometry,
SEEK/INITIALIZE/SET MULTIPLE/SET FEATURES/SMART parameter validation, IDENTIFY
claims, SMART enable/autosave persistence and sidecar failure handling, cached-write immediate read, cache-disable
flush, truncated backing read errors, partial DMA reset and DMA opcode aliases,
and power commands `E0/E1/E2/E3/E5/E6` including cycle-based standby timers and
sleep recovery. POSIX-only sparse fixtures exercise `34/39` extended PIO writes
and `24/29` reads at high LBAs with a 257-sector count, a >2-TiB logical capacity,
and 257-sector DMA read buffer growth; they allocate only the original
small patterned prefix and skip if the filesystem rejects the logical size.
They are excluded on Windows because extending a file there needs explicit
sparse-file preparation before it can safely use these logical sizes.

No CMake configure, compiler, compiled test or Windows/PSBBN runtime was run
while adding these tests, following the user's explicit no-compilation request.
The following are **future commands**, for when compilation is authorized, from
an already configured project build with `ENABLE_TESTS=ON`:

```sh
cmake --build build --target dev9_ata_test
ctest --test-dir build -R '^dev9_ata_test$' --output-on-failure
cmake --build build --target core_test
GTEST_FILTER='DEV9FIFO.*' ctest --test-dir build -R '^core_test$' --output-on-failure
```

For a CMake multi-configuration build use `--config Release` when building,
`-C Release` with CTest, and the corresponding Release executable directory.
This CMake test target is not registered in the user's existing native
`PCSX2_qt.slnx`; its Windows `Release AVX2|x64` MSBuild build and cold boot of a
disposable PSBBN clone remain separate required validation.

Remaining gaps: host write/flush failure injection, deterministic scheduling of
an already-dequeued write during FLUSH, actual SPEED DMA start/stop/force-break
integration, and Windows sparse-image behavior. `HDD_ReadAsync` currently has no
production callers; public DMA commands use `HDD_ReadSync`, so the dormant
asynchronous read helper's allocation path is not covered by the growth test.
The ordinary concurrent worker
is exercised, but this fixture does not claim deterministic race coverage or
power-loss durability. A future sanitizer build should run the DMA
buffer-growth and interrupted-transfer cases to check allocation ownership.
