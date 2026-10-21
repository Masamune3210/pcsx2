# PSBBN JIT control-flow witness (v19)

## Continue here

Existing branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
Base: `b8a917d02f2a5fc09090daf4f5cc77142709e5f3`.
This continues [v18](PSBBN_CODEX_JIT_STATIC_LINK_FETCH_20261021_v18.md).

**The new change is bounded diagnostic instrumentation, not another emulation
correction. It has not been compiled or executed.** The user supplied the v18
run and three state paths. No agent build, C++ test run, emulator launch, state
load, guest/HDD write or Ghidra/decompiler change occurred.

## Research question and actual run

What first transfers control into bad userspace code after v18 removes the previous
fault loop? The current log does not contain the source PC of that transfer.

[Bounded evidence and input hashes](PSBBN_JIT_CONTROL_FLOW_RUN_EVIDENCE_20261021_v19.json)
record the supplied `G:/Users/Johnny/Documents/PCSX2/logs/emulog.txt`:

- Version `PCSX2 v2.9.92-225-gb8a917d02`; **49,711 lines**, **4,490,964 bytes**.
- SHA256 `578be68369789613627bcd8fe074fee28645d415abb90ac164ac5882654edbb5`.
- Root mount/freeing init memory is independently confirmed in the matching kernel
  log. Four journal witness records at lines **43195-43198** still show correct
  `C0005290` store/readback at `C00052DC`.
- Fine dispatch activates at **44265**, followed by the expected one-time cache
  purge at **44266**.
- **Zero** unarmed `TLB Miss, pc=` messages and **zero** former `403FC0` faults.
- Seven cross-page fallback compilation messages: the earlier linker boundary and
  later `2AB24FFC / 2AB25000` boundaries in more than one ASID.
- Four `EE: Unrecognized op` records at **48725, 48728, 48729, 48733** contain
  `00000005`, `7665642F`, and `000732C5` twice. Word `7665642F` contains `/dev` bytes.
- The preceding code refill is **48723**, sampled next-PC `2AB59284`, address
  `2AB59280`. Subsequent memory faults at **48731, 48735** sample next-PC
  `7FFF7750`, address `00006E88`. The normal convention suggests a faulting
  instruction at `7FFF774C`; the incoming branch and exact exception context are
  not present in this log.

### Do not overinterpret these messages

`x86/iR5900Misc.cpp::recUnknown` prints **during compilation**. These records prove
the compiler processed unrecognized words; they do not individually establish that
each word executed. Backwards-analysis warnings also describe compilation.

`Branch fb89960 in delay slot!` prints `cpuRegs.code`, not the branch PC. Treat
`0FB89960` there as an opcode/data word. It must not become an invented source-PC
claim in the next investigation.

There is no source-backed diagnosis yet of the first incorrect return register,
stack write, virtual alias reuse or exception return. Adding another architectural
guard based only on these messages would obscure that distinction.

## Scheduled-state comparison and availability

The supplied prefix is `PSBBN-auto-1792559553114767` in
`H:/Emulation Files/pcsx2/sstates`.

Captures **002** and **003** were successfully opened as ZIPs and decoded earlier
in this turn. They were never loaded into PCSX2. Captured hashes:

- 002: `ee36fa7776fa10228532cc0b6fcd19b55064eec98c5a0ab649b6b8667ad6746d`.
- 003: `4da1edf8c544973d6454ab9b104233fd565fb36e3ac1f42b2aa7598f1d2b9d88`.

Both have CPU PC **800185F0**, GP **80016000**, and `[GP+14] == 0`.
The captured instructions load that word, branch back through a polling loop when
zero, and call the scheduler path otherwise. This is consistent with the known
kernel idle-loop region, not proof of a stopped CPU.

From 002 to 003, EE cycles advance **2,952,072,003** and **28** EE RAM pages change.
Cause changes from `00000800` to `00000400`; saved EPC also differs. Screenshot
hashes are identical. This establishes differing virtual CPU/memory and
interrupt-related state, not successful userspace boot or a diagnosed hang.

