# DEV9 FIFO safety changes, 2026-10-20

Scope: `pcsx2/DEV9/DEV9.cpp`, the reset declaration in `DEV9.h`, and prospective real-production-function regression source `tests/ctest/core/dev9_fifo_tests_20261020.cpp`. The user explicitly prohibited compilation. No compiler, C++ test executable, Windows build, or PSBBN runtime was run, and no disk image was changed.

## Root causes and changes

- `IOPWriteFIFO` previously copied both wrapped segments to `dev9.fifo + base`. The second segment now starts at `dev9.fifo`. Besides overwriting the tail, a second segment larger than the tail could overwrite fields following the FIFO in `dev9Struct`.
- `SPD_R_DBUF_STAT` permits independent counter resets. A write-counter-only reset can leave `fifo_bytes_write < fifo_bytes_read`; a read-counter-only reset can expose more than the capacity. Debug assertions checked only the upper bound, and release paths could pass negative lengths to `memcpy` or the ATA FIFO callbacks. All four FIFO transfer helpers now reject inconsistent occupancy before subtraction, modulo arithmetic and copying. FIFO interrupt and DBUF status calculation likewise reject invalid pairs. Counters remain unchanged rather than silently discarding data or acknowledging a transfer; software can recover by resetting both counters. Invalid status reads return zero and invalid FIFO states do not generate normal FIFO/completion interrupts. Exact hardware behavior for one-counter-only reset remains unverified.
- Signed cumulative byte counters could overflow after roughly 2 GiB without reset. Immediately before a transfer can overflow, both counters lose the same whole-ring offset. Their difference and their individual positions modulo 8192 remain unchanged; the rebased write counter plus at most one FIFO transfer remains well below `INT_MAX`. Ordinary counters are not rebased, preserving normal independent-reset behavior.
- IOP DMA residuals are validated before calculating a copy length: pointer present, size and progress nonnegative, and progress no greater than size. Invalid residuals leave data and progress unchanged, and cannot signal successful completion.
- HDD wrap handling now checks `dmaReady` again after the first segment: that callback can complete a command and release its transfer buffer. No second callback is made after completion. Empty/full FIFO paths avoid zero-byte ATA callbacks.
- `DEV9ResetATAFIFO()` discards FIFO occupancy and cancels the pending IOP pointer, length and progress without a successful DMA completion interrupt. ATA reset integration is handled in the ATA changes. Physical FIFO bytes need not be zeroed because zero occupancy makes them inaccessible. This helper does not assert DMA8 completion or modify IOP DMA channel registers; guest recovery after reset remains a runtime validation item.

The FIFO guards prevent unsafe lengths. They do not establish the cause of the guest's historical `ATA DMA force break`: that literal is absent from the current DEV9 and IOP DMA sources. DMA8 transfer setup and completion were traced through `pcsx2/IopDma.cpp`; force break, DMA pause, direction changes, mismatched ATA/IOP lengths, and SPEED reset timing still need hardware/runtime investigation. No speculative force-break behavior was added.

## Regression source and limits

Seven GoogleTest cases call the real `IOPWriteFIFO`, `IOPReadFIFO`, and `DEV9ResetATAFIFO` implementations:

1. Wrapped writes with tails of 1, 17 and 512 bytes, position-dependent input patterns and sentinel checks on every untouched FIFO byte. Equal-sized segments let the original defect produce a data mismatch without deliberately overwriting adjacent structure fields.
2. Wrapped reads with a preexisting IOP offset, ordered bytes, and untouched destination sentinels.
3. Full FIFO preserving a pending write without reporting progress.
4. Independent counter reset preserving invalid state without a negative copy or false progress.
5. Overrun, negative-size and negative-progress residuals in both directions.
6. Write counter near `INT_MAX` preserving ring position, contents and occupancy after rebasing.
7. Bridge reset clearing pending IOP ownership and occupancy.

The source was written before the production patch, but the prohibition on compiling prevents the TDD red/green execution cycle. These are **uncompiled, unexecuted prospective tests**, not a passing regression suite. Their presence is not behavioral verification. They save and restore `dev9` and call direct copy helpers to avoid inventing ATA/IRQ mocks.

The source is deliberately not registered in CMake or MSBuild in this checkpoint. Future integration: add `dev9_fifo_tests_20261020.cpp` to `tests/ctest/core/CMakeLists.txt`'s `core_test`, which already links the production `PCSX2` library. After compilation is authorized, run `core_test --gtest_filter=DEV9FIFO.*`, preferably with ASan/UBSan, then the complete `core_test` suite. Verify the wrapped-write case against the original copy destination before restoring the correction. That integration and all Windows `Release AVX2|x64`/PSBBN validation remain unrun. HDD callback continuation and FIFO interrupt transitions need additional fixture coverage.

## Checks actually performed

- Read `AGENTS.md`, the audit note and workplan before source investigation.
- Reviewed the production FIFO helpers, DBUF read/reset handling, DMA8 entry/interrupt handling and ATA FIFO callbacks, including buffer release at completion.
- `git diff --check -- pcsx2/DEV9/DEV9.cpp pcsx2/DEV9/DEV9.h` passed.
- A Python source-presence check passed: five guarded FIFO paths, guarded IOP residual arithmetic, corrected wrapped destination, reset declaration, seven test cases, and absence of CMake registration. It reads source strings only; it does not mirror or execute the C++ algorithm.
- `clang-format` was not available; no formatter or compiler claim is made.
