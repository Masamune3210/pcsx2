// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "x86/BaseblockEx.h"

#include <algorithm>
#include <array>

// Native blocks keep their original linking policy. In guest-TLB mode, an
// independently remappable target VPN must be resolved at execution, including
// pending targets which have no BaseBlocks metadata to invalidate yet.
inline void EERecLinkStaticBlock(BaseBlocks& blocks, bool useGuestTLB, u32 virtualStart,
	u32 virtualTarget, u32 physicalTarget, s32* jump, uptr dispatcher, BaseBlocks::Metrics* metrics = nullptr)
{
	const bool managedTarget = virtualTarget < 0x80000000u || virtualTarget >= 0xc0000000u;
	if (useGuestTLB && managedTarget && (virtualStart >> 12) != (virtualTarget >> 12))
		BaseBlocks::PatchLink(jump, dispatcher, metrics);
	else
		blocks.Link(physicalTarget, jump, metrics);
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


// A conservative negative filter for physical code-cache invalidation.
// A page can be skipped only if no recompiler block has *ever* touched it
// during this cache generation. Bits intentionally remain set after individual
// blocks are invalidated; this prevents false negatives for overlapping
// blocks, pending incoming links, and in-flight recompilation.
template <u32 RAMBytes>
class EERecCompiledPhysicalPages
{
	static_assert(RAMBytes != 0 && (RAMBytes & 0xfffu) == 0);
	static constexpr u32 PageCount = RAMBytes >> 12;
	std::array<u64, (PageCount + 63u) / 64u> m_seen{};

public:
	void Reset() { m_seen.fill(0); }

	void Mark(u32 physicalAddress)
	{
		if (physicalAddress < RAMBytes)
		{
			const u32 page = physicalAddress >> 12;
			m_seen[page >> 6] |= (u64{1} << (page & 63u));
		}
	}

	// BaseBlocks records each compiled block as a contiguous physical extent
	// from its starting PFN. The next guest virtual page may map a different
	// PFN, so also mark the *metadata extent* even if no opcode was fetched
	// from some of its intervening physical pages. Missing such a page could
	// incorrectly skip invalidation of overlapping BaseBlocks metadata.
	void MarkMetadataExtent(u32 physicalStart, u32 byteCount)
	{
		if (!byteCount)
			return;
		const u64 end = static_cast<u64>(physicalStart) + byteCount;
		for (u64 pageStart = static_cast<u64>(physicalStart) & ~u64{0xfff};
			pageStart < end && pageStart < RAMBytes; pageStart += 0x1000u)
			Mark(static_cast<u32>(pageStart));
	}

	// Clear sizes are in 32-bit guest instructions / dwords. Covers every
	// touched physical page, including a code block crossing a 4 KiB edge.
	// Addresses outside the managed RAM window are conservatively retained.
	bool MayContainCode(u32 physicalAddress, u32 dwords) const
	{
		if (dwords == 0)
			return false;
		const u64 lastByte = static_cast<u64>(physicalAddress) + static_cast<u64>(dwords) * 4 - 1;
		if (lastByte >= RAMBytes)
			return true;
		for (u32 page = physicalAddress >> 12; page <= static_cast<u32>(lastByte >> 12); ++page)
		{
			if (m_seen[page >> 6] & (u64{1} << (page & 63u)))
				return true;
		}
		return false;
	}
};

// Shared with the standalone cache regressions. This is the recompiler's
// existing overlap/link invalidation, using physical dispatch entries only.
inline void EERecClearPhysicalBlocks(BaseBlocks& blocks, const uptr* physicalLUT,
	BASEBLOCK* compilingBlock, uptr recompile, u32 address, u32 size, BaseBlocks::Metrics* metrics = nullptr)
{
	if (size == 0)
		return;
	// Clear sizes are dwords. Keep range arithmetic wide, including a request
	// reaching the top of the 32-bit physical address space, without wrapping.
	constexpr u64 physicalEnd = u64{1} << 32;
	const u64 end = std::min(physicalEnd, static_cast<u64>(address) + static_cast<u64>(size) * 4);
	int blockidx = blocks.LastIndex(static_cast<u32>(end - 4));
	if (blockidx == -1)
		return;

	u64 lowerextent = physicalEnd, upperextent = 0, ceiling = physicalEnd;
	BASEBLOCKEX* block = blocks[blockidx + 1];
	if (block)
		ceiling = block->startpc;

	int toRemoveLast = blockidx;
	while ((block = blocks[blockidx]))
	{
		const u32 blockstart = block->startpc;
		const u64 blockend = std::min(physicalEnd, static_cast<u64>(blockstart) + static_cast<u64>(block->size) * 4);
		BASEBLOCK* dispatch = EERecGetPhysicalBlock(physicalLUT, blockstart);
		if (dispatch == compilingBlock)
		{
			if (toRemoveLast != blockidx)
				blocks.Remove(blockidx + 1, toRemoveLast, metrics);
			toRemoveLast = --blockidx;
			continue;
		}
		if (blockend <= address)
		{
			lowerextent = std::max(lowerextent, blockend);
			break;
		}
		lowerextent = std::min(lowerextent, static_cast<u64>(blockstart));
		upperextent = std::max(upperextent, blockend);
		if (metrics)
			metrics->dispatchWriteAttempts++;
		if (dispatch->GetFnptr() != recompile)
		{
			dispatch->SetFnptr(recompile);
			if (metrics)
				metrics->dispatchWrites++;
		}
		else if (metrics)
			metrics->dispatchWriteSkips++;
		blockidx--;
	}
	if (toRemoveLast != blockidx)
		blocks.Remove(blockidx + 1, toRemoveLast, metrics);
	upperextent = std::min(upperextent, ceiling);

	for (int i = 0; (block = blocks[i]); i++)
	{
		if (compilingBlock == EERecGetPhysicalBlock(physicalLUT, block->startpc))
			continue;
		const u64 blockend = static_cast<u64>(block->startpc) + static_cast<u64>(block->size) * 4;
		pxAssertMsg(!((block->startpc >= address && block->startpc < end) ||
			(block->startpc < address && blockend > address)), "[EE] Impossible block clearing failure");
	}
	if (upperextent > lowerextent)
	{
		BASEBLOCK* const first = EERecGetPhysicalBlock(physicalLUT, static_cast<u32>(lowerextent));
		for (u64 i = 0; i < (upperextent - lowerextent) / 4; i++)
		{
			if (metrics)
				metrics->dispatchWriteAttempts++;
			if (first[i].GetFnptr() != recompile)
			{
				first[i].SetFnptr(recompile);
				if (metrics)
					metrics->dispatchWrites++;
			}
			else if (metrics)
				metrics->dispatchWriteSkips++;
		}
	}
}
