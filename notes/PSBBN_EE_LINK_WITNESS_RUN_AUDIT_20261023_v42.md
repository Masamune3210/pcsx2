# PSBBN v42: emulog(6).zip run audit and missing link-owner setting

Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
Predecessors: v40 source-witness evidence and v41 link-ownership instrumentation.
Evidence: user-uploaded `emulog(6).zip` -> `emulog.txt`, 72,907,890 bytes, 291,468 lines. The log is not committed.
Build: **`PCSX2 v2.9.92-356-gf36316aad`**, matching the committed v41 source. Extended MMU confirmed.
Status: **log analyzed; this was NOT a usable v41 link-ownership test because its opt-in setting was not enabled**. No code changes, rebase, build or PSBBN execution performed in this analysis.

## Test configuration: primary blocker

At 14.004s the log prints:
- `EE-JIT-WITNESS-CONFIG enabled=1 watchPA=01ed8000 watchVA=0fb6d000 maxEvents=2048 maxLive=512`
- `EE-JIT-EVICT-CONFIG enabled=1 limit=0 (0 means unlimited)`
- No `EE-JIT-LINK-OWNER-CONFIG`, `EE-JIT-LINK-OWNER`, `EE-JIT-LINK-REPATCH`, or `EE-JIT-LINK-SAMPLE` anywhere in the capture.

The binary **does include v41** (git suffix `f36316aad`); there is no evidence of an old build. The missing opt-in setting is the likely explanation. It is read by `Host::GetBoolSettingValue("EmuCore", "EEJitLinkOwnershipWitness", false)` on first recompiler reset, and is enabled only for Extended MMU. This run therefore cannot prove target patch ownership or native return safety.

The unlimited older eviction tracer printed **206,363 per-block records**, again perturbing wall-clock measurements. The whole log has 291,468 lines, so it is not suitable for FPS or compilation-throughput comparison.

## Fresh source-witness evidence

Generation 3 detailed window: **68.4929 to 70.1675 seconds** of logged elapsed time, all 2,048 records in that generation. There are 2,048 corresponding source-page comparisons and no data-reference lines.

- **2,048/2,048** source pages have `bytesSame=1`. They are read-only in this sample (`oldWrite=0`, `nowWrite=0`).
- **1,170** `asid-projection` removals and **847** `tlb-projection` removals yield `same-bytes-virtual-unmapped` (**2,017**). These unmapped states are snapshots during mapping removal, not proof of future mapping identity.
- **31** `virtual-clear` records are `incomplete` because the diagnostic explicitly does not reconstruct a current guest mapping for that cause.
- **Zero** witnessed physical instruction-byte differences; source captures complete in all 2,048 events; one source page per block; zero watched constant-data references; compile and retirement mode both fine (1).
- **95** distinct `(blockPA, words)` identities. All **95** retain exactly one old source hash within this bounded sample. **78** occur repeatedly; **77** are seen across multiple compilation ASIDs, at most **9** ASIDs for an identity.
- These results *reproduce and strengthen the bounded v40 observation* in a later run. Do not extrapolate source equality or absence of constant-data dependencies to every block.

## Cache generations and reset reasons

There are **three** EE recompiler resets, and **none** uses arena exhaustion (`resetReasons=4`):
1. 14.005s: initial startup (`resetReasons=3`); `cacheUsed=0`.
2. 52.7185s: CPU provider/VM reset (`resetReasons=2`), after 1,874 block compiles and 485,825 cached bytes.
3. 68.2299s: native -> fine 4-KiB guest-dispatch transition (`resetReasons=8`), after 20,444 block compiles and 5,808,715 cached bytes.

Subsequent generation 3 reached 65,536 and 131,072 compile milestones at 74.631s and 80.629s, respectively, but the log ends in provider shutdown (~91.966s), not cache exhaustion. The generation-3 end has 9,473 live blocks retired for shutdown. No FPS/performance conclusion.

### All-block eviction tracer (distinct from 2,048 source samples)

Exactly **206,363** per-block entries:
- `asid-projection`: 99,128
- `tlb-projection`: 74,162
- `host-write-protection`: 11,497
- `provider-shutdown`: 9,473
- `arena-reset`: 8,629
- `virtual-clear`: 2,576
- `projection-reset`: 725
- `manual-reprotect`: 163
- `manual-integrity`: 7
- `overlap-recompile`: 3

The 99,128 + 74,162 = **173,290** ASID/TLB projection retirements across this run are not 173,290 independently proven reusable blocks. Provider-shutdown and arena-reset are teardown records, not separate mapping-driven invalidation requests.

## Next actionable test

Build is already verified by the matching log banner; no rebuild is required to enable the existing option. **In the active PCSX2 configuration** put:

```ini
[EmuCore]
EEJitLinkOwnershipWitness = true
EEJitEvictionTrace = false
EEJitSourceWitness = false
```

Preserve other existing keys in `[EmuCore]`; do not create a second section if one already exists. **Restart VM/emulation fully** because these flags are latched at the first JIT reset. Confirm that the next log contains `EE-JIT-LINK-OWNER-CONFIG enabled=1` and does not contain `EE-JIT-EVICT-CONFIG enabled=1` before any performance-sensitive comparisons.

Collect `EE-JIT-LINK-OWNER`, `EE-JIT-LINK-REPATCH` and bounded `EE-JIT-LINK-SAMPLE` lines through a cache generation and a reset/shutdown. A link ledger's `retired` state still does not prove absence/presence of a live native return address. Do not enable dormant reuse or weaken physical write invalidation yet.

## Handoff

- Keep this document **on the PSBBN branch**. Do not put it in `testing` or rebase the branch simply to get unrelated upstream changes.
- v41 link ownership code exists at `f36316aad`; this audit makes no emulator code modifications.
- Next task is the **configuration-corrected v41 run**, followed by link-ownership classification and a separate active-return/quiescence proof if needed.
