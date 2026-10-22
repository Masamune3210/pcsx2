// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <array>

// This diagnostic compares copies of instruction bytes and physical-page
// identities. It does not establish that host-generated code can be reused:
// constant data loads, active return addresses and JIT links also matter.
struct EERecSourceWitnessPage
{
	u32 guestAddress = 0;
	u32 physicalAddress = 0;
	u32 words = 0;
	u64 hash = 0;
};

struct EERecSourceWitnessSnapshot
{
	static constexpr u32 MaxPages = 8;
	std::array<EERecSourceWitnessPage, MaxPages> pages{};
	u32 pageCount = 0;
	bool complete = false;
};

enum class EERecSourceWitnessVerdict : u8
{
	Incomplete,
	PhysicalBytesChanged,
	PhysicalBytesUnchangedVirtualUnmapped,
	PhysicalBytesUnchangedMappingChanged,
	PhysicalBytesUnchangedMappingSame
};

constexpr const char* EERecSourceWitnessVerdictName(EERecSourceWitnessVerdict value)
{
	switch (value)
	{
		case EERecSourceWitnessVerdict::Incomplete: return "incomplete";
		case EERecSourceWitnessVerdict::PhysicalBytesChanged: return "physical-bytes-changed";
		case EERecSourceWitnessVerdict::PhysicalBytesUnchangedVirtualUnmapped: return "same-bytes-virtual-unmapped";
		case EERecSourceWitnessVerdict::PhysicalBytesUnchangedMappingChanged: return "same-bytes-mapping-changed";
		case EERecSourceWitnessVerdict::PhysicalBytesUnchangedMappingSame: return "same-bytes-mapping-same";
	}
	return "incomplete";
}

constexpr EERecSourceWitnessVerdict EERecClassifySourceWitness(
	bool complete, bool physicalBytesReadable, bool physicalBytesUnchanged,
	bool virtualMapped, bool virtualSamePhysical)
{
	if (!complete || !physicalBytesReadable)
		return EERecSourceWitnessVerdict::Incomplete;
	if (!physicalBytesUnchanged)
		return EERecSourceWitnessVerdict::PhysicalBytesChanged;
	if (!virtualMapped)
		return EERecSourceWitnessVerdict::PhysicalBytesUnchangedVirtualUnmapped;
	return virtualSamePhysical ?
		EERecSourceWitnessVerdict::PhysicalBytesUnchangedMappingSame :
		EERecSourceWitnessVerdict::PhysicalBytesUnchangedMappingChanged;
}
