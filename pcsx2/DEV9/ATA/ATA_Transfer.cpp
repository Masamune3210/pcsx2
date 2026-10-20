// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "common/Assertions.h"
#include "common/FileSystem.h"

#include "ATA.h"
#include "DEV9/DEV9.h"

#ifdef _WIN32
#include <io.h>
#elif defined(__POSIX__)
#define INVALID_HANDLE_VALUE -1
#include <unistd.h>
#include <fcntl.h>
#endif

void ATA::IO_Thread()
{
	// Keeping the lock through the actual IO makes an empty queue meaningful:
	// no caller can mistake a dequeued, still-running write for completion.
	std::unique_lock ioWaitHandle(ioMutex);
	while (true)
	{
		ioThreadIdle_bool = true;
		ioThreadIdle_cv.notify_all();
		ioReady.wait(ioWaitHandle, [&] { return ioRead || ioWrite || ioClose.load(); });
		ioThreadIdle_bool = false;

		// Accepted cached writes must reach the file before any later read.
		if (ioWrite || ioRead || ioClose.load())
		{
			if (IO_Write())
				continue;
			ioWrite = false;
		}
		if (ioRead)
		{
			IO_Read();
			continue;
		}
		if (ioClose.load())
		{
			ioClose.store(false);
			ioThreadIdle_bool = true;
			ioThreadIdle_cv.notify_all();
			return;
		}
	}
}

void ATA::IO_Read()
{
	// The emulation thread snapshots the address before waking this worker.
	std::clearerr(hddImage);
	const bool seekFailed = FileSystem::FSeek64(hddImage, ioReadSector * 512, SEEK_SET) != 0;
	const size_t sectorsRead = seekFailed ? 0 : std::fread(readBuffer, 512, ioReadSectors, hddImage);
	ioReadError = seekFailed || sectorsRead != ioReadSectors;
	if (ioReadError)
	{
		if (!ioWriteError)
			ioErrorSector = ioReadSector + sectorsRead;
		Console.Error("DEV9: ATA: File read error");
		ioErrorPending = true;
	}
	ioRead = false;
}

bool ATA::IO_Write()
{
	WriteQueueEntry entry;
	if (!writeQueue.Dequeue(&entry))
		return false;

	// A failed host write still consumes this entry, but the fault is sticky:
	// cached acknowledgement cannot be undone and a later flush must fail.
	const std::unique_ptr<u8[]> data(entry.data);
	if (!ioWriteError && !ioReadError)
		ioErrorSector = entry.sector; // Also identifies the range if its later durability flush fails.
	const auto fail = [&](u32 byteOffset) {
		Console.Error("DEV9: ATA: File write error at sector %llu", static_cast<unsigned long long>(entry.sector));
		if (!ioWriteError && !ioReadError)
			ioErrorSector = entry.sector + byteOffset / 512;
		ioWriteError = true;
		ioErrorPending = true;
		hddSparseBlockValid = false;
	};
	std::clearerr(hddImage);
	const u64 imagePos = entry.sector * 512;
	if (imagePos > hddImageSize || entry.length > hddImageSize - imagePos ||
		FileSystem::FSeek64(hddImage, imagePos, SEEK_SET) != 0)
	{
		fail(0);
		return true;
	}
	if (hddSparse)
	{
		u32 written = 0;
		while (written != entry.length)
		{
			IO_SparseCacheUpdateLocation(imagePos + written);
			u32 writeSize = static_cast<u32>(hddSparseBlockSize - ((imagePos + written) % hddSparseBlockSize));
			writeSize = std::min(writeSize, entry.length - written);
			const bool sparseWrite = hddSparse && IsAllZero(&entry.data[written], writeSize);
			if (sparseWrite && IO_SparseZero(imagePos + written, writeSize))
			{
				written += writeSize;
				continue;
			}
			if (sparseWrite)
			{
				// Sparse APIs are an optimization. Retry the same bytes normally;
				// failure of that required write is reported to the guest below.
				hddSparse = false;
				hddSparseBlockValid = false;
				std::clearerr(hddImage);
			}
			if (FileSystem::FSeek64(hddImage, imagePos + written, SEEK_SET) != 0)
			{
				fail(written);
				return true;
			}
			const size_t chunkWritten = std::fwrite(&entry.data[written], 1, writeSize, hddImage);
			if (chunkWritten != writeSize || std::fflush(hddImage) != 0)
			{
				fail(written + ((chunkWritten != writeSize) ? static_cast<u32>(chunkWritten) : 0));
				return true;
			}
			if (hddSparseBlockValid)
				memcpy(&hddSparseBlock[(imagePos + written) - HddSparseStart], &entry.data[written], writeSize);
			written += writeSize;
		}
	}
	else
	{
		const size_t bytesWritten = std::fwrite(entry.data, 1, entry.length, hddImage);
		if (bytesWritten != entry.length || std::fflush(hddImage) != 0)
			fail((bytesWritten != entry.length) ? static_cast<u32>(bytesWritten) : 0);
	}
	return true;
}

