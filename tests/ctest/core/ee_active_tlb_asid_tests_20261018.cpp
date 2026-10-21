// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#include "EEActiveTLB.h"

#include <gtest/gtest.h>

namespace
{
tlbs MakeEntry(u32 vpn, u8 asid, u32 physical0, u32 physical1, bool global = false)
{
	tlbs t{};
	t.EntryHi.UL = (vpn & ~0x1fffU) | asid;
	t.EntryLo0.UL = ((physical0 >> 12) << 6) | 0x6 | (global ? 1 : 0);
	t.EntryLo1.UL = ((physical1 >> 12) << 6) | 0x6 | (global ? 1 : 0);
	return t;
}
}

TEST(EEActiveTLB, SameVirtualPageSeparatesASIDs)
{
	// Overlapping non-global guest VPNs are legal across distinct ASIDs.
	// A flat host map must never expose both simultaneously.
	tlbs table[] = {
		MakeEntry(0x2ab20000, 0x05, 0x00100000, 0x00101000),
		MakeEntry(0x2ab20000, 0x18, 0x01e81000, 0x01e82000),
	};
	const auto five = BuildEEActiveTLBPageMap(table, 2, 0x05);
	const auto eighteen = BuildEEActiveTLBPageMap(table, 2, 0x18);
	const auto other = BuildEEActiveTLBPageMap(table, 2, 0x77);
	EXPECT_EQ(five.at(0x2ab21).physicalAddress, 0x00101000U);
	EXPECT_EQ(eighteen.at(0x2ab21).physicalAddress, 0x01e82000U);
	EXPECT_TRUE(other.empty());
}

TEST(EEActiveTLB, GlobalTLBEntryMatchesEveryASID)
{
	tlbs table[] = { MakeEntry(0x00300000, 0x44, 0x00500000, 0x00501000, true) };
	for (const u8 asid : {0x00, 0x05, 0x18, 0xff})
	{
		const auto m = BuildEEActiveTLBPageMap(table, 1, asid);
		EXPECT_EQ(m.at(0x00300).physicalAddress, 0x00500000U);
		EXPECT_EQ(m.at(0x00301).physicalAddress, 0x00501000U);
	}
}

TEST(EEActiveTLB, NonGlobalAndInvalidEntriesNeverLeak)
{
	tlbs table[] = {
		MakeEntry(0x2ac1a000, 0x05, 0x01ec0000, 0x01ec1000),
		MakeEntry(0x2ad00000, 0x18, 0x01ed0000, 0x01ed1000),
	};
	table[1].EntryLo1.V = 0;
	const auto five = BuildEEActiveTLBPageMap(table, 2, 0x05);
	const auto eighteen = BuildEEActiveTLBPageMap(table, 2, 0x18);
	EXPECT_TRUE(five.contains(0x2ac1a));
	EXPECT_FALSE(five.contains(0x2ad00));
	EXPECT_FALSE(eighteen.contains(0x2ac1a));
	EXPECT_TRUE(eighteen.contains(0x2ad00));
	EXPECT_FALSE(eighteen.contains(0x2ad01));
}

TEST(EEActiveTLB, LargerPagesRetainPhysicalOffsets)
{
	tlbs t = MakeEntry(0x10000000, 7, 0x00400000, 0x00404000);
	t.PageMask.UL = 0x00006000; // 16KB halves, four 4KB pages each.
	const auto m = BuildEEActiveTLBPageMap(&t, 1, 7);
	EXPECT_EQ(m.size(), 8U);
	EXPECT_EQ(m.at(0x10000).physicalAddress, 0x00400000U);
	EXPECT_EQ(m.at(0x10003).physicalAddress, 0x00403000U);
	EXPECT_EQ(m.at(0x10004).physicalAddress, 0x00404000U);
	EXPECT_EQ(m.at(0x10007).physicalAddress, 0x00407000U);
}

TEST(EEActiveTLB, ScratchpadAndHalfGlobalBits)
{
	tlbs t = MakeEntry(0x70000000, 8, 0, 0);
	t.EntryLo0.S = 1;
	// One G bit does not make the pair global.
	t.EntryLo0.G = 1;
	t.EntryLo1.G = 0;
	EXPECT_TRUE(BuildEEActiveTLBPageMap(&t, 1, 9).empty());
	const auto m = BuildEEActiveTLBPageMap(&t, 1, 8);
	EXPECT_EQ(m.size(), 4U);
	EXPECT_TRUE(m.at(0x70000).scratchpad);
	EXPECT_EQ(m.at(0x70003).physicalAddress, 0x3000U);
}

