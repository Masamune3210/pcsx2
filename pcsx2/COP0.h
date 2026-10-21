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
extern void SyncActiveTLBMappings();
extern bool EETLBStoreNeedsModification(u32 address);
extern bool EEInterpreterTLBStoreNeedsModification(u32 address);
extern void ResetActiveTLBMappings();
extern void WriteCP0EntryHi(u32 value);

extern void COP0_UpdatePCCR();
extern void COP0_DiagnosticPCCR();

// PSBBN diagnostic observer. Uses existing Trace.Enabled + Trace.EE.cop0.
enum class PSBBNTlbEvent
{
	TlbwiBefore, TlbwiAfter, TlbwrBefore, TlbwrAfter, TlbpBefore, TlbpAfter,
	Miss, ExceptionBefore, ExceptionAfter, EretBefore, EretAfter, Count
};
extern void PSBBNResetTlbTrace();
extern bool PSBBNTlbTraceEnabled();
extern void PSBBNTraceCacheIhin(u32 effectiveAddress);
extern void PSBBNTraceTlb(PSBBNTlbEvent event, u32 address, u32 detail = 0,
	int selected = -1, bool exact_interpreter_pc = false);
extern void PSBBNTraceSyscall(u32 call);
extern void PSBBNBeginLateTrace();
extern void PSBBNTraceLateException(u32 code, u32 bd, bool after);
extern void PSBBNTraceLateHeartbeat();
extern void PSBBNTraceUserFaultBefore(u32 code, u32 bd);
extern void PSBBNResetUserFaultWindow();
extern void PSBBNTraceReturnAddressWrite(u32 pc, u32 opcode, u32 oldRa, u32 oldGp);
extern void PSBBNTraceReturnStackAccess(u32 pc, u32 opcode);
extern void PSBBNTraceReturnSlotTranslation(u32 pc, u32 opcode, u32 sp, const char* phase);
extern void PSBBNTraceGpFrameBefore(u32 pc, u32 opcode);
extern void PSBBNTraceGpFrameAfter(u32 pc, u32 opcode, u32 priorGp);
// Targeted EE JIT ReiserFS read-after-write witness; phase: pre/post SW, pre/post LW.
extern void PSBBNTraceJournalWordAccess(u32 guestPc, u32 phase);
