// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once

// Host-side execution policy, not guest architectural state. Standard retains
// the native EE dispatch mode; Extended permits experimental OS/MMU mechanisms.
// Both must deliver architecturally correct exceptions and memory writes.
//
// The selected mode is LATCHED at VM boot. Never change it while translated
// host code may still be entered or returned to. Future dynamic transitions
// require a CPU safepoint, complete dispatch/JIT reset and savestate audit.
#include <atomic>
#include <string_view>

enum class EEExecutionProfileMode : unsigned char
{
	Standard,
	ExtendedMMU,
};

enum class EEExecutionProfilePreference : unsigned char
{
	Auto,
	Standard,
	ExtendedMMU,
};

constexpr EEExecutionProfilePreference ParseEEExecutionProfilePreference(std::string_view setting)
{
	return (setting == "Extended" || setting == "extended") ? EEExecutionProfilePreference::ExtendedMMU :
		(setting == "Standard" || setting == "standard") ? EEExecutionProfilePreference::Standard :
		EEExecutionProfilePreference::Auto;
}

// Disc serial is evidence of the boot DISC, not proof about arbitrary HDD
// contents. The known BB Navigator serial can enable the profile for an ISO
// boot; a direct no-disc HDD boot must use the explicit Extended override
// until a trustworthy HDD boot identity is implemented.
constexpr bool IsKnownBBNavigatorBootSerial(std::string_view serial)
{
	return serial == "SCPN-60160";
}

constexpr EEExecutionProfileMode ResolveEEExecutionProfile(
	EEExecutionProfilePreference preference, std::string_view bootDiscSerial)
{
	if (preference == EEExecutionProfilePreference::ExtendedMMU)
		return EEExecutionProfileMode::ExtendedMMU;
	if (preference == EEExecutionProfilePreference::Standard)
		return EEExecutionProfileMode::Standard;
	return IsKnownBBNavigatorBootSerial(bootDiscSerial) ?
		EEExecutionProfileMode::ExtendedMMU : EEExecutionProfileMode::Standard;
}

namespace EEExecutionProfile
{
	inline std::atomic<EEExecutionProfileMode> s_active{EEExecutionProfileMode::Standard};
	inline std::atomic<bool> s_bootSelectionLatched{false};

	// Return false for a second attempt, including after disc/ELF changes.
	// A future runtime switch must own JIT unwind/reset, not use this setter.
	inline bool SelectAtBoot(EEExecutionProfileMode mode)
	{
		if (s_bootSelectionLatched.exchange(true, std::memory_order_acq_rel))
			return false;
		s_active.store(mode, std::memory_order_release);
		return true;
	}

	inline bool IsExtendedMMU()
	{
		return s_active.load(std::memory_order_acquire) == EEExecutionProfileMode::ExtendedMMU;
	}

	inline void ResetAtShutdown()
	{
		s_active.store(EEExecutionProfileMode::Standard, std::memory_order_release);
		s_bootSelectionLatched.store(false, std::memory_order_release);
	}
}
