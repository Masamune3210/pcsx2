// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "DEV9/ATA/ATA.h"
#include "DEV9/DEV9.h"

void ATA::PostCmdNoData()
{
	regStatus &= ~(ATA_STAT_BUSY | ATA_STAT_DRQ);

	pendingInterrupt = true;
	if (regControlEnableIRQ && GetSelectedDevice() == 0)
		_DEV9irq(ATA_INTR_INTRQ, 1);
}

void ATA::CmdNoDataAbort()
{
	// Some callers (notably INIT DEVICE PARAMETERS) must complete with DRDY
	// clear. Re-entering PreCmd would reject that case or clear an earlier fault.
	regError = ATA_ERR_ABORT;
	regStatus |= ATA_STAT_ERR;
	regStatusSeekLock = (regStatus & ATA_STAT_SEEK) ? 1 : -1;
	PostCmdNoData();
}

//GENRAL FEATURE SET

void ATA::HDD_FlushCache() //Can't when DRQ set
{
	if (!PreCmd())
		return;
	DevCon.WriteLn("DEV9: HDD_FlushCache");

	// An empty queue may have just had its final entry dequeued. Async takes
	// ioMutex after that write completes, then performs the durability barrier.
	awaitFlush = true;
	Async(static_cast<u32>(-1));
}

void ATA::HDD_InitDevParameters()
{
	// Unlike ordinary commands, initialization is accepted with DRDY clear.
	regStatus |= ATA_STAT_BUSY;
	regStatus &= ~(ATA_STAT_WRERR | ATA_STAT_DRQ | ATA_STAT_ERR);
	regError = 0;
	if (HDD_ReportIOError())
		return;
	DevCon.WriteLn("DEV9: HDD_InitDevParameters");

	if (regNsector == 0)
	{
		curCHSValid = false;
		CmdNoDataAbort();
		return;
	}

	curSectors = regNsector;
	curHeads = static_cast<u8>((regSelect & 0x0F) + 1);
	curCHSValid = true;
	CreateHDDinfo(hddImageSize / 512);
	PostCmdNoData();
}

void ATA::HDD_ReadVerifySectors(bool isLBA48)
{
	if (!PreCmd())
		return;
	DevCon.WriteLn("DEV9: HDD_ReadVerifySectors");

	IDE_CmdLBA48Transform(isLBA48);

	regStatus |= ATA_STAT_SEEK;
	// Verification must touch the image, including short-read/error handling.
	HDD_ReadSync(&ATA::HDD_ReadVerifyComplete);
}

void ATA::HDD_Recalibrate()
{
	if (!PreCmd())
		return;
	DevCon.WriteLn("DEV9: HDD_Recalibrate");

	lba48 = false;
	// Report minimum address (LBA 0 or CHS 0/0/1).
	// Set the task file directly so recalibration also repairs an invalid CHS.
	regSelect = regSelect & 0xf0;
	regHcyl = 0;
	regLcyl = 0;
	regSector = (regSelect & 0x40) ? 0 : 1;

	// If recalibrate fails, we would set ATA_STAT_ERR in regStatus and ATA_ERR_TRACK0 in regError.
	// we will never fail, so set ATA_STAT_SEEK in regStatus to indicate we finished seeking.
	regStatus |= ATA_STAT_SEEK;

	PostCmdNoData();
}

void ATA::HDD_SeekCmd()
{
	if (!PreCmd())
		return;
	DevCon.WriteLn("DEV9: HDD_SeekCmd");

	lba48 = false;
	regStatus &= ~ATA_STAT_SEEK;
	if (!HDD_CanSeek())
	{
		regStatus |= ATA_STAT_ERR;
		regStatusSeekLock = -1;
		regError |= ATA_ERR_ID;
	}
	else
		regStatus |= ATA_STAT_SEEK;

	PostCmdNoData();
}

