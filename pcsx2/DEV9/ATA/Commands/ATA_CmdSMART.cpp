// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "DEV9/ATA/ATA.h"
#include "DEV9/DEV9.h"
#include "common/FileSystem.h"

#include <algorithm>
#include <array>
#include <cstring>

void ATA::HDD_Smart()
{
	DevCon.WriteLn("DEV9: HDD_Smart");

	if ((regStatus & ATA_STAT_READY) == 0)
		return;

	if (regHcyl != 0xC2 || regLcyl != 0x4F)
	{
		CmdNoDataAbort();
		return;
	}

	if (!fetSmartEnabled && regFeature != 0xD8)
	{
		CmdNoDataAbort();
		return;
	}

	switch (regFeature)
	{
		case 0xD9: //SMART_DISABLE
			SMART_EnableOps(false);
			return;
		case 0xD8: //SMART_ENABLE
			SMART_EnableOps(true);
			return;
		case 0xD2: //SMART_ATTR_AUTOSAVE
			SMART_SetAutoSaveAttribute();
			return;
		case 0xD3: //SMART_ATTR_SAVE
			SMART_SaveAttribute();
			return;
		case 0xDA: //SMART_STATUS (is fault in disk?)
			SMART_ReturnStatus();
			return;
		case 0xD1: //SMART_READ_THRESH
			Console.Error("DEV9: ATA: SMART_READ_THRESH Not Implemented");
			CmdNoDataAbort();
			return;
		case 0xD0: //SMART_READ_DATA
			SMART_ReadData();
			return;
		case 0xD5: //SMART_READ_LOG
			Console.Error("DEV9: ATA: SMART_READ_LOG Not Implemented");
			CmdNoDataAbort();
			return;
		case 0xD4: //SMART_EXECUTE_OFFLINE
			SMART_ExecuteOfflineImmediate();
			return;
		default:
			Console.Error("DEV9: ATA: Unknown SMART command %x", regFeature);
			CmdNoDataAbort();
			return;
	}
}

void ATA::SMART_SetAutoSaveAttribute()
{
	if (!PreCmd())
		return;
	const bool previous = smartAutosave;
	switch (regNsector)
	{
		case 0x00:
			smartAutosave = false;
			break;
		case 0xF1:
			smartAutosave = true;
			break;
		default:
			Console.Error("DEV9: ATA: Unknown SMART_ATTR_AUTOSAVE command %02x", regNsector);
			CmdNoDataAbort();
			return;
	}
	if (!SaveSMARTSettings())
	{
		smartAutosave = previous;
		CmdNoDataAbort();
		return;
	}
	PostCmdNoData();
}

void ATA::SMART_SaveAttribute()
{
	if (!PreCmd())
		return;
	if (!SaveSMARTState())
	{
		regStatus |= ATA_STAT_WRERR;
		CmdNoDataAbort();
		return;
	}
	PostCmdNoData();
}

void ATA::SMART_ExecuteOfflineImmediate()
{
	if (!PreCmd())
		return;
	// ATA-4 defines Sector Number as not applicable. Later self-test selectors
	// were previously accepted as fake successes; this is solely offline data
	// collection, backed by a full image read in bounded asynchronous chunks.
	if (smartScanActive)
		SMART_Cancel();
	smartScanActive = true;
	smartScanSector = smartScanCycles = 0;
	smartScanImageSectors = hddImageSize / 512;
	smartOfflineStatus = 0x40; // Vendor-defined: verification in progress.
	if (smartScanStarted != ~u32(0))
		++smartScanStarted;
	smartDirty = true;
	PostCmdNoData();
}

void ATA::SMART_EnableOps(bool enable)
{
	if (!PreCmd())
		return;
	const bool previous = fetSmartEnabled;
	fetSmartEnabled = enable;
	if (!SaveSMARTSettings())
	{
		fetSmartEnabled = previous;
		CmdNoDataAbort();
		return;
	}
	if (!enable)
		SMART_Cancel();
	PostCmdNoData();
}

void ATA::SMART_ReturnStatus()
{
	if (!PreCmd())
		return;
	if (!smartErrors)
	{
		regHcyl = 0xC2;
		regLcyl = 0x4F;
	}
	else
	{
		regHcyl = 0x2C;
		regLcyl = 0xF4;
	}
	PostCmdNoData();
}

