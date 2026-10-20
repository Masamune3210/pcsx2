// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "common/Assertions.h"
#include "common/FileSystem.h"

#include "ATA.h"
#include "DEV9/DEV9.h"

#include <cerrno>
#include <cstring>

#if _WIN32
#include "pathcch.h"
#include <io.h>
#elif defined(__POSIX__)
#define INVALID_HANDLE_VALUE -1
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
static_assert(sizeof(off_t) >= 8, "off_t is not 64bit");
#endif

ATA::ATA()
{
	//Power on, Would do self-Diag + Hardware Init
	ResetBegin();
	ResetEnd(true);
}

ATA::~ATA()
{
	Close();
}

int ATA::Open(const std::string& hddPath)
{
	Close();
	ATA_HardReset();
	memset(sceSec, 0, sizeof(sceSec));
	// SMART configuration is drive metadata, separate from guest disk sectors.
	fetSmartEnabled = true;
	smartAutosave = true;
	smartSettingsPath = (Path::IsAbsolute(hddPath) ? hddPath :
		Path::Combine(FileSystem::GetWorkingDirectory(), hddPath)) + ".ata-settings";
	errno = 0;
	std::FILE* settings = FileSystem::OpenCFile(smartSettingsPath.c_str(), "rb");
	if (settings)
	{
		u8 data[8];
		const bool valid = std::fread(data, 1, sizeof(data), settings) == sizeof(data) &&
			std::fgetc(settings) == EOF && !std::ferror(settings) &&
			std::memcmp(data, "PCSX2A", 6) == 0 && data[6] == 1 && (data[7] & ~0x03) == 0;
		const bool closed = std::fclose(settings) == 0;
		if (!valid || !closed)
		{
			Console.Error("DEV9: ATA: Invalid SMART settings sidecar");
			return -1;
		}
		fetSmartEnabled = (data[7] & 1) != 0;
		smartAutosave = (data[7] & 2) != 0;
	}
	else if (errno != ENOENT)
	{
		Console.Error("DEV9: ATA: Unable to read SMART settings sidecar");
		return -1;
	}

	DevCon.WriteLn("DEV9: ATA: HddFile : %s", hddPath.c_str());

	//Open File
	if (!FileSystem::FileExists(hddPath.c_str()))
		return -1;

	hddImage = FileSystem::OpenCFile(hddPath.c_str(), "r+b");
	const s64 size = hddImage ? FileSystem::FSize64(hddImage) : -1;
	if (!hddImage || size < 0)
	{
		Console.Error("DEV9: ATA: Failed to open HDD image '%s'", hddPath.c_str());
		Close();
		return -1;
	}
	readBufferLen = 256 * 512;
	readBuffer = new u8[readBufferLen];

	// Open and read the content of the hddid file
	std::string hddidPath = Path::ReplaceExtension(hddPath, "hddid");
	std::optional<std::vector<u8>> fileContent = FileSystem::ReadBinaryFile(hddidPath.c_str());

	if (fileContent.has_value() && fileContent.value().size() <= sizeof(sceSec))
	{
		// Copy the content to sceSec
		std::copy(fileContent.value().begin(), fileContent.value().end(), sceSec);
	}
	else
	{
		// fill sceSec with default data if hdd id file is not present
		memcpy(sceSec, "Sony Computer Entertainment Inc.", 32); // Always this magic header.
		memcpy(sceSec + 0x20, "SCPH-20401", 10); // sometimes this matches HDD model, the rest 6 bytes filles with zeroes, or sometimes with spaces
		memcpy(sceSec + 0x30, "  40", 4); // or " 120" for PSX DESR, reference for ps2 area size. The rest bytes filled with zeroes

		sceSec[0x40] = 0; // 0x40 - 0x43 - 4-byte HDD internal SCE serial, does not match real HDD serial, currently hardcoded to 0x1000000
		sceSec[0x41] = 0;
		sceSec[0x42] = 0;
		sceSec[0x43] = 0x01;

		// purpose of next 12 bytes is unknown
		sceSec[0x44] = 0; // always zero
		sceSec[0x45] = 0; // always zero
		sceSec[0x46] = 0x1a;
		sceSec[0x47] = 0x01;
		sceSec[0x48] = 0x02;
		sceSec[0x49] = 0x20;
		sceSec[0x4a] = 0; // always zero
		sceSec[0x4b] = 0; // always zero
		// next 4 bytes always these values
		sceSec[0x4c] = 0x01;
		sceSec[0x4d] = 0x03;
		sceSec[0x4e] = 0x11;
		sceSec[0x4f] = 0x01;
		// 0x50 - 0x80 is a random unique block of data
		// 0x80 and up - zero filled
	}

	//Store HddImage size for later use
	hddImageSize = static_cast<u64>(size);
	lba48Supported = (hddImageSize > ((static_cast<s64>(1) << 28) - 1) * 512);

	CreateHDDinfo(hddImageSize / 512);

	InitSparseSupport(hddPath);

	{
		std::lock_guard ioSignallock(ioMutex);
		ioRead = false;
		ioWrite = false;
		ioReadError = false;
		ioWriteError = false;
		ioErrorPending = false;
		ioErrorSector = 0;
		ioClose.store(false);
		ioThreadIdle_bool = false;
	}

	ioThread = std::thread(&ATA::IO_Thread, this);
	ioRunning = true;

	return 0;
}

