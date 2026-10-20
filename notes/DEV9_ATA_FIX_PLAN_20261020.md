# ATA HDD fixes, 2026-10-20

Implements the existing [workplan](DEV9_ATA4_CODEX_WORKPLAN_20261019_v1.md)
and [source findings](DEV9_ATA4_HDD_SOURCE_FINDINGS_20261019.md), following the
user's instruction to fix the documented bugs. Keep the active branch and push
reviewable checkpoints. No compilation, CMake configuration, or guest boot is
authorized; C++ regression sources will be added but remain unrun.

1. Add public-register, disposable-image regression cases before implementation.
   Fix the independent FIFO wrap/length and array ownership defects first.
2. Initialize all ATA state, cancel incomplete transfers on reset, and bound every
   DMA copy. Preserve accepted writes across reset, but discard incomplete buffers
   and stale SPEED/IOP FIFO transfers. Use exclusive sector capacity for bounds.
3. Serialize backing-file access and queue observation. Reads drain accepted writes;
   flush waits for in-flight work and performs an OS durability operation. Report
   file errors to the guest and retain a sticky write fault until reopen.
4. Implement PIO data-out through DATA words, with single/multiple block IRQ phases,
   count-zero decoding, partial tails, task-file progression, and queued persistence.
5. Correct parameters, aliases, CHS, HOB, masked interrupts, and capability claims;
   implement command-visible basic power state/timer behavior.
6. Review integration, run allowed whitespace/source/registration checks, document
   remaining gaps and exact unrun validation. Commit and push each coherent stage.

Review focus: dropped/duplicated bytes, first invalid sector, reset/thread ownership,
read-after-write coherence, error completion, and truthful IDENTIFY capabilities.
Regression tests must exercise production code and real temporary files, rather
than a second model of the arithmetic. No user disk image is used.

Full savestate/disk-generation policy and undocumented Sony authentication require
separate evidence/design if they cannot safely be implemented within this scope;
they must remain explicit rather than being called fixed by unrelated changes.
