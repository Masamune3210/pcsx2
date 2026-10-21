// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "x86/BaseblockEx.h"

#include <algorithm>

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