bool ATA::SaveSMARTSettings()
{
	if (!hddImage || smartSettingsPath.empty())
		return false;
	const u8 data[8] = {'P', 'C', 'S', 'X', '2', 'A', 1,
		static_cast<u8>((fetSmartEnabled ? 1 : 0) | (smartAutosave ? 2 : 0))};
	const std::string temporaryPath = smartSettingsPath + ".tmp";
	// Exclusive creation neither truncates nor follows a pre-existing temp entry.
	std::FILE* settings = FileSystem::OpenCFile(temporaryPath.c_str(), "wbx");
	if (!settings)
		return false;
	bool success = std::fwrite(data, 1, sizeof(data), settings) == sizeof(data) &&
		std::fflush(settings) == 0;
#ifdef _WIN32
	if (success)
		success = _commit(_fileno(settings)) == 0;
#elif defined(__POSIX__)
	if (success)
		success = fsync(fileno(settings)) == 0;
#else
	success = false;
#endif
	if (std::fclose(settings) != 0)
		success = false;
	if (success)
	{
#ifdef _WIN32
		const std::wstring source = FileSystem::GetWin32Path(temporaryPath);
		const std::wstring target = FileSystem::GetWin32Path(smartSettingsPath);
		success = MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
		success = FileSystem::RenamePath(temporaryPath.c_str(), smartSettingsPath.c_str());
#endif
	}
	if (!success)
	{
		FileSystem::DeleteFilePath(temporaryPath.c_str());
		return false;
	}
	// Rename commits the setting. A later directory-sync failure cannot safely
	// restore the old file, so retain the committed setting and report the limit.
#if defined(__POSIX__)
	const std::string directory = std::string(Path::GetDirectory(smartSettingsPath));
	const int directoryFD = open(directory.empty() ? "." : directory.c_str(), O_RDONLY | O_DIRECTORY);
	if (directoryFD < 0)
		Console.Error("DEV9: ATA: SMART settings committed; unable to sync directory for power-loss durability");
	else
	{
		if (fsync(directoryFD) != 0)
			Console.Error("DEV9: ATA: SMART settings committed; directory sync failed for power-loss durability");
		close(directoryFD);
	}
#endif
	return true;
}