void ATA::HDD_SetFeatures()
{
	if (!PreCmd())
		return;
	DevCon.WriteLn("DEV9: HDD_SetFeatures");

	switch (regFeature)
	{
		case 0x02:
			fetWriteCacheEnabled = true;
			break;
		case 0x82:
			fetWriteCacheEnabled = false;
			awaitFlush = true; //Flush Cache
			Async(static_cast<u32>(-1));
			return;
		case 0x66: // Preserve modified settings across software reset.
			fetRevertToDefaults = false;
			break;
		case 0xCC: // Restore the default software-reset reversion policy.
			fetRevertToDefaults = true;
			break;
		case 0x03: //Set transfer mode
		{
			const u8 xferMode = regNsector;

			const int mode = xferMode & 0x07;
			switch ((xferMode) >> 3)
			{
				case 0x00: //pio default
					// IORDY disabling is not advertised in IDENTIFY word 49.
					if (mode != 0)
					{
						CmdNoDataAbort();
						return;
					}
					DevCon.WriteLn("DEV9: PIO Default");
					pioMode = 2;
					break;
				case 0x01: //PIO modes 0-4; advanced modes are in IDENTIFY word 64.
					if (mode > 4)
					{
						CmdNoDataAbort();
						return;
					}
					DevCon.WriteLn("DEV9: PIO Mode %i", mode);
					pioMode = mode;
					break;
				case 0x04: //Multi word dma mode (0,1,2)
					if (mode > 2)
					{
						CmdNoDataAbort();
						return;
					}
					DevCon.WriteLn("DEV9: MDMA Mode %i", mode);
					mdmaMode = mode;
					udmaMode = -1;
					break;
				case 0x08: //UDMA modes 0-6, including the existing later ATA extensions.
					if (mode > 6)
					{
						CmdNoDataAbort();
						return;
					}
					DevCon.WriteLn("DEV9: UDMA Mode %i", mode);
					mdmaMode = -1;
					udmaMode = mode;
					break;
				default:
					Console.Error("DEV9: ATA: Unknown transfer mode");
					CmdNoDataAbort();
					return;
			}
		}
		break;
		default:
			Console.Error("DEV9: ATA: Unknown feature mode");
			CmdNoDataAbort();
			return;
	}
	PostCmdNoData();
}

void ATA::HDD_SetMultipleMode()
{
	if (!PreCmd())
		return;
	DevCon.WriteLn("DEV9: HDD_SetMultipleMode");

	// Preserve zero as the existing multiple-transfer disable extension.
	const u8 sectors = regNsector;
	if (sectors != 0 && (sectors < 2 || sectors > 128 || (sectors & (sectors - 1)) != 0))
	{
		CmdNoDataAbort();
		return;
	}

	curMultipleSectorsSetting = regNsector;

	PostCmdNoData();
}

void ATA::HDD_Nop()
{
	if (!PreCmd())
		return;
	DevCon.WriteLn("DEV9: HDD_Nop");

	if (regFeature == 0)
	{
		//This would abort queues if the
		//PS2 HDD supported them.
	}
	//Always ends in error
	regError |= ATA_ERR_ABORT;
	regStatus |= ATA_STAT_ERR;
	regStatusSeekLock = (regStatus & ATA_STAT_SEEK) ? 1 : -1;
	PostCmdNoData();
}

void ATA::HDD_ReadVerifyComplete()
{
	HDD_SetErrorAtTransferEnd();
	nsectorLeft = 0;
	PostCmdNoData();
}

void ATA::HDD_PowerCommand()
{
	if (!PreCmd())
		return;
	if (regCommand == 0xE5)
	{
		regNsector = powerState == PowerState::Standby ? 0 :
			(powerState == PowerState::Idle ? 0x80 : 0xff);
		PostCmdNoData();
		return;
	}
	if (regCommand == 0xE2 || regCommand == 0xE3)
	{
		u32 seconds = 0;
		if (regNsector <= 240)
			seconds = regNsector * 5;
		else if (regNsector <= 251)
			seconds = (regNsector - 240) * 30 * 60;
		else if (regNsector == 252)
			seconds = 21 * 60;
		else if (regNsector == 253)
			seconds = 10 * 60 * 60; // Allowed 8-12 hour implementation choice.
		else if (regNsector == 255)
			seconds = 21 * 60 + 15;
		else
		{
			CmdNoDataAbort(); // 254 is reserved.
			return;
		}
		standbyPeriod = static_cast<u64>(seconds) * 36864000;
		standbyRemaining = standbyPeriod;
	}
	if (regCommand == 0xE1 || regCommand == 0xE3)
	{
		powerState = PowerState::Idle;
		PostCmdNoData();
	}
	else
	{
		powerState = regCommand == 0xE6 ? PowerState::Sleep : PowerState::Standby;
		awaitFlush = true;
		Async(static_cast<u32>(-1));
	}
}
