// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// The isolated ATA executable compiles the real ATA sources against this bridge.
// Only emulator configuration, IRQ delivery and SPEED callbacks are replaced.
// Register constants below are copied from pcsx2/DEV9/DEV9.h; see README.md.
#include "common/Console.h"
#include "common/Pcsx2Defs.h"

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include <winioctl.h>
#endif

#define SPD_REGBASE 0x10000000
#define ATA_DEV9_HDD_BASE (SPD_REGBASE + 0x40)
#define ATA_R_DATA (ATA_DEV9_HDD_BASE + 0x00)
#define ATA_R_ERROR (ATA_DEV9_HDD_BASE + 0x02)
#define ATA_R_FEATURE (ATA_DEV9_HDD_BASE + 0x02)
#define ATA_R_NSECTOR (ATA_DEV9_HDD_BASE + 0x04)
#define ATA_R_SECTOR (ATA_DEV9_HDD_BASE + 0x06)
#define ATA_R_LCYL (ATA_DEV9_HDD_BASE + 0x08)
#define ATA_R_HCYL (ATA_DEV9_HDD_BASE + 0x0a)
#define ATA_R_SELECT (ATA_DEV9_HDD_BASE + 0x0c)
#define ATA_R_STATUS (ATA_DEV9_HDD_BASE + 0x0e)
#define ATA_R_CMD (ATA_DEV9_HDD_BASE + 0x0e)
#define ATA_R_ALT_STATUS (ATA_DEV9_HDD_BASE + 0x1c)
#define ATA_R_CONTROL (ATA_DEV9_HDD_BASE + 0x1c)
#define ATA_INTR_INTRQ (1 << 0)
#define ATA_ERR_MARK 0x01
#define ATA_ERR_TRACK0 0x02
#define ATA_ERR_ABORT 0x04
#define ATA_ERR_MCR 0x08
#define ATA_ERR_ID 0x10
#define ATA_ERR_MC 0x20
#define ATA_ERR_ECC 0x40
#define ATA_ERR_ICRC 0x80
#define ATA_STAT_ERR 0x01
#define ATA_STAT_INDEX 0x02
#define ATA_STAT_ECC 0x04
#define ATA_STAT_DRQ 0x08
#define ATA_STAT_SEEK 0x10
#define ATA_STAT_WRERR 0x20
#define ATA_STAT_READY 0x40
#define ATA_STAT_BUSY 0x80

struct ATAHarnessConfig
{
	struct
	{
		bool HddEnable = true;
	} DEV9;
};
extern ATAHarnessConfig EmuConfig;

struct ATAHarnessDEV9
{
	u16 irqcause = 0;
};
extern ATAHarnessDEV9 dev9;

namespace ATAHarness
{
	extern unsigned int irqCount;
}

void _DEV9irq(int cause, int cycles);
void DEV9runFIFO();
void DEV9ResetATAFIFO();
