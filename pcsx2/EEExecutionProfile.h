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

// A negative code-page presence filter is an optimization, never the source
// of coherence truth. Standard must always invoke the real invalidator;
// Extended may skip only when absence of compiled code is established.
constexpr bool EECanSkipPhysicalCodeClear(EEExecutionProfileMode profile, bool mayContainCompiledCode)
{
	return profile == EEExecutionProfileMode::ExtendedMMU && !mayContainCompiledCode;
}

// The EE exception path is hot during guest OS TLB refill. Console.Error
// for each ordinary miss is not an architectural effect, and can stall
// heavily faulting Extended-MMU sessions. Standard keeps legacy diagnostics;
// Extended requires an explicit verbose opt-in.
constexpr bool EELogTLBFault(bool extendedMMU, bool verbose, bool originalLogCondition)
{
	return originalLogCondition && (!extendedMMU || verbose);
}

constexpr bool EELogBranchDelayWarning(bool extendedMMU, bool verbose, bool isTLBException)
{
	return !extendedMMU || verbose || !isTLBException;
}

// Guest IOP stdout can include extremely high-volume per-register
// instrumentation from an IOP driver. Filter only these exact diagnostic
// prefixes, never device access, guest I/O, or other guest console messages.
constexpr bool EEIsGuestDEV9RegisterTrace(std::string_view line)
{
	size_t prefixLength = 0;
	size_t valueDigits = 0;
	if (line.starts_with("DEV9 read8 ") || line.starts_with("DEV9 write8 "))
	{
		prefixLength = line.starts_with("DEV9 read8 ") ? 11 : 12;
		valueDigits = 2;
	}
	else if (line.starts_with("DEV9 read16 ") || line.starts_with("DEV9 write16 "))
	{
		prefixLength = line.starts_with("DEV9 read16 ") ? 12 : 13;
		valueDigits = 4;
	}
	else if (line.starts_with("DEV9 read32 ") || line.starts_with("DEV9 write32 "))
	{
		prefixLength = line.starts_with("DEV9 read32 ") ? 12 : 13;
		valueDigits = 8;
	}
	else
	{
		return false;
	}

	// Suppress only one complete register diagnostic, not a multi-line
	// message which happens to begin with the same prefix.
	line.remove_prefix(prefixLength);
	const size_t end = 8 + 2 + valueDigits;
	if (line.size() < end || line.substr(8, 2) != ": ")
		return false;
	const auto isHex = [](char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
			(c >= 'A' && c <= 'F');
	};
	for (size_t i = 0; i < 8; i++)
		if (!isHex(line[i]))
			return false;
	for (size_t i = 10; i < end; i++)
		if (!isHex(line[i]))
			return false;
	const std::string_view suffix = line.substr(end);
	return suffix.empty() || suffix == "\n" || suffix == "\r" || suffix == "\r\n";
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
