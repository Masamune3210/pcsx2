# PSBBN v40: witnessed unchanged physical code repeatedly evicted on ASID/TLB transitions

Repository: `Masamune3210/pcsx2`
Active branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Predecessors: v37 measured actual block retirement, v38 implemented source/data observations, v39 bounded/source-audited the witness.
Evidence file: user-uploaded **`emulog(5).zip`**, containing `emulog.txt` (161,735,723 bytes, approximately 602,619 lines). Log NOT committed to Git. Windows build `PCSX2 v2.9.92-346-ga8a1560c6`.

## Verified configuration and run lifetime

- `EE execution profile: ExtendedMMU (setting=Extended, boot disc serial=20020207-164243)`.
- `EE-JIT-WITNESS-CONFIG enabled=1 watchPA=01ed8000 watchVA=0fb6d000 maxEvents=2048 maxLive=512`.
- **Old per-block eviction tracer was also enabled with `limit=0` (unlimited)**, creating **479,939** verbose `EE-JIT-EVICT` records and greatly perturbing wall-clock/CPU timing. Never benchmark FPS using this log.
- Four `EE/iR5900 Recompiler Reset` entries: first JIT startup ~17.24s; mode transition to fine 4 KiB guest dispatch ~32.39s (`resetReasons=8`); and **two true 64 MiB arena-exhaustion resets** ~48.94s and ~63.98s (`resetReasons=4`). At exhaustion, generated ~66.94–66.97 MB in ~199–201k compilations per generation, preserving only ~2.9–3.0 MB of live host bytes in 9,195–9,623 live blocks. The final generation ends in provider shutdown ~70.92s.
- The witness cap of 2,048 detailed events was exhausted **from 32.5696s to 34.1505s**, a 1.58s early window in **generation 2 only**, not the whole 54-second active session. Lifetime `matched=2076 printed=2048 dropped=0`. Do not extrapolate the sample to gen3/gen4.

## Source-vs-mapping witness: the main finding

Exactly **2,048** `EE-JIT-WITNESS` and **2,048** `EE-JIT-WITNESS-PAGE` records, **zero** `EE-JIT-WITNESS-DATA` records. Each complete source snapshot contains one guest virtual source page. Every witnessed physical-page hash at retirement equals its compilation hash (2,048/2,048 `bytesSame=1`).

| Field / classification | Count |
|---|---:|
| `same-bytes-virtual-unmapped` | **2,024** |
| `incomplete` from `virtual-clear`, with mapping comparison intentionally disabled | **24** |
| `physical-bytes-changed` | **0** |
| `same-bytes-mapping-changed` or `same-bytes-mapping-same` | **0** |
| Caused by `asid-projection` / `mappingAction=remove` | **1,412** |
| Caused by `tlb-projection` / `mappingAction=remove` | **612** |
| `sourceComplete=1`, `pages=1`, `compileMode=nowMode=1` | **2,048** each |
| `dataRefs=0`, `dataOverflow=0`, `translationChanging=0` | **2,048** each |
| `oldWrite=0`, `nowWrite=0` | **2,048** each |
| Compiling ASID differs from current ASID at eviction | **1,412** |

**Interpretation, carefully scoped:** This **watched read-only code page** is repeatedly compiled and removed without *sampled instruction bytes changing between compile and each eviction*. It is predominantly evicted because the original virtual address is no longer mapped **under the current ASID** during `SyncActiveTLBMappings`. The unmapped result is **expected** while the previous ASID's mapping is being removed; it does NOT mean no mappings or unchanged translation when the old ASID returns. For TLB-projection retirements, current ASID can match the compilation ASID, and the selected VPN can still be temporarily absent. The classification describes current map state, not a comprehensive proof of future execution safety.

## Even stronger: identical source identities across recompilations

