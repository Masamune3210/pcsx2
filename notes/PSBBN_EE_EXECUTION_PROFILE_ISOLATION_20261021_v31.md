# CURRENT OBJECTIVE: isolate experimental EE MMU/JIT behavior behind a boot-scoped execution profile (v31)

## State and immediate objective (2026-10-21)

Repository: `Masamune3210/pcsx2`.
Continue on **existing** `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
This supersedes the v26–v30 performance/cache-optimization queue **as the current priority**, without throwing away its findings or source work.

User raised a high-priority compatibility concern: shared EE recompiler/TLB changes made to boot PlayStation BB Navigator's Linux may introduce subtle retail-game regressions. Immediate goal is a **verified Standard/Extended MMU execution-policy boundary**, not further speculative cache reuse. Keep PSBBN and ordinary PS2 games on one instruction implementation; gate *experimental policy and JIT bookkeeping* where safe, not demonstrated hardware-correctness fixes. Do not rewrite history or merge into `testing` yet.

**Status: policy framework and two narrowly gated optimizations committed, but NO Windows build, unit test, retail game, savestate or Linux boot has been run.** “Standard” is currently a **selection and partial isolation**, NOT verified equivalence to `testing`. The entire old behavior is not restored by a single toggle.

## Historical comparison actually performed

- GitHub branch compare `testing...`current branch`: current branch is **269 commits ahead, 0 behind**, merge base `82ec795368b6e57c46471a95929f33dd0f4f7eee` at review. Source differences encompass EE, DEV9 ATA/HDD, UI diagnostics, and savestate/debug tooling. These must not be mistaken for one coherent cache patch.
- Compare `note-working/psbbn-tlb-boot-20261017...`current branch`: **142 commits ahead, 0 behind**, base `cee2b8475a29805ae8e996cf13d00ad57ede2ae6`. This history includes DEV9 ATA-4 and many EE JIT changes; file lists inspected rather than re-reading all 142 patches.
- Inspected predecessor source `pcsx2/COP0.cpp`, `iR5900.cpp`, `vtlb.cpp` and `SaveState.cpp` at the older `psbbn-tlb-boot` ref, plus current `VMManager.cpp`, `COP0.cpp`, `EEActiveTLB.h`, `iR5900.cpp`, and v25–v30 handoffs.

### Change provenance / segmentation inventory

| Source / commits | Historical change | Classification / isolation decision |
|---|---|---|
| `7cfa20c6b0`, `835e501059`, `93c9894542`, `b62852bf16`, `b4930a94f7` in `iR5900.cpp`, `COP0.cpp` | 4 KiB virtual instruction dispatch, new JIT TLB page aliases, demand-driven switch from original 64 KiB dispatcher, fix incorrect identity aliases | **Extended candidate**. Existing runtime 4 KiB switch is STILL demand-driven irrespective of profile; isolate only after proving a safe behavior for unrecognized games using mapped code. |
| `fa1b7c993b`, `459b07defc` in `COP0.cpp`, `Interpreter.cpp` | EE Random/Wired changes, interpreter per-instruction model | **Potential PS2 hardware correctness**, not an OS-only option. Audit with reference behavior and games instead of simply switching off. |
| `4c39a1eb8f`, `ca8b47be7f`, `df32e47318`, `608eebfa19`, `52bf75e5ea` in interpreter and JIT | Cross-page branch delay-slot and DI runtime continuations with guest exception handling | **Fine-mode-only today**, but remaining exception/DI timing validation is mandatory, including Jak X and original recDI compatibility titles. |
| `97c6571a21`, `b8a917d02f`, `8a5a407a08`, `62876d3827`, `72da4aa094`, `d9c96fc934`, `74ebb33ba5` | Physical cache identity, static links, write permissions, compiled-page filter, metadata extent protection, idempotent links | **Shared, not isolated yet**. Must compare compiled-code invalidation and SMC/link behavior against `testing` before gating. Keep correctness fixes common if proven. |
| `3807ccf2e3` in `COP0.cpp`, `EEActiveTLB.h` | D-only permission update skips host remap/code clearing | **Extended only in v31**. Standard retains conservative remove/install on D changes; tests added. |
| `7f0d0b85da` in `COP0.cpp` | Memoizes unchanged 48-slot/ASID TLB projection | **Extended only in v31**. Standard always recomputes/diffs effective view. Guest architectural TLB writes continue in both. |
| `040735b00f`, `4f3966782e`, `7f0d0b85da` | Physical/virtual instruction fetch guards and translation-boundary handling | **Partial common correctness and partial fine-mode**; audit each behavior separately. No wholesale revert without fault tests. |
| `b835bd6cbc`, `418f7a3095`, `213a21b651`, `c5922539da` | Logging, mutation/cycle measurements, const-address vTLB dependency counters | Trace-only where indicated; should not affect normal guest code generation except original opt-in trace mechanisms. |
| `DEV9/ATA/*`, `DEV9.cpp`, ATA-4 work, printk UI `LogWindow.cpp` | HDD and read-only kernel-log improvements carried into combined history | **Separate subsystem**, not part of EE profile. Preserve independent test plan and HDD-enabled savestates. |
| `R5900.cpp`, `SaveState.cpp`, `vtlb.cpp` across predecessor and current histories | Guest fault semantics, ASID rebinding, state-load resets and fastmem map coherence | **Shared architecture**, not safe to gate simply by software title. Require independent source/reference comparison and tests. |

### Newly committed profile layer

- New `pcsx2/EEExecutionProfile.h`: `Standard` and `ExtendedMMU` session modes; `Auto` / forced Standard / forced Extended selection; **conservative Standard default**; exact known boot-disc serial `SCPN-60160` opt-in; boot latch prevents unsafe hot profile changes. Pure functions can be tested independently.
- `pcsx2/VMManager.cpp::UpdateDiscDetails(booting)` resolves the selection **after per-game settings are applied** and **only on initial VM boot**, before executing code. `CPUThread` teardown guard resets the session. ELF or disc changes during the VM DO NOT modify profile. A boot log prints selected mode and identity.
- **Manual override** for no-disc HDD boots, or to A/B the same executable: in the relevant active PCSX2 global or per-game INI:
  ```ini
  [EmuCore]
  EEExecutionProfile = Extended
  ```
  Supported values: `Auto` (default), `Standard`, `Extended`. `Auto` selects Extended only for confirmed disc serial `SCPN-60160`; booting the BB Navigator HDD with no disc does **not** currently provide a reliable serial. Use manual `Extended` for that test.
- `COP0.cpp::SyncActiveTLBMappings` gates the v27 full projection memoization on `EEExecutionProfile::IsExtendedMMU()`, leaving the old full projection path in Standard. `EEActiveTLB.h::PlanEEActiveTLBMappingChanges` now accepts `allowPermissionOnlyShortcut` and treats D-only changes as remove/install in Standard, while Extended retains v25's permission-only shortcut.
- Pure regression cases added to `ee_rec_coherence_policy_tests_20261021.cpp` (selection, serial, override, latch) and `ee_active_tlb_asid_tests_20261018.cpp` (conservative vs optimized D-only diff). These are **prospective tests, not executed**.
- No GUI setting yet. Do not claim the standard profile returns all internals to upstream until every shared path has been assigned a classification and verified.

## Why this is an incremental boundary, not a completed fork-in-one-binary

The previous `psbbn-tlb-boot` branch already modified fundamental COP0/TLB semantics; comparing only commits after v16 would miss those changes. Current shared `SyncActiveTLBMappings` remains active in BOTH profiles, and `recClearPhysical`, `BaseBlocks` link metadata, precise load/store faults, Random, physical page presence and data vTLB constant specialization also remain shared. Only two relatively recent **optimizations** are actually gated by v31.

Most importantly, the current on-demand 4 KiB activation in `recError` is **NOT yet restricted by profile**, to avoid suddenly breaking games which legitimately need guest virtual instruction mapping. A separate proof is required before making Standard strictly refuse mapped-code execution; wrong fallback could be a larger compatibility regression than the changes we're trying to isolate.

Profile selection is host policy, NOT EE CP0 architectural state. No savestate format/version change has been made. State loads rebuild JIT/TLB mappings using the active boot session selection; loading a PSBBN state under Standard vs Extended has not been validated and should be considered unsupported until an explicit compatibility check exists.

## Next implementation phases: required before promising safe Standard

1. **Historical source split by call site**: compare `testing`, the predecessor `psbbn-tlb-boot`, and current behavior for shared COP0/vTLB, `recClearPhysical`/BaseBlocks, interpreter Random/exception paths and savestate. Create a mechanical per-call-site manifest of `Standard common hardware fix`, `Extended experimental`, `trace-only`, `separate DEV9`, `unclassified`. Only gate proven experimental code. Baseline ordinary-game behavioral tests must run against `testing`.
2. **Test bootstrap/profile lifecycle**: run `EEActiveTLBNative`, full `core_test`, known BB Navigator ISO serial and no-disc HDD explicit override. Verify boot latch and safe VM teardown, reload settings and disc changes; do not synthesize unverified HDD autodetection or guess KELF bytes.
3. **Savestate isolation contract**: decide whether to encode selected host profile in state metadata or enforce load-time profile matching and recalculate all JIT/fastmem aliases at a safe reset. Never persist generated host JIT code pointers across state loads.
4. **JIT dispatch policy**: establish deterministic safe Standard behavior for games that actually use 4 KiB guest instruction mappings. Add a full native game regression test matrix before gating all demand-driven 4 KiB activation. Extended should continue to work for Linux/MMU stress, Standard should not silently turn into experimental mode.
5. **Shared invalidation/link separation**: isolate optional cache optimizations from mandatory physical write/SMC coherence, host fastmem translations, link backpatch ownership, and missing DMA/other RAM writer barriers. Baseline and Extended must pass generated-code tests; no cache reuse/reclamation until dependencies and safe unwinds are proven.
6. **Runtime benchmarking**: A/B Standard and Extended **on the same build and guest workload**; verify root mount/userspace advancement, guest faults/BD/EPC, FPS, blocksEvicted, repeatSourceFingerprints, `syncUs`, `liveHostBytes`, `cacheUsed`, JIT resets and ordinary retail-game compatibility. Do not compare only total wall time or noisy log volume.

No Windows/MSVC build or emulator run completed as part of v31. No new guest Ghidra metadata was discovered.
