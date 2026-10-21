// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#include "EEActiveTLB.h"

#include <gtest/gtest.h>

#include <vector>

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

TEST(EEActiveTLB, PermissionOnlyUpdatesRetainBackingAndPublishBothDirtyDirections)
{
	const EEActiveTLBPage readOnly{0x001da000, false, false};
	const EEActiveTLBPage writable{0x001da000, false, true};
	EXPECT_TRUE(readOnly.HasSameBacking(writable));
	EXPECT_TRUE(writable.HasSameBacking(readOnly));
	EXPECT_NE(readOnly, writable); // Full architectural equality still includes D.
	EEActiveTLBPageMap active{{0x7fff7, writable}};
	EXPECT_FALSE(EEActiveTLBStoreNeedsModification(active, 0x7fff7190));
	EEActiveTLBPageMap desired{{0x7fff7, readOnly}};
	active.swap(desired);
	EXPECT_TRUE(EEActiveTLBStoreNeedsModification(active, 0x7fff7190));
	active.swap(desired);
	EXPECT_FALSE(EEActiveTLBStoreNeedsModification(active, 0x7fff7190));
}

TEST(EEActiveTLB, BackingComparisonRequiresSamePhysicalPageAndMemoryKind)
{
	const EEActiveTLBPage page{0x3000, false, false};
	EXPECT_TRUE(page.HasSameBacking(page));
	EXPECT_FALSE(page.HasSameBacking(EEActiveTLBPage{0x4000, false, false}));
	EXPECT_FALSE(page.HasSameBacking(EEActiveTLBPage{0x3000, true, false}));
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

namespace
{
struct MappingAction
{
	EEActiveTLBMappingAction action;
	u32 page;
	EEActiveTLBPage mapping;
	bool operator==(const MappingAction&) const = default;
};

std::vector<MappingAction> MappingActions(const EEActiveTLBPageMap& before, const EEActiveTLBPageMap& after)
{
	std::vector<MappingAction> actions;
	PlanEEActiveTLBMappingChanges(before, after,
		[&](EEActiveTLBMappingAction action, u32 page, const EEActiveTLBPage& mapping)
		{
			actions.push_back({action, page, mapping});
		});
	return actions;
}

// Independent bounded-address model: enumerate virtual pages first, then pick
// the earliest eligible slot. No production projection/memo helpers are used.
EEActiveTLBPageMap ModelSmallTLB(const std::array<tlbs, 48>& entries, u8 asid)
{
	EEActiveTLBPageMap result;
	for (u32 page = 0; page < 64; ++page)
	{
		for (const tlbs& t : entries)
		{
			if (!(t.EntryLo0.G && t.EntryLo1.G) && t.EntryHi.ASID != asid)
				continue;
			const u32 mask = t.PageMask.Mask;
			const u32 first = (t.EntryHi.VPN2 & ~mask) * 2;
			if (t.EntryLo0.S)
			{
				if (page < first || page - first >= 4)
					continue;
				result.emplace(page, EEActiveTLBPage{(page - first) << 12, true, true});
				break;
			}
			if (page < first || page - first >= 2 * (mask + 1) ||
				(page & ((~mask << 1) & 0xfffff)) != (first & ((~mask << 1) & 0xfffff)))
				continue;
			const u32 half = (page - first) / (mask + 1);
			const EntryLo_t& lo = half ? t.EntryLo1 : t.EntryLo0;
			if (!lo.V)
				continue;
			result.emplace(page, EEActiveTLBPage{
				((lo.PFN & ~mask) + (page - first) % (mask + 1)) << 12, false, lo.D != 0});
			break;
		}
	}
	return result;
}
}

TEST(EEActiveTLB, ProductionPlannerEmitsPermissionChangesWithoutMappingChanges)
{
	const EEActiveTLBPage writable{0x100000, false, true};
	const EEActiveTLBPage readOnly{0x100000, false, false};
	const EEActiveTLBPageMap before{{0x20000, writable}};
	const EEActiveTLBPageMap after{{0x20000, readOnly}};
	EXPECT_EQ(MappingActions(before, after), (std::vector<MappingAction>{
		{EEActiveTLBMappingAction::PermissionOnly, 0x20000, readOnly}}));
	EXPECT_EQ(MappingActions(after, before), (std::vector<MappingAction>{
		{EEActiveTLBMappingAction::PermissionOnly, 0x20000, writable}}));
	EXPECT_EQ(MappingActions(before, before), (std::vector<MappingAction>{
		{EEActiveTLBMappingAction::Unchanged, 0x20000, writable}}));
}

TEST(EEActiveTLB, ProductionPlannerRemovesBeforeInstallingPFNAndValidityChanges)
{
	const EEActiveTLBPage old{0x100000, false, true};
	const EEActiveTLBPage replacement{0x200000, false, true};
	const EEActiveTLBPageMap before{{0x20000, old}};
	const EEActiveTLBPageMap after{{0x20000, replacement}};
	EXPECT_EQ(MappingActions(before, after), (std::vector<MappingAction>{
		{EEActiveTLBMappingAction::Remove, 0x20000, old},
		{EEActiveTLBMappingAction::Install, 0x20000, replacement}}));
	EXPECT_EQ(MappingActions(before, {}), (std::vector<MappingAction>{
		{EEActiveTLBMappingAction::Remove, 0x20000, old}}));
	EXPECT_EQ(MappingActions({}, after), (std::vector<MappingAction>{
		{EEActiveTLBMappingAction::Install, 0x20000, replacement}}));
}

TEST(EEActiveTLB, ProjectionMemoSkipsOnlyUnchangedSelectedInputs)
{
	std::array<tlbs, 48> entries{};
	entries[47] = MakeEntry(0x20000000, 5, 0x100000, 0x101000);
	EEActiveTLBProjectionMemo memo;
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
	memo.Remember(entries.data(), entries.size(), 5);
	EXPECT_TRUE(memo.Matches(entries.data(), entries.size(), 5));
	entries[47].EntryLo1.D = 0;
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
	entries[47].EntryLo1.D = 1;
	entries[47].EntryLo1.PFN++;
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
	entries[47].EntryLo1.PFN--;
	entries[47].EntryLo1.V = 0;
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
	entries[47].EntryLo1.V = 1;
	entries[47].PageMask.Mask = 3;
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
	entries[47].PageMask.Mask = 0;
	entries[47].EntryLo0.S = 1;
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
	entries[47].EntryLo0.S = 0;
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 6));
	memo.Invalidate();
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
}