void ATA::InitSparseSupport(const std::string& hddPath)
{
#ifdef _WIN32
	hddSparse = false;

	const std::wstring wHddPath = FileSystem::GetWin32Path(hddPath);
	const DWORD fileAttributes = GetFileAttributes(wHddPath.c_str());
	hddSparse = fileAttributes & FILE_ATTRIBUTE_SPARSE_FILE;

	if (!hddSparse)
		return;

	// Get OS specific file handle for spare writing.
	// HANDLE is owned by FILE* hddImage.
	hddNativeHandle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(hddImage)));
	if (hddNativeHandle == INVALID_HANDLE_VALUE)
	{
		Console.Error("DEV9: ATA: Failed to open file for sparse");
		hddSparse = false;
		return;
	}

	// Get sparse block size (Initially assumed as 4096 bytes).
	hddSparseBlockSize = 4096;

	// We need the drive letter for the drive the file actually resides on
	// which means we need to deal with any junction links in the path.
	DWORD len = GetFinalPathNameByHandle(hddNativeHandle, nullptr, 0, FILE_NAME_NORMALIZED);

	if (len != 0)
	{
		std::unique_ptr<TCHAR[]> name = std::make_unique<TCHAR[]>(len);
		len = GetFinalPathNameByHandle(hddNativeHandle, name.get(), len, FILE_NAME_NORMALIZED);
		if (len != 0)
		{
			PCWSTR rootEnd;
			if (PathCchSkipRoot(name.get(), &rootEnd) == S_OK)
			{
				const size_t rootLength = rootEnd - name.get();
				std::wstring finalPath(name.get(), rootLength);

				DWORD sectorsPerCluster;
				DWORD bytesPerSector;
				DWORD temp1, temp2;
				if (GetDiskFreeSpace(finalPath.c_str(), &sectorsPerCluster, &bytesPerSector, &temp1, &temp2) == TRUE)
					hddSparseBlockSize = sectorsPerCluster * bytesPerSector;
				else
					Console.Error("DEV9: ATA: Failed to get sparse block size (GetDiskFreeSpace() returned false)");
			}
			else
				Console.Error("DEV9: ATA: Failed to get sparse block size (PathCchSkipRoot() returned false)");
		}
		else
			Console.Error("DEV9: ATA: Failed to get sparse block size (PathBuildRoot() returned 0)");
	}
	else
		Console.Error("DEV9: ATA: Failed to get sparse block size (GetFinalPathNameByHandle() returned 0)");

	/*  https://askbob.tech/the-ntfs-blog-sparse-and-compressed-file/
	 *  NTFS Sparse Block Size are the same size as a compression unit
	 *  Cluster Size    Compression Unit
	 *  --------------------------------
	 *  512bytes         8kb (0x02000)
	 *    1kb           16kb (0x04000)
	 *    2kb           32kb (0x08000)
	 *    4kb           64kb (0x10000)
	 *    8kb           64kb (0x10000)
	 *   16kb           64kb (0x10000)
	 *   32kb           64kb (0x10000)
	 *   64kb           64kb (0x10000)
	 *  --------------------------------
	 */

	// Get the filesystem type.
	WCHAR fsName[MAX_PATH + 1];
	const BOOL ret = GetVolumeInformationByHandleW(hddNativeHandle, nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH);
	if (ret == FALSE)
	{
		Console.Error("DEV9: ATA: Failed to get sparse block size (GetVolumeInformationByHandle() returned false)");
		// Assume NTFS.
		wcscpy(fsName, L"NTFS");
	}
	if ((wcscmp(fsName, L"NTFS") == 0))
	{
		switch (hddSparseBlockSize)
		{
			case 512:
				hddSparseBlockSize = 8192;
				break;
			case 1024:
				hddSparseBlockSize = 16384;
				break;
			case 2048:
				hddSparseBlockSize = 32768;
				break;
			case 4096:
			case 8192:
			case 16384:
			case 32768:
			case 65536:
				hddSparseBlockSize = 65536;
				break;
			default:
				break;
		}
	}
	// Otherwise assume SparseBlockSize == block size.

#elif defined(__POSIX__)
	// fd is owned by FILE* hddImage.
	hddNativeHandle = fileno(hddImage);
	hddSparse = false;
	if (hddNativeHandle != -1)
	{
		// No way to check if we can hole punch without trying it
		// so just assume sparse files are supported.
		hddSparse = true;

		// Get sparse block size (Initially assumed as 4096 bytes).
		hddSparseBlockSize = 4096;
		struct stat fileInfo;
		if (fstat(hddNativeHandle, &fileInfo) == 0)
			hddSparseBlockSize = fileInfo.st_blksize;
		else
			Console.Error("DEV9: ATA: Failed to get sparse block size (fstat returned != 0)");
	}
	else
		Console.Error("DEV9: ATA: Failed to open file for sparse");
#endif
	hddSparseBlock = std::make_unique<u8[]>(hddSparseBlockSize);
	hddSparseBlockValid = false;
}