All three paths later disappeared from the original directory during this turn.
**001 was never opened successfully.** Its log capture PC is known, but its bytes,
hash and state were not inspected. The evidence JSON explicitly retains 002/003
observations from the earlier successful reads and marks current file absence.
No raw ZIP copies were saved, and no later hash recheck of those archives is claimed.

## Instrumentation added

The existing opt-in is **`EmuConfig.Trace.Enabled && EmuConfig.Trace.EE.cop0`**.
Enable it before a cold boot so the affected blocks are compiled with observers.
No new setting, environment variable, guest address trigger or guest mutation exists.

- `iR5900.cpp::SetBranchReg`: after its existing full GPR flush, records JR/JALR
  source/target. The source is the compile-time virtual branch PC, with the physical
  source captured when code is emitted and its current physical translation sampled
  when the branch runs. No additional register flush was added.
- `iCOP0.cpp::recERET`: records the completed interpreter-backed ERET target after
  its existing flush/call. Boundary fallback similarly records its result after the
  existing branch/slot helper returns.
- Host-only **64-record ring**, without per-transfer printing. It records sequence,
  cycle before the block's scheduler charge, kind, target/JIT readability, EntryHi,
  Status, RA, SP, GP and T9. Ordinary indirect and ERET records require effective
  user mode with EXL/ERL clear. Boundary results can include a kernel exception target.
- `recUnknown` retains its existing message and calls a compile observer. For the
  first **four groups per epoch**, it prints the exact slot-aware compile PC,
  virtual block start, dispatcher-entry/register samples, JIT versus architectural
  versus direct-vmap words, up to two raw matching TLB entries, and the most recent
  **16** ring records.
- Kind **0** = JR/JALR, **1** = ERET, **2** = boundary fallback result.
  `targetJITReadable` describes JIT backing, not architectural validity by itself.
  These are partial control-flow records; ordinary static branches and most
  interpreter execution are not a complete instruction history.
- CPU trace reset and external CPU-provider cache reset start new epochs. Internal
  code-cache purges retain history, including the first fine-dispatch transition.
  No history or diagnostic state is serialized into savestates.

All generated observers are selected during compilation. A cold boot with tracing
disabled emits no added observer calls. Previously emitted calls still check the
flag at runtime. Enabled diagnostics add host-call overhead; timing/performance
effects have not been measured. Existing scheduler flag generation stays after
the new calls. No branch target, opcode, fault result or mapping is replaced.

## Verification and next action

Actual verification: complete log counts/hash, read-only ZIP decoding/comparison
while 002/003 were available, JSON parsing, source whitespace checks and independent
read-only review of ABI, flush placement, compiler-PC conventions, format arguments,
bounded indexing, direct shadow reads and reset lifetimes. No actionable review
finding remains after clarifying `targetJITReadable` and raw TLB output.

**Not performed:** C++ build/tests, generated-code execution, Windows validation,
boot validation or a new architectural fix. Existing 20 prospective cache/mode
tests remain unrun. Diagnostic evidence is not proof that the first bad transfer
has been located or that ordinary games/savestates are regression-free.

When the user authorizes/builds the same branch, cold boot the same disposable
image/config with the existing COP0 trace enabled. Inspect `EE-JIT-COMPILE` and
`EE-JIT-FLOW` groups around the first unknown compile:

1. Compare compile PC and dispatcher-entry sample before interpreting unknown words.
2. Compare JIT/architectural/vmap bytes and raw TLB candidates. Agreement at a
   stack/data address supports wrong guest control flow; disagreement identifies a
   translation/cache boundary to investigate.
3. Follow the last relevant transfer's source, target, ASID, RA/SP/GP/T9. Compare
   compiled source physical backing with its current translation. Matching PFNs do
   not alone exclude stale self-modifying code.
4. Use sequence/kind and slot labels to distinguish actual recorded transfers,
   ERET/fallback results and repeated compilation. Do not treat this partial ring
   as a full executed-instruction trace.

Keep root/journal checks and earlier fixes intact. Do not repair a disk, suppress
unknown opcodes, force an address mapping or claim full PSBBN boot from kernel idle.
No new Ghidra function names/types/signatures were established by this work.