TEST(EEActiveTLB, ProjectionMemoIgnoresInactiveAndCacheDescriptorOnlyChanges)
{
	std::array<tlbs, 48> entries{};
	entries[0] = MakeEntry(0x20000000, 5, 0x100000, 0x101000, true);
	entries[47] = MakeEntry(0x30000000, 7, 0x200000, 0x201000);
	EEActiveTLBProjectionMemo memo;
	memo.Remember(entries.data(), entries.size(), 5);
	entries[0].EntryLo0.C = 3; // WriteTLB must still refresh cachedTlbs.
	entries[0].EntryHi.ASID = 99; // global selection ignores ASID
	entries[47].EntryLo1.PFN++;
	EXPECT_TRUE(memo.Matches(entries.data(), entries.size(), 6));
	entries[0].EntryLo1.G = 0; // one G bit does not make a global pair
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 6));
}

TEST(EEActiveTLB, ProjectionMemoRejectsEntryCountChangesAndOversizedInputs)
{
	std::array<tlbs, 49> entries{};
	EEActiveTLBProjectionMemo memo;
	memo.Remember(entries.data(), 48, 0);
	EXPECT_FALSE(memo.Matches(entries.data(), 47, 0));
	EXPECT_FALSE(memo.Matches(entries.data(), 49, 0));
	memo.Remember(entries.data(), 49, 0);
	EXPECT_FALSE(memo.Matches(entries.data(), 48, 0));
}

TEST(EEActiveTLB, ProjectionMemoAndPlannerPreserveASIDReuseGlobalAndOverlapPriority)
{
	std::array<tlbs, 48> entries{};
	entries[0] = MakeEntry(0x20000000, 5, 0x100000, 0x101000);
	entries[1] = MakeEntry(0x20000000, 6, 0x200000, 0x201000);
	entries[47] = MakeEntry(0x20000000, 5, 0x300000, 0x301000, true);
	entries[0].EntryLo1.V = entries[1].EntryLo1.V = entries[47].EntryLo1.V = 0;
	const auto five = BuildEEActiveTLBPageMap(entries.data(), entries.size(), 5);
	const auto six = BuildEEActiveTLBPageMap(entries.data(), entries.size(), 6);
	EXPECT_EQ(MappingActions(five, six), (std::vector<MappingAction>{
		{EEActiveTLBMappingAction::Remove, 0x20000, {0x100000, false, true}},
		{EEActiveTLBMappingAction::Install, 0x20000, {0x200000, false, true}}}));
	EEActiveTLBProjectionMemo memo;
	memo.Remember(entries.data(), entries.size(), 5);
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 6));
	entries[0].EntryLo0.PFN = 0x400; // reuse ASID 5 with different source
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
	entries[0].EntryLo0.V = 0; // lower-priority global slot becomes visible
	const auto global = BuildEEActiveTLBPageMap(entries.data(), entries.size(), 5);
	EXPECT_EQ(global.at(0x20000).physicalAddress, 0x300000U);
	EXPECT_EQ(MappingActions(five, global), (std::vector<MappingAction>{
		{EEActiveTLBMappingAction::Remove, 0x20000, {0x100000, false, true}},
		{EEActiveTLBMappingAction::Install, 0x20000, {0x300000, false, true}}}));
}

