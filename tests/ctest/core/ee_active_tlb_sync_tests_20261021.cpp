// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Full core_test only. These tests call real COP0 synchronization and host
// vTLB mappings; they do not execute generated stores or JIT dispatch.
#include "Common.h"
#include "COP0.h"
#include "vtlb.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>

namespace
{
class EEActiveTLBSync : public testing::Test
{
protected:
	static constexpr u32 VirtualAddress = 0x01000000u;
	static constexpr u32 PhysicalA = 0x00100000u;
	static constexpr u32 PhysicalB = 0x00200000u;
	static constexpr u32 PhysicalC = 0x00300000u;
	alignas(4096) std::array<u8, 4096> pageA{}, pageB{}, pageC{};
	std::unique_ptr<vtlb_private::VTLBVirtual[]> map;

	static tlbs Entry(u8 asid, u32 physical, bool global = false)
	{
		tlbs entry{};
		entry.EntryHi.UL = VirtualAddress | asid;
		entry.EntryLo0.UL = ((physical >> 12) << 6) | 6u | (global ? 1u : 0u);
		entry.EntryLo1.G = global;
		return entry;
	}

	void MapDisposableBuffers()
	{
		// Exercise the real inline physical map, not VMapBuffer or a mock.
		vtlb_MapBlock(pageA.data(), PhysicalA, 4096);
		vtlb_MapBlock(pageB.data(), PhysicalB, 4096);
		vtlb_MapBlock(pageC.data(), PhysicalC, 4096);
	}

	void InitializeChild()
	{
		// vtlb_Init changes private handler IDs/counts as well as MapData.
		// Never run this setup in the parent or try to restore only MapData.
		ASSERT_EQ(vtlb_private::vtlbdata.vmap, nullptr);
		Cpu = &intCpu;
		EmuConfig.Cpu.Recompiler.EnableFastmem = false;
		EmuConfig.Cpu.Recompiler.EnableEECache = false;
		EmuConfig.Gamefixes.GoemonTlbHack = false;
		EmuConfig.Trace.Enabled = false;
		std::fill_n(tlb, 48, tlbs{});
		_cpuRegistersPack = {};
		pageA.fill(0x11);
		pageB.fill(0x22);
		pageC.fill(0x33);
		map = std::make_unique<vtlb_private::VTLBVirtual[]>(vtlb_private::VTLB_VMAP_ITEMS);
		vtlb_private::vtlbdata.vmap = map.get();
		vtlb_private::vtlbdata.ppmap = nullptr;
		vtlb_Init();
		MapDisposableBuffers();
	}

	template <typename Checks>
	void RunIsolated(Checks checks)
	{
		ASSERT_EXIT(
			{
				InitializeChild();
				if (!HasFatalFailure())
					checks();
				std::_Exit(HasFailure() ? 1 : 0);
			},
			::testing::ExitedWithCode(0), "");
	}

	const vtlb_private::VTLBVirtual& Mapping() const
	{
		return vtlb_private::vtlbdata.vmap[VirtualAddress >> 12];
	}