void ATA::Close()
{
	//Wait for async code to finish
	if (ioRunning)
	{
		ioClose.store(true);
		{
			std::lock_guard ioSignallock(ioMutex);
			ioWrite = true;
		}
		ioReady.notify_all();

		ioThread.join();
		ioRunning = false;
	}

	// Joining drains accepted writes; incomplete commands never enter the queue.
	if (hddImage && !IO_Flush())
		Console.Error("DEV9: ATA: Failed to flush HDD image on close");

	//Close File Handle
	if (hddSparse)
	{
		// hddNativeHandle is owned by hddImage.
		// It will get closed in fclose(hddImage).
		hddNativeHandle = INVALID_HANDLE_VALUE;

		hddSparse = false;
		hddSparseBlock = nullptr;
		hddSparseBlockValid = false;
	}
	if (hddImage)
	{
		std::fclose(hddImage);
		hddImage = nullptr;
	}

	delete[] readBuffer;
	readBuffer = nullptr;
	readBufferLen = 0;
	delete[] currentWrite;
	currentWrite = nullptr;
	currentWriteLength = 0;
	dmaReady = false;
	pioDataOut = false;
	pioPtr = pioEnd = 0;
	waitingCmd = nullptr;
	pioDRQEndTransferFunc = nullptr;
	awaitFlush = false;
	rdTransferred = wrTransferred = 0;
	nsector = nsectorLeft = 0;
}

void ATA::ResetBegin()
{
	// Synchronize with any file read before reusing its buffer/task file. Already
	// accepted writes remain queued; only the unfinished host transfer is canceled.
	std::lock_guard ioSignallock(ioMutex);
	ioRead = false;
	ioReadError = false;
	ioErrorPending = ioWriteError;
	dmaReady = false;
	pioDataOut = false;
	pioPtr = pioEnd = 0;
	pioDRQEndTransferFunc = nullptr;
	waitingCmd = nullptr;
	awaitFlush = false;
	rdTransferred = wrTransferred = 0;
	nsector = nsectorLeft = 0;
	delete[] currentWrite;
	currentWrite = nullptr;
	currentWriteLength = 0;
	currentWriteSectors = 0;
	regStatus = 0;
	regError = 0;
	regSelect = 0;
	regControlHOBRead = false;
	regFeatureHOB = regNsectorHOB = regSectorHOB = regLcylHOB = regHcylHOB = 0;
	lba48 = false;
	DEV9ResetATAFIFO();
	PreCmdExecuteDeviceDiag();
}
void ATA::ResetEnd(bool hard)
{
	curHeads = 16;
	curSectors = 63;
	curCylinders = 0;
	curCHSValid = true;
	curMultipleSectorsSetting = 128;
	if (powerState == PowerState::Sleep)
		powerState = PowerState::Standby;
	standbyRemaining = standbyPeriod;

	//UDMA Mode setting is preserved
	//across SRST
	if (hard)
	{
		powerState = PowerState::Active;
		standbyPeriod = standbyRemaining = 0;
		pioMode = 4;
		mdmaMode = 2;
		udmaMode = -1;
	}
	else
	{
		pioMode = 4;
		if (udmaMode == -1)
			mdmaMode = 2;
	}

	regStatus |= ATA_STAT_SEEK;
	regStatusSeekLock = 0;

	HDD_ExecuteDeviceDiag(false);
	regControlEnableIRQ = false;
	if (hddImage)
		CreateHDDinfo(hddImageSize / 512);
}

void ATA::ATA_HardReset()
{
	//DevCon.WriteLn("DEV9: *ATA_HARD RESET");
	ResetBegin();
	ResetEnd(true);
	softwareReset = false;
}

