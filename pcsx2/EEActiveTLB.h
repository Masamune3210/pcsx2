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
#include <optional>
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


	// A changed TLB slot cannot affect a guest virtual page outside the union
	// of its old and new selected-page projections. On a small edit, precisely
	// enumerate that union instead of re-expanding/diffing all 48 slots' pages.
	// Anything too broad or without a trusted old snapshot falls back to the
	// established full projection algorithm. The output contains unique VPNs.
	template <std::size_t N>
	bool CollectChangedPages(const tlbs* entries, std::size_t count, u8 asid,
		std::array<u32, N>& pages, std::size_t& pageCount) const
	{
		pageCount = 0;
		if (!m_valid || count != m_count || count > m_slots.size())
			return false;
		for (std::size_t i = 0; i < count; ++i)
		{
			const SlotInput current = SelectInput(entries[i], asid);
			if (m_slots[i] == current)
				continue;
			if (!AppendSlotPages(m_slots[i], pages, pageCount) ||
				!AppendSlotPages(current, pages, pageCount))
				return false;
		}
		return true;
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


	template <std::size_t N>
	static bool AppendSlotPages(const SlotInput& slot, std::array<u32, N>& pages,
		std::size_t& count)
	{
		const auto add = [&](u32 vpn) {
			if (std::find(pages.begin(), pages.begin() + count, vpn) != pages.begin() + count)
				return true;
			if (count == N)
				return false;
			pages[count++] = vpn;
			return true;
		};
		if (slot.flags & 0x10u)
		{
			for (u32 n = 0; n < 4; ++n)
				if (!add((slot.vpn >> 12) + n))
					return false;
			return true;
		}
		if (!(slot.flags & (1u | 4u)))
			return true;
		const u32 halfPages = slot.mask + 1;
		// Conservatively reject large masks before the enumeration loop.
		if (halfPages > N)
			return false;
		const u32 firstPage = slot.vpn >> 12;
		const u32 compareMask = ((~slot.mask) << 1) & 0xfffff;
		const u32 compareVpn = firstPage & compareMask;
		for (u32 half = 0; half < 2; ++half)
		{
			if (!(slot.flags & (half ? 4u : 1u)))
				continue;
			const u32 basePage = firstPage + half * halfPages;
			for (u32 n = 0; n < halfPages; ++n)
			{
				const u32 page = basePage + n;
				if ((page & compareMask) == compareVpn && !add(page))
					return false;
			}
		}
		return true;
	}

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


// Evaluate the selected first-slot TLB winner for exactly one VPN.
// Must agree with BuildEEActiveTLBPageMap (including SPR and first-slot wins),
// rather than assuming that the changed TLB slot owns its candidate pages.
inline std::optional<EEActiveTLBPage> FindEEActiveTLBPage(
	const tlbs* entries, std::size_t entryCount, u8 asid, u32 virtualPage)
{
	for (std::size_t i = 0; i < entryCount; ++i)
	{
		const tlbs& t = entries[i];
		if (!t.isGlobal() && t.EntryHi.ASID != asid)
			continue;
		if (t.isSPR())
		{
			const u32 offset = virtualPage - (t.VPN2() >> 12);
			if (offset < 4)
				return EEActiveTLBPage{offset << 12, true};
			continue;
		}
		const u32 halfPages = t.Mask() + 1;
		const u32 firstPage = t.VPN2() >> 12;
		const u32 compareMask = ((~t.Mask()) << 1) & 0xfffff;
		if ((virtualPage & compareMask) != (firstPage & compareMask))
			continue;
		for (u32 half = 0; half < 2; ++half)
		{
			const EntryLo_t& lo = half ? t.EntryLo1 : t.EntryLo0;
			if (!lo.V)
				continue;
			const u32 offset = virtualPage - (firstPage + half * halfPages);
			if (offset < halfPages)
			{
				const u32 base = half ? t.PFN1() : t.PFN0();
				return EEActiveTLBPage{base + (offset << 12), false, lo.D != 0};
			}
		}
	}
	return std::nullopt;
}

// Resolve a bounded set of VPNs in one slot-major walk. The original
// FindEEActiveTLBPage() is the independent single-page correctness oracle.
// First matching TLB slot wins, exactly as in the full projection builder.
// Avoid re-decoding a slot's mask/ASID/EntryLo for every candidate VPN.
template <std::size_t N>
inline void FindEEActiveTLBPages(
	const tlbs* entries, std::size_t entryCount, u8 asid,
	const std::array<u32, N>& pages, std::size_t pageCount,
	std::array<std::optional<EEActiveTLBPage>, N>& resolved)
{
	for (std::size_t j = 0; j < pageCount; ++j)
		resolved[j].reset();
	std::size_t unresolved = pageCount;
	for (std::size_t i = 0; i < entryCount && unresolved; ++i)
	{
		const tlbs& t = entries[i];
		if (!t.isGlobal() && t.EntryHi.ASID != asid)
			continue;
		const u32 firstPage = t.VPN2() >> 12;
		if (t.isSPR())
		{
			for (std::size_t j = 0; j < pageCount; ++j)
			{
				if (resolved[j])
					continue;
				const u32 offset = pages[j] - firstPage;
				if (offset < 4)
				{
					resolved[j] = EEActiveTLBPage{offset << 12, true};
					--unresolved;
				}
			}
			continue;
		}
		const u32 halfPages = t.Mask() + 1;
		const u32 compareMask = ((~t.Mask()) << 1) & 0xfffff;
		const u32 compareVpn = firstPage & compareMask;
		for (std::size_t j = 0; j < pageCount; ++j)
		{
			if (resolved[j] || (pages[j] & compareMask) != compareVpn)
				continue;
			for (u32 half = 0; half < 2; ++half)
			{
				const EntryLo_t& lo = half ? t.EntryLo1 : t.EntryLo0;
				if (!lo.V)
					continue;
				const u32 offset = pages[j] - (firstPage + half * halfPages);
				if (offset < halfPages)
				{
					const u32 physical = half ? t.PFN1() : t.PFN0();
					resolved[j] = EEActiveTLBPage{physical + (offset << 12), false, lo.D != 0};
					--unresolved;
					break;
				}
			}
		}
	}
}

// Plan changes for a bounded candidate VPN set. Caller performs all removals
// before installs and only publishes new map entries after the host side
// effects, exactly as for the full-map projection. No shared state is mutated.
struct EEActiveTLBDelta
{
	u32 page = 0;
	std::optional<EEActiveTLBPage> oldMapping;
	std::optional<EEActiveTLBPage> newMapping;
	bool NeedsRemove() const
	{
		return oldMapping && (!newMapping || !oldMapping->HasSameBacking(*newMapping));
	}
	bool NeedsInstall() const
	{
		return newMapping && (!oldMapping || !oldMapping->HasSameBacking(*newMapping));
	}
};
// Publish only changed selected mappings. The caller has already completed
// remove-before-install host side effects, so identical old/new permissions
// and backing need no unordered_map insertion or assignment.
template <std::size_t N>
inline std::size_t PublishEEActiveTLBDelta(
	EEActiveTLBPageMap& active, const std::array<EEActiveTLBDelta, N>& changes,
	std::size_t pageCount)
{
	std::size_t updates = 0;
	for (std::size_t i = 0; i < pageCount; ++i)
	{
		const EEActiveTLBDelta& change = changes[i];
		if (change.oldMapping == change.newMapping)
			continue;
		if (change.newMapping)
			active.insert_or_assign(change.page, *change.newMapping);
		else
			active.erase(change.page);
		++updates;
	}
	return updates;
}

template <std::size_t N>
inline std::size_t PlanEEActiveTLBDelta(const EEActiveTLBPageMap& active,
	const tlbs* entries, std::size_t entryCount, u8 asid,
	const std::array<u32, N>& pages, std::size_t pageCount,
	std::array<EEActiveTLBDelta, N>& changes)
{
	std::array<std::optional<EEActiveTLBPage>, N> resolved{};
	FindEEActiveTLBPages(entries, entryCount, asid, pages, pageCount, resolved);
	for (std::size_t i = 0; i < pageCount; ++i)
	{
		EEActiveTLBDelta& change = changes[i];
		change.page = pages[i];
		const auto old = active.find(change.page);
		change.oldMapping = old == active.end() ? std::nullopt :
			std::optional<EEActiveTLBPage>(old->second);
		change.newMapping = resolved[i];
	}
	return pageCount;
}