	void ExpectPage(const std::array<u8, 4096>& page)
	{
		ASSERT_FALSE(Mapping().isHandler(VirtualAddress));
		EXPECT_EQ(Mapping().assumePtr(VirtualAddress), reinterpret_cast<uptr>(page.data()));
		EXPECT_EQ(vtlb_memRead<mem8_t>(VirtualAddress), page.front());
	}
};

TEST_F(EEActiveTLBSync, DirtyBitBothDirectionsAndIdenticalSyncPreserveRealHostMapping)
{
	RunIsolated([this] {
		cpuRegs.CP0.n.EntryHi = 5;
		tlb[47] = Entry(5, PhysicalA);
		SyncActiveTLBMappings();
		ExpectPage(pageA);
		const uptr original = Mapping().raw();
		EXPECT_FALSE(EETLBStoreNeedsModification(VirtualAddress + 0x44));

		for (const bool writable : {false, true, false, true})
		{
			tlb[47].EntryLo0.D = writable;
			SyncActiveTLBMappings();
			EXPECT_EQ(Mapping().raw(), original);
			EXPECT_EQ(EETLBStoreNeedsModification(VirtualAddress + 0x44), !writable);
			// Identical consecutive syncs must retain both mapping and permission.
			SyncActiveTLBMappings();
			EXPECT_EQ(Mapping().raw(), original);
			EXPECT_EQ(EETLBStoreNeedsModification(VirtualAddress + 0x44), !writable);
		}
	});
}

TEST_F(EEActiveTLBSync, PFNRemapASIDReuseGlobalFallbackAndValidityReachRealVMap)
{
	RunIsolated([this] {
		tlb[0] = Entry(5, PhysicalA);
		tlb[1] = Entry(6, PhysicalB);
		tlb[47] = Entry(99, PhysicalC, true);
		cpuRegs.CP0.n.EntryHi = 5;
		SyncActiveTLBMappings();
		ExpectPage(pageA);

		tlb[0] = Entry(5, PhysicalB);
		SyncActiveTLBMappings();
		ExpectPage(pageB);
		WriteCP0EntryHi(6);
		ExpectPage(pageB);
		tlb[1] = Entry(6, PhysicalA); // same ASID reused with a new PFN
		SyncActiveTLBMappings();
		ExpectPage(pageA);

		tlb[1].EntryLo0.V = 0;
		SyncActiveTLBMappings();
		ExpectPage(pageC); // later global slot becomes selected
		WriteCP0EntryHi(7);
		ExpectPage(pageC);
		tlb[47].EntryLo1.G = 0; // a single G bit is not global
		SyncActiveTLBMappings();
		EXPECT_TRUE(Mapping().isHandler(VirtualAddress));
		EXPECT_FALSE(EETLBStoreNeedsModification(VirtualAddress));

		tlb[47].EntryLo1.G = 1;
		tlb[47].EntryLo0.D = 0;
		SyncActiveTLBMappings();
		ExpectPage(pageC);
		EXPECT_TRUE(EETLBStoreNeedsModification(VirtualAddress));
		tlb[47].EntryLo0.V = 0;
		SyncActiveTLBMappings();
		EXPECT_TRUE(Mapping().isHandler(VirtualAddress));
		EXPECT_FALSE(EETLBStoreNeedsModification(VirtualAddress));
	});
}

TEST_F(EEActiveTLBSync, ResetAndVTLBInitReinstallIdenticalArchitecturalInputs)
{
	RunIsolated([this] {
		cpuRegs.CP0.n.EntryHi = 5;
		tlb[47] = Entry(5, PhysicalA);
		tlb[47].EntryLo0.D = 0;
		const tlbs saved = tlb[47];
		SyncActiveTLBMappings();
		ExpectPage(pageA);
		const uptr original = Mapping().raw();

		ResetActiveTLBMappings();
		EXPECT_TRUE(Mapping().isHandler(VirtualAddress));
		EXPECT_FALSE(EETLBStoreNeedsModification(VirtualAddress));
		vtlb_Init();
		MapDisposableBuffers();
		EXPECT_TRUE(tlb[47] == saved);
		EXPECT_TRUE(Mapping().isHandler(VirtualAddress));
		SyncActiveTLBMappings();
		ExpectPage(pageA);
		EXPECT_EQ(Mapping().raw(), original);
		EXPECT_TRUE(EETLBStoreNeedsModification(VirtualAddress));

		// Init also invalidates a still-live projection without an explicit reset.
		vtlb_Init();
		MapDisposableBuffers();
		EXPECT_TRUE(tlb[47] == saved);
		EXPECT_TRUE(Mapping().isHandler(VirtualAddress));
		SyncActiveTLBMappings();
		ExpectPage(pageA);
		EXPECT_EQ(Mapping().raw(), original);
		EXPECT_TRUE(EETLBStoreNeedsModification(VirtualAddress));
	});
}
} // namespace
