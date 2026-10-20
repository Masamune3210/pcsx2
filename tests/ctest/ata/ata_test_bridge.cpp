// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "DEV9/DEV9.h"

ATAHarnessConfig EmuConfig;
ATAHarnessDEV9 dev9;
unsigned int ATAHarness::irqCount = 0;

void _DEV9irq(int cause, int cycles)
{
	dev9.irqcause |= cause;
	++ATAHarness::irqCount;
}

// PIO tests do not model SPEED DMA. These functions are external to ATA and
// deliberately do not consume or manufacture any ATA data or command state.
void DEV9runFIFO()
{
}

void DEV9ResetATAFIFO()
{
}