bool ATA::IO_Flush()
{
	bool success = std::fflush(hddImage) == 0;
#ifdef _WIN32
	const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(hddImage)));
	if (handle == INVALID_HANDLE_VALUE || !FlushFileBuffers(handle))
		success = false;
#elif defined(__POSIX__)
	if (fsync(fileno(hddImage)) != 0)
		success = false;
#else
	// No native durability primitive is available on this platform.
	success = false;
#endif
	if (!success)
	{
		Console.Error("DEV9: ATA: File flush error");
		ioWriteError = true;
		ioErrorPending = true;
	}
	return success && !ioWriteError;
}

bool ATA::HDD_ReportIOError()
{
	bool writeError;
	bool readError;
	u64 errorSector;
	{
		std::lock_guard ioSignallock(ioMutex);
		if (!ioReadError && !ioWriteError)
			return false;
		writeError = ioWriteError;
		readError = ioReadError;
		errorSector = ioErrorSector;
		ioReadError = false;
		ioErrorPending = false;
	}
	const bool activeUncachedWrite = writeError && !fetWriteCacheEnabled &&
		(regCommand == 0x30 || regCommand == 0x31 || regCommand == 0x34 ||
			regCommand == 0x39 || regCommand == 0xC5 || regCommand == 0xCA ||
			regCommand == 0xCB || regCommand == 0x35);
	if ((readError || activeUncachedWrite) && transferStartLBA >= 0 &&
		errorSector >= static_cast<u64>(transferStartLBA) &&
		errorSector - transferStartLBA < static_cast<u64>(transferSectorsRequested))
	{
		const u32 remaining = transferSectorsRequested - (errorSector - transferStartLBA);
		regNsector = static_cast<u8>(remaining);
		if (lba48)
			regNsectorHOB = static_cast<u8>(remaining >> 8);
	}
	waitingCmd = nullptr;
	awaitFlush = false;
	nsectorLeft = 0;
	nsector = 0;
	dmaReady = false;
	pioDataOut = false;
	pioPtr = pioEnd = 0;
	pioDRQEndTransferFunc = nullptr;
	rdTransferred = wrTransferred = 0;
	delete[] currentWrite;
	currentWrite = nullptr;
	currentWriteLength = 0;
	currentWriteSectors = 0;
	DEV9ResetATAFIFO();
	HDD_SetLBA(errorSector);
	regStatus &= ~(ATA_STAT_BUSY | ATA_STAT_DRQ);
	regStatus |= ATA_STAT_ERR;
	if (writeError)
		regStatus |= ATA_STAT_WRERR;
	// Bit 6 is UNC for reads but means write-protect for ATA-4 writes.
	// A host write/flush failure is a device fault and inability to complete.
	regError |= writeError ? ATA_ERR_ABORT : ATA_ERR_ECC;
	regStatusSeekLock = (regStatus & ATA_STAT_SEEK) ? 1 : -1;
	PostCmdNoData();
	return true;
}

bool ATA::IO_SparseCacheLoad()
{
	const u64 readSize = std::min(hddSparseBlockSize, hddImageSize - HddSparseStart);
	const s64 orgPos = FileSystem::FTell64(hddImage);
	if (orgPos < 0 || std::fflush(hddImage) != 0)
		return false;

	// Read actual bytes instead of mixing buffered stdio with native SEEK_HOLE
	// seeks, which can leave the shared file position inconsistent.
	const bool loaded = FileSystem::FSeek64(hddImage, HddSparseStart, SEEK_SET) == 0 &&
		std::fread(hddSparseBlock.get(), 1, readSize, hddImage) == readSize;
	const bool restored = FileSystem::FSeek64(hddImage, orgPos, SEEK_SET) == 0;
	if (!loaded || !restored)
	{
		hddSparseBlockValid = false;
		return false;
	}
	memset(&hddSparseBlock[readSize], 0, hddSparseBlockSize - readSize);
	hddSparseBlockValid = true;
	return true;
}