u16 ATA::Read(u32 addr, int width)
{
	switch (addr)
	{
		case ATA_R_DATA:
			// The emulated drive exposes a 16-bit data port, not the optional
			// SET FEATURES 8-bit transfer extension. Invalid widths consume no word.
			return width == 16 ? ATAreadPIO() : 0xffff;
		case ATA_R_ERROR:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_ERROR %dbit read %x, Active %s", width, regError, (GetSelectedDevice() == 0) ? "True" : "False");
			if (GetSelectedDevice() != 0)
				return 0;
			return regError;
		case ATA_R_NSECTOR:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_NSECTOR %dbit read %x, Active %s", width, nsector, (GetSelectedDevice() == 0) ? "True" : "False");
			if (GetSelectedDevice() != 0)
				return 0;
			if (!regControlHOBRead)
				return regNsector;
			else
				return regNsectorHOB;
		case ATA_R_SECTOR:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_NSECTOR %dbit read %x, Active %s", width, regSector, (GetSelectedDevice() == 0) ? "True" : "False");
			if (GetSelectedDevice() != 0)
				return 0;
			if (!regControlHOBRead)
				return regSector;
			else
				return regSectorHOB;
		case ATA_R_LCYL:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_LCYL %dbit read %x, Active %s", width, regLcyl, (GetSelectedDevice() == 0) ? "True" : "False");
			if (GetSelectedDevice() != 0)
				return 0;
			if (!regControlHOBRead)
				return regLcyl;
			else
				return regLcylHOB;
		case ATA_R_HCYL:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_HCYL %dbit read %x, Active %s", width, regHcyl, (GetSelectedDevice() == 0) ? " True " : " False ");
			if (GetSelectedDevice() != 0)
				return 0;
			if (!regControlHOBRead)
				return regHcyl;
			else
				return regHcylHOB;
		case ATA_R_SELECT:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_SELECT %dbit read %x, Active %s", width, regSelect, (GetSelectedDevice() == 0) ? " True " : " False ");
			return regSelect;
		case ATA_R_STATUS:
			if (GetSelectedDevice() != 0)
				return 0;
			// Clear irqcause
			pendingInterrupt = false;
			dev9.irqcause &= ~ATA_INTR_INTRQ;
			[[fallthrough]];
		case ATA_R_ALT_STATUS:
			//DevCon.WriteLn("DEV9: ATA: %s %dbit read %x, Active %s", addr == ATA_R_ALT_STATUS ? "ATA_R_ALT_STATUS" : "ATA_R_STATUS", width, regStatus, (GetSelectedDevice() == 0) ? " True " : " False ");

			if (!EmuConfig.DEV9.HddEnable)
				return 0xff7f; // PS2 confirmed response when no HDD is actually connected. The Expansion bay always says HDD support is connected.

			if (GetSelectedDevice() != 0)
				return 0;

			// When an error occurs, the seek bit shall not be changed until the Status Register is read, after which the bit then indicates the current Seek status.
			// This handles reporting the locked value, and then unlocking if read form STATUS rather then ALT_STATUS.
			// locking is performed where the errror occurs, by setting regStatusSeekLock to either 1 or -1 based on the locked SEEK value.
			if (regStatusSeekLock != 0)
			{
				u8 hard = (regStatus & ~ATA_STAT_SEEK);
				hard |= (regStatusSeekLock > 0) ? ATA_STAT_SEEK : static_cast<u8>(0);
				if (addr == ATA_R_STATUS)
					regStatusSeekLock = 0;
				return hard;
			}

			return regStatus;
		default:
			Console.Error("DEV9: ATA: Unknown %dbit read at address %x", width, addr);
			return 0xff;
	}
}

