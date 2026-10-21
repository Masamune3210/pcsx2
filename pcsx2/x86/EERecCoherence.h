// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"

// Compiler policy only: execution still uses the normal COP0 implementations.
// TLBR also changes the active ASID through WriteCP0EntryHi(). EntryLo/PageMask
// writes only prepare a TLB write and do not yet change a translation.
constexpr bool EERecInstructionChangesTranslation(u32 code)
{
	if ((code >> 26) != 0x10u)
		return false;
	const u32 rs = (code >> 21) & 31u;
	if (rs == 4u) // MTC0 EntryHi
		return ((code >> 11) & 31u) == 10u;
	if (rs != 16u)
		return false;
	const u32 function = code & 63u;
	return function == 1u || function == 2u || function == 6u; // TLBR/TLBWI/TLBWR
}

constexpr bool EERecCanSwapTranslationSlot(bool fineDispatch, u32 code)
{
	// Moving MTC0 EntryHi ahead of JAL/JALR can read the pre-link GPR or
	// change the translation used to evaluate a branch. Keep slot ordering.
	return !fineDispatch || !EERecInstructionChangesTranslation(code);
}

enum class EERecTranslationBoundary
{
	None,
	EndAfterInstruction,
	DispatchAfterBranch,
};

constexpr EERecTranslationBoundary EERecPlanTranslationBoundary(bool fineDispatch, u32 code, bool delaySlot)
{
	if (!fineDispatch || !EERecInstructionChangesTranslation(code))
		return EERecTranslationBoundary::None;
	return delaySlot ? EERecTranslationBoundary::DispatchAfterBranch :
		EERecTranslationBoundary::EndAfterInstruction;
}

// A fine-mode branch at a page's last word fetches its slot at execution time,
// including when the next page currently happens to be contiguous. Native
// dispatch is unchanged. This closes the branch-slot source dependency hole;
// recDI's eager following instruction remains a separate cross-page blocker.
constexpr bool EERecNeedsRuntimeBoundarySlot(bool fineDispatch, u32 branchPC, bool slotReadable)
{
	return (branchPC & 0xfffu) == 0xffcu && (fineDispatch || !slotReadable);
}

// KUSEG and KSEG2/3 constant-address loads/stores can embed a vTLB
// translation selected *at compile time*. Their host pointer or handler can
// become stale after a data-only remap even when instruction bytes are stable.
// This classification is for diagnostic dependency accounting; it does NOT
// currently authorize code reuse or alter emitted load/store instructions.
constexpr bool EERecConstantAddressUsesGuestTLB(u32 address)
{
	return address < 0x80000000u || address >= 0xc0000000u;
}

// DI intentionally postpones changing EIE until the following instruction
// (a longstanding PCSX2 compatibility behavior). Do not compile that next
// instruction into a fine-mode block at the last word of a guest page:
// its instruction PFN can be unrelated or not mapped yet. Execute exactly
// that continuation at runtime instead. The encompassing branch bridge already
// owns delay slots, so do not create a nested interpreter bridge there.
constexpr bool EERecNeedsRuntimeDINextInstruction(bool fineDispatch, u32 nextPC, bool delaySlot)
{
	return fineDispatch && !delaySlot && (nextPC & 0xfffu) == 0;
}

// Ordinary untaken interpreter branches leave PC at the unexecuted slot;
// likely branches annul it by advancing PC again. A taken JR can deliberately
// target the slot's address, so the taken marker is required as well as PC.
constexpr bool EERecNeedsFallthroughDelaySlot(u32 branchPC, u32 currentPC, bool branchTaken)
{
	return !branchTaken && currentPC == branchPC + 4u;
}
