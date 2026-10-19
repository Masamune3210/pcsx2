// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+
//
// Active address-space view of the EE's architectural TLB.
// The host vTLB is indexed only by VPN, so its live mappings must be
// restricted to the selected ASID and global entries.
#pragma once

#include "R5900.h"

#include <cstddef>
#include <unordered_map>

struct EEActiveTLBPage
{
	u32 physicalAddress = 0;
	bool scratchpad = false;
	// Hardware EntryLo.D is write permission, not a host page-protection bit.
	// Preserve it even when two ASIDs select the same physical page (Linux COW).
	bool writable = true;

	bool operator==(const EEActiveTLBPage&) const = default;
};

using EEActiveTLBPageMap = std::unordered_map<u32, EEActiveTLBPage>;

// A matched, valid and non-writable mapping must raise TLB Modification on a
// guest store. An absent entry is NOT Mod; normal TLB miss handling applies.
// A direct-mapped kernel page is intentionally not represented in this map.
inline bool EEActiveTLBStoreNeedsModification(const EEActiveTLBPageMap& active, u32 address)
{
	const auto page = active.find(address >> 12);
	return page != active.end() && !page->second.scratchpad && !page->second.writable;
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
