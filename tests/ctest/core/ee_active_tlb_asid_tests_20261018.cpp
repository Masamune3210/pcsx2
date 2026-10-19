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
