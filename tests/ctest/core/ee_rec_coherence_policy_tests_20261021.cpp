// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "EEExecutionProfile.h"
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

TEST(EERecCoherencePolicy, DelayedDIFetchCrossingGuestPageUsesRuntimeInterpreter)
{
	// `pc` has already advanced past the DI opcode during recompilation.
	EXPECT_TRUE(EERecNeedsRuntimeDINextInstruction(true, 0x01001000u, false));
	EXPECT_TRUE(EERecNeedsRuntimeDINextInstruction(true, 0x01002000u, false));
	EXPECT_FALSE(EERecNeedsRuntimeDINextInstruction(true, 0x01000ffcu, false));
	EXPECT_FALSE(EERecNeedsRuntimeDINextInstruction(false, 0x01001000u, false));
	// Branch code owns its delay slot, so no nested interpreter bridge.
	EXPECT_FALSE(EERecNeedsRuntimeDINextInstruction(true, 0x01001000u, true));
}

TEST(EERecCoherencePolicy, GuestTLBManagedConstantDataAddressesNeedDependenciesBeforeReuse)
{
	EXPECT_TRUE(EERecConstantAddressUsesGuestTLB(0x00000000u));
	EXPECT_TRUE(EERecConstantAddressUsesGuestTLB(0x7fffffffu));
	EXPECT_TRUE(EERecConstantAddressUsesGuestTLB(0xc0000000u));
	EXPECT_TRUE(EERecConstantAddressUsesGuestTLB(0xffffffffu));
	EXPECT_FALSE(EERecConstantAddressUsesGuestTLB(0x80000000u));
	EXPECT_FALSE(EERecConstantAddressUsesGuestTLB(0xa0000000u));
	EXPECT_FALSE(EERecConstantAddressUsesGuestTLB(0xbfffffffu));
}

TEST(EEExecutionProfile, TlbFaultLogSuppressionKeepsStandardAndExplicitVerboseBehavior)
{
	// Standard mode must continue reporting its original recompiler
	// and developer-build TLB exceptions without a new setting.
	EXPECT_TRUE(EELogTLBFault(false, false, true));
	EXPECT_TRUE(EELogTLBFault(false, true, true));
	EXPECT_FALSE(EELogTLBFault(false, false, false));
	// Extended guest TLB refill is routine: suppress logging by default.
	EXPECT_FALSE(EELogTLBFault(true, false, true));
	EXPECT_TRUE(EELogTLBFault(true, true, true));
	EXPECT_FALSE(EELogTLBFault(true, true, false));
}

TEST(EEExecutionProfile, BranchDelayDiagnosticSuppressionIsOnlyForExtendedTlbExceptions)
{
	EXPECT_TRUE(EELogBranchDelayWarning(false, false, true));
	EXPECT_TRUE(EELogBranchDelayWarning(false, false, false));
	EXPECT_FALSE(EELogBranchDelayWarning(true, false, true));
	EXPECT_TRUE(EELogBranchDelayWarning(true, true, true));
	EXPECT_TRUE(EELogBranchDelayWarning(true, false, false));
}

TEST(EEExecutionProfile, AutomaticRecognitionRequiresVerifiedBootDiscSerial)
{
	EXPECT_EQ(ResolveEEExecutionProfile(EEExecutionProfilePreference::Auto, "SCPN-60160"),
		EEExecutionProfileMode::ExtendedMMU);
	EXPECT_EQ(ResolveEEExecutionProfile(EEExecutionProfilePreference::Auto, "SLUS-00000"),
		EEExecutionProfileMode::Standard);
	EXPECT_EQ(ResolveEEExecutionProfile(EEExecutionProfilePreference::Auto, ""),
		EEExecutionProfileMode::Standard);
}

TEST(EEExecutionProfile, ManualPreferenceOverridesDiscDetection)
{
	EXPECT_EQ(ResolveEEExecutionProfile(EEExecutionProfilePreference::Standard, "SCPN-60160"),
		EEExecutionProfileMode::Standard);
	EXPECT_EQ(ResolveEEExecutionProfile(EEExecutionProfilePreference::ExtendedMMU, ""),
		EEExecutionProfileMode::ExtendedMMU);
	EXPECT_EQ(ParseEEExecutionProfilePreference("Extended"), EEExecutionProfilePreference::ExtendedMMU);
	EXPECT_EQ(ParseEEExecutionProfilePreference("Standard"), EEExecutionProfilePreference::Standard);
	EXPECT_EQ(ParseEEExecutionProfilePreference("Auto"), EEExecutionProfilePreference::Auto);
	EXPECT_EQ(ParseEEExecutionProfilePreference("unknown"), EEExecutionProfilePreference::Auto);
}

TEST(EEExecutionProfile, RuntimeSelectionIsLatchedUntilShutdown)
{
	EEExecutionProfile::ResetAtShutdown();
	EXPECT_FALSE(EEExecutionProfile::IsExtendedMMU());
	EXPECT_TRUE(EEExecutionProfile::SelectAtBoot(EEExecutionProfileMode::ExtendedMMU));
	EXPECT_TRUE(EEExecutionProfile::IsExtendedMMU());
	EXPECT_FALSE(EEExecutionProfile::SelectAtBoot(EEExecutionProfileMode::Standard));
	EXPECT_TRUE(EEExecutionProfile::IsExtendedMMU());
	EEExecutionProfile::ResetAtShutdown();
	EXPECT_TRUE(EEExecutionProfile::SelectAtBoot(EEExecutionProfileMode::Standard));
	EXPECT_FALSE(EEExecutionProfile::IsExtendedMMU());
	EEExecutionProfile::ResetAtShutdown();
}

TEST(EEExecutionProfile, StandardNeverSuppressesPhysicalClearFromNegativePageFilter)
{
	// A bitmap is only an optimization. The Standard profile must always
	// reach the real invalidation machinery, even if the filter says empty.
	EXPECT_FALSE(EECanSkipPhysicalCodeClear(EEExecutionProfileMode::Standard, false));
	EXPECT_FALSE(EECanSkipPhysicalCodeClear(EEExecutionProfileMode::Standard, true));
	EXPECT_TRUE(EECanSkipPhysicalCodeClear(EEExecutionProfileMode::ExtendedMMU, false));
	EXPECT_FALSE(EECanSkipPhysicalCodeClear(EEExecutionProfileMode::ExtendedMMU, true));
}

TEST(EEExecutionProfile, ConsecutiveVMSessionsDoNotInheritEarlierBootSelection)
{
	EEExecutionProfile::ResetAtShutdown();
	EXPECT_TRUE(EEExecutionProfile::SelectAtBoot(ResolveEEExecutionProfile(
		EEExecutionProfilePreference::Auto, "SCPN-60160")));
	EXPECT_TRUE(EEExecutionProfile::IsExtendedMMU());
	// VMManager::Shutdown on normal exit and CPUThreadShutdown on thread exit
	// must perform this reset; otherwise the next boot's SelectAtBoot fails.
	EEExecutionProfile::ResetAtShutdown();
	EXPECT_TRUE(EEExecutionProfile::SelectAtBoot(ResolveEEExecutionProfile(
		EEExecutionProfilePreference::Auto, "SLUS-00000")));
	EXPECT_FALSE(EEExecutionProfile::IsExtendedMMU());
	EEExecutionProfile::ResetAtShutdown();
}
