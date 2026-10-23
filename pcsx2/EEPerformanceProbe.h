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
inline Event physicalClearWork;
inline Event jitResets;
inline std::atomic<std::uint64_t> compiledHostBytes{0};
inline std::atomic<std::uint64_t> physicalClearRequests{0};
inline std::atomic<std::uint64_t> neverCompiledSkips{0};

inline void Configure(bool active)
{
	// Only called at the start of a new EE recompiler provider lifetime.
	enabled.store(false, std::memory_order_relaxed);
	for (Event* event : {&compilations, &tlbSyncs, &physicalClearWork, &jitResets})
	{
		event->calls.store(0, std::memory_order_relaxed);
		event->nanoseconds.store(0, std::memory_order_relaxed);
	}
	compiledHostBytes.store(0, std::memory_order_relaxed);
	physicalClearRequests.store(0, std::memory_order_relaxed);
	neverCompiledSkips.store(0, std::memory_order_relaxed);
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

inline std::uint64_t Read(const std::atomic<std::uint64_t>& value)
{
	return value.load(std::memory_order_relaxed);
}
} // namespace EEPerformanceProbe
