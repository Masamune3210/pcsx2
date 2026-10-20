// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "DEV9/ATA/ATA.h"
#include "DEV9/DEV9.h"

void ATA::DRQCmdPIODataToHost(u8* buff, int buffLen, int buffIndex, int size, bool sendIRQ)
{
	//Data in PIO ready to be sent
	pioPtr = 0;
	pioEnd = size >> 1;
	pioDataOut = false;

	if (size <= 0 || size > static_cast<int>(sizeof(pioBuffer)) || buffIndex < 0 ||
		buffIndex > buffLen || size > buffLen - buffIndex)
	{
		pioEnd = 0;
		CmdNoDataAbort();
		return;
	}
	memcpy(pioBuffer, &buff[buffIndex], size);

	regStatus &= ~ATA_STAT_BUSY;
	regStatus |= ATA_STAT_DRQ;

	if (sendIRQ)
	{
		pendingInterrupt = true;
		if (regControlEnableIRQ && GetSelectedDevice() == 0)
			_DEV9irq(ATA_INTR_INTRQ, 1);
	}
}
void ATA::PostCmdPIODataToHost()
{
	pioPtr = 0;
	pioEnd = 0;
	//AnyMoreData?
	if (pioDRQEndTransferFunc != nullptr)
	{
		regStatus |= ATA_STAT_BUSY;
		regStatus &= ~ATA_STAT_DRQ;
		//Call cmd to retrive more data
		(this->*pioDRQEndTransferFunc)();
	}
	else
		regStatus &= ~ATA_STAT_DRQ;
}

//FromHost
u16 ATA::ATAreadPIO()
{
	//DevCon.WriteLn("DEV9: *ATA_R_DATA 16bit read, pio_count %i,  pio_size %i", pioPtr, pioEnd);
	if (GetSelectedDevice() == 0 && !pioDataOut && !softwareReset &&
		(regStatus & (ATA_STAT_BUSY | ATA_STAT_DRQ)) == ATA_STAT_DRQ && pioPtr < pioEnd)
	{
		u16 ret;
		memcpy(&ret, &pioBuffer[pioPtr * 2], sizeof(ret));
		//DevCon.WriteLn("DEV9: *ATA_R_DATA returned value is  %x", ret);
		pioPtr++;
		if (pioPtr >= pioEnd) //Fnished transfer (Changed from MegaDev9)
			PostCmdPIODataToHost();

		return ret;
	}
	return 0xffff;
}

void ATA::ATAwritePIO(u16 value)
{
	if (GetSelectedDevice() != 0 || !pioDataOut || softwareReset ||
		(regStatus & (ATA_STAT_BUSY | ATA_STAT_DRQ)) != ATA_STAT_DRQ ||
		currentWrite == nullptr || wrTransferred < 0 ||
		static_cast<u32>(wrTransferred) + sizeof(value) > currentWriteLength)
		return;

	memcpy(currentWrite + wrTransferred, &value, sizeof(value));
	wrTransferred += sizeof(value);
	if (pioDataOutComplete != nullptr)
	{
		if (static_cast<u32>(wrTransferred) == currentWriteLength)
		{
			const auto complete = pioDataOutComplete;
			pioDataOutComplete = nullptr;
			pioDataOut = false;
			regStatus &= ~ATA_STAT_DRQ;
			regStatus |= ATA_STAT_BUSY;
			(this->*complete)();
		}
		return;
	}
	if (wrTransferred % 512 != 0)
		return;

	const int completed = wrTransferred / 512;
	HDD_AdvanceSectors(completed);
	const bool finalBlock = static_cast<u32>(wrTransferred) == currentWriteLength;
	const bool blockComplete = finalBlock || completed % sectorsPerInterrupt == 0;
	if (blockComplete)
	{
		// Once the next block is requested, the preceding block is accepted.
		// Give it independent ownership so SRST can discard only unfinished data.
		const u32 length = wrTransferred - pioWriteBlockStart;
		u8* data = new u8[length];
		memcpy(data, currentWrite + pioWriteBlockStart, length);
		const WriteQueueEntry entry{data, length, currentWriteSectors + pioWriteBlockStart / 512};
		{
			std::lock_guard ioSignallock(ioMutex);
			writeQueue.Enqueue(entry);
		}
		pioWriteBlockStart = wrTransferred;
	}
	if (finalBlock)
	{
		delete[] currentWrite;
		currentWrite = nullptr;
		pioDataOut = false;
		regStatus &= ~ATA_STAT_DRQ;
		regStatus |= ATA_STAT_BUSY;
		HDD_SetErrorAtTransferEnd();
		HDD_CompleteWrite();
	}
	else if (completed % sectorsPerInterrupt == 0)
	{
		if (!fetWriteCacheEnabled)
		{
			{
				std::lock_guard ioSignallock(ioMutex);
				while (IO_Write())
				{
				}
				ioWrite = false;
				IO_Flush();
			}
			if (HDD_ReportIOError())
				return;
		}
		// The first block is polled. Each subsequent DRQ block signals INTRQ;
		// words and sectors inside a negotiated multiple block do not.
		pendingInterrupt = true;
		if (regControlEnableIRQ && GetSelectedDevice() == 0)
			_DEV9irq(ATA_INTR_INTRQ, 1);
	}
}

