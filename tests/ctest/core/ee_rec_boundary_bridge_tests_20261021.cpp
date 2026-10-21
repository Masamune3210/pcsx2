// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Full core_test only: exercises the real interpreter bridge and real vTLB
// unmapped handlers. The standalone EEActiveTLBNative target cannot link it.
#include "Common.h"
#include "R5900OpcodeTables.h"
#include "vtlb.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>

extern void EEInterpretCrossPageBranchFromJIT(u32 branchPC);
extern void EEInterpretDINextInstructionFromJIT(u32 nextPC);

namespace
{
class EERecBoundaryBridge : public testing::Test
{
protected:
	static constexpr u32 BranchPC = 0x01000ffcu;
	alignas(4096) std::array<u32, 1024> branchPage{}, slotPage{};
	std::unique_ptr<vtlb_private::VTLBVirtual[]> map;

	void InitializeChild()
	{
		// Called INSIDE ASSERT_EXIT's child. vtlb_Init also changes private
		// handler IDs/counts which MapData alone cannot restore in-process.
		ASSERT_EQ(vtlb_private::vtlbdata.vmap, nullptr);
		std::fill_n(tlb, 48, tlbs{});
		EmuConfig.Cpu.Recompiler.EnableFastmem = false;
		EmuConfig.Cpu.Recompiler.EnableEECache = false;
		EmuConfig.Gamefixes.GoemonTlbHack = false;
		EmuConfig.Speedhacks.EECycleRate = 0;
		map = std::make_unique<vtlb_private::VTLBVirtual[]>(vtlb_private::VTLB_VMAP_ITEMS);
		vtlb_private::vtlbdata.vmap = map.get();
		vtlb_private::vtlbdata.ppmap = nullptr;
		vtlb_Init();
		vtlb_VMapBuffer(BranchPC & ~0xfffu, branchPage.data(), 4096);
		_cpuRegistersPack = {};
		cpuRegs.CP0.n.Config = 1u << 18;
		cpuRegs.CP0.n.Random = 47;
		cpuRegs.cycle = 100;
		cpuRegs.nextEventCycle = 1; // bridge must defer scheduling until JIT epilogue
		cpuRegs.GPR.r[1].UD[0] = 1;
		Cpu = &recCpu;
	}

	template <typename Checks>
	void RunIsolated(Checks checks)
	{
		ASSERT_EXIT(
			{
				InitializeChild();
				if (!HasFatalFailure())
					checks();
				// GoogleTest assertions must fail the child exit status too.
				std::_Exit(HasFailure() ? 1 : 0);
			},
			::testing::ExitedWithCode(0), "");
	}

	void Execute(u32 branch, bool mappedSlot, u32 slot = 0x24420001u)
	{
		branchPage.back() = branch;
		if (mappedSlot)
		{
			slotPage.front() = slot; // default: addiu v0,v0,1
			vtlb_VMapBuffer(BranchPC + 4, slotPage.data(), 4096);
		}
		EEInterpretCrossPageBranchFromJIT(BranchPC);
		EXPECT_EQ(Cpu, &recCpu);
		EXPECT_EQ(cpuRegs.branch, 0u);
	}