- 2,048 detailed events correspond to **71 distinct physical block starts** on physical page `0x01ED8000`.
- Treat **(physical block start, guest instruction count)** as a pair, because the recompiler may generate differently sized blocks from the same starting PA. There are **92 distinct (PA, words) combinations**.
- **All 92 combinations have exactly one compilation fingerprint each** across the captured instances; there is no observed per-identity instruction-content change. This corrects the misleading impression from grouping only by PA, where some starts have 2–4 distinct fingerprints **because their block lengths differ**, not because equal-length source bytes changed.
- **72 of the 92 identities recur** (2,028 events), and **71 of those recur across multiple compilation ASIDs** (2,026 events). A stable identity can recur across **11 compiling ASIDs**.
- All **2,048 host-code pointers** in the witness sample are distinct. These are successive emitted host blocks, not duplicate reports of one metadata removal.
- Example: PA `0x01ED8174`, **3 instruction words**: **43** distinct emitted block retirements across **11** compiling ASIDs with the same instruction fingerprint; PA `0x01ED8138`, 2 words: 42 retirements, 11 ASIDs and one fingerprint.
- No watched block recorded a constant-data dependency through the three instrumented `recVTLB` const-address access families. This is **not** proof that arbitrary generated code is translation-independent; branch/link patching, dynamic data translation, helper addresses, return/unwind state and instruction-fetch location may remain sensitive.

## All-block eviction context, NOT the same as witness sampling

All-generation log contains 479,939 detailed block-retirement entries: `asid-projection=258,486`, `tlb-projection=170,523`, `arena-reset=26,542`, `host-write-protection=11,530`, `provider-shutdown=7,565`, `virtual-clear=4,861`, plus small other categories. **Do not count arena-reset/provider-shutdown records as individual mapping-driven invalidation requests.** The sample's physical-byte equality cannot be assumed for all 479,939 records.

Relevant JIT generation-2 exhausted code arena before its reset: `blockCompiles=201134 compiledBytes=66943476 cacheUsed=67043332 liveBlocks=9623 liveHostBytes=3037794 linkRecords=264550 repeatSourceFingerprints=108445 linkWrites=11363528`. Generation 3: `blockCompiles=199497 compiledBytes=66966037 cacheUsed=67043528 liveBlocks=9195 liveHostBytes=2919767 linkRecords=261556 repeatSourceFingerprints=106102 linkWrites=9986993`. Source fingerprints are a diagnostic approximation, not code reuse keys.

## What is now established vs NOT established

**Established:** Frequent ASID/TLB transitions repeatedly retire physical block metadata and force re-emission of unchanged physical source, even for the same VA/PA, same length and multiple ASIDs. The current invalidation route destroys physical-cache metadata while changing virtual map aliases. For the watched sample the underlying physical contents are not responsible for the vast majority of eviction events. There is a concrete retention opportunity **if safety barriers can be proven**.

**Not established:** Code generated under one ASID is safe to run in another; same bytes imply same generated output or helper state; incoming and outgoing jump patches and return-address owners are safe; all instruction source PFNs and constant-data dependencies are known; physical writes/DMA can be ignored; or any cache policy should be changed now. A 64-bit noncryptographic hash is not a substitute for full source equality/provenance.

**Important negative result:** No witness reported `same-bytes-mapping-same` at eviction, because old virtual mappings are being removed under the *current* ASID. A preservation/reuse scheme would need to distinguish **dormant code stored behind mapping/context generation guards** from **executable code in the current guest mapping**, rather than simply skipping `recClearPhysical` on ASID change.

## Next branch objective

1. **Prove host jump ownership and address-space-specific execution dependencies before implementation of retained code.** Instrument bounded `BaseBlocks` outgoing link site ownership (source block and host address interval, cache generation), patch target and life state, and map-removal cause. Quantify how much of ~10–11 million per-generation link patch writes address retired code vs sources still executable or potentially returning.
2. **Design (do not blindly enable) Extended-only shadow/dormant code cache** keyed by at least compile virtual PC, physical source PFNs/content, relevant ASID/permissions, mode, constant-data and auxiliary guest-address assumptions. Require generation/translation guard to reactivate after the guest mapping is installed and rechecked. A mere virtual unmap must not expose code from a different ASID or stale embedded pointers.
3. Preserve physical RAM write-barrier invalidation, including DMA/IOP writes, and all JIT reset/savestate/host return constraints. Do not enlarge arena merely to hide recompile pressure.
4. **Next diagnostic run should disable the old unlimited `EEJitEvictionTrace`** (or set a small cap). Witness output alone is sufficient for a short targeted sample, and all runtime comparisons should use separate trace-off performance sessions.
5. Keep the personal-fork Standard behavior intact. Extended remains experimental; do not port untested cache policy into Standard.
6. If a new test is performed, sample a later generation or a second hot PFN with bounded output to test whether the early generation-2 witness conclusion generalizes. No more complete unlimited per-block dumps unless a concrete question requires them.

**This session:** log decoded and analyzed, docs updated. No emulator source changes, Windows build, Ghidra work, or tests. Do not claim a speed improvement.
