# PSBBN JIT ReiserFS journal read-after-write witness (2026-10-21)

## Branch and purpose

Continue `note-working/psbbn-jit-reiserfs-mmu-witness-20261020` (descends from `note-working/dev9-ata4-hdd-audit-20261019`; preserve ATA-4 work). This document supersedes the **next-step** sections of `PSBBN_JIT_REISERFS_MMU_WITNESS_20261020_v1.md`. No guest hack, forced memory write, or architectural exception fix is in this branch.

## New verified debugger evidence

Sony 2.4.17 guest kernel, PSBBN SCPN-60160, EE JIT:

* `0x800B1348` journal_init receives superblock `0x81FA6000`.
* `0x800B13C0` calls guest `memset` with destination `0xC0005000`, zero and `0x115C0` bytes (length finalized by the MIPS delay slot).
* `0x800B1470` bitmap initialization is invoked; journal slot array at `0xC0005290`.
* `0x800B156C` calls `get_list_bitmap`; `0x800ADC70` enters with `a0=0x81FA6000`, `a1=0xC00052B8`.
* The load at `0x800ADCE4` from `journal + 0x290` returns zero. The independent load at `0x800ADD1C` from the selected slot also returns zero. The first bitmap slot is available.
* At `0x800B1574`, the bitmap function returns **`v0=0xC0005290`**, indicating a successful allocation.
* `0x800B1578`: `sw v0,0x2dc(v1)` is supposed to save the slot pointer into `0xC00052DC` (`journal+0x2dc`).
* `0x800B1580`: `lw v1,0x2dc(v0)` immediately reloads the same field, but `v1=0` by `0x800B1584`; branch is **not taken**.
* `0x800B1590` error-reporting path is reached; after resuming Linux prints `journal-2005, get_list_bitmap failed for journal list 0`, then fails root mount and panics on `03:01`.

The kernel's error string therefore identifies a **post-return field check**, not necessarily a failed `get_list_bitmap` selection. Neither a silently discarded store nor a failed/incorrectly translated load is conclusively isolated yet. The debugger's Memory tab reports `??` for `0xC0005xxx`, but the guest successfully loaded slot owners; do not treat debugger `??` as corruption.

## Committed targeted JIT witness

Code changes on this branch:

* `pcsx2/x86/ix86-32/iR5900LoadStore.cpp`: generate host observer calls **only** for JIT guest `SW @ 0x800B1578` and `LW @ 0x800B1580`, before and after each original memory instruction. Use `iFlushCall(FLUSH_FULLVTLB)` so JIT-allocated GPR values are synchronized before observer calls. Do not substitute the guest load/store.
* `pcsx2/COP0.h`: declares `PSBBNTraceJournalWordAccess(u32 guestPc, u32 phase)`.
* `pcsx2/COP0.cpp`: read-only observer, opt-in through existing **Trace.Enabled + Trace.EE.cop0**, capped to 32 events per trace reset. Reports precise *compiled* guest PC, `v0`, `v1`, effective address, vTLB handler vs directly mapped word, architectural TLB active-ASID physical mapping and writability, physical RAM word when safe, and CP0 `Status/Cause/EPC/EntryHi`. Does not use the optional `ppmap`, does not raise exceptions, and does not mutate guest state.

The four `PSBBN-JOURNAL-WORD` records should have `phase=0` (before SW), `phase=1` (after SW), `phase=2` (before LW), and `phase=3` (after LW). Each includes `guestPC`, `ea`, `v0`, `v1`, `vmapDirect`, `vmapHandler`, `vmapValue`, `archMapped`, `archPhysical`, `archWritable`, `physValue` and CP0. A vmap handler means the safe observer **cannot** read virtual memory, not that the guest field is necessarily zero.

## Run instructions / decision matrix

1. Build this exact branch in Windows (Release AVX2 x64, same settings as the failing boot). No assistant-side native build or runtime verification has occurred.
2. Enable existing EE COP0 trace logging: both general Trace and EE COP0 must be active **before** the JIT code is compiled. Disable pausing debugger execution breakpoints when collecting the uninterrupted witness.
3. Boot PSBBN under EE recompiler. Capture all `PSBBN-JOURNAL-WORD` lines and `PSBBN-TLB` misses (especially at `0xC00052DC`) plus the final printk lines. If an old savestate bypasses recompilation of these instructions, use a pre-journal snapshot or cold boot.
4. If `phase=0` sees expected `v0=C0005290`, `v1=C0005000` and `phase=1` still sees zero in *both* directly mapped and valid physical memory, investigate the JIT store/permission/exception route (including EntryLo.D). If `phase=1` shows the correct pointer but `phase=2` does not, investigate the mapping/ASID transition or intervening overwrite. If `phase=2` shows the expected pointer but `phase=3` reports `v1=0`, the guest load itself is wrong or using a different alias/translation. If `phase=0` contains wrong operands, inspect guest register allocation/propagation **before** blaming vTLB.
5. A JIT TLB miss is currently logged without reliable guest exception delivery; do **not** patch this by directly calling `cpuTlbMissW` without correct guest instruction PC, delay-slot, register state, and translated-block abort semantics.

## Verification status

GitHub source modifications were committed and inspected; the `LOADSTORE_RECOMPILE` define is present in `pcsx2/Config.h` and the modified opcode emitters are in the compiled `#else` section. **Not compiled or executed natively**; these four witness lines are expected output, not claimed observations. Next action after runtime capture is a principled emulator fix guided by the first disagreeing phase, not another guest patch or another nested investigation branch.
