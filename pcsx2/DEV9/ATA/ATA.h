// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "common/RedtapeWindows.h"
#include "common/Path.h"

#include "DEV9/SimpleQueue.h"

class ATA
{
public:
	//Transfer
	bool dmaReady = false;
	int nsector = 0;     //sector count
	int nsectorLeft = 0; //sectors left to transfer
private:
	bool lba48Supported = false;

	std::FILE* hddImage = nullptr;
	u64 hddImageSize = 0;
	// HPA changes guest-visible capacity only; backing image size remains native.
	u64 hpaCurrentSectors = 0;
	u64 hpaNonVolatileSectors = 0;
	std::string hpaStatePath;
	bool hpaNonVolatileSet = false; // Once per power-on or hardware reset.
	bool hpaReadNativeMaxReady = false;
	bool hpaLbaSetMax = false; // SET MAX LBA-specific current-CHS reporting rule.


	bool hddSparse = false;
	u64 hddSparseBlockSize = 4096;
	u64 HddSparseStart = 0;
	std::unique_ptr<u8[]> hddSparseBlock;
	bool hddSparseBlockValid = false;

#ifdef _WIN32
	HANDLE hddNativeHandle = INVALID_HANDLE_VALUE;
#elif defined(__POSIX__)
	int hddNativeHandle = -1;
#endif

	int pioMode = 2;
	int mdmaMode = 0;
	int udmaMode = -1;

	//Info
	u8 curHeads = 16;
	u8 curSectors = 63;
	u16 curCylinders = 0;
	bool curCHSValid = true;

	u8 curMultipleSectorsSetting = 128;

	u8 identifyData[512] = {0};

	//LBA48 in use?
	bool lba48 = false;

	//Enable/disable features
	bool fetSmartEnabled = true;
	std::string smartSettingsPath;
	bool fetSecurityEnabled = false;
	bool fetWriteCacheEnabled = true;
	bool fetRevertToDefaults = true;
	bool fetHostProtectedAreaEnabled = false;

	//Regs
	u16 regCommand = 0; //WriteOnly, Only to be written BSY and DRQ are cleared, DMACK is not set and device is not sleeping, except for DEVICE RESET
	//PIO Read/Write, Only to be written DMACK is not set and DRQ is 1
	//COMMAND REG (WriteOnly) Only to be written DMACK is not set
	//Bit 0 = 0
	bool regControlEnableIRQ = false; //Bit 1 = 1 Disable Interrupt
	//Bit 2 = 1 Software Reset
	bool regControlHOBRead = false; //Bit 7 = HOB (cleared by any write to RegCommand, Sets if Low order or High order bytes are read in ATAread16)
	bool softwareReset = false;
	//End COMMAND REG
	u8 regError = 0; //ReadOnly

	//DEVICE REG (Read/Write)
	u8 regSelect = 0;
	//Bit 0-3: LBA Bits 24-27 (Unused in 48bit) or Command Dependent
	//Bit 4: Selected Device
	//Bit 5: Obsolete (All?)
	//Bit 6: Command Dependent
	//Bit 7: Obsolete (All?)
	//End COMMAND REG
	u8 regFeature = 0; //WriteOnly, Only to be written BSY and DRQ are cleared and DMACK is not set
	u8 regFeatureHOB = 0;

	//Following regs are Read/Write, Only to be written BSY and DRQ are cleared and DMACK is not set
	u8 regSector = 0; //Sector Number or LBA Low
	u8 regSectorHOB = 0;
	u8 regLcyl = 0; //LBA Mid
	u8 regLcylHOB = 0;
	u8 regHcyl = 0; //LBA High
	u8 regHcylHOB = 0;
	//TODO handle nsector code
	u8 regNsector = 0;
	u8 regNsectorHOB = 0;

	u8 regStatus = 0; // ReadOnly. When read via AlternateStatus, pending interrupts are not cleared.
	// When an error occurs, the SEEK bit shall not be changed until the Status Register is read,
	// after which this bit again indicates Seek completed.
	// A value of -1 is locked clear, a value of 1 is locked set, 0 is unlocked.
	s8 regStatusSeekLock = 0;

