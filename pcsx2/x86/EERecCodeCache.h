// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "x86/BaseblockEx.h"

#include <algorithm>

// Native blocks keep their original linking policy. In guest-TLB mode, an
// independently remappable target VPN must be resolved at execution, including
// pending targets which have no BaseBlocks metadata to invalidate yet.
inline void EERecLinkStaticBlock(BaseBlocks& blocks, bool useGuestTLB, u32 virtualStart,
	u32 virtualTarget, u32 physicalTarget, s32* jump, uptr dispatcher)
{
	const bool managedTarget = virtualTarget < 0x80000000u || virtualTarget >= 0xc0000000u;
	if (useGuestTLB && managedTarget && (virtualStart >> 12) != (virtualTarget >> 12))
		*jump = static_cast<s32>(dispatcher - reinterpret_cast<uptr>(jump + 1));
	else
		blocks.Link(physicalTarget, jump);
}

// A readable physical RAM pointer does not make an unmapped guest VPN executable.
// The dispatch-page bias is the same one used by recUnmapTLBPage.
inline const u32* EERecGetInstructionPtr(bool useGuestTLB, u32 address, uptr dispatchPage,
	const BASEBLOCK* unmapped, const u32* physicalCode)
{
	const uptr unmappedPage = reinterpret_cast<uptr>(unmapped) -
		static_cast<uptr>(address & 0xffff0000u) * (sizeof(BASEBLOCK) / 4);
	return useGuestTLB && dispatchPage == unmappedPage ? nullptr : physicalCode;
}

inline void EERecEnterGuestTLBDispatch(u32& mode, bool& reset)
{
	if (mode == 0)
	{
		mode = 1;
		reset = true; // discard links emitted under native assumptions
	}
}

inline void EERecResetDispatchMode(u32& mode, bool externalReset)
{
	if (externalReset)
		mode = 0;
}

// BaseBlocks keys and all extents below are PHYSICAL addresses. The fixed
// physical LUT must never be replaced by the active guest-virtual dispatch
// LUT: a low physical address can itself be an unmapped/remapped guest VPN.
inline BASEBLOCK* EERecGetPhysicalBlock(const uptr* physicalLUT, u32 address)
{
	return reinterpret_cast<BASEBLOCK*>(physicalLUT[address >> 16] +
		static_cast<uptr>(address) * (sizeof(BASEBLOCK) / 4));
}

// Shared with the standalone cache regressions. This is the recompiler's
// existing overlap/link invalidation, using physical dispatch entries only.
inline void EERecClearPhysicalBlocks(BaseBlocks& blocks, const uptr* physicalLUT,
	BASEBLOCK* compilingBlock, uptr recompile, u32 address, u32 size)
{
	if (size == 0)
		return;
	int blockidx = blocks.LastIndex(address + size * 4 - 4);
	if (blockidx == -1)
		return;

	u32 lowerextent = static_cast<u32>(-1), upperextent = 0, ceiling = static_cast<u32>(-1);
	BASEBLOCKEX* block = blocks[blockidx + 1];
	if (block)
		ceiling = block->startpc;

	int toRemoveLast = blockidx;
	while ((block = blocks[blockidx]))
	{
		const u32 blockstart = block->startpc;
		const u32 blockend = blockstart + block->size * 4;
		BASEBLOCK* dispatch = EERecGetPhysicalBlock(physicalLUT, blockstart);
		if (dispatch == compilingBlock)
		{
			if (toRemoveLast != blockidx)
				blocks.Remove(blockidx + 1, toRemoveLast);
			toRemoveLast = --blockidx;
			continue;
		}
		if (blockend <= address)
		{
			lowerextent = std::max(lowerextent, blockend);
			break;
		}
		lowerextent = std::min(lowerextent, blockstart);
		upperextent = std::max(upperextent, blockend);
		dispatch->SetFnptr(recompile);
		blockidx--;
	}
	if (toRemoveLast != blockidx)
		blocks.Remove(blockidx + 1, toRemoveLast);
	upperextent = std::min(upperextent, ceiling);

	for (int i = 0; (block = blocks[i]); i++)
	{
		if (compilingBlock == EERecGetPhysicalBlock(physicalLUT, block->startpc))
			continue;
		const u32 blockend = block->startpc + block->size * 4;
		pxAssertMsg(!((block->startpc >= address && block->startpc < address + size * 4) ||
			(block->startpc < address && blockend > address)), "[EE] Impossible block clearing failure");
	}
	if (upperextent > lowerextent)
	{
		BASEBLOCK* const first = EERecGetPhysicalBlock(physicalLUT, lowerextent);
		for (u32 i = 0; i < (upperextent - lowerextent) / 4; i++)
			first[i].SetFnptr(recompile);
	}
}