void ATA::Write(u32 addr, u16 value, int width)
{
	if (addr == ATA_R_DATA)
	{
		if (width == 16)
			ATAwritePIO(value);
		return;
	}
	if (addr != ATA_R_CONTROL && (softwareReset || powerState == PowerState::Sleep))
		return;
	if (addr == ATA_R_SELECT && (regStatus & ATA_STAT_DRQ) != 0)
		value = (regSelect & ~0x10) | (value & 0x10);
	if (addr != ATA_R_CONTROL &&
		((regStatus & ATA_STAT_BUSY) != 0 || ((regStatus & ATA_STAT_DRQ) != 0 && addr != ATA_R_SELECT)))
	{
		// Invalid host sequencing must not replace a live transfer's buffer.
		return;
	}
	switch (addr)
	{
		case ATA_R_FEATURE:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_FEATURE %dbit write %x", width, value);
			ClearHOB();
			regFeatureHOB = regFeature;
			regFeature = static_cast<u8>(value);
			break;
		case ATA_R_NSECTOR:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_NSECTOR %dbit write %x", width, value);
			ClearHOB();
			regNsectorHOB = regNsector;
			regNsector = static_cast<u8>(value);
			break;
		case ATA_R_SECTOR:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_SECTOR %dbit write %x", width, value);
			ClearHOB();
			regSectorHOB = regSector;
			regSector = static_cast<u8>(value);
			break;
		case ATA_R_LCYL:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_LCYL %dbit write %x", width, value);
			ClearHOB();
			regLcylHOB = regLcyl;
			regLcyl = static_cast<u8>(value);
			break;
		case ATA_R_HCYL:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_HCYL %dbit write %x", width, value);
			ClearHOB();
			regHcylHOB = regHcyl;
			regHcyl = static_cast<u8>(value);
			break;
		case ATA_R_SELECT:
		{
			ClearHOB();
			//DevCon.WriteLn("DEV9: ATA: ATA_R_SELECT %dbit write %x", width, value);
			const int oldDev = GetSelectedDevice();
			const int newDev = (value >> 4) & 1;
			// Suppress INTRQ when not selected device
			if (oldDev == 0 && newDev == 1)
			{
				dev9.irqcause &= ~ATA_INTR_INTRQ;
			}
			else if (oldDev == 1 && newDev == 0)
			{
				if (regControlEnableIRQ && pendingInterrupt)
					_DEV9irq(ATA_INTR_INTRQ, 1);
			}

			regSelect = static_cast<u8>(value);
			break;
		}
		case ATA_R_CONTROL:
			//DevCon.WriteLn("DEV9: ATA: ATA_R_CONTROL %dbit write %x", width, value);
			if ((value & 0x2) != 0)
			{
				// Suppress INTRQ
				dev9.irqcause &= ~ATA_INTR_INTRQ;
				regControlEnableIRQ = false;
			}
			else
			{
				if (GetSelectedDevice() == 0 && regControlEnableIRQ == false && pendingInterrupt)
					_DEV9irq(ATA_INTR_INTRQ, 1);
				regControlEnableIRQ = true;
			}

			if ((value & 0x4) != 0 && !softwareReset)
			{
				ResetBegin();
				softwareReset = true;
			}
			else if ((value & 0x4) == 0 && softwareReset)
			{
				ResetEnd(false);
				softwareReset = false;
				regControlEnableIRQ = (value & 2) == 0;
			}
			regControlHOBRead = (value & 0x80) != 0;

			break;
		case ATA_R_CMD:
			// Device 1 is absent. EXECUTE DEVICE DIAGNOSTIC addresses both devices.
			if (GetSelectedDevice() != 0 && value != 0x90)
				return;
			//DevCon.WriteLn("DEV9: ATA: ATA_R_CMD %dbit write %x", width, value);
			regCommand = value;
			regControlHOBRead = false;
			pendingInterrupt = false;
			dev9.irqcause &= ~ATA_INTR_INTRQ;
			IDE_ExecCmd(value);
			break;
		default:
			Console.Error("DEV9: ATA: Unknown %dbit write at address %x, value %x", width, addr, value);
			break;
	}
}

void ATA::Async(u32 cycles)
{
	if (!hddImage)
		return;

	// Internal completion pumps pass -1 and must not advance emulated time.
	if (cycles != static_cast<u32>(-1) && standbyPeriod != 0 &&
		(powerState == PowerState::Active || powerState == PowerState::Idle) &&
		(regStatus & (ATA_STAT_BUSY | ATA_STAT_DRQ)) == 0)
	{
		if (cycles >= standbyRemaining)
			powerState = PowerState::Standby;
		else
			standbyRemaining -= cycles;
	}

	bool reportError;
	{
		std::lock_guard ioSignallock(ioMutex);
		if (ioRead || ioWrite)
			return;
		reportError = ioErrorPending;
	}
	if (reportError && HDD_ReportIOError())
		return;

	if (waitingCmd != nullptr)
	{
		if (HDD_ReportIOError())
			return;
		const auto cmd = waitingCmd;
		waitingCmd = nullptr;
		(this->*cmd)();
		return;
	}

	if ((regStatus & (ATA_STAT_BUSY | ATA_STAT_DRQ)) == 0 || awaitFlush)
	{
		bool startWrites = false;
		{
			std::lock_guard ioSignallock(ioMutex);
			if (!writeQueue.IsQueueEmpty())
			{
				ioWrite = true;
				startWrites = true;
			}
			else if (awaitFlush)
				IO_Flush();
		}
		if (startWrites)
			ioReady.notify_all();
		else if (awaitFlush)
		{
			if (!HDD_ReportIOError())
			{
				awaitFlush = false;
				PostCmdNoData();
			}
		}
	}
}

