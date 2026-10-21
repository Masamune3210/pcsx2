# PSBBN 4 KiB dispatcher regression and native fast-path correction (2026-10-21 v11)

## Status and constraints
Repo `Masamune3210/pcsx2`; continue existing branch `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`. Native PS2 compatibility and performance have priority over PSBBN Linux; this is **not** a Linux-specific address hack or a release-ready change.

The tested build in `emulog(20261021-021310).txt` is `v2.9.92-205-gb035160c8`, containing the global 4 KiB `recLUTPages` dispatch introduced in `7cfa20c6`. It fails **before** starting Sony Linux. BIOS and OSDSYS/HDDLOAD load successfully and authenticates the KELF. Near emulator time 86.55s, first logged JIT store TLB miss:
```
cpuTlbMiss pc:80024b60 addr:01114650 status=70030c00 code=c
PSBBN-TLB ... rawpc=80024b5c ... BadVAddr=01114650 ... Cause=0000000c
```
It enters the BIOS TLBS handler at `0x80000000`, then incurs another data TLBL at `0x00000010` from `0x80000020`, then attempts instruction-fetch TLBL at `0x00000000` repeatedly with `Status=70030c02`, `EPC=80024b5c`, `Cause=8` (nested exception). Around **177,000** `cpuTlbMiss` lines are logged until pausing. No ReiserFS or journal activity occurs on this boot. This indicates a severe native PS2 bootstrap regression, not a failed root mount.

`psbbn-kernel-log(3).txt` contains older observer sessions (prior mounts) and an incomplete fresh observer header at 21:11:50; **no current successful or failed Linux root mount can be attributed to this build**. Do not use prior session's mount as this run's result.

## Experimental correction: demand-driven dispatch
- Commit `b62852bf1622394954df21ff672603541856a423` restores the existing 64 KiB `recLUT/hwLUT` path for all regular EE JIT execution. Both translated jump dispatchers now check a runtime flag and take the existing `addr >> 16` lookup when no guest-code TLB alias is needed.
- On an unmapped legacy `recLUT` instruction-fetch, `recError` checks if an **actual valid 4 KiB TLB code alias has already been projected into `recLUTPages`**. If yes, it switches to the 4 KiB dispatcher and immediately exits so the next dispatch uses the corrected translation. If not, the previously working guest instruction-fetch TLBL code remains intact and Linux can fill the page normally.
- `PC_GETBLOCK` and `HWADDR` use whichever map the dispatcher currently uses, so recompiler block physical identity and guest opcode-fetch address agree with dispatch. A recompiler reset restores native 64 KiB dispatch by default. Source followup `a9b955b95b5bf3dd5f0ff3d6218d6714aedb2383` uses the project's existing `xForwardJNZ32` emitter spelling.
- This is a **non-Linux-specific opt-in**: it is triggered by real TLB code mapping outside the fixed legacy 64 KiB aliases, regardless of executable/title. No special `0FB643C0` page mappings and no patched guest state.

## Verification / caveats
- Both commits are **source-only and UNCOMPILED/UNTESTED in Windows**, not known to fix this regression yet. The most recent *verified* PSBBN JIT mount is from the earlier 64 KiB build with precise data TLB exceptions, before 4 KiB global dispatch.
- The switch from normal to 4 KiB dispatch occurs only on a recLUT fetch miss. A title intentionally remapping code *inside* an already direct-mapped legacy region may still require better TLB dispatch invalidation. This needs independent validation, not speculative claims of general correctness.
- A runtime conditional branch now exists in the JIT dispatcher, even in default 64 KiB mode. Benchmark its cost on regular retail PS2 titles; eventually consider specialized dispatchers with a host pointer switch instead, or a per-segment overlay without hot-path overhead.
- The earlier `FLUSH_INTERPRETER` before recompiled memory operations still has possible performance costs and should also be revisited once correct.
- Next Windows test: build branch head, cold boot BIOS/HDDLOAD/PSBBN and check whether the first `0x01114650` TLB fault is gone, whether JIT mounts root again, and whether the first code fetch at `0x0FB643C0` causes a single 4 KiB dispatch activation and successfully executes. If the original 0x01114650 fault persists, do NOT blame a Linux stage; compare vTLB/architectural entry state and host register PC/stack against pre-global-4KiB build.
- No branch nesting. Never fold this into `testing` without native PS2 game regressions and JIT performance measurements.