TEST(EEActiveTLB, ProjectionMemoNormalizesOnlyIrrelevantScratchpadAndInvalidHalfFields)
{
	std::array<tlbs, 48> entries{};
	entries[0] = MakeEntry(0x70000000, 5, 0, 0);
	entries[0].EntryLo0.S = 1;
	entries[47] = MakeEntry(0x20000000, 5, 0x100000, 0x101000);
	entries[47].EntryLo1.V = 0;
	EEActiveTLBProjectionMemo memo;
	memo.Remember(entries.data(), entries.size(), 5);
	entries[0].EntryLo0.V = entries[0].EntryLo0.D = 0;
	entries[0].EntryLo1.PFN = 99;
	entries[47].EntryLo1.PFN = 100;
	entries[47].EntryLo1.D = 0;
	EXPECT_TRUE(memo.Matches(entries.data(), entries.size(), 5));
	entries[47].EntryLo1.V = 1;
	EXPECT_FALSE(memo.Matches(entries.data(), entries.size(), 5));
}

TEST(EEActiveTLB, SeededProjectionAndProductionActionsMatchIndependentModel)
{
	std::array<tlbs, 48> entries{};
	EEActiveTLBProjectionMemo memo;
	EEActiveTLBPageMap active;
	u32 state = 0x9e3779b9;
	auto random = [&]()
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	};
	u8 asid = 0;
	u32 memoHits = 0;
	for (u32 step = 0; step < 1500; ++step)
	{
		const std::size_t slot = random() % entries.size();
		tlbs& t = entries[slot];
		switch (step % 12)
		{
			case 0:
			{
				// Keep PRNG consumption ordered across C++ compilers.
				const u32 vpn = (random() % 16) << 13;
				const u8 entryAsid = static_cast<u8>(random() % 4);
				const u32 physical0 = (random() % 64) << 12;
				const u32 physical1 = (random() % 64) << 12;
				const bool global = (random() & 3) == 0;
				t = MakeEntry(vpn, entryAsid, physical0, physical1, global);
				break;
			}
			case 1: t.EntryLo0.D ^= 1; break;
			case 2: t.EntryLo1.V ^= 1; break;
			case 3: asid = static_cast<u8>(random() % 4); break;
			case 4: t.EntryLo0.PFN = random() % 64; break;
			case 5: t.PageMask.Mask = (random() & 1) ? 3 : 0; break;
			case 6: t.EntryLo0.G ^= 1; t.EntryLo1.G ^= 1; break;
			case 7: t.EntryLo0.S ^= 1; break;
			case 8: memo.Invalidate(); break; // reset/load requires reprojection
			case 9: t.EntryHi.ASID = random() % 4; break;
			case 10: t.EntryLo1.D ^= 1; break;
			case 11: break; // consecutive identical state
		}
		const auto expected = ModelSmallTLB(entries, asid);
		if (memo.Matches(entries.data(), entries.size(), asid))
		{
			++memoHits;
			EXPECT_EQ(active, expected) << "step=" << step;
			continue;
		}
		const auto desired = BuildEEActiveTLBPageMap(entries.data(), entries.size(), asid);
		ASSERT_EQ(desired, expected) << "step=" << step;
		bool installing = false;
		for (const auto& [action, page, mapping] : MappingActions(active, desired))
		{
			if (action == EEActiveTLBMappingAction::Remove)
			{
				EXPECT_FALSE(installing);
				EXPECT_TRUE(!desired.contains(page) || !mapping.HasSameBacking(desired.at(page)));
				EXPECT_EQ(active.erase(page), 1U);
			}
			else
			{
				installing = true;
				if (action == EEActiveTLBMappingAction::Install)
					EXPECT_FALSE(active.contains(page));
				else
				{
					ASSERT_TRUE(active.contains(page));
					EXPECT_TRUE(active.at(page).HasSameBacking(mapping));
					EXPECT_EQ(action == EEActiveTLBMappingAction::Unchanged, active.at(page) == mapping);
				}
				active.insert_or_assign(page, mapping);
			}
		}
		EXPECT_EQ(active, expected) << "step=" << step;
		memo.Remember(entries.data(), entries.size(), asid);
	}
	EXPECT_GT(memoHits, 100U);
}