#if defined(PCSX2_DEBUG) || defined(PCSX2_DEVBUILD)
// Asserts that the region of file indicated by HddSparseStart & hddSparseBlockSizeReadable is all zeros
// Optional debug check; file errors must not terminate the emulator.
void ATA::IO_SparseCacheAssertFileZeros(u64 hddSparseBlockSizeReadable)
{
	const s64 orgPos = FileSystem::FTell64(hddImage);
	if (orgPos < 0)
	{
		Console.Error("DEV9: ATA: Unable to check sparse file position");
		return;
	}

	std::unique_ptr<u8[]> temp = std::make_unique<u8[]>(hddSparseBlockSize);
	memset(temp.get(), 0, hddSparseBlockSize);

	const bool loaded = FileSystem::FSeek64(hddImage, HddSparseStart, SEEK_SET) == 0 &&
		std::fread(temp.get(), 1, hddSparseBlockSizeReadable, hddImage) == hddSparseBlockSizeReadable;
	const bool restored = FileSystem::FSeek64(hddImage, orgPos, SEEK_SET) == 0;
	if (!loaded || !restored)
	{
		Console.Error("DEV9: ATA: Unable to read sparse file check buffer");
		return;
	}

	bool regionIsZeros = IsAllZero(temp.get(), hddSparseBlockSizeReadable);

	// Check if file is actully zeros.
	if (!regionIsZeros)
	{
		Console.WriteLn("DEV9: ATA: Sparse area not sparse, BlockStart: %s, BlockEnd: %s",
			std::to_string(HddSparseStart).c_str(), std::to_string(HddSparseStart + hddSparseBlockSize).c_str());
	}
	else
		Console.WriteLn("DEV9: ATA: Sparse area is sparse (Yay), BlockStart: %s, BlockEnd: %s",
			std::to_string(HddSparseStart).c_str(), std::to_string(HddSparseStart + hddSparseBlockSize).c_str());

	pxAssert(regionIsZeros);
}
#endif

void ATA::IO_SparseCacheUpdateLocation(u64 byteOffset)
{
	const u64 currentBlockStart = (byteOffset / hddSparseBlockSize) * hddSparseBlockSize;
	if (currentBlockStart != HddSparseStart)
	{
		HddSparseStart = currentBlockStart;
		hddSparseBlockValid = false;
		// Only update cache when we perform a sparse write.
	}
}

// Also sets hddImage write ptr.
bool ATA::IO_SparseZero(u64 byteOffset, u64 byteSize)
{
	if (!hddSparseBlockValid && !IO_SparseCacheLoad())
		return false;

	//Assert as range check
	pxAssert(byteOffset >= HddSparseStart);
	pxAssert(byteOffset - HddSparseStart + byteSize <= hddSparseBlockSize);

	//Write to cache
	memset(&hddSparseBlock[byteOffset - HddSparseStart], 0, byteSize);

	//Is block non-zero?
	if (!IsAllZero(hddSparseBlock.get(), hddSparseBlockSize))
	{
#if defined(PCSX2_DEBUG) || defined(PCSX2_DEVBUILD)
		std::unique_ptr<u8[]> zeroBlock = std::make_unique<u8[]>(hddSparseBlockSize);
		memset(zeroBlock.get(), 0, hddSparseBlockSize);
		pxAssert(memcmp(hddSparseBlock.get(), zeroBlock.get(), hddSparseBlockSize) != 0);
#endif

		//No, do normal write
		if (std::fwrite((char*)&hddSparseBlock[byteOffset - HddSparseStart], byteSize, 1, hddImage) != 1 ||
			std::fflush(hddImage) != 0)
		{
			hddSparseBlockValid = false;
			return false;
		}
		return true;
	}

#if defined(PCSX2_DEBUG) || defined(PCSX2_DEVBUILD)
	std::unique_ptr<u8[]> zeroBlock = std::make_unique<u8[]>(hddSparseBlockSize);
	memset(zeroBlock.get(), 0, hddSparseBlockSize);
	pxAssert(memcmp(hddSparseBlock.get(), zeroBlock.get(), hddSparseBlockSize) == 0);
#endif

	// Native hole punching must observe all preceding buffered writes.
	if (std::fflush(hddImage) != 0)
		return false;

	//Yes, try sparse write
#ifdef _WIN32
	FILE_ZERO_DATA_INFORMATION sparseRange;
	sparseRange.FileOffset.QuadPart = HddSparseStart;
	sparseRange.BeyondFinalZero.QuadPart = HddSparseStart + hddSparseBlockSize;
	DWORD dwTemp;
	const BOOL ret = DeviceIoControl(hddNativeHandle, FSCTL_SET_ZERO_DATA, &sparseRange, sizeof(sparseRange), nullptr, 0, &dwTemp, nullptr);

	if (ret == FALSE)
		return false;

#elif defined(__linux__)
	const int ret = fallocate(hddNativeHandle, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, HddSparseStart, hddSparseBlockSize);

	if (ret == -1)
		return false;

#elif defined(__APPLE__)
	fpunchhole_t sparseRange{0};
	sparseRange.fp_offset = HddSparseStart;
	sparseRange.fp_length = hddSparseBlockSize;

	const int ret = fcntl(hddNativeHandle, F_PUNCHHOLE, &sparseRange);

	if (ret == -1)
		return false;

#else
	Console.Error("DEV9: ATA: Hole punching not supported on current OS");
	return false;
#endif
	if (FileSystem::FSeek64(hddImage, byteOffset + byteSize, SEEK_SET) != 0)
	{
		hddSparseBlockValid = false;
		return false;
	}
	return true;
}

