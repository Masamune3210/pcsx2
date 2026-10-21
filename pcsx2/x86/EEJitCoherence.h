// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <array>
#include <cstddef>

// These describe observed invalidation callers, not inferred instruction writers.
// In particular a host write-protection fault does not identify CPU versus DMA.
enum class EEJitInvalidationCause : u8
{
	Unknown,
	VirtualClear,
	TLBProjection,
	ASIDProjection,
	ProjectionReset,
	HostWriteProtection,
	ManualIntegrity,
	ManualReprotect,
	OverlapRecompile,
	Count
};

inline constexpr const char* EEJitInvalidationCauseName(EEJitInvalidationCause cause)
{
	constexpr const char* names[] = {"unknown", "virtual-clear", "tlb-projection",
		"asid-projection", "projection-reset", "host-write-protection",
		"manual-integrity", "manual-reprotect", "overlap-recompile"};
	static_assert(sizeof(names) / sizeof(names[0]) == static_cast<std::size_t>(EEJitInvalidationCause::Count));
	const auto index = static_cast<std::size_t>(cause);
	return index < static_cast<std::size_t>(EEJitInvalidationCause::Count) ? names[index] : "unknown";
}

// Implemented by the x86 EE recompiler. Existing virtual CPU-provider callbacks
// remain separate so a known physical address is never translated twice.
void recClearPhysical(u32 address, u32 dwords,
	EEJitInvalidationCause cause = EEJitInvalidationCause::Unknown);
void EEJitRecordFastmemBackpatch(u32 hostBytes);

// Diagnostic source fingerprints are NOT a correctness/reuse key: data mapping,
// compiler assumptions and lifetime dependencies are deliberately not inferred.
struct EEJitSourceFingerprint
{
	u32 virtualPC = 0;
	u32 physicalPC = 0;
	u32 words = 0;
	u32 mode = 0;
	u64 content = 0;
	bool operator==(const EEJitSourceFingerprint&) const = default;
};

template <std::size_t Capacity = 4096>
class EEJitSourceHistory
{
	static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0);
	struct Entry
	{
		EEJitSourceFingerprint key{};
		bool valid = false;
	};
	std::array<Entry, Capacity> m_entries{};

public:
	void Reset() { m_entries = {}; }

	// Bounded direct-mapped history. Collisions can hide repeats; matching keys
	// count only source-fingerprint repeats, never proven reusable translations.
	bool Observe(const EEJitSourceFingerprint& key)
	{
		const u64 hash = key.content ^ (u64{key.virtualPC} << 17) ^
			(u64{key.physicalPC} << 1) ^ (u64{key.words} << 7) ^ key.mode;
		Entry& entry = m_entries[(hash ^ (hash >> 32)) & (Capacity - 1)];
		const bool repeated = entry.valid && entry.key == key;
		entry = {key, true};
		return repeated;
	}
};
