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