	u64 ExpectedCycle(u32 branch, bool executesSlot) const
	{
		const u32 cycles = R5900::GetInstruction(branch).cycles +
			(executesSlot ? R5900::GetInstruction(0x24420001u).cycles : 0u);
		return 100u + std::max(1u, cycles >> 3);
	}
};

TEST_F(EERecBoundaryBridge, UntakenOrdinaryBranchExecutesDiscontiguousSlot)
{
	RunIsolated([this] {
		Execute(0x10200008u, true); // beq at,zero (untaken)
		EXPECT_EQ(cpuRegs.pc, BranchPC + 8);
		EXPECT_EQ(cpuRegs.GPR.r[2].UD[0], 1u);
		EXPECT_EQ(cpuRegs.cycle, ExpectedCycle(0x10200008u, true));
	});
}

TEST_F(EERecBoundaryBridge, UntakenOrdinaryBranchSlotFetchFaultPreservesBD)
{
	RunIsolated([this] {
		Execute(0x10200008u, false);
		EXPECT_EQ(cpuRegs.CP0.n.EPC, BranchPC);
		EXPECT_EQ(cpuRegs.CP0.n.BadVAddr, BranchPC + 4);
		EXPECT_NE(cpuRegs.CP0.n.Cause & 0x80000000u, 0u);
		EXPECT_EQ(cpuRegs.CP0.n.Cause & 0x7cu, 8u);
		EXPECT_EQ(cpuRegs.cycle, ExpectedCycle(0x10200008u, false));
	});
}

TEST_F(EERecBoundaryBridge, UntakenLikelyAnnulsUnmappedSlotAndChargesBranch)
{
	RunIsolated([this] {
		Execute(0x50200008u, false); // beql at,zero (untaken)
		EXPECT_EQ(cpuRegs.pc, BranchPC + 8);
		EXPECT_EQ(cpuRegs.CP0.n.Cause & 0x80000000u, 0u);
		EXPECT_EQ(cpuRegs.GPR.r[2].UD[0], 0u);
		EXPECT_EQ(cpuRegs.cycle, ExpectedCycle(0x50200008u, false));
	});
}

TEST_F(EERecBoundaryBridge, JALRWithSameSourceAndLinkRegisterCapturesOldTarget)
{
	RunIsolated([this] {
		cpuRegs.GPR.r[1].UD[0] = 0x02000000u;
		Execute(0x00200809u, true); // jalr at,at
		EXPECT_EQ(cpuRegs.pc, 0x02000000u);
		EXPECT_EQ(cpuRegs.GPR.r[1].UD[0], BranchPC + 8);
		EXPECT_EQ(cpuRegs.GPR.r[2].UD[0], 1u);
		EXPECT_EQ(cpuRegs.cycle, ExpectedCycle(0x00200809u, true));
	});
}

TEST_F(EERecBoundaryBridge, TakenJRToSlotDoesNotExecuteItTwice)
{
	RunIsolated([this] {
		cpuRegs.GPR.r[1].UD[0] = BranchPC + 4;
		Execute(0x00200008u, true);
		EXPECT_EQ(cpuRegs.pc, BranchPC + 4);
		EXPECT_EQ(cpuRegs.GPR.r[2].UD[0], 1u);
	});
}

TEST_F(EERecBoundaryBridge, JALRSlotFaultKeepsLinkAndPreciseException)
{
	RunIsolated([this] {
		cpuRegs.GPR.r[1].UD[0] = 0x02000000u;
		Execute(0x00200809u, false);
		EXPECT_EQ(cpuRegs.GPR.r[1].UD[0], BranchPC + 8);
		EXPECT_EQ(cpuRegs.CP0.n.EPC, BranchPC);
		EXPECT_EQ(cpuRegs.CP0.n.BadVAddr, BranchPC + 4);
		EXPECT_NE(cpuRegs.CP0.n.Cause & 0x80000000u, 0u);
		EXPECT_EQ(cpuRegs.cycle, ExpectedCycle(0x00200809u, false));
	});
}

TEST_F(EERecBoundaryBridge, TakenLikelyExecutesMappedSlot)
{
	RunIsolated([this] {
		Execute(0x50000008u, true); // beql zero,zero (taken)
		EXPECT_EQ(cpuRegs.pc, BranchPC + 4 + 8 * 4);
		EXPECT_EQ(cpuRegs.GPR.r[2].UD[0], 1u);
		EXPECT_EQ(cpuRegs.cycle, ExpectedCycle(0x50000008u, true));
	});
}

TEST_F(EERecBoundaryBridge, JALRLinkIsVisibleToEntryHiDelaySlot)
{
	RunIsolated([this] {
		cpuRegs.GPR.r[25].UD[0] = 0x02000000u;
		cpuRegs.GPR.r[31].UD[0] = 0x55u;
		Execute(0x0320f809u, true, 0x409f5000u); // jalr ra,t9; mtc0 ra,EntryHi
		EXPECT_EQ(cpuRegs.pc, 0x02000000u);
		EXPECT_EQ(cpuRegs.GPR.r[31].UD[0], BranchPC + 8);
		EXPECT_EQ(cpuRegs.CP0.n.EntryHi, BranchPC + 8);
	});
}
TEST_F(EERecBoundaryBridge, DIContinuationExecutesMappedNextInstructionBeforeDisabling)
{
	RunIsolated([this] {
		slotPage[0] = 0x24420001u; // addiu v0,v0,1
		vtlb_VMapBuffer(BranchPC + 4, slotPage.data(), 4096);
		cpuRegs.CP0.n.Status.b.EIE = 1;
		EEInterpretDINextInstructionFromJIT(BranchPC + 4);
		EXPECT_EQ(cpuRegs.pc, BranchPC + 8);
		EXPECT_EQ(cpuRegs.GPR.r[2].UD[0], 1u);
		EXPECT_EQ(cpuRegs.CP0.n.Status.b.EIE, 0u);
		EXPECT_EQ(cpuRegs.cycle, 100u + std::max(1u, R5900::GetInstruction(0x24420001u).cycles >> 3));
		EXPECT_EQ(Cpu, &recCpu);
	});
}

TEST_F(EERecBoundaryBridge, DIContinuationUnmappedFetchKeepsEIEAndPreciseEPC)
{
	RunIsolated([this] {
		cpuRegs.CP0.n.Status.b.EIE = 1;
		EEInterpretDINextInstructionFromJIT(BranchPC + 4);
		EXPECT_EQ(cpuRegs.CP0.n.EPC, BranchPC + 4);
		EXPECT_EQ(cpuRegs.CP0.n.BadVAddr, BranchPC + 4);
		EXPECT_EQ(cpuRegs.CP0.n.Cause & 0x80000000u, 0u);
		EXPECT_EQ(cpuRegs.CP0.n.Cause & 0x7cu, 8u);
		EXPECT_EQ(cpuRegs.CP0.n.Status.b.EIE, 1u); // deferred DI not yet applied
		EXPECT_EQ(Cpu, &recCpu);
	});
}

TEST_F(EERecBoundaryBridge, DIContinuationUntakenBranchExecutesItsDelaySlot)
{
	RunIsolated([this] {
		slotPage[0] = 0x10200008u; // beq at,zero: untaken
		slotPage[1] = 0x24420001u; // slot: addiu v0,v0,1
		vtlb_VMapBuffer(BranchPC + 4, slotPage.data(), 4096);
		cpuRegs.CP0.n.Status.b.EIE = 1;
		EEInterpretDINextInstructionFromJIT(BranchPC + 4);
		EXPECT_EQ(cpuRegs.pc, BranchPC + 12);
		EXPECT_EQ(cpuRegs.GPR.r[2].UD[0], 1u);
		EXPECT_EQ(cpuRegs.CP0.n.Status.b.EIE, 0u);
		EXPECT_EQ(Cpu, &recCpu);
	});
}

TEST_F(EERecBoundaryBridge, DIContinuationUntakenLikelyAnnulsSlot)
{
	RunIsolated([this] {
		slotPage[0] = 0x50200008u; // beql at,zero: untaken / annul slot
		slotPage[1] = 0x24420001u;
		vtlb_VMapBuffer(BranchPC + 4, slotPage.data(), 4096);
		cpuRegs.CP0.n.Status.b.EIE = 1;
		EEInterpretDINextInstructionFromJIT(BranchPC + 4);
		EXPECT_EQ(cpuRegs.pc, BranchPC + 12);
		EXPECT_EQ(cpuRegs.GPR.r[2].UD[0], 0u);
		EXPECT_EQ(cpuRegs.CP0.n.Status.b.EIE, 0u);
		EXPECT_EQ(Cpu, &recCpu);
	});
}

} // namespace
