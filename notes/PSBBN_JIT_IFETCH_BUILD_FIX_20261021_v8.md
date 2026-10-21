# PSBBN JIT instruction-fetch TLBL: MSVC compile correction (v8)

Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`.

After `3efa5335` added the recLUT instruction-fetch TLB miss handler, Windows MSVC reported C2065 at `pcsx2/x86/ix86-32/iR5900.cpp:555`: `VTLB_PAGE_BITS` undeclared.

Commit `07c7bbcd35fbc02a43544892e6b2d19d9ee53f0f` qualifies the existing constant as `vtlb_private::VTLB_PAGE_BITS`, matching its declaration in `pcsx2/vtlb.h` (`namespace vtlb_private`, page size 4096). Only the index expression changes; no semantic changes to guest memory or exception delivery.

**Status:** Namespace declaration checked against current branch source; MSVC rebuild and PSBBN runtime **not yet verified**. Next: compile updated head, then cold boot PSBBN. Prior known runtime status was root mounted readonly under the JIT data-fault fix, followed by `Jump to unmapped recLUT page (PC: 0x0FB643C0)`; the new instruction-fetch TLBL handling remains untested.
