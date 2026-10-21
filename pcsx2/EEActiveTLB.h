// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+
//
// Active address-space view of the EE's architectural TLB.
// The host vTLB is indexed only by VPN, so its live mappings must be
// restricted to the selected ASID and global entries.
#pragma once

#include "R5900.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

struct EEActiveTLBPage
{
	u32 physicalAddress = 0;
	bool scratchpad = false;
	// Hardware EntryLo.D is write permission, not a host page-protection bit.
	// Preserve it even when two ASIDs select the same physical page (Linux COW).
	bool writable = true;

	// Both entries must already be present and valid. EntryLo.D lives in the
	// architectural permission map; host backing and instruction identity do not
	// change when only D changes. Keep full equality below permission-sensitive.
	bool HasSameBacking(const EEActiveTLBPage& other) const
	{
		return physicalAddress == other.physicalAddress && scratchpad == other.scratchpad;
	}

	bool operator==(const EEActiveTLBPage&) const = default;
};

using EEActiveTLBPageMap = std::unordered_map<u32, EEActiveTLBPage>;

enum class EEActiveTLBMappingAction
{
	Remove,
	Install,
	PermissionOnly,
	Unchanged,
};

// Production diff policy, independent of host mapping/JIT side effects. All
// removals precede installations. The callback must not mutate either map.
template <typename Emit>
inline void PlanEEActiveTLBMappingChanges(
	const EEActiveTLBPageMap& active, const EEActiveTLBPageMap& desired, Emit&& emit,
	bool allowPermissionOnlyShortcut = true)
{
	for (const auto& [page, old] : active)
	{
		const auto now = desired.find(page);
		if (now == desired.end() || !old.HasSameBacking(now->second) ||
			(!allowPermissionOnlyShortcut && old.writable != now->second.writable))
			emit(EEActiveTLBMappingAction::Remove, page, old);
	}
	for (const auto& [page, now] : desired)
	{
		const auto old = active.find(page);
		if (old == active.end() || !old->second.HasSameBacking(now) ||
			(!allowPermissionOnlyShortcut && old->second.writable != now.writable))
			emit(EEActiveTLBMappingAction::Install, page, now);
		else
			emit(old->second.writable == now.writable ? EEActiveTLBMappingAction::Unchanged :
				EEActiveTLBMappingAction::PermissionOnly, page, now);
	}
}

// Memoize only the inputs to BuildEEActiveTLBPageMap, never architectural TLB
// writes or cache-mode descriptors. One fixed record per slot preserves first-
// slot overlap priority, including all 48 entries, without heap allocation.
// Equality is sufficient, not necessary: a shadowed changed slot may rebuild.
class EEActiveTLBProjectionMemo
{
public:
	bool Matches(const tlbs* entries, std::size_t count, u8 asid) const
	{
		if (!m_valid || count != m_count || count > m_slots.size())
			return false;
		for (std::size_t i = 0; i < count; ++i)
		{
			if (!(m_slots[i] == SelectInput(entries[i], asid)))
				return false;
		}
		return true;
	}

	// Publish only after the corresponding active page map is installed.
	void Remember(const tlbs* entries, std::size_t count, u8 asid)
	{
		m_valid = false;
		if (count > m_slots.size())
			return;
		for (std::size_t i = 0; i < count; ++i)
			m_slots[i] = SelectInput(entries[i], asid);
		m_count = count;
		m_valid = true;
	}

	// Host mapping/dispatch reset and state load must invalidate this memo.
	void Invalidate() { m_valid = false; }

private:
	struct SlotInput
	{
		u32 vpn = 0, mask = 0, physical0 = 0, physical1 = 0;
		// Low bits encode V0,D0,V1,D1; bit 4 selects legacy SPR mapping.
		u32 flags = 0;
		bool operator==(const SlotInput&) const = default;
	};

	static SlotInput SelectInput(const tlbs& t, u8 asid)
	{
		if (!t.isGlobal() && t.EntryHi.ASID != asid)
			return {};
		if (t.isSPR())
			return {t.VPN2(), 0, 0, 0, 0x10};
		if (!t.EntryLo0.V && !t.EntryLo1.V)
			return {};
		return {t.VPN2(), t.Mask(), t.EntryLo0.V ? t.PFN0() : 0,
			t.EntryLo1.V ? t.PFN1() : 0,
			(t.EntryLo0.V ? 1u | (t.EntryLo0.D ? 2u : 0u) : 0u) |
			(t.EntryLo1.V ? 4u | (t.EntryLo1.D ? 8u : 0u) : 0u)};
	}