	bool pendingInterrupt = false;
	enum class PowerState { Active, Idle, Standby, Sleep };
	PowerState powerState = PowerState::Active;
	u64 standbyPeriod = 0; // IOP cycles, independent of host wall time.
	u64 standbyRemaining = 0;

	//Transfer
	//Write Buffer(s)
	bool awaitFlush = false;
	u8* currentWrite = nullptr; //array
	u32 currentWriteLength = 0;
	u64 currentWriteSectors = 0;
	s64 transferStartLBA = 0;
	int transferSectorsRequested = 0;
	bool transferRangeError = false;

	struct WriteQueueEntry
	{
		u8* data;
		u32 length;
		u64 sector;
	};
	SimpleQueue<WriteQueueEntry> writeQueue;

	std::thread ioThread;
	bool ioRunning = false;
	std::mutex ioMutex;

	std::condition_variable ioThreadIdle_cv;
	bool ioThreadIdle_bool = false;

	std::condition_variable ioReady;
	std::atomic_bool ioClose{false};
	// All queue operations, file access and these completion flags use ioMutex.
	bool ioWrite = false;
	bool ioRead = false;
	bool ioReadError = false;
	bool ioWriteError = false; // Deferred write failures remain latched until reopen.
	bool ioErrorPending = false;
	u64 ioErrorSector = 0;
	u64 ioReadSector = 0;
	u32 ioReadSectors = 0;
	void (ATA::*waitingCmd)() = nullptr;
	//Write Buffer(s)

	//Read Buffer
	int rdTransferred = 0;
	int wrTransferred = 0;
	//Max transfer on 28bit is 256*512 = 128KB
	//Max tranfer on 48bit is 65536*512 = 32MB
	int readBufferLen = 0;
	u8* readBuffer = nullptr;
	//Read Buffer

	//PIO Buffer
	int pioPtr = 0;
	int pioEnd = 0;
	u8 pioBuffer[512] = {};
	u8 diagnosticBuffer[512] = {};
	bool diagnosticBufferValid = false;
	bool pioDataOut = false;
	int pioWriteBlockStart = 0;
	int pioReadErrorBlock = -1;
	int pioReadErrorSector = 0;

	int sectorsPerInterrupt = 1;
	void (ATA::*pioDRQEndTransferFunc)() = nullptr;
	void (ATA::*pioDataOutComplete)() = nullptr;
	//PIO Buffer

	//Smart
	bool smartAutosave = true;
	bool smartErrors = false;
	std::string smartStatePath;
	bool smartScanActive = false;
	bool smartDirty = false;
	u8 smartOfflineStatus = 0;
	u32 smartScanStarted = 0;
	u32 smartScanCompleted = 0;
	u32 smartScanFailed = 0;
	u32 smartScanInterrupted = 0;
	u32 smartSaveFailures = 0;
	u64 smartScanSector = 0;
	u64 smartScanImageSectors = 0;
	u64 smartLastFailureSector = ~u64(0);
	u64 smartScanCycles = 0;
	//Smart

	u8 sceSec[256 * 2] = {0};

public:
	ATA();
	~ATA();

	int Open(const std::string& hddPath);
	void Close();

	void ATA_HardReset();

	u16 Read(u32 addr, int width);
	void Write(u32 addr, u16 value, int width);

	void Async(u32 cycles);

	int ReadDMAToFIFO(u8* buffer, int space);
	int WriteDMAFromFIFO(u8* buffer, int available);

	u16 ATAreadPIO();
	void ATAwritePIO(u16 value);

private:
	void InitSparseSupport(const std::string& hddPath);
	bool SaveSMARTSettings();
	bool LoadHPAState(const std::string& hddPath);
	bool SaveHPALimit(u64 sectors);
	u64 HDD_GetNativeSectors() const { return hddImageSize / 512; }
	u64 HDD_GetAccessibleSectors() const { return hpaCurrentSectors; }


	//Info
	void CreateHDDinfo(u64 sizeSectors);
	void CreateHDDinfoCsum();

	//State
	void ResetBegin();
	void ResetEnd(bool hard);

	u8 GetSelectedDevice()
	{
		return (regSelect >> 4) & 1;
	}
	void SetSelectedDevice(u8 value)
	{
		if (value == 1)
			regSelect |= (1 << 4);
		else
			regSelect &= ~(1 << 4);
	}