bool ATA::IsAllZero(const void* data, size_t len)
{
	const u8* bytes = static_cast<const u8*>(data);
	// memcpy permits unaligned input without violating strict aliasing.
	while (len >= sizeof(u64))
	{
		u64 word;
		memcpy(&word, bytes, sizeof(word));
		if (word != 0)
			return false;
		bytes += sizeof(word);
		len -= sizeof(word);
	}
	while (len-- != 0)
		if (*bytes++ != 0)
			return false;
	return true;
}

void ATA::HDD_ReadAsync(void (ATA::*drqCMD)())
{
	nsectorLeft = 0;
	if (!HDD_CanAssessOrSetError())
		return;

	std::lock_guard ioSignallock(ioMutex);
	nsectorLeft = nsector;
	if (readBufferLen < nsector * 512)
	{
		delete[] readBuffer;
		readBuffer = new u8[nsector * 512];
		readBufferLen = nsector * 512;
	}
	waitingCmd = drqCMD;
	ioReadSector = static_cast<u64>(HDD_GetLBA());
	ioReadSectors = nsector;
	ioReadError = false;
	ioRead = true;
	ioReady.notify_all();
}

void ATA::HDD_ReadSync(void (ATA::*drqCMD)())
{
	nsectorLeft = 0;
	if (!HDD_CanAssessOrSetError())
		return;

	{
		std::lock_guard ioSignallock(ioMutex);
		// This includes writes accepted since the last Async tick, plus the
		// final dequeued write (whose IO held the mutex until completion).
		while (IO_Write())
		{
		}
		ioWrite = false;
		if (!ioWriteError)
		{
			nsectorLeft = nsector;
			if (readBufferLen < nsector * 512)
			{
				delete[] readBuffer;
				readBuffer = new u8[nsector * 512];
				readBufferLen = nsector * 512;
			}
			ioReadSector = static_cast<u64>(HDD_GetLBA());
			ioReadSectors = nsector;
			IO_Read();
		}
	}
	if (!HDD_ReportIOError())
		(this->*drqCMD)();
}

bool ATA::HDD_CanAssessOrSetError()
{
	if (!HDD_CanAccess(&nsector))
	{
		//Read what we can
		if (nsector == -1)
		{
			regStatus |= ATA_STAT_ERR;
			regError |= ATA_ERR_ID;
			regStatus &= ~ATA_STAT_SEEK;
			regStatusSeekLock = -1;
			PostCmdNoData();
			return false;
		}
		transferRangeError = true;
		regStatusSeekLock = 1;
	}
	return true;
}
void ATA::HDD_SetErrorAtTransferEnd()
{
	// nsector is the valid prefix after clipping; the next sector is the first
	// failing address. No extra +1: capacity and completed counts are exclusive.
	HDD_AdvanceSectors(nsector);
	if (transferRangeError)
	{
		regStatus |= ATA_STAT_ERR;
		regError |= ATA_ERR_ID;
	}
}