	std::array<SlotInput, 48> m_slots{};
	std::size_t m_count = 0;
	bool m_valid = false;
};

// This tracker belongs to one synchronous architectural-TLB projection.
// A physical compiled-code page needs clearing at most once per projection:
// replacing multiple virtual aliases cannot execute guest instructions between
// those clears. Unmapped/scratchpad and non-RAM pages retain their original
// invalidation behavior. Create a NEW tracker for each projection.
class EEPhysicalCodeInvalidationSet
{
public:
	bool ShouldInvalidate(const EEActiveTLBPage& page, u32 exposedRAMSize)
	{
		if (page.scratchpad || page.physicalAddress >= exposedRAMSize)
			return true;

		const u32 physicalPage = page.physicalAddress >> 12;
		if (std::find(m_firstPages.begin(), m_firstPages.begin() + m_firstCount, physicalPage) !=
			m_firstPages.begin() + m_firstCount)
			return false;

		// Almost all TLB refills touch only a handful of pages. Avoid creating
		// a host heap allocation for every ordinary/native-game TLB update.
		if (m_firstCount < m_firstPages.size())
		{
			m_firstPages[m_firstCount++] = physicalPage;
			return true;
		}
		return m_extraPages.insert(physicalPage).second;
	}

private:
	std::array<u32, 16> m_firstPages{};
	std::size_t m_firstCount = 0;
	std::unordered_set<u32> m_extraPages;
};

// A matched, valid and non-writable mapping must raise TLB Modification on a
// guest store. An absent entry is NOT Mod; normal TLB miss handling applies.
// Fixed kernel aliases bypass TLB write permissions, even if present in the map.
inline bool EEActiveTLBStoreNeedsModification(const EEActiveTLBPageMap& active, u32 address)
{
	if (address >= 0x80000000u && address < 0xc0000000u)
		return false; // fixed KSEG0/1 aliases are not governed by TLB entries
	const auto page = active.find(address >> 12);
	return page != active.end() && !page->second.scratchpad && !page->second.writable;
}

// Base/immediate arithmetic wraps at 32 bits. SQ and SQC2 ignore the low four
// address bits; merged word/dword stores retain their original fault address.
inline u32 EEActiveTLBStoreAddress(u32 base, u32 opcode)
{
	const u32 address = base + static_cast<s32>(static_cast<s16>(opcode & 0xffffu));
	const u32 op = opcode >> 26;
	return (op == 0x1fu || op == 0x3eu) ? address & ~0xfu : address;
}

// Pure selector: no host memory access and no emulated CPU side effects.
// Operates on actual R5900 tlbs types, so tests exercise production logic.
inline EEActiveTLBPageMap BuildEEActiveTLBPageMap(const tlbs* entries, std::size_t entryCount, u8 asid)
{
	EEActiveTLBPageMap result;

	for (std::size_t i = 0; i < entryCount; i++)
	{
		const tlbs& t = entries[i];
		if (!t.isGlobal() && t.EntryHi.ASID != asid)
			continue;

		if (t.isSPR())
		{
			// EE SPR is 16KB and uses a special buffer instead of a PFN.
			// Preserve the legacy scratchpad mapping semantics.
			for (u32 n = 0; n < 4; n++)
				result.try_emplace((t.VPN2() >> 12) + n, EEActiveTLBPage{n << 12, true});
			continue;
		}

		const u32 halfPages = t.Mask() + 1;
		const u32 firstPage = t.VPN2() >> 12;
		const u32 compareMask = ((~t.Mask()) << 1) & 0xfffff;
		const u32 compareVpn = firstPage & compareMask;

		for (u32 half = 0; half < 2; half++)
		{
			const EntryLo_t& lo = half ? t.EntryLo1 : t.EntryLo0;
			if (!lo.V)
				continue;

			const u32 basePage = firstPage + half * halfPages;
			const u32 basePhysical = half ? t.PFN1() : t.PFN0();
			for (u32 n = 0; n < halfPages; n++)
			{
				const u32 virtualPage = basePage + n;
				if ((virtualPage & compareMask) == compareVpn)
					result.try_emplace(virtualPage, EEActiveTLBPage{basePhysical + (n << 12), false, lo.D != 0});
			}
		}
	}

	// Multiple simultaneously matching entries for the *same* ASID/VPN have
	// architecturally undefined priority. First TLB slot wins for determinism.
	return result;
}