s64 ATA::HDD_GetLBA()
{
	if ((regSelect & 0x40) != 0)
	{
		if (!lba48)
		{
			return (regSector |
					(regLcyl << 8) |
					(regHcyl << 16) |
					((regSelect & 0x0f) << 24));
		}
		else
		{
			return (static_cast<s64>(regHcylHOB) << 40) |
				   (static_cast<s64>(regLcylHOB) << 32) |
				   (static_cast<s64>(regSectorHOB) << 24) |
				   (static_cast<s64>(regHcyl) << 16) |
				   (static_cast<s64>(regLcyl) << 8) |
				   regSector;
		}
	}
	else
	{
		const u32 cylinder = regLcyl | (regHcyl << 8);
		const u32 head = regSelect & 0x0f;
		if (!curCHSValid || cylinder >= curCylinders || head >= curHeads ||
			regSector == 0 || regSector > curSectors)
			return -1;
		return (static_cast<s64>(cylinder) * curHeads + head) * curSectors + regSector - 1;
	}
}

void ATA::HDD_SetLBA(s64 sectorNum)
{
	if ((regSelect & 0x40) != 0)
	{
		if (!lba48)
		{
			regSelect = static_cast<u8>((regSelect & 0xf0) | static_cast<int>((sectorNum >> 24) & 0x0f));
			regHcyl = static_cast<u8>(sectorNum >> 16);
			regLcyl = static_cast<u8>(sectorNum >> 8);
			regSector = static_cast<u8>(sectorNum);
		}
		else
		{
			regSector = static_cast<u8>(sectorNum);
			regLcyl = static_cast<u8>(sectorNum >> 8);
			regHcyl = static_cast<u8>(sectorNum >> 16);
			regSectorHOB = static_cast<u8>(sectorNum >> 24);
			regLcylHOB = static_cast<u8>(sectorNum >> 32);
			regHcylHOB = static_cast<u8>(sectorNum >> 40);
		}
	}
	else
	{
		const s64 track = sectorNum / curSectors;
		const u32 cylinder = static_cast<u32>(track / curHeads);
		regSector = static_cast<u8>(sectorNum % curSectors + 1);
		regSelect = static_cast<u8>((regSelect & 0xf0) | (track % curHeads));
		regLcyl = static_cast<u8>(cylinder);
		regHcyl = static_cast<u8>(cylinder >> 8);
	}
}

void ATA::HDD_AdvanceSectors(int completed)
{
	const int remaining = std::max(0, transferSectorsRequested - completed);
	regNsector = static_cast<u8>(remaining);
	if (lba48)
		regNsectorHOB = static_cast<u8>(remaining >> 8);
	HDD_SetLBA(transferStartLBA + completed);
	nsectorLeft = std::max(0, nsector - completed);
}

bool ATA::HDD_CanSeek()
{
	int sectors = 0;
	return HDD_CanAccess(&sectors);
}

bool ATA::HDD_CanAccess(int* sectors)
{
	// Capacity is an exclusive count of complete sectors. Subtracting first
	// avoids overflow, and an empty/sub-sector image has no valid seek address.
	u64 capacity = std::min<u64>(hddImageSize / 512,
		lba48 ? (1ULL << 48) - 1 : (1ULL << 28) - 1);
	if ((regSelect & 0x40) == 0)
		capacity = std::min<u64>(capacity, static_cast<u64>(curCylinders) * curHeads * curSectors);
	const s64 start = HDD_GetLBA();
	if (!curCHSValid || start < 0 || static_cast<u64>(start) >= capacity || *sectors < 0)
	{
		*sectors = -1;
		return false;
	}
	const u64 available = capacity - static_cast<u64>(start);
	if (static_cast<u64>(*sectors) > available)
	{
		*sectors = static_cast<int>(available);
		return false;
	}
	return true;
}

//QEMU stuff
void ATA::ClearHOB()
{
	/* any write clears HOB high bit of device control register */
	regControlHOBRead = false;
}