TEST(EEActiveTLB, CapturedLinuxCodeNodeAndSentinelCannotShareAnASID)
{
	// Synthetic page frames at the VPNs recorded in the 21 PSBBN captures.
	// The architecture must not expose the ASID 0x18 code/sentinel alongside
	// the separate non-global ASID 0x05 node.
	tlbs table[48]{};
	table[2] = MakeEntry(0x2ad00000, 0x18, 0x01ed0000, 0x01ed1000);
	table[4] = MakeEntry(0x2ab20000, 0x18, 0x01e81000, 0x01e82000);
	table[35] = MakeEntry(0x2ac1a000, 0x05, 0x01ec1000, 0x01ec2000);

	const auto current18 = BuildEEActiveTLBPageMap(table, 48, 0x18);
	EXPECT_TRUE(current18.contains(0x2ab21));
	EXPECT_TRUE(current18.contains(0x2ad00));
	EXPECT_FALSE(current18.contains(0x2ac1a));

	const auto current05 = BuildEEActiveTLBPageMap(table, 48, 0x05);
	EXPECT_FALSE(current05.contains(0x2ab21));
	EXPECT_FALSE(current05.contains(0x2ad00));
	EXPECT_TRUE(current05.contains(0x2ac1a));
}


TEST(EEActiveTLB, DirtyBitSeparatesEvenAndOddWritePermissions)
{
	tlbs t = MakeEntry(0x20000000, 0x05, 0x00100000, 0x00101000);
	t.EntryLo0.D = 0;
	t.EntryLo1.D = 1;
	const auto m = BuildEEActiveTLBPageMap(&t, 1, 0x05);
	ASSERT_TRUE(m.contains(0x20000));
	ASSERT_TRUE(m.contains(0x20001));
	EXPECT_FALSE(m.at(0x20000).writable);
	EXPECT_TRUE(m.at(0x20001).writable);
	EXPECT_TRUE(EEActiveTLBStoreNeedsModification(m, 0x20000040));
	EXPECT_FALSE(EEActiveTLBStoreNeedsModification(m, 0x20001040));
	EXPECT_FALSE(EEActiveTLBStoreNeedsModification(m, 0x20002040)); // no valid translation
}

TEST(EEActiveTLB, CowSharedFrameIsWriteProtectedIndependentlyPerASID)
{
	// Linux-like distinct ASIDs share the same physical stack page in COW mode.
	// This reproduces the virtual page and PFN observed in PSBBN capture 015.
	tlbs table[] = {
		MakeEntry(0x7fff6000, 0x05, 0x001da000, 0x001db000),
		MakeEntry(0x7fff6000, 0x06, 0x001dc000, 0x001db000),
		MakeEntry(0x7fff6000, 0x08, 0x001dd000, 0x001db000),
	};
	for (tlbs& t : table)
		t.EntryLo1.D = 0;
	for (const u8 asid : {0x05, 0x06, 0x08})
	{
		const auto m = BuildEEActiveTLBPageMap(table, 3, asid);
		ASSERT_TRUE(m.contains(0x7fff7));
		EXPECT_EQ(m.at(0x7fff7).physicalAddress, 0x001db000U);
		EXPECT_FALSE(m.at(0x7fff7).writable);
		EXPECT_TRUE(EEActiveTLBStoreNeedsModification(m, 0x7fff7190));
		EXPECT_TRUE(EEActiveTLBStoreNeedsModification(m, 0x7fff7120));
	}
	EXPECT_FALSE(EEActiveTLBStoreNeedsModification(
		BuildEEActiveTLBPageMap(table, 3, 0x18), 0x7fff7190));
}

TEST(EEActiveTLB, CowFaultReplacementSeparatesPhysicalFramesAndWritePermissions)
{
	// Guest Linux may have two ASIDs reading a shared D=0 stack page.
	// After TLB Mod, the faulting process can install its *new* writable PFN
	// without granting writes to the other process's original shared PFN.
	tlbs table[] = {
		MakeEntry(0x7fff6000, 0x05, 0x001da000, 0x001db000),
		MakeEntry(0x7fff6000, 0x06, 0x001dc000, 0x001db000),
	};
	for (tlbs& t : table)
		t.EntryLo1.D = 0;

	const auto beforeFive = BuildEEActiveTLBPageMap(table, 2, 0x05);
	const auto beforeSix = BuildEEActiveTLBPageMap(table, 2, 0x06);
	EXPECT_TRUE(EEActiveTLBStoreNeedsModification(beforeFive, 0x7fff7190));
	EXPECT_TRUE(EEActiveTLBStoreNeedsModification(beforeSix, 0x7fff7190));
	EXPECT_EQ(beforeFive.at(0x7fff7).physicalAddress, 0x001db000U);
	EXPECT_EQ(beforeSix.at(0x7fff7).physicalAddress, 0x001db000U);

	// Emulates Linux resolving COW for ASID 05: change both PFN and D.
	// The production active-map synchronization must notice either change.
	table[0].EntryLo1.UL = ((0x002ab000U >> 12) << 6) | 0x6;
	const auto afterFive = BuildEEActiveTLBPageMap(table, 2, 0x05);
	const auto afterSix = BuildEEActiveTLBPageMap(table, 2, 0x06);
	EXPECT_EQ(afterFive.at(0x7fff7).physicalAddress, 0x002ab000U);
	EXPECT_FALSE(EEActiveTLBStoreNeedsModification(afterFive, 0x7fff7190));
	EXPECT_EQ(afterSix.at(0x7fff7).physicalAddress, 0x001db000U);
	EXPECT_TRUE(EEActiveTLBStoreNeedsModification(afterSix, 0x7fff7190));
	EXPECT_NE(beforeFive.at(0x7fff7), afterFive.at(0x7fff7));
	EXPECT_EQ(beforeSix.at(0x7fff7), afterSix.at(0x7fff7));
}

