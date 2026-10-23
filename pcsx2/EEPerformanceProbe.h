// SPDX-FileCopyrightText: 2026 PCSX2 contributors
// SPDX-License-Identifier: GPL-3.0+
#pragma once

// Small, opt-in host-side EE performance accounting. No guest instructions,
// guest-visible state, file logging or dispatch policy are altered.
// Totals are inclusive: TLB synchronization can invoke physical invalidation,
// so these categories must not be added together as disjoint CPU time.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <initializer_list>

namespace EEPerformanceProbe
{
using Clock = std::chrono::steady_clock;
struct Event
{
	std::atomic<std::uint64_t> calls{0};
	std::atomic<std::uint64_t> nanoseconds{0};
};

inline std::atomic<bool> enabled{false};
inline Event compilations;
inline Event tlbSyncs;
// Disjoint TLB sync routes. Categories sum to tlbSyncs, barring a
// concurrent read while the emulator is still running.
inline Event tlbMemoHits;
inline Event tlbSmallDeltas;
inline Event tlbFullRebuilds;
inline Event physicalClearWork;
inline Event jitResets;
inline std::atomic<std::uint64_t> compiledHostBytes{0};
inline std::atomic<std::uint64_t> physicalClearRequests{0};
inline std::atomic<std::uint64_t> neverCompiledSkips{0};
inline std::atomic<std::uint64_t> asidSyncs{0};
inline std::atomic<std::uint64_t> candidatePages{0};
inline std::atomic<std::uint64_t> removedMappings{0};
inline std::atomic<std::uint64_t> installedMappings{0};
inline std::atomic<std::uint64_t> permissionUpdates{0};
inline std::atomic<std::uint64_t> unchangedMappings{0};

inline void Configure(bool active)
{
	// Only called at the start of a new EE recompiler provider lifetime.
	enabled.store(false, std::memory_order_relaxed);
	for (Event* event : {&compilations, &tlbSyncs, &tlbMemoHits,
		&tlbSmallDeltas, &tlbFullRebuilds, &physicalClearWork, &jitResets})
	{
		event->calls.store(0, std::memory_order_relaxed);
		event->nanoseconds.store(0, std::memory_order_relaxed);
	}
	compiledHostBytes.store(0, std::memory_order_relaxed);
	physicalClearRequests.store(0, std::memory_order_relaxed);
	neverCompiledSkips.store(0, std::memory_order_relaxed);
	for (std::atomic<std::uint64_t>* counter : {&asidSyncs, &candidatePages,
		&removedMappings, &installedMappings, &permissionUpdates, &unchangedMappings})
		counter->store(0, std::memory_order_relaxed);
	enabled.store(active, std::memory_order_relaxed);
}

inline bool Enabled()
{
	return enabled.load(std::memory_order_relaxed);
}

inline void Record(Event& event, Clock::time_point started)
{
	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
	event.calls.fetch_add(1, std::memory_order_relaxed);
	event.nanoseconds.fetch_add(static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
}

inline void RecordTLBSync(Event& category, Clock::time_point started,
	bool asid, std::uint64_t candidates, std::uint64_t removed,
	std::uint64_t installed, std::uint64_t permissionOnly, std::uint64_t unchanged)
{
	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
	const auto nanos = static_cast<std::uint64_t>(elapsed);
	tlbSyncs.calls.fetch_add(1, std::memory_order_relaxed);
	tlbSyncs.nanoseconds.fetch_add(nanos, std::memory_order_relaxed);
	category.calls.fetch_add(1, std::memory_order_relaxed);
	category.nanoseconds.fetch_add(nanos, std::memory_order_relaxed);
	if (asid)
		asidSyncs.fetch_add(1, std::memory_order_relaxed);
	candidatePages.fetch_add(candidates, std::memory_order_relaxed);
	removedMappings.fetch_add(removed, std::memory_order_relaxed);
	installedMappings.fetch_add(installed, std::memory_order_relaxed);
	permissionUpdates.fetch_add(permissionOnly, std::memory_order_relaxed);
	unchangedMappings.fetch_add(unchanged, std::memory_order_relaxed);
}

inline std::uint64_t Read(const std::atomic<std::uint64_t>& value)
{
	return value.load(std::memory_order_relaxed);
}
} // namespace EEPerformanceProbe
