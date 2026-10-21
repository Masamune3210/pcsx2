# PSBBN JIT return provenance and missing store Modification check (v20)

## Continue here

Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
Base: `099f64d471a9925af5bb648f1e69216db77aa66e`.
Continues [v19](PSBBN_CODEX_JIT_CONTROL_FLOW_WITNESS_20261021_v19.md).
[Bounded run/state evidence](PSBBN_JIT_RETURN_RUN_EVIDENCE_20261021_v20.json).

**Source-confirmed correction, not yet compiled or runtime validated:** fine-mode
JIT stores bypassed the interpreter-only EntryLo.D check. This can write a valid
read-only/COW mapping without first giving the guest its Modification exception.
The new run proves a bad return into a mapped stack address; it does not prove the
specific earlier overwrite was caused by this permission gap.

## New user-run evidence

`G:/Users/Johnny/Documents/PCSX2/logs/emulog.txt` identifies
`PCSX2 v2.9.92-226-g099f64d47`, with 49,801 lines and 4,513,069 bytes.
SHA256: `f8bad2c67c38164b8f50763f6edf5e2362db12601646a46b42531a080d8886b1`.
Matching kernel log confirms root mount. Four correct journal samples, seven
boundary-fallback messages, zero unarmed-vTLB messages and zero old 403FC0 faults.

The first unknown compile is line **48726**, PC **7FFF76A0**, opcode **00000005**.
JIT, architectural TLB and direct-vmap views all report physical **001DB6A0** and
the same word. All four compile groups agree across those views. This is evidence
of wrong incoming control flow, not a target-fetch translation mismatch.

| Flow sequence | Recorded transfer | Registers after transfer/delay slot |
|---|---|---|
| 29137 | ERET 8001F428 -> 2AB59280 | ASID07, SP7FFF7600, RA2AB591FC, GP2AC26400; target initially unmapped |
| 29138 | ERET 80000058 -> 2AB59280 | Same SP/RA/GP; target now physical01E4E280 |
| 29139 | JR 2AB59288 -> 2AB591FC | SP7FFF7600, RA2AB591FC, GP2AC26400 |
| 29140 | JR 2AB59204 -> 7FFF76A0 | SP7FFF7630, RA7FFF76A0, GP2AAB81A0 |

For sequence29140 compiled/current source backing both equal **01E4E204**.
Later snapshot bytes at this code region decode as:

```text
2AB591FC  8FBC0010  LW    gp,10(sp)
2AB59200  8FBF0024  LW    ra,24(sp)
2AB59204  03E00008  JR    ra
2AB59208  27BD0030  ADDIU sp,sp,30
```

This brackets the RA/GP change to a short epilogue if its saved code matches the
executed block. It suggests return-slot **7FFF7624**, using SP before the delay
slot. The v19 witness did not record the actual load result or slot bytes. Do not
claim it proved a bad LW implementation or the original slot writer.

First-group raw TLB slot9: Mask=00000000, Hi=7FFF6107, Lo0=0000F51A,
Lo1=000076DA. The odd page is **valid, D=0**, PFN001DB000. Reads remain legal;
stores require Modification before changing the page. A read-only entry alone
does not establish that this particular page was shared across processes.

## Source defect and correction

Before this change `COP0.cpp::EEInterpreterTLBStoreNeedsModification` returned
false for the recompiler. `recVTLB.cpp` generated fastmem, direct-vmap and constant
pointer writes without checking EntryLo.D. Fastmem host code-page protection is a
different mechanism and does not supply guest COW permissions.

- `COP0.cpp/.h`: expose `EETLBStoreNeedsModification`, querying the existing active
  ASID/global projection independently of CPU provider. Preserve the interpreter
  wrapper and its existing call sites.
- `EEActiveTLB.h`: shared predicate explicitly exempts fixed KSEG0/1, including
  overlapping malformed TLB entries. Scratchpad remains exempt; absent/invalid
  mappings remain ordinary miss cases. Add wrapped base-plus-signed-immediate
  calculation, with SQ/SQC2 alignment and original merged-store fault address.
- `iR5900.cpp::recompileNextInstruction`: when compiling fine guest-TLB mode,
  emit a pre-store helper for all **11 `IS_STORE` classes**, after the existing
  full precise-memory flush/PC/BD/armed setup and before `opcode.recompile()`.
  This covers scalar, FPU/vector, constant, fastmem and read-modify-write stores.