TEST(EEActiveTLB, TlbDirtyRewriteChangesPermissionWithoutChangingPFN)
{
	tlbs t = MakeEntry(0x7fff6000, 0x05, 0x001da000, 0x001db000);
	t.EntryLo1.D = 0;
	const auto before = BuildEEActiveTLBPageMap(&t, 1, 0x05);
	t.EntryLo1.D = 1; // guest TLBWI/TLBWR after resolving COW
	const auto after = BuildEEActiveTLBPageMap(&t, 1, 0x05);
	EXPECT_EQ(before.at(0x7fff7).physicalAddress, after.at(0x7fff7).physicalAddress);
	EXPECT_NE(before.at(0x7fff7), after.at(0x7fff7));
	EXPECT_TRUE(EEActiveTLBStoreNeedsModification(before, 0x7fff7190));
	EXPECT_FALSE(EEActiveTLBStoreNeedsModification(after, 0x7fff7190));
}

TEST(EEActiveTLB, ReadOnlyGlobalEntryTrapsForEveryASID)
{
	tlbs t = MakeEntry(0x00400000, 0x05, 0x00100000, 0x00101000, true);
	t.EntryLo0.D = 0;
	for (const u8 asid : {0x00, 0x05, 0x18, 0xff})
	{
		const auto m = BuildEEActiveTLBPageMap(&t, 1, asid);
		EXPECT_TRUE(EEActiveTLBStoreNeedsModification(m, 0x00400048));
		EXPECT_FALSE(EEActiveTLBStoreNeedsModification(m, 0x00401048));
	}
}

TEST(EEActiveTLB, ScratchpadMappingDoesNotUseNormalDirtyFaults)
{
	tlbs t = MakeEntry(0x70000000, 0x07, 0, 0);
	t.EntryLo0.S = 1;
	t.EntryLo0.D = 0;
	t.EntryLo1.D = 0;
	const auto m = BuildEEActiveTLBPageMap(&t, 1, 0x07);
	ASSERT_EQ(m.size(), 4U);
	for (u32 offset = 0; offset < 0x4000; offset += 0x1000)
		EXPECT_FALSE(EEActiveTLBStoreNeedsModification(m, 0x70000000 + offset));
}

TEST(EEActiveTLB, CapturedReadOnlyReturnStackRequiresModification)
{
	tlbs t{};
	t.PageMask.UL = 0;
	t.EntryHi.UL = 0x7fff6107;
	t.EntryLo0.UL = 0x0000f51a;
	t.EntryLo1.UL = 0x000076da;
	const auto active = BuildEEActiveTLBPageMap(&t, 1, 7);
	ASSERT_EQ(active.at(0x7fff7).physicalAddress, 0x001db000u);
	EXPECT_TRUE(EEActiveTLBStoreNeedsModification(active, 0x7fff7624));
	EXPECT_FALSE(EEActiveTLBStoreNeedsModification(BuildEEActiveTLBPageMap(&t, 1, 5), 0x7fff7624));
}

TEST(EEActiveTLB, FixedKernelAliasesIgnoreOverlappingReadOnlyTLBEntries)
{
	// TLB entries do not control KSEG0/1, even if a malformed guest inserts one.
	for (const u32 address : {0x80000040u, 0x9ffff040u, 0xa0000040u, 0xbffff040u})
	{
		tlbs t = MakeEntry(address, 7, 0x00100000, 0x00101000);
		t.EntryLo0.D = t.EntryLo1.D = 0;
		const auto active = BuildEEActiveTLBPageMap(&t, 1, 7);
		ASSERT_TRUE(active.contains(address >> 12));
		EXPECT_FALSE(EEActiveTLBStoreNeedsModification(active, address));
	}
}