void ATA::HDD_IdentifyDevice()
{
	if (!PreCmd())
		return;
	DevCon.WriteLn("DEV9: HddidentifyDevice");

	//IDE transfer start
	CreateHDDinfo(hddImageSize / 512);

	pioDRQEndTransferFunc = nullptr;
	DRQCmdPIODataToHost(identifyData, 256 * 2, 0, 256 * 2, true);
}

void ATA::HDD_ReadBuffer()
{
	if (!PreCmd())
		return;
	// The ATA-4 prerequisite is a preceding completed WRITE BUFFER. Do not
	// expose an unfinished buffer or memory left over from an image transfer.
	if (!diagnosticBufferValid)
	{
		CmdNoDataAbort();
		return;
	}
	DRQCmdPIODataToHost(diagnosticBuffer, sizeof(diagnosticBuffer), 0, sizeof(diagnosticBuffer), true);
}

void ATA::HDD_WriteBuffer()
{
	if (!PreCmd())
		return;
	currentWriteLength = sizeof(diagnosticBuffer);
	currentWrite = new u8[currentWriteLength];
	diagnosticBufferValid = false;
	pioDataOut = true;
	pioDataOutComplete = &ATA::HDD_WriteBufferComplete;
	regStatus &= ~ATA_STAT_BUSY;
	regStatus |= ATA_STAT_DRQ;
}

void ATA::HDD_WriteBufferComplete()
{
	memcpy(diagnosticBuffer, currentWrite, sizeof(diagnosticBuffer));
	diagnosticBufferValid = true;
	delete[] currentWrite;
	currentWrite = nullptr;
	currentWriteLength = 0;
	wrTransferred = 0;
	PostCmdNoData();
}

//Read Buffer

void ATA::HDD_ReadMultiple(bool isLBA48)
{
	sectorsPerInterrupt = curMultipleSectorsSetting;
	HDD_ReadPIO(isLBA48);
}

void ATA::HDD_ReadSectors(bool isLBA48)
{
	sectorsPerInterrupt = 1;
	HDD_ReadPIO(isLBA48);
}

void ATA::HDD_ReadPIO(bool isLBA48)
{
	//Log_Info("HDD_ReadPIO");
	if (!PreCmd())
		return;

	if (sectorsPerInterrupt == 0)
	{
		CmdNoDataAbort();
		return;
	}

	IDE_CmdLBA48Transform(isLBA48);

	regStatus &= ~ATA_STAT_SEEK;
	if (!HDD_CanSeek())
	{
		regStatus |= ATA_STAT_ERR;
		regStatusSeekLock = -1;
		regError |= ATA_ERR_ID;
		PostCmdNoData();
		return;
	}
	else
		regStatus |= ATA_STAT_SEEK;

	HDD_ReadSync(&ATA::HDD_ReadPIOS2);
}

void ATA::HDD_ReadPIOS2()
{
	//Log_Info("HDD_ReadPIO Stage 2");
	// A fresh PIO command starts at byte zero, including retries after partial DMA.
	rdTransferred = 0;
	pioDRQEndTransferFunc = &ATA::HDD_ReadPIOEndBlock;
	DRQCmdPIODataToHost(readBuffer, readBufferLen, 0, 256 * 2, true);
}

void ATA::HDD_ReadPIOEndBlock()
{
	//Log_Info("HDD_ReadPIO End Block");
	rdTransferred += 512;
	HDD_AdvanceSectors(rdTransferred / 512);
	if (rdTransferred >= nsector * 512)
	{
		//Log_Info("HDD_ReadPIO Done");
		HDD_SetErrorAtTransferEnd();
		regStatus &= ~ATA_STAT_BUSY;
		pioDRQEndTransferFunc = nullptr;
		rdTransferred = 0;
	}
	else
	{
		if ((rdTransferred / 512) % sectorsPerInterrupt == 0)
			DRQCmdPIODataToHost(readBuffer, readBufferLen, rdTransferred, 256 * 2, true);
		else
			DRQCmdPIODataToHost(readBuffer, readBufferLen, rdTransferred, 256 * 2, false);
	}
}

void ATA::HDD_WritePIO(bool isLBA48, bool multiple)
{
	if (!PreCmd())
		return;
	sectorsPerInterrupt = multiple ? curMultipleSectorsSetting : 1;
	if (sectorsPerInterrupt == 0)
	{
		CmdNoDataAbort();
		return;
	}
	IDE_CmdLBA48Transform(isLBA48);
	if (!HDD_CanAssessOrSetError())
		return;

	regStatus |= ATA_STAT_SEEK;
	currentWriteLength = nsector * 512;
	currentWriteSectors = transferStartLBA;
	currentWrite = new u8[currentWriteLength];
	wrTransferred = 0;
	pioWriteBlockStart = 0;
	nsectorLeft = nsector;
	pioDataOut = true;
	regStatus &= ~ATA_STAT_BUSY;
	regStatus |= ATA_STAT_DRQ;
}
