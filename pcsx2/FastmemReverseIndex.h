// SPDX-FileCopyrightText: 2026 PCSX2 contributors
// SPDX-License-Identifier: GPL-3.0+
#pragma once

// Intrusive reverse fastmem map. For every mapped guest virtual 4 KiB page,
// maintain one node indexed by the VPN, linked into its backing file-offset
// bucket. It eliminates per-remap unordered_multimap allocations and bucket
// searches without changing host Map/Unmap, page protection, or alias rules.
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

class FastmemReverseIndex
{
public:
	using Page = std::uint32_t;
	static constexpr Page Unmapped = std::numeric_limits<Page>::max();
	static constexpr Page PageShift = 12;
	static constexpr Page PageSize = 1u << PageShift;

	void Initialize(std::size_t virtualPageCount, std::size_t backingBytes)
	{
		m_heads.assign((backingBytes + PageSize - 1) / PageSize, Unmapped);
		m_next.assign(virtualPageCount, Unmapped);
		m_prev.assign(virtualPageCount, Unmapped);
	}

	void Clear()
	{
		std::fill(m_heads.begin(), m_heads.end(), Unmapped);
		std::fill(m_next.begin(), m_next.end(), Unmapped);
		std::fill(m_prev.begin(), m_prev.end(), Unmapped);
	}

	void Reset()
	{
		m_heads.clear();
		m_next.clear();
		m_prev.clear();
		m_heads.shrink_to_fit();
		m_next.shrink_to_fit();
		m_prev.shrink_to_fit();
	}

	void Insert(Page virtualPage, Page backingOffset)
	{
		assert((backingOffset & (PageSize - 1)) == 0);
		assert(virtualPage < m_next.size());
		const Page bucket = backingOffset >> PageShift;
		assert(bucket < m_heads.size());
		const Page first = m_heads[bucket];
		m_prev[virtualPage] = Unmapped;
		m_next[virtualPage] = first;
		if (first != Unmapped)
			m_prev[first] = virtualPage;
		m_heads[bucket] = virtualPage;
	}

	void Erase(Page virtualPage, Page oldBackingOffset)
	{
		assert((oldBackingOffset & (PageSize - 1)) == 0);
		assert(virtualPage < m_next.size());
		const Page bucket = oldBackingOffset >> PageShift;
		assert(bucket < m_heads.size());
		const Page prev = m_prev[virtualPage];
		const Page next = m_next[virtualPage];
		if (prev == Unmapped)
		{
			assert(m_heads[bucket] == virtualPage);
			m_heads[bucket] = next;
		}
		else
			m_next[prev] = next;
		if (next != Unmapped)
			m_prev[next] = prev;
		m_next[virtualPage] = Unmapped;
		m_prev[virtualPage] = Unmapped;
	}

	template <typename Fn>
	void ForEach(Page backingOffset, Fn&& callback) const
	{
		assert((backingOffset & (PageSize - 1)) == 0);
		const Page bucket = backingOffset >> PageShift;
		assert(bucket < m_heads.size());
		for (Page page = m_heads[bucket]; page != Unmapped; page = m_next[page])
			callback(page);
	}

private:
	std::vector<Page> m_heads;
	std::vector<Page> m_next;
	std::vector<Page> m_prev;
};
