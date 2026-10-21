// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "x86/EERecCoherence.h"
#include <gtest/gtest.h>

TEST(EERecCoherencePolicy, TranslationMutatorsEndFineBlocks)
{
	for (const u32 code : {0x42000001u, 0x42000002u, 0x42000006u, 0x40825000u, 0x40805000u})
	{
		EXPECT_EQ(EERecPlanTranslationBoundary(true, code, false), EERecTranslationBoundary::EndAfterInstruction);
		EXPECT_EQ(EERecPlanTranslationBoundary(true, code, true), EERecTranslationBoundary::DispatchAfterBranch);
		EXPECT_EQ(EERecPlanTranslationBoundary(false, code, false), EERecTranslationBoundary::None);
	}
}

TEST(EERecCoherencePolicy, PreparingTLBEntriesDoesNotChangeTranslation)
{
	for (const u32 code : {0x40821000u, 0x40821800u, 0x40822800u, 0x42000008u, 0x40025000u, 0x24420001u})
		EXPECT_FALSE(EERecInstructionChangesTranslation(code));
}

TEST(EERecCoherencePolicy, TranslationSlotsCannotMoveAheadOfBranchLinkWrite)
{
	constexpr u32 mtc0RaEntryHi = 0x409f5000u;
	EXPECT_FALSE(EERecCanSwapTranslationSlot(true, mtc0RaEntryHi));
	EXPECT_FALSE(EERecCanSwapTranslationSlot(true, 0x42000002u));
	EXPECT_TRUE(EERecCanSwapTranslationSlot(false, mtc0RaEntryHi));
	EXPECT_TRUE(EERecCanSwapTranslationSlot(true, 0x24420001u));
}

TEST(EERecCoherencePolicy, FineBoundarySlotsAreFetchedAtRuntime)
{
	EXPECT_TRUE(EERecNeedsRuntimeBoundarySlot(true, 0x1000fffcu, true));
	EXPECT_TRUE(EERecNeedsRuntimeBoundarySlot(true, 0x1000fffcu, false));
	EXPECT_FALSE(EERecNeedsRuntimeBoundarySlot(false, 0x1000fffcu, true));
	EXPECT_TRUE(EERecNeedsRuntimeBoundarySlot(false, 0x1000fffcu, false));
	EXPECT_FALSE(EERecNeedsRuntimeBoundarySlot(true, 0x1000fff8u, true));
}

TEST(EERecCoherencePolicy, TakenRegisterJumpToSlotDoesNotRunSlotTwice)
{
	EXPECT_TRUE(EERecNeedsFallthroughDelaySlot(0x10ffcu, 0x11000u, false));
	EXPECT_FALSE(EERecNeedsFallthroughDelaySlot(0x10ffcu, 0x11000u, true));
	EXPECT_FALSE(EERecNeedsFallthroughDelaySlot(0x10ffcu, 0x11004u, false));
}