- `recCheckTLBStore`: normal return changes no guest registers. Denial clears the
  armed marker, calls `cpuTlbModification` and immediately exits through the same
  recompiler exit mechanism as precise TLBL/TLBS. The failed store and any merged
  load/store body do not execute; the guest controls fault repair and retry.

The correction is independent of tracing. Native 64KiB dispatch emits no new
permission helper. The existing first fine-mode transition discards native code;
internal cache purges retain fine mode. No new blanket flush or map rebuild was
added: the helper reuses the pre-existing full memory-op flush and performs a
single active-map lookup. Fine-mode stores gain a host call; performance is
unmeasured. Native-mode Dirty enforcement remains outside this change.

No guest addresses, disk repairs, replacement register values, mappings or opcode
suppression were added. Existing BIOS/PSBBN/HDD fixes, fastmem and savestates remain.

## Bounded stack witness

The existing COP0 trace opt-in now emits post-operation witnesses for
LW/SW/LD/SD with base SP and target/source RA or GP. After the actual operation,
one full diagnostic flush captures exact slot-aware PC, emitted/current opcode,
EA, actual low64 register value, EntryHi/Status, and safe 4/8-byte RAM shadows.

Direct-vmap and existing fastmem mapping-table resolution must yield canonical
main RAM before it is read. No handler is called and no guest fastmem pointer is
dereferenced. Fastmem table agreement does **not** prove host alias installation.
Raw first TLB match and total matches are retained for independent comparison.

A separate64-record ring dumps the last32 `EE-JIT-STACK` records at the existing
first-four unknown-compile groups. A shared `order` field correlates memory and
flow events; cycle samples precede block scheduler charge. Epoch/reset behavior
is unchanged from v19. No diagnostics are serialized. Tracing disabled during
compilation emits no new stack observer calls; previously emitted calls retain
their runtime guard. Enabled flushes can perturb allocation/timing or mask a
register-allocation bug. This is not a complete memory-write history: other
register bases, widths, interpreter execution, kernel copies and DMA are omitted.

## Scheduled states and evidence limits

All four archives with prefix **PSBBN-auto-1792560903592519** were successfully
opened and hashed read-only from `H:/Emulation Files/pcsx2/sstates`. No state was
loaded into PCSX2. Full hashes, CPU fields, screenshots and bounded RAM windows
are in the receipt. 001 PC8000DAD0 precedes userspace; 002/003 PC800185D8 and
004 PC800185F0 are in the later kernel idle-loop region. Their cycles are
1820453637, 4482238550, 7434310551 and 10386382549 respectively.

At the later002 snapshot, physical001DB624 holds **00409540** and001DB6A0 holds
**2AC26400**, differing from what might be assumed from the earlier bad return
and unknown compile. Later RAM must not substitute for runtime slot contents.
The captures do not prove successful userspace boot or a stopped CPU.

## Actual verification and next run

- Source whitespace checks, JSON parsing, fresh emulator/kernel log hashes and
  all four archive hashes checked. No production image/state writes occurred.
- Five prospective cases were written before the correction: captured read-only
  return stack/ASID separation, fixed aliases despite overlapping TLB entries,
  signed/wrapped EA, SQ/SQC2 alignment, merged-store original fault address.
  Existing Dirty/COW/invalid/global/scratchpad cases remain in the same suite.
- Independent read-only review checked all store metadata, flush/ABI/PC/BD exit,
  address literals, observer formats/bounds and reset lifetimes. No actionable
  finding remained.
- **No C++ build, tests, generated-code execution, emulator launch, Windows native
  validation or PSBBN recovery demonstration.** Red/green tests were not observed;
  explicit user instructions prohibit compilation/test runs without permission.
  Tests do not yet demonstrate actual no-write behavior or precise EPC/BD at
  runtime. Native game/BIOS/savestate/performance regression checks remain pending.

When the user supplies or explicitly authorizes another build/run, use native
`PCSX2_qt.slnx`, `Release AVX2|x64`, and cold boot the same disposable image/config.
Retain existing COP0 tracing for provenance. First check whether `code=4`
Modification faults now occur before stack writes and the guest repairs/retries.
Confirm root/journal success and actual later userspace progress. If unknown
compilation persists, correlate `EE-JIT-STACK` and `EE-JIT-FLOW` by order, comparing
the RA load with both shadows and the earlier store to its slot. Do not assume
that any new fault alone proves correct COW isolation or full PSBBN boot.

No new Ghidra function names/types/signatures were established.
