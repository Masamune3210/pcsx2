// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

struct tlbs;

extern void WriteCP0Status(u32 value);
extern void WriteCP0Config(u32 value);
extern void cpuUpdateOperationMode();
extern void WriteTLB(int i);
extern void UnmapTLB(const tlbs& t, int i);
extern void MapTLB(const tlbs& t, int i);

extern void COP0_UpdatePCCR();
extern void COP0_DiagnosticPCCR();

// Interpreter instruction-step model; Wired values 0..47 define the range.
// No private phase state is needed, so the existing CP0 savestate is sufficient.
__fi void COP0_AdvanceRandom()
{
	const u32 wired = cpuRegs.CP0.n.Wired & 0x3f;
	if (wired >= 48)
		return; // Reserved/out-of-range Wired behavior is outside this candidate.
	u32& random = cpuRegs.CP0.n.Random;
	random = (random <= wired || random >= 48) ? 47 : random - 1;
}

// PSBBN diagnostic observer. Uses existing Trace.Enabled + Trace.EE.cop0.
enum class PSBBNTlbEvent
{
	TlbwiBefore, TlbwiAfter, TlbwrBefore, TlbwrAfter, TlbpBefore, TlbpAfter,
	Miss, ExceptionBefore, ExceptionAfter, EretBefore, EretAfter, Count
};
extern void PSBBNResetTlbTrace();
extern bool PSBBNTlbTraceEnabled();
extern void PSBBNTraceTlb(PSBBNTlbEvent event, u32 address, u32 detail = 0,
	int selected = -1, bool exact_interpreter_pc = false);
