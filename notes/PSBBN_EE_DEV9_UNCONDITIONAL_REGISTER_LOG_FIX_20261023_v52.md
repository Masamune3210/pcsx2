# PSBBN v52: remove unconditional EE-side DEV9 register console spam

**Branch:** `note-working/psbbn-tlb-small-delta-projection-20261023` (continued in place; do not modify `testing`).
**Previous handoff:** `notes/PSBBN_IOP_KPRINTF_DEV9_LOGGING_20261023_v51.md`. That document's hypothesized guest-stdout source is **disproven** by the user's next investigation.

## Evidence and corrected root cause

- User ran `PCSX2 v2.9.92-418-g4996e19a1` (v51). Supplied `emulog(20261023-175800).txt` (~126,050 lines, stopped around 25.8s) plus Visual Studio screenshot `image(20261023-180353).png`.
- DEV9 read/write flood persisted and no `IOP-DEV9-CONSOLE-TRACE` / `IOP-DEV9-CONSOLE-SUMMARY` activation was seen. All prior output-sink filtering targeted the **wrong subsystem**.
- The screenshot explicitly identifies the hot-path emitter in **`pcsx2/Memory.cpp`**: `_ext_memRead8<p>()` has `Console.WriteLn("DEV9 read8 ...")` at ~line 529; read16/read32 at ~559/585, and `_ext_memWrite8/16/32` at corresponding write handlers. These are **EE physical fallback memory handlers**, not messages printed by an IOP guest driver or BIOS stdout.
- The DEV9 device register reads/writes themselves are legitimate; only unconditional console printing is inappropriate. Global trace settings had no control because the statements used `Console.WriteLn` directly.

## Implementation

1. **Actual source fixed** in `pcsx2/Memory.cpp`: change all six unconditional `Console.WriteLn("DEV9 (read|write)(8|16|32) ...")` statements to **`HW_LOG("DEV9 ...")`**, the existing `DebugTools/Debug.h` EE KnownHw logger. This is gated by dev-build hardware tracing, `EmuConfig.Trace.Enabled` and `EE.KnownHw`, with a cheap disabled-path check. Preserve each `DEV9read*`/`DEV9write*`, masked address, value, return type, ordering, and existing message arguments. The register trace can still be requested explicitly through EE hardware tracing.
2. **Delete the erroneous v50-v52 guest-output interception code** and associated classifier tests, rather than leaving filtering all over unrelated IOP BIOS/console paths:
   - `pcsx2/IopBios.cpp`, `pcsx2/ps2/Iop/PsxBios.cpp`, `pcsx2/IopHw.h`, `pcsx2/ps2/Iop/IopHwWrite.cpp`, `pcsx2/EEExecutionProfile.h` and `tests/ctest/core/ee_rec_coherence_policy_tests_20261021.cpp` restored exactly to their pre-v50 v49 contents.
   - `pcsx2/DEV9/DEV9.cpp`: remove unused IOP console summary hook and include, **preserve v50's working IF_CTRL repeat-warning aggregation**.
3. **Preserve other effective changes:** `pcsx2/CDVD/CDVD.cpp` opt-in MagicGate raw/flow trace (v50), DEV9 IF_CTRL aggregate warnings (v50), EE TLB fault log suppression (v49), TLB incremental mapping (v46), 256 MiB default/selectable EE JIT arena (v47/v48), retired-source link detachment (v43/v44). There is no guest behavior change beyond removing stdout/trace overhead.
4. Verified through GitHub connector diff relative to the v49 base: only **`pcsx2/Memory.cpp`, `pcsx2/CDVD/CDVD.cpp`, `pcsx2/DEV9/DEV9.cpp` and old notes** remain changed; the speculative IOP filters and their tests are absent. No compiler/runtime confirmation from assistant.

## User test

Fetch current HEAD of this **same branch**, build on Windows/MSVC, run PSBBN using Extended MMU and 256 MiB EE JIT. Leave `[EmuCore/TraceLog] Enabled=false`, `EE.KnownHw=false`, `EEVerboseTLBFaultLog=false`, `EEJitLinkOwnershipWitness=false`. **Keep file logging enabled for the first verification**.

Expected:
- The `DEV9 read8/16/32` and `DEV9 write8/16/32` floods disappear entirely, since all six actual source lines are trace-gated.
- `DEV9: ...` errors/warnings remain visible; DEV9 IF_CTRL duplicate warnings remain aggregated. No `IOP-DEV9-CONSOLE-TRACE` / summary is expected; these filters are deliberately removed.
- `[MGRAW]` and `[MGFLOW]` remain absent unless `[EmuCore] MagicGateProtocolTrace=true`; relevant `[MG]` key warnings still printed.
- Real DEV9 HDD/ATA reads/writes must continue; PSBBN must still load OS ELF and reach userspace/UI without new faults. Test includes an existing GTest core run if practical; no dedicated unit test needed for straightforward trace macro redirection.
- For FPS, do a **matched scene and stage** compared to user's ~6 FPS, with file/system console logging off in both benchmark runs and all verbose witnesses off. Lower log line counts prove the logging fix, **not** a speedup. If FPS stays ~6, focus next on EE JIT/MMU/GS timing instrumentation, not more cache size or more log filters.
- To restore the DEV9 register-by-register output for diagnosis, enable normal PCSX2 global tracing and **EE hardware known-register trace**, rather than the now-discontinued `IOPVerboseDEV9RegisterLog` setting.

**Current verification status:** implemented and committed; Windows/MSVC compile and PSBBN runtime remain unverified. Acknowledge earlier false attribution to IOP output and do not revive it without new direct evidence.