	s64 HDD_GetLBA();
	void HDD_SetLBA(s64 sectorNum);
	void HDD_AdvanceSectors(int completed);

	bool HDD_CanSeek();
	bool HDD_CanAccess(int* sectors);

	void ClearHOB();

	//Transfer
	void IO_Thread();
	void IO_Read();
	bool IO_Write();
	bool IO_Flush(); // Caller holds ioMutex, or has joined the worker.
	bool HDD_ReportIOError(); // Acquires ioMutex; caller must not hold it.
	bool IO_SparseZero(u64 byteOffset, u64 byteSize);
	void IO_SparseCacheUpdateLocation(u64 Offset);
	bool IO_SparseCacheLoad();
#if defined(PCSX2_DEBUG) || defined(PCSX2_DEVBUILD)
	void IO_SparseCacheAssertFileZeros(u64 hddSparseBlockSizeReadable);
#endif
	bool IsAllZero(const void* data, size_t len);
	void HDD_ReadAsync(void (ATA::*drqCMD)());
	void HDD_ReadSync(void (ATA::*drqCMD)());
	bool HDD_CanAssessOrSetError();
	void HDD_SetErrorAtTransferEnd();

	//Commands
	void IDE_ExecCmd(u16 value);

	bool PreCmd();
	void HDD_Unk();

	void IDE_CmdLBA48Transform(bool islba48);

	void DRQCmdDMADataToHost();
	void PostCmdDMADataToHost();
	void DRQCmdDMADataFromHost();
	void HDD_CompleteWrite();
	void HDD_ReadDMA(bool isLBA48);
	void HDD_WriteDMA(bool isLBA48);

	void PreCmdExecuteDeviceDiag();
	void PostCmdExecuteDeviceDiag(bool sendIRQ);
	void HDD_ExecuteDeviceDiag(bool sendIRQ);

	void PostCmdNoData();
	void CmdNoDataAbort();
	void HDD_FlushCache();
	void HDD_InitDevParameters();
	void HDD_ReadVerifySectors(bool isLBA48);
	void HDD_ReadVerifyComplete();
	void HDD_Recalibrate();
	void HDD_SeekCmd();
	void HDD_SetFeatures();
	void HDD_SetMultipleMode();
	void HDD_Nop();
	void HDD_PowerCommand();
	void HDD_ReadNativeMaxAddress();
	void HDD_SetMaxAddress(bool precededByNativeMax);


	void DRQCmdPIODataToHost(u8* buff, int buffLen, int buffIndex, int size, bool sendIRQ);
	void PostCmdPIODataToHost();
	void HDD_IdentifyDevice();
	void HDD_ReadBuffer();
	void HDD_WriteBuffer();
	void HDD_WriteBufferComplete();

	void HDD_ReadMultiple(bool isLBA48);
	void HDD_ReadSectors(bool isLBA48);
	void HDD_ReadPIO(bool isLBA48);
	bool HDD_CheckMultipleRange();
	void HDD_ReadPIOPostError();
	void HDD_ReadPIOS2();
	void HDD_ReadPIOEndBlock();
	void HDD_WritePIO(bool isLBA48, bool multiple);

	void HDD_Smart();
	void SMART_SetAutoSaveAttribute();
	void SMART_SaveAttribute();
	void SMART_ExecuteOfflineImmediate();
	void SMART_EnableOps(bool enable);
	void SMART_ReturnStatus();
	void SMART_ReadData();
	void SMART_BuildData(u8* data) const;
	bool LoadSMARTState(const std::string& hddPath);
	bool SaveSMARTState();
	void SMART_Async(u32 cycles);
	void SMART_Interrupt();
	void SMART_Cancel();

	void HDD_SCE();
	void SCE_IDENTIFY_DRIVE();

	//In here temporally
	static void WriteUInt16(u8* data, int* index, u16 value);
	static void WriteUInt32(u8* data, int* index, u32 value);
	static void WriteUInt64(u8* data, int* index, u64 value);
	static void WritePaddedString(u8* data, int* index, const std::string& value, u32 len);
};
