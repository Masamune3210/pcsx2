# PSBBN JIT: static links and instruction translation (v18)

## Checkpoint

Continue on existing branch `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
Base: `97c6571a212734d4066b4e8d972a6df13a5a8017`.
This continues [v17](PSBBN_CODEX_JIT_PHYSICAL_CACHE_INVALIDATION_20261021_v17.md)
using the user's actual follow-up log and a scheduled snapshot.

**New source correction and 13 added regression cases are uncompiled and unrun.**
The supplied log identifies the earlier v17 executable; it does not validate this
new correction. No agent build, native test run, emulator launch, savestate load,
HDD modification or guest binary change occurred.

## Actual v17 run

Inputs and bounded extraction are recorded in
[the evidence JSON](PSBBN_JIT_STATIC_LINK_FETCH_EVIDENCE_20261021_v18.json).
Large logs, RAM and savestate archives were not imported into Git.

- `G:/Users/Johnny/Documents/PCSX2/logs/emulog.txt`: 15,883,195 bytes,
  **180,471 lines**, identifies `PCSX2 v2.9.92-224-g97c6571a2`.
  SHA256 `e704f456d5400acfa9c85e667ec23fcb704c402369d564d5fed5f163dce23666`.
- Matching `psbbn-kernel-log.txt`: 4,318 bytes. Independently confirms transaction
  log checking, root mounted read-only and unused kernel memory freed.
- Journal witness lines **43195-43198**: mapped, writable store/readback of
  `C0005290` at `C00052DC`, physical `002EF2DC`, remains correct.
- Fine dispatch activates at line **44265**, PC `0FB643C0`.
- Three cross-page fallback messages remain at **44632, 44655, 44741**.
- **Zero** unrecognized-instruction entries; **zero** old `addr: 6f60` faults.
  These older symptoms are absent during this run, not proof of complete boot.
- There are **50** unarmed `TLB Miss, pc=...` diagnostics. The normal logger caps
  this channel, so 50 is not an assertion that no subsequent bad reads occurred.
- The final repeating group contains **132,053** TLBL entries at sampled next-PC
  `00403FC0`, address `7FFF8220`, Status `00010C11`, code `8`, from lines
  **48293-180458**. The run was paused and shut down.

The failure therefore persists with a changed final location. Do not describe v17
as a full PSBBN boot fix or infer that the HDD caused this userspace failure.

## Earliest focused transition

Read the actual log before focusing on the repeated final fault:

| Lines | Observation |
|---|---|
| 48242-48243 | Kernel store faults at sampled PC `801A472C`, address `2AAA9000` |
| 48244 | User code fetch/refill at `2AB2E020` |
| 48245-48246 | Store fault at sampled next-PC `2AB2E068`, address `7FFF73D1`, then branch-delay notice |
| 48247-48292 | Unarmed virtual reads at `00404040..0040406C`, repeatedly using those PCs during code inspection |
| 48293 onward | First `00403FC0 / 7FFF8220` load fault, followed by a code refill and kernel writes while handling the stack fault, then the long loop |

The final log PC uses the next-PC convention. The scheduled capture corroborates
faulting EPC **00403FBC**, rather than identifying the branch at 00403FC0 as the
faulting load.

## Read-only scheduled-state evidence

The log names two existing captures. Snapshot 002 was inspected as a ZIP; it was
**not loaded into PCSX2**:
`H:/Emulation Files/pcsx2/sstates/PSBBN-auto-1792558186919420-002.p2s`.
SHA256 `88ba2c1c5f3092b0de4dd54f3470f04a3899aa4fa5dfa5bc382eb072ac472262`.

`SaveState.cpp:177-190` and `R5900.h` define the serialized order. The 32-byte
`cpuRegs` tag ends at offset **333**; CPU PC is at +680 and CP0 at +544.
The `Cycles` tag is at **4455**. Working backward past two saved bools, the
976-byte cached descriptor structure and 48 16-byte architectural TLB entries
places the TLB at **2709**. Both saved OSD parameters are declared `bool` in
`ps2/BiosTools.cpp`. The saved PC **80019EE0** matches log capture 002.

At capture time: ASID **04**, EPC **00403FBC**, BadVAddr **7FFF8220**, Cause **8**,
Status **10010C01**. Relevant architectural entries and bytes:

| Virtual address | Slot / selected Lo | Valid | Physical / instruction |
|---|---|---|---|
| 00403FBC | 0 / 0007AB1A | yes | 01EACFBC: `8FBC0020`, `lw gp,0x20(sp)` |
| 00403FC0 | 0 / 0007AB1A | yes | 01EACFC0: `1440001F`, `bne v0,zero,00404040` |
| 00404040 | 32 / 00000010 | **no** | No architectural physical instruction |
| 7FFF8220 | 27 / 00000010 | **no** | No valid stack translation |
| 2AB2E064 | 35 / 0007A0DA | yes | 01E83064: `A0880000`, delay-slot byte store |

The observed load EA and opcode imply a base SP of **7FFF8200** at that fault.
This is an inference from EPC/opcode/EA, not a claim to have traced the writer of
SP or reconstructed every saved user GPR. The capture is later than the first
divergence. It cannot alone prove which opcode or branch executed earlier.

Physical RAM at numerical addresses `00403FBC` and `00404040` contains zero words.
Those physical bytes are not the guest's valid translated code. The latter page
can be physically readable while the guest virtual instruction address is invalid.

An additional layout cross-check found 45 stored cached-TLB descriptor tuples,
all matching architectural entries, versus 47 architectural cached-entry candidates.
The two missing PFN pairs are recorded in the JSON. The initial strict equality
cross-check exposed this difference; it was retained as a separate observation,
not used to fabricate missing mappings or asserted as this failure's cause.

## Source-confirmed fetch bypass and correction

Three static-branch paths called `recBlocks.Link(HWADDR(target), ...)`: ordinary
immediate branches, timeout-loop exits and short page-split concatenation.
An unmapped fine-mode page retains identity `HWADDR` arithmetic; its physical
number may have RAM backing. A direct physical link can therefore reach a block
or `JITCompile` without consulting the guest dispatch table's fault entry.
Pending links are also hazardous: a target without block metadata has nothing
for `BaseBlocks::Remove` to unlink, and later physical compilation can patch a
branch after its virtual target has changed mapping.

`recRecompile` and boundary-slot checks separately accepted `PSM(HWADDR(pc))` as
sufficient proof of an instruction mapping. This is false for low unmapped VPNs.
Together these are a source-backed mechanism consistent with the observed
unarmed inspection of 00404040. The exact first runtime branch path and the cause
of the bad user SP remain unproven.

Changes in `EERecCodeCache.h` and `iR5900.cpp`:

- Fine-mode static targets in another TLB-managed virtual page go through
  `DispatcherReg`, without registering a pending physical link. Same-page targets,
  fixed KSEG0/1 targets and the ordinary native 64 KiB policy retain direct links.
  Source page comparison uses saved **virtual block start**, not physical metadata
  or the compiler PC advanced past a delay slot.
- Instruction-pointer lookup checks the actual fine-table unmapped bias before
  accepting physical bytes. Entry validation precedes opcode/debugger inspection;
  the scanner also validates before breakpoint helpers. Delay-slot/timeout peeks,
  backwards analysis and instruction emission use the same accessor.
- Missing boundary slots execute through the existing runtime interpreter fallback,
  including `DUMP_BLOCKS` builds; compilation does not inject their guest exceptions.
- First activation of fine dispatch requests a complete JIT cache reset before
  reentry. Links emitted under native assumptions cannot survive that transition.
  Internal cache exhaustion preserves fine mode. The external CPU-provider Reset
  callback retains its prior native-policy behavior; it also serves settings and
  savestate cache clearing, so this is **not** proof of correct mode restoration
  across savestates.

No runtime mode comparison was inserted before a signed scheduler branch: selection
happens during compilation and changes the existing branch displacement. Native
generated hot paths retain their original linking policy. Fine-mode cross-page
branches now incur virtual dispatch; that necessary cost is not benchmarked here.

## Verification and remaining work

Thirteen new prospective helper regressions bring this file to **20 cases**:
physical invalidation/link behavior, current and pending cross-page targets,
same-page/native/fixed-kernel links, boundary source identity, missing versus
readable instruction backing and mode/cache-reset policy. They share production
helpers and real `BaseBlocks`; they have **not** been compiled or executed.

Actual checks: input hashing, full log counts, read-only snapshot decoding,
architectural/physical opcode comparison, JSON parse checks, `git diff --check`,
and independent read-only source review. Review caught the preexisting native-link
transition hazard, an optional DUMP_BLOCKS fallback omission, and reset-policy
wording; the implementation and documented limits were adjusted and re-reviewed.

Next, when explicitly authorized, run the existing native harness with
`--gtest_filter=EERecCodeCache.*:EERecDispatchMode.*:EEActiveTLB.*`, build the user's
`PCSX2_qt.slnx` in `Release AVX2|x64`, then cold boot the same disposable image clone.
See v17 for the one-command-per-line build recipe. Verify root/journal behavior,
proper guest fetch fault/refill for an unmapped low code page, no unarmed compiler
reads, and actual later userspace progress. Do not equate replacing one fault loop
with another with successful boot.

Still required: generated-code execution, accurate branch/BD/EPC fallback tests,
first SP writer/control-flow trace, full savestate mode validation, BIOS/retail
compatibility and performance checks. Physical-only sharing of code compiled at
different virtual aliases, and mapped delay slots on noncontiguous physical pages,
retain broader identity/protection assumptions outside this bounded correction.
No new guest function names or Ghidra datatype/signature annotations were proven.