void ATA::SMART_BuildData(u8* data) const
{
	std::memset(data, 0, 512);
	data[0] = 1; // Vendor data revision; no fabricated physical SMART attributes.
	data[362] = smartOfflineStatus;
	// Estimate at the emulated 8 MiB/s scan schedule. Host latency and host
	// commands may extend this; the ATA-4 word is a 1..65535 second estimate.
	const u64 scanSectors = smartScanImageSectors ? smartScanImageSectors : hddImageSize / 512;
	const u16 seconds = static_cast<u16>(std::clamp<u64>((scanSectors + 16383) / 16384, 1, 65535));
	data[364] = seconds & 0xFF;
	data[365] = seconds >> 8;
	data[367] = 1; // Offline implemented; bit2=0: suspend/resume on host command.
	data[368] = 2; // Event autosave; no power-mode saving or later feature claims.
	std::memcpy(data + 386, "PCSX2VSMART", 11);
	data[397] = 1; // Sidecar/vendor-field format version.
	data[398] = smartErrors ? 1 : 0;
	const auto put = [data](size_t offset, u64 value, size_t count) {
		for (size_t i = 0; i < count; ++i)
			data[offset + i] = static_cast<u8>(value >> (i * 8));
	};
	put(400, smartScanStarted, 4);
	put(404, smartScanCompleted, 4);
	put(408, smartScanFailed, 4);
	put(412, smartScanInterrupted, 4);
	put(416, smartScanSector, 8);
	put(424, smartLastFailureSector, 8);
	put(432, smartScanImageSectors, 8);
	put(440, smartSaveFailures, 4);
	u8 sum = 0;
	for (size_t i = 0; i < 511; ++i)
		sum += data[i];
	data[511] = static_cast<u8>(0 - sum);
}

void ATA::SMART_ReadData()
{
	if (!PreCmd())
		return;
	u8 data[512];
	SMART_BuildData(data);
	DRQCmdPIODataToHost(data, sizeof(data), 0, sizeof(data), true);
}

void ATA::SMART_Interrupt()
{
	if (!smartScanActive || smartOfflineStatus == 4)
		return;
	smartOfflineStatus = 4;
	if (smartScanInterrupted != ~u32(0))
		++smartScanInterrupted;
	smartDirty = true;
}

void ATA::SMART_Cancel()
{
	if (smartScanActive)
	{
		smartScanActive = false;
		smartOfflineStatus = 5;
		if (smartScanInterrupted != ~u32(0))
			++smartScanInterrupted;
		smartDirty = true;
	}
	// Reset/close are vendor autosave events. No automatic metadata writes are
	// issued while in Standby or Sleep (§6.11.6).
	if (hddImage && fetSmartEnabled && smartAutosave && smartDirty &&
		powerState != PowerState::Standby && powerState != PowerState::Sleep && !SaveSMARTState())
		Console.Error("DEV9: ATA: Failed to autosave SMART state on interruption");
}

void ATA::SMART_Async(u32 cycles)
{
	if (!hddImage || cycles == static_cast<u32>(-1) || !fetSmartEnabled ||
		powerState == PowerState::Standby || powerState == PowerState::Sleep ||
		(regStatus & (ATA_STAT_BUSY | ATA_STAT_DRQ)) != 0)
		return;
	if (!smartScanActive)
		return;
	// At most 64 KiB per pump; the accumulated budget cannot create an
	// unbounded burst after the emulator was busy handling guest commands.
	constexpr u64 chunkCycles = 36864000 / 128; // 8 MiB/s of emulated IOP time.
	smartScanCycles = std::min<u64>(smartScanCycles + cycles, chunkCycles);
	if (smartScanCycles < chunkCycles)
		return;
	bool failed = false;
	{
		std::lock_guard lock(ioMutex);
		// Accepted writes must drain first; reads share FILE* positioning too.
		if (ioRead || ioWrite || !writeQueue.IsQueueEmpty())
			return;
		smartScanCycles = 0;
		smartOfflineStatus = 0x40;
		std::array<u8, 64 * 1024> buffer;
		const size_t sectors = static_cast<size_t>(std::min<u64>(128, smartScanImageSectors - smartScanSector));
		if (sectors != 0)
		{
			std::clearerr(hddImage);
			const bool positioned = FileSystem::FSeek64(hddImage, smartScanSector * 512, SEEK_SET) == 0;
			const size_t actual = positioned ? std::fread(buffer.data(), 512, sectors, hddImage) : 0;
			failed = !positioned || actual != sectors || std::ferror(hddImage);
			smartScanSector += actual;
			std::clearerr(hddImage);
		}
	}
	smartDirty = true;
	if (!failed && smartScanSector != smartScanImageSectors)
		return;
	smartScanActive = false;
	if (failed)
	{
		smartOfflineStatus = 6;
		smartErrors = true; // Vendor reliability policy: failed backing-image verification.
		smartLastFailureSector = smartScanSector;
		if (smartScanFailed != ~u32(0))
			++smartScanFailed;
	}
	else
	{
		smartOfflineStatus = 2;
		if (smartScanCompleted != ~u32(0))
			++smartScanCompleted;
	}
	// §8.41.4 requires the collected data to be saved, independently of D2.
	// No guest taskfile or IRQ is changed by any background completion.
	if (!SaveSMARTState())
	{
		smartOfflineStatus = 6;
		smartErrors = true;
		Console.Error("DEV9: ATA: Failed to persist completed SMART verification");
	}
}