TEST(EEActiveTLB, StoreAddressPreservesSignedOffsetsAndWraparound)
{
	EXPECT_EQ(EEActiveTLBStoreAddress(0x7fff7600, 0xafbf0024), 0x7fff7624u);
	EXPECT_EQ(EEActiveTLBStoreAddress(0x7fff7600, 0xafbffffc), 0x7fff75fcu);
	EXPECT_EQ(EEActiveTLBStoreAddress(0xfffffff0, 0xafbf0024), 0x00000014u);
}

TEST(EEActiveTLB, QuadwordStorePermissionUsesAlignedAddress)
{
	EXPECT_EQ(EEActiveTLBStoreAddress(0x7fff7605, 0x7fbf0024), 0x7fff7620u); // SQ
	EXPECT_EQ(EEActiveTLBStoreAddress(0x7fff7605, 0xfbbf0024), 0x7fff7620u); // SQC2
}

TEST(EEActiveTLB, ReadModifyWriteStorePermissionKeepsOriginalFaultAddress)
{
	// SWL/SWR/SDL/SDR perform aligned host accesses internally, but permission
	// faults still identify the guest effective address, including its low bits.
	for (const u32 opcode : {0xabbf0025u, 0xbbbf0025u, 0xb3bf0025u, 0xb7bf0025u})
		EXPECT_EQ(EEActiveTLBStoreAddress(0x7fff7600, opcode), 0x7fff7625u);
}


TEST(EEActiveTLB, TLBRemapClearsEachPhysicalCodePageOncePerSync)
{
	EEPhysicalCodeInvalidationSet sync;
	const EEActiveTLBPage first{0x00123000, false, true};
	const EEActiveTLBPage secondAlias{0x00123000, false, false};
	constexpr u32 ram = 0x02000000;
	EXPECT_TRUE(sync.ShouldInvalidate(first, ram));
	EXPECT_FALSE(sync.ShouldInvalidate(secondAlias, ram));
	EXPECT_TRUE(sync.ShouldInvalidate(EEActiveTLBPage{0x00124000, false, true}, ram));
	// The next architectural synchronization must be able to invalidate it.
	EEPhysicalCodeInvalidationSet nextSync;
	EXPECT_TRUE(nextSync.ShouldInvalidate(first, ram));
}

TEST(EEActiveTLB, CodeInvalidationDedupRetainsScratchpadAndNonRAMBehavior)
{
	EEPhysicalCodeInvalidationSet sync;
	constexpr u32 ram = 0x02000000;
	const EEActiveTLBPage scratch{0x00000000, true, true};
	const EEActiveTLBPage outOfRAM{0x1fc00000, false, true};
	EXPECT_TRUE(sync.ShouldInvalidate(scratch, ram));
	EXPECT_TRUE(sync.ShouldInvalidate(scratch, ram)); // virtual scratch clears are distinct
	EXPECT_TRUE(sync.ShouldInvalidate(outOfRAM, ram));
	EXPECT_TRUE(sync.ShouldInvalidate(outOfRAM, ram)); // preserve non-RAM treatment
}

TEST(EEActiveTLB, CodeInvalidationDedupTracksPhysicalNotVirtualAliases)
{
	EEPhysicalCodeInvalidationSet sync;
	constexpr u32 ram = 0x02000000;
	// Same PFN from distinct ASIDs/VPNs needs one physical invalidation per
	// synchronous remap even when the second entry's write permission differs.
	EXPECT_TRUE(sync.ShouldInvalidate(EEActiveTLBPage{0x01e81000, false, true}, ram));
	EXPECT_FALSE(sync.ShouldInvalidate(EEActiveTLBPage{0x01e81000, false, false}, ram));
	EXPECT_TRUE(sync.ShouldInvalidate(EEActiveTLBPage{0x01e82000, false, false}, ram));
}


TEST(EEActiveTLB, CodeInvalidationDedupHandlesMoreThanInlineCapacity)
{
	EEPhysicalCodeInvalidationSet sync;
	constexpr u32 ram = 0x02000000;
	// Larger guest TLB masks span many physical pages. Do not miss repeats
	// when the common allocation-free inline dedup capacity is exceeded.
	for (u32 page = 0; page < 40; ++page)
		EXPECT_TRUE(sync.ShouldInvalidate(EEActiveTLBPage{0x00100000 + (page << 12), false, true}, ram));
	for (u32 page = 0; page < 40; ++page)
		EXPECT_FALSE(sync.ShouldInvalidate(EEActiveTLBPage{0x00100000 + (page << 12), false, true}, ram));
}
