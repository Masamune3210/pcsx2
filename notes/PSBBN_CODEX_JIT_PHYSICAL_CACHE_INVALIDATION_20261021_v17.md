# PSBBN EE JIT: physical code-cache invalidation (v17)

## Continue here

Repository: `Masamune3210/pcsx2`. Existing branch:
`note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.
Investigation base: `e053f47bc3203c7b53074de09be73ffccf30788e`.
This continues [v16](PSBBN_CODEX_JIT_USERSPACE_DIVERGENCE_20261021_v16.md)
and [v15](PSBBN_JIT_GUEST_TLB_CODE_IDENTITY_ALIAS_20261021_v15.md).

**Status: a source-proven code-cache defect is corrected in source. No compilation,
C++ test execution, emulator launch or new PSBBN runtime result.** Existing explicit
no-build/no-launch instructions were preserved. The next action is the controlled
Windows verification below when authorized, not another speculative MMU change.

## Research question and evidence limits

Could the first wrong userspace execution in v16 result from JIT cache maintenance
using a guest virtual alias where it needs a physical code-block identity?

The raw `emulog(20261021-033453).txt` and `psbbn-kernel-log(5).txt` were not attached
to this Codex chat, tracked in the checkout, or found at their exact filenames in
the local Downloads root. The v16 line references remain supplied evidence, not
logs independently re-read in this investigation. Do not infer that no copy exists
elsewhere. No production HDD image or running emulator was accessed.

The recorded root mount/journal-pointer readback is established by v16. The first
incorrect instruction or register transition around its lines 47509-47571 is still
**not independently identified**. In particular, this report does not establish
that the physical-cache defect caused the first suspicious jump, the ASCII opcode
decodes or the final `00006F60` TLBL loop.

## Proven source defect

`iR5900.cpp::recRecompile` inserts metadata with
`recBlocks.New(HWADDR(startpc), ...)`. `BaseBlocks` keys and block extents are
physical addresses. Guest virtual dispatch is a different operation.

Before this correction, `recClear` used `PC_GETBLOCK(blockstart)` and
`PC_GETBLOCK(lowerextent)` for these physical metadata addresses. Once 4 KiB
dispatch activates, that macro consults `recLUTPages`, the current **guest virtual**
table. v15 deliberately initialized TLB-managed low guest addresses to the shared
`recLutUnmapped` table. Consequently, invalidation could:

1. Remove a physical block's metadata and reset its incoming native branch links.
2. Write `JITCompile` into an unrelated guest dispatch entry, including the shared
   unmapped-page table.
3. Leave the actual physical `recRAM` dispatch pointer pointing to old native code.

An unmapped entry changed to `JITCompile` can reach compilation without going
through `recError`'s fetch-TLBL path. If the same numerical physical address has a
readable `PSM` pointer, the first-fetch null-backing guard cannot detect that the
guest virtual page was unmapped. This is a concrete source mechanism capable of
defeating v15's invariant; its occurrence in the supplied run is unproven.

A literal pointer-arithmetic check illustrated the distinction, without executing
C++: for an 8-byte `BASEBLOCK`, physical PC `00003000`, synthetic RAM-table base
`10000000` and unmapped-table base `20000000`, the old fine-table lookup selects
`20006000`, while the physical block entry is `10006000`.

Three connected address-domain defects were also established:

- `recClear` tested the incoming **virtual** address against `maxrecmem` before
  translating it. High KUSEG/KSEG2 aliases could be rejected even when their
  physical frame contained compiled code. The high-water mark itself was derived
  from a legacy virtual-address mask rather than the physical compiled extent.
- Manual-integrity and write-protection callbacks already supplied physical RAM
  addresses. Passing them through `HWADDR` could translate a physical address a
  second time through an unrelated low guest mapping.
- COP0's new-mapping invalidation ran before the JIT alias was installed. Relying
  on `HWADDR` there could select an old/unmapped alias or the legacy table before
  fine dispatch activated. Outgoing and incoming PFNs are already available in
  the active-TLB projection; there is no need to infer them through a live alias.

The host write-protection callback also passed the host page size **in bytes** to
an invalidation interface whose size is **32-bit words**. A 4 KiB page therefore
requested a 16 KiB clear. The corrected call passes 1024 words.

## Source changes

- `pcsx2/x86/EERecCodeCache.h`: isolates the existing overlap/block-link
  invalidation algorithm with an immutable physical LUT. All physical metadata
  lookups, compiling-block comparisons and extent resets use that table.
- `pcsx2/x86/ix86-32/iR5900.cpp`: adds `recClearPhysical`; retains the virtual-PC
  `recClear` callback for fastmem backpatching and overlap recompilation; converts
  before the physical high-water check. Manual discard/page-reset callbacks use
  the physical entry point. `maxrecmem` records physical start plus compiled size,
  avoiding translation of a potentially unmapped next-page end PC.
- `pcsx2/COP0.cpp`: ordinary TLB replacement, ASID reconciliation and reset pass
  the projection's explicit outgoing/incoming PFN to physical invalidation. This
  is independent of alias installation order and selected dispatch mode. Existing
  scratchpad handling remains separate.
- `pcsx2/vtlb.cpp`: write-protection invalidation uses its known physical address
  and converts host bytes to words. Guest-PC fastmem backpatching remains virtual.
- `tests/ctest/core/ee_rec_code_cache_tests_20261020.cpp`: seven prospective
  regressions use the **production** physical-cache helper and real `BaseBlocks`:
  dispatch and incoming-link reset, unrelated-page preservation, interior write,
  overlapping blocks, compiling-block preservation, crossing a 64 KiB boundary,
  and zero-length clear.
- CMake registers those tests only for x86. The existing `EEActiveTLBNative`
  MSBuild harness also includes them and the real `BaseblockEx.cpp`; its standalone
  assertion handler reports and aborts on real assertion failures.

The change adds no work to generated instruction/load/store/branch hot paths.
Native 64 KiB dispatch, fine-table initialization and guest execution semantics
are unchanged. No TLB mappings are fabricated; no instruction is suppressed or
converted to a NOP. No guest binary, HDD sector, savestate format, DEV9 path or
Ghidra/decompiler component was changed.

## Verification actually performed

- `git diff --check`: passed.
- Native `.vcxproj`: parsed as XML; new source registrations inspected.
- Reviewed all EE clear callers, physical metadata producers, manual integrity
  callbacks, host write protection, reserve/reset/shutdown and branch-link removal.
- Independent read-only review found two test-harness integration defects:
  unconditional ARM64 registration and missing standalone Debug/Devel assertion
  linkage. Both were corrected and independently re-reviewed; no further
  actionable finding was reported. Review is source evidence only.

**Not performed:** CMake configuration; compilation/build; C++ regression execution
or red/green proof; Windows AVX2 validation; BIOS/retail-game, savestate or fastmem
runtime testing; controlled same-image interpreter/JIT comparison; PSBBN boot.
The seven tests cover the physical invalidation algorithm. They do **not** execute
the COP0 reconciliation path, virtual wrapper/high-water check, real host page
fault callback, generated dispatchers or a guest instruction stream.

## Next Windows verification, when authorized

Keep the same branch. Build the existing native harness in `Release AVX2|x64`, run
`EERecCodeCache.*` and `EEActiveTLB.*`, then build the user's `PCSX2_qt.slnx` in
`Release AVX2|x64`. Example PowerShell commands, one command per line:

```powershell
MSBuild.exe .\tests\ctest\core\EEActiveTLBNative.vcxproj '/p:SolutionDir=G:\Users\Johnny\Downloads\Source Files\testing\pcsx2\' '/p:Configuration=Release AVX2' /p:Platform=x64 /m
& '.\bin\tests\EEActiveTLBNative-x64-Release AVX2.exe' '--gtest_filter=EERecCodeCache.*:EEActiveTLB.*'
MSBuild.exe .\PCSX2_qt.slnx '/p:Configuration=Release AVX2' /p:Platform=x64 /m
```

Preserve/hash an untouched installed test image and configuration; boot a disposable
clone cold under JIT. Confirm BIOS/HDDLOAD and root mounting/journal readback remain
working. Examine the earliest suspicious control transfer, ASCII opcodes and
`00006F60` faults, not just whether root mounted. Do ordinary BIOS/retail-game,
fastmem/code-write invalidation and savestate checks before any merge to `testing`.

If the userspace failure persists, first inspect the original log's focused
47500-47590 region and corresponding TLB/refill/fallback context. Capture the
precise branch/return PC, opcode, physical backing, ASID and GPR/exception state;
the existing compile-time cross-page fallback messages do not show that runtime
transition. The fallback's cancellation/BD/EPC and static code-link behavior across
mapping changes remain independent validation targets. Do not claim this source
correction proves PSBBN boot or fixes every MMU/JIT invariant.

No new PSBBN binary function names, structures or Ghidra annotations were proven.
