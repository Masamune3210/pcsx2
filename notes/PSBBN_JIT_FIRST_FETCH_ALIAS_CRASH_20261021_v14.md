# EE JIT PSBBN userspace: first-instruction fetch alias failure (v14, 2026-10-21)

## Existing investigation branch and status
- Repo: `Masamune3210/pcsx2`.
- Continue: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`. No new branches.
- Functional source fix: `4f3966782eb59de8aa18172b0096f297b5eec894` in `pcsx2/x86/ix86-32/iR5900.cpp`.
- **UNBUILT/UNTESTED** in Windows. No claimed runtime progression beyond the last tested build. Keep native PS2/BIOS/retail-game correctness the primary requirement.

## Exact evidence from user's Visual Studio stop
- Host exception: `0xC0000005`, **read at null address 0**, in `recRecompile(const unsigned int startpc)` line 2536 at the forward scan's `cpuRegs.code = *(int*)PSM(HWADDR(i));`.
- `startpc=263614664 = 0x0FB670C8`, `i=263614664 = 0x0FB670C8`. Page `0x0FB67000`, offset `0xC8` (**NOT** the earlier branch delay-slot boundary).
- `cpuRegs.code=2353135616 = 0x8C420000`, encoding `LW v0, 0(v0)`. This value was left in the register from prior scanning and **cannot** be assumed to be the instruction at `0x0FB670C8` because the dereference at that PC faulted before loading it.
- Previous build crashed at the backward pass `0x0FB74000`, a cross-4 KiB delay slot; the distinct fallback fix for that case remains in this branch. It is **not** the current fault.
- `PSM(phys)` calls `vtlb_GetPhyPtr((phys)&0x1fffffff)` and returns null when host physical pmap reports handler/out-of-range. The active JIT LUT dispatched to the compiler, but its `HWADDR(guestPC)` did not resolve to a readable guest physical instruction. Need a safe guard/reconciliation and to determine if this was a missing guest mapping versus an inconsistent JIT alias.

## New patch, no guest-specific magic addresses
- `recRecompile` now validates `PSM(HWADDR(startpc))` **before allocating/altering compiled-block bookkeeping**, preventing the host fault at `i==startpc`.
- New cold-path `recHandleUnavailableCodePage(guestPC)` compares the current JIT physical alias with a pure `BuildEEActiveTLBPageMap` view for the current guest ASID.
  - If a valid active guest TLB entry maps to physically backed EE RAM, remap the specific 4 KiB JIT code alias and switch into fine-grained dispatch, then exit/retry the EE dispatcher. A bounded warning logs the original JIT address and expected guest TLB physical page. This is a consistency repair, not a fabricated guest mapping.
  - If the guest TLB translation is really absent **and** its host vTLB is the unmapped virtual handler, inject guest instruction-fetch `TLBL` with precise `BadVAddr`, `EPC`, BD=0 and exit host JIT dispatch.
  - If the architectural entry exists but its physical backing is invalid/MMIO, or another inconsistency remains, pause with `R5900 Recompiler: Instruction fetch has no physical backing` and record PC, JIT physical, active-ASID TLB status/physical, vTLB handler state and dispatch mode. This avoids misdiagnosing valid TLB entries as guest misses.
- The forward instruction scan also checks each pointer. If it encounters an inaccessible instruction after a successfully fetched initial instruction, it ends the compiled block there and lets the next dispatch evaluate that page; it does not speculate across the fault.
- Existing rare cross-page branch-delay-slot interpretation fallback remains; no additional interpreter hot-path cost. Ordinary native 64 KiB dispatch remains the default. The new checks occur only when recompiler is compiling a fresh block, and the expensive architectural lookup only on an actual null physical fetch.

## Test next
1. Rebuild existing branch under MSVC x64 AVX2, then cold-boot the same PSBBN image.
2. If the original address `0x0FB670C8` is reached, expect one of:
   - `EE JIT repaired instruction alias`: a stale JIT physical code alias was repaired from a valid guest TLB entry; verify execution advances and that no repeated repair loop appears.
   - `cpuTlbMiss` at `0FB670C8`: Linux handles genuine instruction-fetch miss and installs code page; validate EPC/ASID/EntryHi/physical target.
   - `R5900 Recompiler: Instruction fetch has no physical backing`: an emulator mapping inconsistency remains. Capture the reported JIT physical address, architectural TLB physical address, ASID, and dispatch mode, not just a generic 0xC0000005.
3. If host crash persists but shifts to another compiler read, use exact `recRecompile` line and guest PC; do not replace invalid guest instruction bytes with NOPs or map physical memory artificially.
4. Do not merge into `testing` without native BIOS/retail-title compatibility, savestate regression, and JIT performance verification. The separate cycle-derived JIT Random register model still needs hardware-fidelity work.

## Ghidra metadata
No new guest binary symbols, signatures, or datatypes established from this host-side JIT failure. Preserve the existing PSBBN ReiserFS Ghidra script and metadata in `Masamune3210/ps2bbndecomp`.
