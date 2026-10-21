// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "COP0.h"
#include "EEActiveTLB.h"
#include "vtlb.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <unordered_set>

#if defined(_M_X64) || defined(__x86_64__)
void recResetControlFlowTrace();
#endif

// Diagnostic observer only. Existing EE COP0 trace settings are the opt-in.
// No guest memory reads: instruction_sample can be stale on fetch faults and JIT misses.
namespace
{
struct PSBBNTraceState
{
	u64 ihin_seen = 0;
	u32 journal_word_emitted = 0;
	bool armed = false;
	bool target_window = false;
	bool zero_window = false;
	struct Channel
	{
		std::array<u32, 32> last{};
		u64 seen = 0;
		u64 repeats = 0;
		u32 emitted = 0;
	};
	std::array<Channel, static_cast<size_t>(PSBBNTlbEvent::Count)> channels{};
};
PSBBNTraceState s_psbbn_trace;
struct PSBBNLateTraceState
{
	bool armed = false;
	u32 ordinary_syscalls = 0, gs_syscalls = 0;
	u32 linux_syscalls = 0;
	u32 exception_before = 0, exception_after = 0, heartbeats = 0;
	u64 next_heartbeat = 0;
};
PSBBNLateTraceState s_psbbn_late;

// Deliberately fixed-size and allocated in host memory, not in the guest.
// A user process can generate many thousands of refills during a normal boot.
// Restart at each scheduled savestate so the late failure is not hidden by
// early-boot trace quotas.
struct PSBBNUserFaultEntry
{
	u32 pc = 0, address = 0, cause = 0, asid = 0;
	u64 occurrences = 0;
};
struct PSBBNUserFaultWindow
{
	std::array<PSBBNUserFaultEntry, 256> entries{};
	u32 used = 0, emitted = 0;
	u64 dropped = 0;
};
PSBBNUserFaultWindow s_psbbn_user_faults;
u32 s_psbbn_badreturn_emitted = 0;

// PSBBN-specific diagnostic: track return-address stack slots created by
// 0x2AB0F2D8 (sw ra, 0x40(sp)), and record later guest stores overlapping
// those slots. Kept entirely in host memory; no guest memory is modified.
struct PSBBNTrackedReturnSlot
{
	u32 address = 0;
	u32 asid = 0;
	u32 entryRa = 0;
	bool valid = false;
};
std::array<PSBBNTrackedReturnSlot, 64> s_psbbn_tracked_returns{};
u32 s_psbbn_tracked_return_cursor = 0;
u32 s_psbbn_tracked_return_emitted = 0;
u32 s_psbbn_focus_asid07_events = 0;
u32 s_psbbn_focus_other_events = 0;
struct PSBBNTrackedGpFrame { u32 address = 0; u32 asid = 0; u32 savedGp = 0; bool valid = false; };
std::array<PSBBNTrackedGpFrame, 64> s_psbbn_gp_frames{};
u32 s_psbbn_gp_frame_cursor = 0;
u32 s_psbbn_gp_frame_events = 0;
u32 s_psbbn_gp_mismatch_events = 0;
}

void PSBBNResetTlbTrace()
{
#if defined(_M_X64) || defined(__x86_64__)
	recResetControlFlowTrace();
#endif
	s_psbbn_trace = {};
	s_psbbn_late = {};
	s_psbbn_user_faults = {};
	s_psbbn_badreturn_emitted = 0;
	s_psbbn_tracked_returns = {};
	s_psbbn_tracked_return_cursor = 0;
	s_psbbn_tracked_return_emitted = 0;
	s_psbbn_focus_asid07_events = 0;
	s_psbbn_focus_other_events = 0;
	s_psbbn_gp_frames = {};
	s_psbbn_gp_frame_cursor = 0;
	s_psbbn_gp_frame_events = 0;
	s_psbbn_gp_mismatch_events = 0;
}

bool PSBBNTlbTraceEnabled()
{
	return EmuConfig.Trace.Enabled && EmuConfig.Trace.EE.cop0;
}

// One guest instruction pair only. No virtual->physical writes, no speculative
// guest loads, and no vtlb_V2P() (its ppmap is optional). The JIT emitter
// flushes live GPRs before calling this observer, so the registered guest PC
// is exact even when cpuRegs.pc is stale inside a translated block.
void PSBBNTraceJournalWordAccess(u32 guestPc, u32 phase)
{
	if (!PSBBNTlbTraceEnabled() || Cpu == &intCpu)
		return;
	if (!((guestPc == 0x800B1578 && phase <= 1) ||
		(guestPc == 0x800B1580 && phase >= 2 && phase <= 3)))
		return;
	if (s_psbbn_trace.journal_word_emitted >= 32)
		return;
	const u32 emitted = ++s_psbbn_trace.journal_word_emitted;

	const u32 v0 = cpuRegs.GPR.r[2].UL[0];
	const u32 v1 = cpuRegs.GPR.r[3].UL[0];
	const u32 base = (phase <= 1) ? v1 : v0;
	const u32 address = base + 0x2dc;
	const auto& cp0 = cpuRegs.CP0.n;
	const u8 asid = static_cast<u8>(cp0.EntryHi);
	const auto active = BuildEEActiveTLBPageMap(tlb, std::size(tlb), asid);
	const auto entry = active.find(address >> 12);
	const bool archMapped = entry != active.end();
	const u32 physical = archMapped ? entry->second.physicalAddress + (address & 0xfff) : 0xffffffffu;
	const bool writable = archMapped && entry->second.writable;
	bool directlyMapped = false;
	u32 virtualWord = 0xffffffffu;
	u32 vmapHandler = 0xffffffffu;
	if (vtlb_private::vtlbdata.vmap)
	{
		const auto& mapping = vtlb_private::vtlbdata.vmap[address >> vtlb_private::VTLB_PAGE_BITS];
		if (mapping.isHandler(address))
			vmapHandler = mapping.assumeHandlerGetID();
		else
			virtualWord = vtlb_ramRead<mem32_t>(address, &directlyMapped);
	}

	const bool physicalReadable = eeMem && archMapped && !entry->second.scratchpad &&
		physical <= Ps2MemSize::MainRam - sizeof(u32);
	u32 physicalWord = 0xffffffffu;
	if (physicalReadable)
		std::memcpy(&physicalWord, eeMem->Main + physical, sizeof(physicalWord));

	Console.WriteLn("PSBBN-JOURNAL-WORD n=%u phase=%u guestPC=%08x ea=%08x v0=%08x v1=%08x vmapDirect=%u vmapHandler=%08x vmapValue=%08x archMapped=%u archPhysical=%08x archWritable=%u physicalReadable=%u physValue=%08x ASID=%02x Status=%08x Cause=%08x EPC=%08x EntryHi=%08x",
		emitted, phase, guestPc, address, v0, v1, directlyMapped ? 1u : 0u,
		vmapHandler, virtualWord, archMapped ? 1u : 0u, physical, writable ? 1u : 0u,
		physicalReadable ? 1u : 0u, physicalWord, asid, cp0.Status.val,
		cp0.Cause, cp0.EPC, cp0.EntryHi);
}

void PSBBNResetUserFaultWindow()
{
	if (!PSBBNTlbTraceEnabled())
		return;
	Console.WriteLn("PSBBN-FAULT window complete unique=%u emitted=%u overflow=%llu",
		s_psbbn_user_faults.used, s_psbbn_user_faults.emitted,
		static_cast<unsigned long long>(s_psbbn_user_faults.dropped));
	s_psbbn_user_faults = {};
	s_psbbn_badreturn_emitted = 0;
	// The observed function can span an autosave. Keep its tracked return
	// slot across intervals; rotate the output budget, not the provenance.
	s_psbbn_tracked_return_emitted = 0;
	s_psbbn_focus_asid07_events = 0;
	s_psbbn_focus_other_events = 0;
	// Retain watched guest GP frames across scheduled savestates.
	s_psbbn_gp_frame_events = 0;
	s_psbbn_gp_mismatch_events = 0;
}

// Observes interpreter CPU address errors and TLB faults before EXL changes.
// Do not read a stale saved EPC as the faulting PC; the caller supplies the
// current cpuRegs.pc and branch-delay flag prior to cpuExceptionInternal.
// Additionally sample Linux O32 sigreturn syscalls to detect signal storms.
void PSBBNTraceUserFaultBefore(u32 code, u32 bd)
{
	if (!PSBBNTlbTraceEnabled() || Cpu != &intCpu)
		return;
	const u32 status = cpuRegs.CP0.n.Status.val;
	if ((status & 0x1eu) != 0x10u) // guest user KSU=2, EXL/ERL clear
		return;
	const u32 cause = code & 0x7cu;
	const bool sigreturn = cause == 0x20u && cpuRegs.GPR.n.v0.UL[0] == 4119u;
	if (!(cause == 4u || cause == 8u || cause == 12u || sigreturn))
		return;

	const u32 pc = cpuRegs.pc;
	const u32 bad = sigreturn ? 0 : cpuRegs.CP0.n.BadVAddr;
	const u32 asid = cpuRegs.CP0.n.EntryHi & 0xffu;
	auto& history = s_psbbn_user_faults;
	PSBBNUserFaultEntry* entry = nullptr;
	for (u32 i = 0; i < history.used; ++i)
	{
		auto& item = history.entries[i];
		if (item.pc == pc && item.address == bad && item.cause == cause && item.asid == asid)
		{
			entry = &item;
			break;
		}
	}
	if (!entry)
	{
		if (history.used >= history.entries.size())
		{
			++history.dropped;
			return;
		}
		entry = &history.entries[history.used++];
		entry->pc = pc;
		entry->address = bad;
		entry->cause = cause;
		entry->asid = asid;
	}
	const u64 count = ++entry->occurrences;
	// One line per unique fault, then exponentially spaced samples. Limit the
	// total output per autosave interval independent of exception frequency.
	if (history.emitted >= 384 ||
		(count > 1 && (count & (count - 1)) != 0))
		return;
	++history.emitted;
	Console.WriteLn("PSBBN-FAULT pc=%08x bad=%08x code=%02x asid=%02x n=%llu bd=%u Status=%08x EntryHi=%08x v0=%08x s0=%08x s1=%08x sp=%08x ra=%08x gp=%08x t9=%08x a0=%08x a1=%08x",
		pc, bad, cause, asid, static_cast<unsigned long long>(count), bd,
		status, cpuRegs.CP0.n.EntryHi, cpuRegs.GPR.n.v0.UL[0],
		cpuRegs.GPR.r[16].UL[0], cpuRegs.GPR.r[17].UL[0],
		cpuRegs.GPR.n.sp.UL[0], cpuRegs.GPR.n.ra.UL[0],
		cpuRegs.GPR.r[28].UL[0], cpuRegs.GPR.r[25].UL[0],
		cpuRegs.GPR.r[4].UL[0], cpuRegs.GPR.r[5].UL[0]);
}

// The first post-ASID SIGSEGV fetched from 0x2AC24400 with $ra equal to
// that address, outside the mapped text/data VMA. Log the first instruction
// *changing* $ra into that suspicious range. Never overwrite guest registers.
void PSBBNTraceReturnAddressWrite(u32 pc, u32 opcode, u32 oldRa, u32 oldGp)
{
	if (!PSBBNTlbTraceEnabled() || Cpu != &intCpu ||
		s_psbbn_badreturn_emitted >= 64)
		return;

	const u32 ra = cpuRegs.GPR.r[31].UL[0];
	const u32 gp = cpuRegs.GPR.r[28].UL[0];
	// The range is an opt-in PSBBN investigative watch, not an EE MMU rule.
	// A matching gp/ra transition is especially relevant to the bad return.
	if (ra < 0x2ac23000u || ra >= 0x2ad00000u)
		return;
	++s_psbbn_badreturn_emitted;
	Console.WriteLn("PSBBN-RA-WRITE n=%u pc=%08x opcode=%08x oldRA=%08x newRA=%08x oldGP=%08x newGP=%08x t9=%08x sp=%08x nextPC=%08x EntryHi=%08x",
		s_psbbn_badreturn_emitted, pc, opcode, oldRa, ra, oldGp, gp,
		cpuRegs.GPR.r[25].UL[0], cpuRegs.GPR.r[29].UL[0], cpuRegs.pc,
		cpuRegs.CP0.n.EntryHi);
}

// Read-only address-space witness at a verified guest prologue/epilogue.
// The architectural TLB projection and the live vTLB physical lookup are
// independent: divergence establishes a stale/wrong active translation.
// Reading eeMem->Main directly does not trigger guest exceptions, refill
// handlers, or other guest-visible MMIO side effects.
void PSBBNTraceReturnSlotTranslation(u32 pc, u32 opcode, u32 sp, const char* phase)
{
	if (!PSBBNTlbTraceEnabled() || Cpu != &intCpu ||
		!((pc == 0x2ab0f2d8u && opcode == 0xafbf0040u) ||
		  (pc == 0x2ab0f3a4u && opcode == 0x8fbf0040u)))
		return;

	// This is a diagnostic only. VM memory may have been released while
	// execution is being stopped, so never dereference a null EE RAM base.
	// Keep one bounded marker to distinguish teardown/lifetime problems from
	// guest TLB translation differences in the next debugger run.
	const auto* const sampledMemory = eeMem;
	if (sampledMemory == nullptr)
	{
		static bool s_reportedMissingEEMemory = false;
		if (!s_reportedMissingEEMemory)
		{
			s_reportedMissingEEMemory = true;
			Console.WriteLn("PSBBN-RA-PHYS-SKIP reason=eeMem-null phase=%s pc=%08x sp=%08x",
				phase, pc, sp);
		}
		return;
	}

	const u32 slot = sp + 0x40u;
	const u32 asid = cpuRegs.CP0.n.EntryHi & 0xffu;
	const auto active = BuildEEActiveTLBPageMap(tlb, std::size(tlb), static_cast<u8>(asid));
	const auto expected = active.find(slot >> 12);
	const bool archValid = expected != active.end() && !expected->second.scratchpad;
	const u32 archPhysical = archValid ? expected->second.physicalAddress + (slot & 0xfffu) : 0xffffffffu;
	// vtlb_V2P uses the optional Goemon gamefix LUT, not the always-live
	// vmap. Normal interpreter runs leave ppmap null. Do not allocate a
	// gamefix LUT for tracing or claim an unavailable lookup is a mapping.
	const bool vtlbAvailable = vtlb_private::vtlbdata.ppmap != nullptr;
	const u32 vtlbPhysical = vtlbAvailable ? vtlb_V2P(slot) : 0xffffffffu;
	u32 archWord = 0xffffffffu;
	u32 vtlbWord = 0xffffffffu;
	if (archValid && archPhysical <= Ps2MemSize::MainRam - sizeof(u32))
		std::memcpy(&archWord, sampledMemory->Main + archPhysical, sizeof(archWord));
	if (vtlbPhysical <= Ps2MemSize::MainRam - sizeof(u32))
		std::memcpy(&vtlbWord, sampledMemory->Main + vtlbPhysical, sizeof(vtlbWord));
	Console.WriteLn("PSBBN-RA-PHYS phase=%s pc=%08x asid=%02x sp=%08x slot=%08x opcode=%08x archValid=%u archPhys=%08x archWord=%08x vtlbPhys=%08x vtlbWord=%08x samePhys=%u ra=%08x gp=%08x status=%08x vtlbAvailable=%u",
		phase, pc, asid, sp, slot, opcode, archValid ? 1u : 0u, archPhysical,
		archWord, vtlbPhysical, vtlbWord,
		(vtlbAvailable && archValid && archPhysical == vtlbPhysical) ? 1u : 0u,
		cpuRegs.GPR.r[31].UL[0], cpuRegs.GPR.r[28].UL[0],
		cpuRegs.CP0.n.Status.val, vtlbAvailable ? 1u : 0u);
}

// Debug witness for the specific saved-RA corruption. The linked user image
// has the ordinary 0x50-byte function frame:
//   2AB0F2D8: sw ra,0x40(sp)   (entry)
//   2AB0F3A4: lw ra,0x40(sp)   (exit)
// In the fourth cold boot the exit read GP (0x2AC24400), causing a bad jump.
// Trace the entry value and any guest CPU store overwriting its tracked slot.
void PSBBNTraceReturnStackAccess(u32 pc, u32 opcode)
{
	if (!PSBBNTlbTraceEnabled() || Cpu != &intCpu)
		return;

	const u32 asid = cpuRegs.CP0.n.EntryHi & 0xffu;
	const u32 sp = cpuRegs.GPR.r[29].UL[0];
	const u32 ra = cpuRegs.GPR.r[31].UL[0];
	const u32 gp = cpuRegs.GPR.r[28].UL[0];
	// Independent budgets keep early ASID 04 activity from hiding the ASID 07 failure.
	u32& focusEmitted = (asid == 0x07u) ? s_psbbn_focus_asid07_events : s_psbbn_focus_other_events;
	const u32 focusBudget = (asid == 0x07u) ? 96u : 24u;

	// Observe function entry and exit in *every* process. The fifth boot
	// executed the epilogue in ASID 07, but the only witnessed prologue
	// belonged to ASID 04. Do not conflate their stack frames.
	if (pc == 0x2ab0f2bcu || (pc == 0x2ab0f3a4u && opcode == 0x8fbf0040u))
	{
		if (focusEmitted < focusBudget)
		{
			++focusEmitted;
			Console.WriteLn("PSBBN-FRAME-PATH pc=%08x opcode=%08x asid=%02x sp=%08x slot=%08x ra=%08x gp=%08x t9=%08x EntryHi=%08x",
				pc, opcode, asid, sp, sp + 0x40u, ra, gp,
				cpuRegs.GPR.r[25].UL[0], cpuRegs.CP0.n.EntryHi);
		}
		if (pc == 0x2ab0f3a4u)
		{
			// A completed frame can be recycled. Retire its original watch.
			for (auto& frame : s_psbbn_tracked_returns)
			{
				if (frame.valid && frame.asid == asid && frame.address == sp + 0x40u)
					frame.valid = false;
			}
		}
		return;
	}

	if (pc == 0x2ab0f2d8u && opcode == 0xafbf0040u)
	{
		// A reused stack address starts a fresh invocation. Do not attribute
		// later writes to an obsolete frame for the same ASID/virtual address.
		for (auto& old : s_psbbn_tracked_returns)
		{
			if (old.valid && old.asid == asid && old.address == sp + 0x40u)
				old.valid = false;
		}
		auto& frame = s_psbbn_tracked_returns[
		(s_psbbn_tracked_return_cursor++) % s_psbbn_tracked_returns.size()];
		frame = {sp + 0x40u, asid, ra, true};
		if (s_psbbn_tracked_return_emitted < 128)
		{
			++s_psbbn_tracked_return_emitted;
			Console.WriteLn("PSBBN-FRAME-SAVE pc=%08x slot=%08x sp=%08x savedRA=%08x gp=%08x t9=%08x asid=%02x sameGP=%u",
				pc, frame.address, sp, ra, gp, cpuRegs.GPR.r[25].UL[0], asid,
				ra == gp ? 1u : 0u);
		}
		return;
	}

	// Check only actual store-class instruction opcodes. The source GPR and
	// effective address are sampled BEFORE execution, so a later exception
	// does not destroy the provenance of the attempted write.
	const u32 op = opcode >> 26;
	u32 width;
	switch (op)
	{
		case 0x28: width = 1; break; // SB
		case 0x29: width = 2; break; // SH
		case 0x2a: // SWL
		case 0x2b: // SW
		case 0x2e: width = 4; break; // SWR
		case 0x2c: // SDL
		case 0x2d: // SDR
		case 0x3f: width = 8; break; // SD
		default: return;
	}
	const u32 base = cpuRegs.GPR.r[(opcode >> 21) & 31].UL[0];
	const u32 address = base + static_cast<s16>(opcode);
	const u32 rt = (opcode >> 16) & 31;
	// Independent focus watch: no prologue was recorded in ASID 07 on the
	// fifth boot. Observe attempts to write the *actual* failing stack slot
	// even if there is no corresponding active host-side frame record.
	// The address is a diagnostic target only, never a new memory rule.
	if (address <= 0x7fff7123u && (address + width - 1u) >= 0x7fff7120u &&
		focusEmitted < focusBudget)
	{
		++focusEmitted;
		Console.WriteLn("PSBBN-STACK-FOCUS pc=%08x opcode=%08x addr=%08x width=%u asid=%02x Status=%08x rt=%u value=%08x ra=%08x gp=%08x sp=%08x EntryHi=%08x attempted=1",
			pc, opcode, address, width, asid, cpuRegs.CP0.n.Status.val,
			rt, cpuRegs.GPR.r[rt].UL[0], ra, gp, sp, cpuRegs.CP0.n.EntryHi);
	}
	for (const auto& frame : s_psbbn_tracked_returns)
	{
		if (!frame.valid || frame.asid != asid ||
			address > frame.address + 3u ||
			address + width - 1u < frame.address)
			continue;
		if (s_psbbn_tracked_return_emitted < 128)
		{
			++s_psbbn_tracked_return_emitted;
			Console.WriteLn("PSBBN-FRAME-STORE pc=%08x opcode=%08x addr=%08x width=%u slot=%08x rt=%u value=%08x entryRA=%08x currentRA=%08x gp=%08x sp=%08x asid=%02x",
				pc, opcode, address, width, frame.address, rt,
				cpuRegs.GPR.r[rt].UL[0], frame.entryRa, ra, gp, sp, asid);
		}
		break;
	}
}

// Diagnostic only: witness the GP stack frame in the shared MIPS PIC function
// at 0x2ABB7038. Do not read/modify guest memory or change CPU semantics.
// A traced store is *attempted*; an exception/DMA write is not ruled out.
void PSBBNTraceGpFrameBefore(u32 pc, u32 opcode)
{
	if (!PSBBNTlbTraceEnabled() || Cpu != &intCpu)
		return;
	const u32 asid = cpuRegs.CP0.n.EntryHi & 0xffu;
	const u32 sp = cpuRegs.GPR.r[29].UL[0];
	const u32 gp = cpuRegs.GPR.r[28].UL[0];
	if (pc == 0x2abb7048u && opcode == 0xafbc0010u)
	{
		const u32 slot = sp + 0x10u;
		for (auto& old : s_psbbn_gp_frames)
		{
			if (old.valid && old.asid == asid && old.address == slot)
				old.valid = false;
		}
		auto& frame = s_psbbn_gp_frames[
			(s_psbbn_gp_frame_cursor++) % s_psbbn_gp_frames.size()];
		frame = {slot, asid, gp, true};
		if (s_psbbn_gp_frame_events < 96)
		{
			++s_psbbn_gp_frame_events;
			Console.WriteLn("PSBBN-GP-SAVE pc=%08x asid=%02x sp=%08x slot=%08x gp=%08x ra=%08x t9=%08x EntryHi=%08x attempted=1",
				pc, asid, sp, slot, gp, cpuRegs.GPR.r[31].UL[0],
				cpuRegs.GPR.r[25].UL[0], cpuRegs.CP0.n.EntryHi);
		}
		return;
	}
	const u32 op = opcode >> 26;
	u32 width = 0;
	switch (op)
	{
		case 0x1f: width = 16; break; // SQ
		case 0x28: width = 1; break; // SB
		case 0x29: width = 2; break; // SH
		case 0x2a: // SWL (conservative overlap window)
		case 0x2b: // SW
		case 0x2e: width = 4; break; // SWR
		case 0x2c: // SDL (conservative overlap window)
		case 0x2d: width = 8; break; // SDR
		case 0x39: // SWC1
		case 0x3a: width = 4; break; // SWC2
		case 0x3d: // SDC1
		case 0x3f: width = 8; break; // SD
		case 0x3e: width = 16; break; // SQC2
		default: return;
	}
	const u32 base = cpuRegs.GPR.r[(opcode >> 21) & 31].UL[0];
	const u32 addr = base + static_cast<s16>(opcode);
	const u32 rt = (opcode >> 16) & 31;
	for (const auto& frame : s_psbbn_gp_frames)
	{
		if (!frame.valid || frame.asid != asid ||
			addr > frame.address + 3u || (addr + width - 1u) < frame.address)
			continue;
		if (s_psbbn_gp_frame_events < 96)
		{
			++s_psbbn_gp_frame_events;
			Console.WriteLn("PSBBN-GP-STORE pc=%08x opcode=%08x asid=%02x addr=%08x width=%u slot=%08x expectedGP=%08x srcReg=%u srcGprLo=%08x currentGP=%08x sp=%08x Status=%08x attempted=1",
				pc, opcode, asid, addr, width, frame.address, frame.savedGp,
				rt, cpuRegs.GPR.r[rt].UL[0], gp, sp, cpuRegs.CP0.n.Status.val);
		}
		break;
	}
}

void PSBBNTraceGpFrameAfter(u32 pc, u32 opcode, u32 priorGp)
{
	if (!PSBBNTlbTraceEnabled() || Cpu != &intCpu ||
		pc != 0x2abb7248u || opcode != 0x8fbc0010u)
		return;
	const u32 asid = cpuRegs.CP0.n.EntryHi & 0xffu;
	const u32 sp = cpuRegs.GPR.r[29].UL[0];
	const u32 slot = sp + 0x10u;
	const u32 gp = cpuRegs.GPR.r[28].UL[0];
	for (const auto& frame : s_psbbn_gp_frames)
	{
		if (!frame.valid || frame.address != slot || frame.asid != asid)
			continue;
		const bool mismatch = gp != frame.savedGp;
		// A busy function can consume the routine quota before the fatal call.
		// Preserve a separate budget specifically for the bad GP restoration.
		if (mismatch && s_psbbn_gp_mismatch_events < 32)
		{
			++s_psbbn_gp_mismatch_events;
			Console.WriteLn("PSBBN-GP-MISMATCH pc=%08x asid=%02x sp=%08x slot=%08x expectedGP=%08x priorGP=%08x restoredGP=%08x EntryHi=%08x",
				pc, asid, sp, slot, frame.savedGp, priorGp, gp, cpuRegs.CP0.n.EntryHi);
		}
		else if (s_psbbn_gp_frame_events < 96)
		{
			++s_psbbn_gp_frame_events;
			Console.WriteLn("PSBBN-GP-RESTORE pc=%08x asid=%02x sp=%08x slot=%08x expectedGP=%08x priorGP=%08x restoredGP=%08x mismatch=%u EntryHi=%08x",
				pc, asid, sp, slot, frame.savedGp, priorGp, gp,
				mismatch ? 1u : 0u, cpuRegs.CP0.n.EntryHi);
		}
		return;
	}
	if (s_psbbn_gp_frame_events < 96)
	{
		++s_psbbn_gp_frame_events;
		Console.WriteLn("PSBBN-GP-RESTORE pc=%08x asid=%02x sp=%08x slot=%08x expectedGP=UNKNOWN priorGP=%08x restoredGP=%08x EntryHi=%08x",
			pc, asid, sp, slot, priorGp, gp, cpuRegs.CP0.n.EntryHi);
	}
}

// CACHE 0x0b is IHIN (instruction-cache hit invalidate). The EE interpreter
// fetches directly from backing memory, so it has no separate I-cache line to
// invalidate. Record exact issuing PCs/effective addresses, with a hard cap,
// until the independent EE JIT instruction-cache coherence issue is addressed.
void PSBBNTraceCacheIhin(u32 effectiveAddress)
{
	if (!PSBBNTlbTraceEnabled() || Cpu != &intCpu)
		return;
	const u64 observed = ++s_psbbn_trace.ihin_seen;
	// First 32 observations, then powers of two up to 65,536:
	// at most 43 lines per reset, even during bulk invalidations.
	if (observed > 65536 || (observed > 32 && (observed & (observed - 1)) != 0))
		return;
	Console.WriteLn("PSBBN-IHIN n=%llu pc=%08x opcode=%08x effective=%08x Status=%08x EPC=%08x EntryHi=%08x base=%08x rs=%u offset=%d",
		static_cast<unsigned long long>(observed), cpuRegs.pc - 4, cpuRegs.code,
		effectiveAddress, cpuRegs.CP0.n.Status.val, cpuRegs.CP0.n.EPC,
		cpuRegs.CP0.n.EntryHi, cpuRegs.GPR.r[(cpuRegs.code >> 21) & 31].UL[0],
		(cpuRegs.code >> 21) & 31, static_cast<s16>(cpuRegs.code));
}

void PSBBNTraceTlb(PSBBNTlbEvent event, u32 address, u32 detail, int selected, bool exact_interpreter_pc)
{
	if (!PSBBNTlbTraceEnabled())
		return;
	// Arm on the linked kernel or the reported KSEG2 region. Once armed, include copied vectors.
	if (!s_psbbn_trace.armed)
	{
		if (!((cpuRegs.pc >= 0x80010000 && cpuRegs.pc < 0x8021db50) ||
			(address >= 0xc0000000 && address < 0xc0010000)))
			return;
		s_psbbn_trace.armed = true;
		Console.WriteLn("PSBBN-TLB armed; 64 snapshots/channel, then capped; pc/code/delay in JIT misses are samples");
	}
	// Preserve a separate bounded window for the first relevant miss after initialization.
	if (!s_psbbn_trace.target_window && event == PSBBNTlbEvent::Miss &&
		address >= 0xc0000000 && address < 0xc0010000)
	{
		s_psbbn_trace.target_window = true;
		s_psbbn_trace.channels = {};
		Console.WriteLn("PSBBN-TLB first KSEG2 target miss: begin target bounded window");
	}
	// Valid refills can exhaust the target budget before the later invalid-entry loop.
	// Reserve exactly one more observer window; never change guest state or mappings.
	if (s_psbbn_trace.target_window && !s_psbbn_trace.zero_window &&
		event == PSBBNTlbEvent::TlbwrBefore && cpuRegs.CP0.n.EntryLo0 == 0 &&
		cpuRegs.CP0.n.EntryLo1 == 0)
	{
		s_psbbn_trace.zero_window = true;
		s_psbbn_trace.channels = {};
		Console.WriteLn("PSBBN-TLB first zero-entry TLBWR: begin final bounded window=2");
	}
	auto& channel = s_psbbn_trace.channels[static_cast<size_t>(event)];
	if (channel.emitted >= 64)
		return;

	const auto& cp0 = cpuRegs.CP0.n;
	// Observer's architectural candidate lookup, deliberately independent of TLBP's implementation.
	// Counts overlapping candidates, including invalid entries; this does not replace vTLB lookup.
	u32 matches = 0;
	int candidate = -1;
	for (int i = 0; i < 48; i++)
	{
		const auto& t = tlb[i];
		if ((address & ~((t.Mask() << 13) | 0x1fffu)) == t.VPN2() &&
			(t.isGlobal() || t.EntryHi.ASID == (cp0.EntryHi & 0xff)))
		{
			matches++;
			if (candidate < 0)
				candidate = i;
		}
	}
	if (selected < 0)
		selected = candidate;
	const tlbs entry = (selected >= 0 && selected < 48) ? tlb[selected] : tlbs{};
	u32 target_handler = 0xffffffffu;
	if (vtlb_private::vtlbdata.vmap)
	{
		const auto mapping = vtlb_private::vtlbdata.vmap[0xc0005000u >> vtlb_private::VTLB_PAGE_BITS];
		// FFFFFFFF means direct pointer; otherwise this is a handler ID, not a validity verdict.
		if (mapping.isHandler(0xc0005000))
			target_handler = mapping.assumeHandlerGetID();
	}
	const u32 exact_pc = exact_interpreter_pc && Cpu == &intCpu ? cpuRegs.pc - 4 : 0xffffffffu;
	const std::array<u32, 32> snapshot = {
		cpuRegs.pc, exact_pc, cpuRegs.code, cp0.EPC, cp0.Cause, cp0.Status.val,
		cp0.BadVAddr & ~0xfffu, cp0.Context, cp0.EntryHi, cp0.EntryLo0, cp0.EntryLo1, cp0.PageMask,
		cp0.Index, cp0.Random, cp0.Wired, static_cast<u32>(cpuRegs.branch), cpuRegs.IsDelaySlot,
		static_cast<u32>(selected), entry.PageMask.UL, entry.EntryHi.UL, entry.EntryLo0.UL,
		entry.EntryLo1.UL, address >> 12, detail, cpuRegs.GPR.r[31].UL[0], cpuRegs.GPR.r[29].UL[0],
		cpuRegs.GPR.r[4].UL[0], cpuRegs.GPR.r[5].UL[0], cpuRegs.GPR.r[6].UL[0], matches,
		target_handler, cp0.ErrorEPC};
	channel.seen++;
	const bool changed = channel.seen == 1 || snapshot != channel.last;
	channel.repeats = changed ? 1 : channel.repeats + 1;
	channel.last = snapshot;
	// Ignore cycle and low address offset when grouping; retain changes and sparse heartbeat repeats.
	const u64 repeat = channel.repeats;
	if (!changed && repeat != 2 && repeat != 3 && repeat != 8 && repeat != 64 &&
		repeat != 512 && repeat != 4096 && repeat != 32768 && repeat != 262144)
		return;
	channel.emitted++;
	static constexpr const char* names[] = {
		"tlbwi-before", "tlbwi-after", "tlbwr-before", "tlbwr-after", "tlbp-before", "tlbp-after",
		"vtlb-miss", "exception-before", "exception-after", "eret-before", "eret-after"};
	Console.WriteLn("PSBBN-TLB %s window=%u n=%llu repeat=%llu sample=%u cpu=%s rawpc=%08x exactpc=%08x instruction_sample=%08x cycle=%llu address=%08x detail=%08x branch_sample=%u delay_sample=%u",
		names[static_cast<size_t>(event)], s_psbbn_trace.zero_window ? 2u : static_cast<u32>(s_psbbn_trace.target_window), static_cast<unsigned long long>(channel.seen),
		static_cast<unsigned long long>(repeat), channel.emitted, Cpu == &intCpu ? "int" : "jit",
		cpuRegs.pc, exact_pc, cpuRegs.code, static_cast<unsigned long long>(cpuRegs.cycle), address, detail,
		static_cast<u32>(cpuRegs.branch), cpuRegs.IsDelaySlot);
	Console.WriteLn("PSBBN-TLB cp0 EPC=%08x Cause=%08x Status=%08x BadVAddr=%08x Context=%08x EntryHi=%08x Lo0=%08x Lo1=%08x Mask=%08x Index=%08x Random=%08x Wired=%08x ErrorEPC=%08x",
		cp0.EPC, cp0.Cause, cp0.Status.val, cp0.BadVAddr, cp0.Context, cp0.EntryHi, cp0.EntryLo0,
		cp0.EntryLo1, cp0.PageMask, cp0.Index, cp0.Random, cp0.Wired, cp0.ErrorEPC);
	Console.WriteLn("PSBBN-TLB slot=%d matches=%u slotMask=%08x slotHi=%08x slotLo0=%08x slotLo1=%08x c0005000_handler=%08x ra_sample=%08x sp_sample=%08x a0_sample=%08x a1_sample=%08x a2_sample=%08x",
		selected, matches, entry.PageMask.UL, entry.EntryHi.UL, entry.EntryLo0.UL, entry.EntryLo1.UL,
		target_handler, cpuRegs.GPR.r[31].UL[0], cpuRegs.GPR.r[29].UL[0], cpuRegs.GPR.r[4].UL[0],
		cpuRegs.GPR.r[5].UL[0], cpuRegs.GPR.r[6].UL[0]);
	if (channel.emitted == 64)
		Console.WriteLn("PSBBN-TLB %s capped for this window", names[static_cast<size_t>(event)]);
}



// Independent budgets: the original TLB windows can cap before userspace stalls.
// No guest memory reads, state writes, syscall suppression, or mode corrections.
static bool PSBBNLateTraceEnabled()
{
	return s_psbbn_trace.armed && Cpu == &intCpu && PSBBNTlbTraceEnabled();
}

void PSBBNBeginLateTrace()
{
	if (!PSBBNLateTraceEnabled() || s_psbbn_late.armed)
		return;
	s_psbbn_late.armed = true;
	Console.WriteLn("PSBBN-LATE armed reason=first-armed-setgscrt-classification pc=%08x", cpuRegs.pc);
}

void PSBBNTraceSyscall(u32 call)
{
	if (!PSBBNLateTraceEnabled())
		return;
	// Observe the BIOS classification without assuming this really is a BIOS call.
	if (call == 2)
		PSBBNBeginLateTrace();
	u32& emitted = call == 0x100 ? s_psbbn_late.linux_syscalls :
		(call == 2 ? s_psbbn_late.gs_syscalls : s_psbbn_late.ordinary_syscalls);
	const u32 limit = call == 2 ? 32 : 16;
	if (emitted >= limit)
		return;
	emitted++;
	Console.WriteLn("PSBBN-SYSCALL call=%08x abi=%s n=%u syscallpc=%08x code=%08x cycle=%llu Status=%08x EPC=%08x EntryHi=%08x v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x a3=%08x ra=%08x sp=%08x branch=%u delay=%u",
		call, call == 0x100 ? "linux-o32" : "bios-v1", emitted, cpuRegs.pc - 4, cpuRegs.code, static_cast<unsigned long long>(cpuRegs.cycle),
		cpuRegs.CP0.n.Status.val, cpuRegs.CP0.n.EPC, cpuRegs.CP0.n.EntryHi,
		cpuRegs.GPR.r[2].UL[0], cpuRegs.GPR.r[3].UL[0], cpuRegs.GPR.r[4].UL[0],
		cpuRegs.GPR.r[5].UL[0], cpuRegs.GPR.r[6].UL[0], cpuRegs.GPR.r[7].UL[0],
		cpuRegs.GPR.r[31].UL[0], cpuRegs.GPR.r[29].UL[0], static_cast<u32>(cpuRegs.branch), cpuRegs.IsDelaySlot);
}

static void PSBBNWriteLateState(const char* event, u32 n, u32 detail)
{
	Console.WriteLn("PSBBN-LATE event=%s n=%u detail=%08x pc=%08x code_sample=%08x cycle=%llu EPC=%08x Cause=%08x Status=%08x BadVAddr=%08x EntryHi=%08x v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x a3=%08x ra=%08x sp=%08x branch=%u delay=%u",
		event, n, detail, cpuRegs.pc, cpuRegs.code, static_cast<unsigned long long>(cpuRegs.cycle),
		cpuRegs.CP0.n.EPC, cpuRegs.CP0.n.Cause, cpuRegs.CP0.n.Status.val, cpuRegs.CP0.n.BadVAddr, cpuRegs.CP0.n.EntryHi,
		cpuRegs.GPR.r[2].UL[0], cpuRegs.GPR.r[3].UL[0], cpuRegs.GPR.r[4].UL[0],
		cpuRegs.GPR.r[5].UL[0], cpuRegs.GPR.r[6].UL[0], cpuRegs.GPR.r[7].UL[0],
		cpuRegs.GPR.r[31].UL[0], cpuRegs.GPR.r[29].UL[0], static_cast<u32>(cpuRegs.branch), cpuRegs.IsDelaySlot);
}

void PSBBNTraceLateException(u32 code, u32 bd, bool after)
{
	if (!s_psbbn_late.armed || !PSBBNLateTraceEnabled())
		return;
	u32& emitted = after ? s_psbbn_late.exception_after : s_psbbn_late.exception_before;
	if (emitted >= 64)
		return;
	PSBBNWriteLateState(after ? "exception-after" : "exception-before", ++emitted, code | (bd ? 0x80000000u : 0));
}

void PSBBNTraceLateHeartbeat()
{
	if (!s_psbbn_late.armed || s_psbbn_late.heartbeats >= 64 || !PSBBNLateTraceEnabled() ||
		cpuRegs.cycle < s_psbbn_late.next_heartbeat)
		return;
	// One sample per nominal EE second; this is guest-cycle time, not wall time.
	s_psbbn_late.next_heartbeat = cpuRegs.cycle + 294912000ULL;
	PSBBNWriteLateState("heartbeat", ++s_psbbn_late.heartbeats, 0);
}

// Updates the CPU's mode of operation (either, Kernel, Supervisor, or User modes).
// Currently the different modes are not implemented.
// Given this function is called so much, it's commented out for now. (rama)
__ri void cpuUpdateOperationMode()
{

	//u32 value = cpuRegs.CP0.n.Status.val;

	//if (value & 0x06 ||
	//	(value & 0x18) == 0) { // Kernel Mode (KSU = 0 | EXL = 1 | ERL = 1)*/
	//	memSetKernelMode();	// Kernel memory always
	//} else { // User Mode
	//	memSetUserMode();
	//}
}

void WriteCP0Status(u32 value)
{
	COP0_UpdatePCCR();
	cpuRegs.CP0.n.Status.val = value;
	cpuSetNextEventDelta(4);
}

void WriteCP0Config(u32 value)
{
	// Protect the read-only ICacheSize (IC) and DataCacheSize (DC) bits
	cpuRegs.CP0.n.Config = value & ~0xFC0;
	cpuRegs.CP0.n.Config |= 0x440;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Performance Counters Update Stuff!
//
// Note regarding updates of PERF and TIMR registers: never allow increment to be 0.
// That happens when a game loads the MFC0 twice in the same recompiled block (before the
// cpuRegs.cycles update), and can cause games to lock up since it's an unexpected result.
//
// PERF Overflow exceptions:  The exception is raised when the MSB of the Performance
// Counter Register is set.  I'm assuming the exception continues to re-raise until the
// app clears the bit manually (needs testing).
//
// PERF Events:
//  * Event 0 on PCR 0 is unused (counter disable)
//  * Event 16 is usable as a specific counter disable bit (since CTE affects both counters)
//  * Events 17-31 are reserved (act as counter disable)
//
// Most event mode aren't supported, and issue a warning and do a standard instruction
// count.  But only mode 1 (instruction counter) has been found to be used by games thus far.
//

static __fi bool PERF_ShouldCountEvent(uint evt)
{
	switch (evt)
	{
			// This is a rough table of actions for various PCR modes.  Some of these
			// can be implemented more accurately later.  Others (WBBs in particular)
			// probably cannot without some severe complications.

			// left sides are PCR0 / right sides are PCR1

		case 1: // cpu cycle counter.
		case 2: // single/dual instruction issued
		case 3: // Branch issued / Branch mispredicated
			return true;

		case 4: // BTAC/TLB miss
		case 5: // ITLB/DTLB miss
		case 6: // Data/Instruction cache miss
			return false;

		case 7: // Access to DTLB / WBB single request fail
		case 8: // Non-blocking load / WBB burst request fail
		case 9:
		case 10:
			return false;

		case 11: // CPU address bus busy / CPU data bus busy
			return false;

		case 12: // Instruction completed
		case 13: // non-delayslot instruction completed
		case 14: // COP2/COP1 instruction complete
		case 15: // Load/Store completed
			return true;
	}

	return false;
}

// Diagnostics for event modes that we just ignore for now.  Using these perf units could
// cause compat issues in some very odd/rare games, so if this msg comes up who knows,
// might save some debugging effort. :)
void COP0_DiagnosticPCCR()
{
	if (cpuRegs.PERF.n.pccr.b.Event0 >= 7 && cpuRegs.PERF.n.pccr.b.Event0 <= 10)
		Console.Warning("PERF/PCR0 Unsupported Update Event Mode = 0x%x", cpuRegs.PERF.n.pccr.b.Event0);

	if (cpuRegs.PERF.n.pccr.b.Event1 >= 7 && cpuRegs.PERF.n.pccr.b.Event1 <= 10)
		Console.Warning("PERF/PCR1 Unsupported Update Event Mode = 0x%x", cpuRegs.PERF.n.pccr.b.Event1);
}
extern int branch;
__fi void COP0_UpdatePCCR()
{
	// Counting and counter exceptions are not performed if we are currently executing a Level 2 exception (ERL)
	// or the counting function is not enabled (CTE)
	if (cpuRegs.CP0.n.Status.b.ERL || !cpuRegs.PERF.n.pccr.b.CTE)
	{
		cpuRegs.lastPERFCycle[0] = cpuRegs.cycle;
		cpuRegs.lastPERFCycle[1] = cpuRegs.lastPERFCycle[0];
		return;
	}

	// Implemented memory mode check (kernel/super/user)

	if (cpuRegs.PERF.n.pccr.val & ((1 << (cpuRegs.CP0.n.Status.b.KSU + 2)) | (cpuRegs.CP0.n.Status.b.EXL << 1)))
	{
		// ----------------------------------
		//    Update Performance Counter 0
		// ----------------------------------

		if (PERF_ShouldCountEvent(cpuRegs.PERF.n.pccr.b.Event0))
		{
			u32 incr = cpuRegs.cycle - cpuRegs.lastPERFCycle[0];
			if (incr == 0)
				incr++;

			// use prev/XOR method for one-time exceptions (but likely less correct)
			//u32 prev = cpuRegs.PERF.n.pcr0;
			cpuRegs.PERF.n.pcr0 += incr;
			//DevCon.Warning("PCR VAL %x", cpuRegs.PERF.n.pccr.val);
			//prev ^= (1UL<<31);		// XOR is fun!
			//if( (prev & cpuRegs.PERF.n.pcr0) & (1UL<<31) )
			if ((cpuRegs.PERF.n.pcr0 & 0x80000000))
			{
				// TODO: Vector to the appropriate exception here.
				// This code *should* be correct, but is untested (and other parts of the emu are
				// not prepared to handle proper Level 2 exception vectors yet)

				//branch == 1 is probably not the best way to check for the delay slot, but it beats nothing! (Refraction)
				/*	if( branch == 1 )
				{
					cpuRegs.CP0.n.ErrorEPC = cpuRegs.pc - 4;
					cpuRegs.CP0.n.Cause |= 0x40000000;
				}
				else
				{
					cpuRegs.CP0.n.ErrorEPC = cpuRegs.pc;
					cpuRegs.CP0.n.Cause &= ~0x40000000;
				}

				if( cpuRegs.CP0.n.Status.b.DEV )
				{
					// Bootstrap vector
					cpuRegs.pc = 0xbfc00280;
				}
				else
				{
					cpuRegs.pc = 0x80000080;
				}
				cpuRegs.CP0.n.Status.b.ERL = 1;
				cpuRegs.CP0.n.Cause |= 0x20000;*/
			}
		}
	}

	if (cpuRegs.PERF.n.pccr.val & ((1 << (cpuRegs.CP0.n.Status.b.KSU + 12)) | (cpuRegs.CP0.n.Status.b.EXL << 11)))
	{
		// ----------------------------------
		//    Update Performance Counter 1
		// ----------------------------------

		if (PERF_ShouldCountEvent(cpuRegs.PERF.n.pccr.b.Event1))
		{
			u32 incr = cpuRegs.cycle - cpuRegs.lastPERFCycle[1];
			if (incr == 0)
				incr++;

			cpuRegs.PERF.n.pcr1 += incr;

			if ((cpuRegs.PERF.n.pcr1 & 0x80000000))
			{
				// TODO: Vector to the appropriate exception here.
				// This code *should* be correct, but is untested (and other parts of the emu are
				// not prepared to handle proper Level 2 exception vectors yet)

				//branch == 1 is probably not the best way to check for the delay slot, but it beats nothing! (Refraction)

				/*if( branch == 1 )
				{
					cpuRegs.CP0.n.ErrorEPC = cpuRegs.pc - 4;
					cpuRegs.CP0.n.Cause |= 0x40000000;
				}
				else
				{
					cpuRegs.CP0.n.ErrorEPC = cpuRegs.pc;
					cpuRegs.CP0.n.Cause &= ~0x40000000;
				}

				if( cpuRegs.CP0.n.Status.b.DEV )
				{
					// Bootstrap vector
					cpuRegs.pc = 0xbfc00280;
				}
				else
				{
					cpuRegs.pc = 0x80000080;
				}
				cpuRegs.CP0.n.Status.b.ERL = 1;
				cpuRegs.CP0.n.Cause |= 0x20000;*/
			}
		}
	}
	cpuRegs.lastPERFCycle[0] = cpuRegs.cycle;
	cpuRegs.lastPERFCycle[1] = cpuRegs.cycle;
}

//////////////////////////////////////////////////////////////////////////////////////////
//


// The vTLB is a flat, VPN-indexed host cache. The architectural TLB retains
// all entries, but only global or active-ASID entries may occupy this cache.
namespace
{
EEActiveTLBPageMap s_active_tlb_pages;

// Bounded host-side diagnostics selected by existing EE COP0 trace.
struct EETLBChurnStats
{
	u64 syncs = 0, tlbWrites = 0, asidChanges = 0;
	u64 removed = 0, installed = 0, unchanged = 0;
	u64 physicalClearRequests = 0, scratchClearRequests = 0;
	u64 syncMicroseconds = 0;
};
EETLBChurnStats s_ee_tlb_churn;
}

void EEReportTLBChurn(const char* reason)
{
	if (!PSBBNTlbTraceEnabled())
		return;
	const auto& s = s_ee_tlb_churn;
	Console.WriteLn("EE-MMU-CHURN reason=%s syncs=%llu tlbWrites=%llu asidSwitches=%llu removed=%llu installed=%llu unchanged=%llu physicalClearRequests=%llu scratchClearRequests=%llu syncUs=%llu",
		reason, static_cast<unsigned long long>(s.syncs),
		static_cast<unsigned long long>(s.tlbWrites),
		static_cast<unsigned long long>(s.asidChanges),
		static_cast<unsigned long long>(s.removed),
		static_cast<unsigned long long>(s.installed),
		static_cast<unsigned long long>(s.unchanged),
		static_cast<unsigned long long>(s.physicalClearRequests),
		static_cast<unsigned long long>(s.scratchClearRequests),
		static_cast<unsigned long long>(s.syncMicroseconds));
}

#if defined(_M_X64) || defined(__x86_64__)
// Implemented by the x64 EE recompiler. Its code dispatch needs the same
// 4 KiB guest virtual->physical aliases as vTLB data accesses.
void recMapTLBPage(u32 virtualPage, u32 physicalPage);
void recUnmapTLBPage(u32 virtualPage);
void recClearPhysical(u32 addr, u32 size);
#endif

static void ClearActiveTLBCodePage(u32 page, const EEActiveTLBPage& mapping)
{
#if defined(_M_X64) || defined(__x86_64__)
	if (!mapping.scratchpad)
	{
		// Use the projection's explicit PFN, regardless of which dispatch
		// mode is selected or whether the virtual alias is installed yet.
		if (PSBBNTlbTraceEnabled())
			++s_ee_tlb_churn.physicalClearRequests;
		recClearPhysical(mapping.physicalAddress, 0x400);
		return;
	}
#endif
	if (Cpu)
	{
		if (PSBBNTlbTraceEnabled())
			++s_ee_tlb_churn.scratchClearRequests;
		Cpu->Clear(page << 12, 0x400);
	}
}

void SyncActiveTLBMappings()
{
	const bool profiling = PSBBNTlbTraceEnabled();
	std::chrono::steady_clock::time_point started;
	if (profiling)
		started = std::chrono::steady_clock::now();
	u32 removed = 0, installed = 0, unchanged = 0;
	EEActiveTLBPageMap desired =
		BuildEEActiveTLBPageMap(tlb, std::size(tlb), static_cast<u8>(cpuRegs.CP0.n.EntryHi));

	// Remove obsolete mappings first. Every removal also invalidates generated
	// guest code and fastmem aliases through the existing PCSX2 machinery.
	for (const auto& [page, old] : s_active_tlb_pages)
	{
		const auto match = desired.find(page);
		if (match == desired.end() || match->second.physicalAddress != old.physicalAddress ||
			match->second.scratchpad != old.scratchpad ||
			match->second.writable != old.writable)
		{
			++removed;
			memClearPageAddr(page << 12);
			ClearActiveTLBCodePage(page, old);
#if defined(_M_X64) || defined(__x86_64__)
			recUnmapTLBPage(page << 12);
#endif
		}
	}
	std::unordered_set<u32> scratchBasesToUpdate;
	for (const auto& [page, now] : desired)
	{
		const auto old = s_active_tlb_pages.find(page);
		if (old != s_active_tlb_pages.end() && old->second.physicalAddress == now.physicalAddress &&
			old->second.scratchpad == now.scratchpad &&
			old->second.writable == now.writable)
		{
			++unchanged;
			continue;
		}
		++installed;
		if (now.scratchpad)
		{
			// Preserve the existing 16KB scratchpad fastmem path; mapping it
			// one 4KB page at a time would unnecessarily force JIT slowmem.
			scratchBasesToUpdate.insert(page - (now.physicalAddress >> 12));
			continue;
		}
		memSetPageAddr(page << 12, now.physicalAddress);
		ClearActiveTLBCodePage(page, now);
#if defined(_M_X64) || defined(__x86_64__)
		recMapTLBPage(page << 12, now.physicalAddress);
#endif
	}
	for (const u32 basePage : scratchBasesToUpdate)
	{
		bool contiguous = true;
		for (u32 i = 0; i < Ps2MemSize::Scratch / 0x1000; i++)
		{
			const auto it = desired.find(basePage + i);
			if (it == desired.end() || !it->second.scratchpad ||
				it->second.physicalAddress != (i << 12))
			{
				contiguous = false;
				break;
			}
		}
		if (contiguous)
		{
			vtlb_VMapBuffer(basePage << 12, eeMem->Scratch, Ps2MemSize::Scratch);
			for (u32 i = 0; i < Ps2MemSize::Scratch / 0x1000; i++)
				Cpu->Clear((basePage + i) << 12, 0x400);
		}
		else
		{
			// Multiple matching TLB entries are architecturally undefined.
			// Preserve the selected per-page view without assuming a contiguous
			// scratchpad fastmem mapping in that exceptional case.
			for (const auto& [page, now] : desired)
			{
				if (now.scratchpad && page - (now.physicalAddress >> 12) == basePage)
				{
					vtlb_VMapBuffer(page << 12, eeMem->Scratch + now.physicalAddress, 0x1000);
					Cpu->Clear(page << 12, 0x400);
				}
			}
		}
	}
	s_active_tlb_pages.swap(desired);
	if (profiling)
	{
		auto& s = s_ee_tlb_churn;
		++s.syncs;
		s.removed += removed;
		s.installed += installed;
		s.unchanged += unchanged;
		s.syncMicroseconds += static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - started).count());
		if ((s.syncs & 0xfffu) == 0)
			EEReportTLBChurn("4096-syncs");
	}
}

// All active translations remain readable through the existing vTLB mapping;
// a matched valid EntryLo with D=0 must fault before either CPU writes RAM.
// Absence from this map is not a TLB Modification: existing vTLB miss
// handling determines whether the address has no entry or an invalid entry.
// The shared predicate exempts fixed kernel aliases even with overlapping entries.
bool EETLBStoreNeedsModification(u32 address)
{
	return EEActiveTLBStoreNeedsModification(s_active_tlb_pages, address);
}

bool EEInterpreterTLBStoreNeedsModification(u32 address)
{
	if (Cpu != &intCpu)
		return false;
	return EETLBStoreNeedsModification(address);
}

void ResetActiveTLBMappings()
{
	for (const auto& [page, mapping] : s_active_tlb_pages)
	{
		memClearPageAddr(page << 12);
		ClearActiveTLBCodePage(page, mapping);
#if defined(_M_X64) || defined(__x86_64__)
		recUnmapTLBPage(page << 12);
#endif
	}
	s_active_tlb_pages.clear();
}

void WriteCP0EntryHi(u32 value)
{
	const u8 previousAsid = static_cast<u8>(cpuRegs.CP0.n.EntryHi);
	cpuRegs.CP0.n.EntryHi = value;
	if (static_cast<u8>(value) != previousAsid)
	{
		if (PSBBNTlbTraceEnabled())
			++s_ee_tlb_churn.asidChanges;
		SyncActiveTLBMappings();
	}
}

void MapTLB(const tlbs&, int)
{
	if (PSBBNTlbTraceEnabled())
		++s_ee_tlb_churn.tlbWrites;
	// The caller has already installed tlb[i]. Update only the physical pages
	// visible under the selected ASID instead of flattening *every* TLB entry.
	SyncActiveTLBMappings();
}

__inline u32 ConvertPageMask(const u32 PageMask)
{
	const u32 mask = std::popcount(PageMask >> 13);

	pxAssertMsg(!((mask & 1) || mask > 12), "Invalid page mask for this TLB entry. EE cache doesn't know what to do here.");

	return (1 << (12 + mask)) - 1;
}

void UnmapTLB(const tlbs& t, int)
{
	// TLBWI/TLBWR remove the old cache-mode descriptor here. Live vTLB page
	// mappings are reconciled once the replacement TLB entry is installed by
	// MapTLB(), so replacing an inactive ASID cannot erase an active mapping.
	if (t.isSPR())
		return;

	for (size_t i = 0; i < cachedTlbs.count; i++)
	{
		if (cachedTlbs.PFN0s[i] == t.PFN0() && cachedTlbs.PFN1s[i] == t.PFN1() && cachedTlbs.PageMasks[i] == ConvertPageMask(t.PageMask.UL))
		{
			for (size_t j = i; j < cachedTlbs.count - 1; j++)
			{
				cachedTlbs.CacheEnabled0[j] = cachedTlbs.CacheEnabled0[j + 1];
				cachedTlbs.CacheEnabled1[j] = cachedTlbs.CacheEnabled1[j + 1];
				cachedTlbs.PFN0s[j] = cachedTlbs.PFN0s[j + 1];
				cachedTlbs.PFN1s[j] = cachedTlbs.PFN1s[j + 1];
				cachedTlbs.PageMasks[j] = cachedTlbs.PageMasks[j + 1];
			}
			cachedTlbs.count--;
			break;
		}
	}
}

void WriteTLB(int i)
{
	tlb[i].PageMask.UL = cpuRegs.CP0.n.PageMask;
	tlb[i].EntryHi.UL = cpuRegs.CP0.n.EntryHi;
	tlb[i].EntryLo0.UL = cpuRegs.CP0.n.EntryLo0;
	tlb[i].EntryLo1.UL = cpuRegs.CP0.n.EntryLo1;

	// Setting the cache mode to reserved values is vaguely defined in the manual.
	// I found that SPR is set to cached regardless.
	// Non-SPR entries default to uncached on reserved cache modes.
	if (tlb[i].isSPR())
	{
		tlb[i].EntryLo0.C = 3;
		tlb[i].EntryLo1.C = 3;
	}
	else
	{
		if (!tlb[i].EntryLo0.isValidCacheMode())
			tlb[i].EntryLo0.C = 2;
		if (!tlb[i].EntryLo1.isValidCacheMode())
			tlb[i].EntryLo1.C = 2;
	}

	if (!tlb[i].isSPR() && ((tlb[i].EntryLo0.V && tlb[i].EntryLo0.isCached()) || (tlb[i].EntryLo1.V && tlb[i].EntryLo1.isCached())))
	{
		const size_t idx = cachedTlbs.count;
		cachedTlbs.CacheEnabled0[idx] = tlb[i].EntryLo0.isCached() ? ~0 : 0;
		cachedTlbs.CacheEnabled1[idx] = tlb[i].EntryLo1.isCached() ? ~0 : 0;
		cachedTlbs.PFN1s[idx] = tlb[i].PFN1();
		cachedTlbs.PFN0s[idx] = tlb[i].PFN0();
		cachedTlbs.PageMasks[idx] = ConvertPageMask(tlb[i].PageMask.UL);

		cachedTlbs.count++;
	}

	MapTLB(tlb[i], i);
}

namespace R5900 {
namespace Interpreter {
namespace OpcodeImpl {
namespace COP0 {

// The EE Random register cycles through the non-wired TLB entries.
// PCSX2 previously left it fixed at 47 after reset/MTC0 Wired, causing
// TLBWR to continually evict one entry. Use the emulated EE cycle counter
// so both interpreter and JIT select changing entries without a per-opcode
// host cost or unsaved host-side phase state.
//
// The exact hardware decrement phase is still approximate because JIT cycle
// accounting is block-based. The range and read-only behavior are preserved.
static void UpdateTLBRandom()
{
	// The EE interpreter already decrements Random once per guest instruction
	// in Interpreter.cpp::COP0_AdvanceRandom(). Do not replace that more
	// accurate register state with the JIT's cycle-derived approximation.
	if (Cpu == &intCpu)
		return;
	const u32 wired = std::min(cpuRegs.CP0.n.Wired & 0x3fu, 47u);
	const u32 available = 48u - wired;
	cpuRegs.CP0.n.Random = 47u - static_cast<u32>(cpuRegs.cycle % available);
}

	void TLBR()
	{
		if (!PSBBNTlbTraceEnabled())
		COP0_LOG("COP0_TLBR %d:%x,%x,%x,%x",
			cpuRegs.CP0.n.Index, cpuRegs.CP0.n.PageMask, cpuRegs.CP0.n.EntryHi,
			cpuRegs.CP0.n.EntryLo0, cpuRegs.CP0.n.EntryLo1);

		const u8 i = cpuRegs.CP0.n.Index & 0x3f;

		if (i > 47)
		{
			Console.Warning("TLBR with index > 47! (%d)", i);
			return;
		}

		cpuRegs.CP0.n.PageMask = tlb[i].PageMask.Mask << 13;
		WriteCP0EntryHi(tlb[i].EntryHi.UL & ~((tlb[i].PageMask.Mask << 13) | 0x1f00));
		cpuRegs.CP0.n.EntryLo0 = tlb[i].EntryLo0.UL & ~(0xFC000000) & ~1;
		cpuRegs.CP0.n.EntryLo1 = tlb[i].EntryLo1.UL & ~(0x7C000000) & ~1;
		// "If both the Global bit of EntryLo0 and EntryLo1 are set to 1, the processor ignores the ASID during TLB lookup."
		// This is reflected during TLBR, where G is only set if both EntryLo0 and EntryLo1 are global.
		cpuRegs.CP0.n.EntryLo0 |= (tlb[i].EntryLo0.UL & 1) & (tlb[i].EntryLo1.UL & 1);
		cpuRegs.CP0.n.EntryLo1 |= (tlb[i].EntryLo0.UL & 1) & (tlb[i].EntryLo1.UL & 1);
	}

	void TLBWI()
	{
		const u8 j = cpuRegs.CP0.n.Index & 0x3f;
		PSBBNTraceTlb(PSBBNTlbEvent::TlbwiBefore, cpuRegs.CP0.n.EntryHi, 0, j, true);

		if (j > 47)
		{
			Console.Warning("TLBWI with index > 47! (%d)", j);
			return;
		}

		if (!PSBBNTlbTraceEnabled())
		COP0_LOG("COP0_TLBWI %d:%x,%x,%x,%x",
			cpuRegs.CP0.n.Index, cpuRegs.CP0.n.PageMask, cpuRegs.CP0.n.EntryHi,
			cpuRegs.CP0.n.EntryLo0, cpuRegs.CP0.n.EntryLo1);

		UnmapTLB(tlb[j], j);
		WriteTLB(j);
		PSBBNTraceTlb(PSBBNTlbEvent::TlbwiAfter, cpuRegs.CP0.n.EntryHi, 0, j, true);
	}

	void TLBWR()
	{
		UpdateTLBRandom();
		const u8 j = cpuRegs.CP0.n.Random & 0x3f;
		PSBBNTraceTlb(PSBBNTlbEvent::TlbwrBefore, cpuRegs.CP0.n.EntryHi, 0, j, true);

		if (j > 47)
		{
			Console.Warning("TLBWR with random > 47! (%d)", j);
			return;
		}

		if (!PSBBNTlbTraceEnabled())
		DevCon.Warning("COP0_TLBWR %d:%x,%x,%x,%x\n",
			cpuRegs.CP0.n.Random, cpuRegs.CP0.n.PageMask, cpuRegs.CP0.n.EntryHi,
			cpuRegs.CP0.n.EntryLo0, cpuRegs.CP0.n.EntryLo1);

		UnmapTLB(tlb[j], j);
		WriteTLB(j);
		PSBBNTraceTlb(PSBBNTlbEvent::TlbwrAfter, cpuRegs.CP0.n.EntryHi, 0, j, true);
	}

	void TLBP()
	{
		PSBBNTraceTlb(PSBBNTlbEvent::TlbpBefore, cpuRegs.CP0.n.EntryHi, 0, -1, true);
		int i;

		EntryHi_t entry_hi;
		entry_hi.UL = cpuRegs.CP0.n.EntryHi;

		cpuRegs.CP0.n.Index = 0xFFFFFFFF;
		for (i = 0; i < 48; i++)
		{
			if (tlb[i].VPN2() == ((entry_hi.VPN2 & ~tlb[i].Mask()) << 13) && (tlb[i].isGlobal() || tlb[i].EntryHi.ASID == entry_hi.ASID))
			{
				cpuRegs.CP0.n.Index = i;
				break;
			}
		}
		if (cpuRegs.CP0.n.Index == 0xFFFFFFFF)
			cpuRegs.CP0.n.Index = 0x80000000;
		PSBBNTraceTlb(PSBBNTlbEvent::TlbpAfter, cpuRegs.CP0.n.EntryHi, 0,
			cpuRegs.CP0.n.Index & 0x80000000 ? -1 : static_cast<int>(cpuRegs.CP0.n.Index & 0x3f), true);
	}

	void MFC0()
	{
		// Note on _Rd_ Condition 9: CP0.Count should be updated even if _Rt_ is 0.
		if ((_Rd_ != 9) && !_Rt_)
			return;

		//if(bExecBIOS == FALSE && _Rd_ == 25) Console.WriteLn("MFC0 _Rd_ %x = %x", _Rd_, cpuRegs.CP0.r[_Rd_]);
		switch (_Rd_)
		{
			case 1:
				UpdateTLBRandom();
				cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.CP0.n.Random;
				break;

			case 12:
				cpuRegs.GPR.r[_Rt_].SD[0] = (s32)(cpuRegs.CP0.r[_Rd_] & 0xf0c79c1f);
				break;

			case 25:
				if (0 == (_Imm_ & 1)) // MFPS, register value ignored
				{
					cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.PERF.n.pccr.val;
				}
				else if (0 == (_Imm_ & 2)) // MFPC 0, only LSB of register matters
				{
					COP0_UpdatePCCR();
					cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.PERF.n.pcr0;
				}
				else // MFPC 1
				{
					COP0_UpdatePCCR();
					cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.PERF.n.pcr1;
				}
				/*Console.WriteLn("MFC0 PCCR = %x PCR0 = %x PCR1 = %x IMM= %x",  params
cpuRegs.PERF.n.pccr, cpuRegs.PERF.n.pcr0, cpuRegs.PERF.n.pcr1, _Imm_ & 0x3F);*/
				break;

			case 24:
				COP0_LOG("MFC0 Breakpoint debug Registers code = %x", cpuRegs.code & 0x3FF);
				break;

			case 9:
			{
				s64 incr = cpuRegs.cycle - cpuRegs.lastCOP0Cycle;
				if (incr == 0)
					incr++;
				cpuRegs.CP0.n.Count += incr;
				cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
				if (!_Rt_)
					break;
			}
				[[fallthrough]];

			default:
				cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.CP0.r[_Rd_];
		}
	}

	void MTC0()
	{
		//if(bExecBIOS == FALSE && _Rd_ == 25) Console.WriteLn("MTC0 _Rd_ %x = %x", _Rd_, cpuRegs.CP0.r[_Rd_]);
		switch (_Rd_)
		{
			case 1:
				// Random is read-only.
				break;

			case 6:
				cpuRegs.CP0.n.Wired = cpuRegs.GPR.r[_Rt_].UL[0] & 0x3f;
				cpuRegs.CP0.n.Random = 47;
				break;

			case 10:
				WriteCP0EntryHi(cpuRegs.GPR.r[_Rt_].UL[0]);
				break;

			case 9:
				cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
				cpuRegs.CP0.r[9] = cpuRegs.GPR.r[_Rt_].UL[0];
				break;

			case 12:
				WriteCP0Status(cpuRegs.GPR.r[_Rt_].UL[0]);
				break;

			case 16:
				WriteCP0Config(cpuRegs.GPR.r[_Rt_].UL[0]);
				break;

			case 24:
				COP0_LOG("MTC0 Breakpoint debug Registers code = %x", cpuRegs.code & 0x3FF);
				break;

			case 25:
				/*if(bExecBIOS == FALSE && _Rd_ == 25) Console.WriteLn("MTC0 PCCR = %x PCR0 = %x PCR1 = %x IMM= %x", params
	cpuRegs.PERF.n.pccr, cpuRegs.PERF.n.pcr0, cpuRegs.PERF.n.pcr1, _Imm_ & 0x3F);*/
				if (0 == (_Imm_ & 1)) // MTPS
				{
					if (0 != (_Imm_ & 0x3E)) // only effective when the register is 0
						break;
					// Updates PCRs and sets the PCCR.
					COP0_UpdatePCCR();
					cpuRegs.PERF.n.pccr.val = cpuRegs.GPR.r[_Rt_].UL[0];
					COP0_DiagnosticPCCR();
				}
				else if (0 == (_Imm_ & 2)) // MTPC 0, only LSB of register matters
				{
					cpuRegs.PERF.n.pcr0 = cpuRegs.GPR.r[_Rt_].UL[0];
					cpuRegs.lastPERFCycle[0] = cpuRegs.cycle;
				}
				else // MTPC 1
				{
					cpuRegs.PERF.n.pcr1 = cpuRegs.GPR.r[_Rt_].UL[0];
					cpuRegs.lastPERFCycle[1] = cpuRegs.cycle;
				}
				break;

			default:
				cpuRegs.CP0.r[_Rd_] = cpuRegs.GPR.r[_Rt_].UL[0];
				break;
		}
	}

	int CPCOND0()
	{
		return (((dmacRegs.stat.CIS | ~dmacRegs.pcr.CPC) & 0x3FF) == 0x3ff);
	}

	//#define CPCOND0	1

	void BC0F()
	{
		if (CPCOND0() == 0)
			intDoBranch(_BranchTarget_);
	}

	void BC0T()
	{
		if (CPCOND0() == 1)
			intDoBranch(_BranchTarget_);
	}

	void BC0FL()
	{
		if (CPCOND0() == 0)
			intDoBranch(_BranchTarget_);
		else
			cpuRegs.pc += 4;
	}

	void BC0TL()
	{
		if (CPCOND0() == 1)
			intDoBranch(_BranchTarget_);
		else
			cpuRegs.pc += 4;
	}

	void ERET()
	{
		const u32 trace_cause = cpuRegs.CP0.n.Cause & 0x7c;
		const bool trace_return = trace_cause == 4 || trace_cause == 8 || trace_cause == 12;
		if (trace_return)
			PSBBNTraceTlb(PSBBNTlbEvent::EretBefore, cpuRegs.CP0.n.BadVAddr, trace_cause, -1, true);
#ifdef ENABLE_VTUNE
		// Allow to stop vtune in a predictable way to compare runs
		// Of course, the limit will depend on the game.
		const u32 million = 1000 * 1000;
		static u32 vtune = 0;
		vtune++;

		// quick_exit vs exit: quick_exit won't call static storage destructor (OS will manage). It helps
		// avoiding the race condition between threads destruction.
		if (vtune > 30 * million)
		{
			Console.WriteLn("VTUNE: quick_exit");
			std::quick_exit(EXIT_SUCCESS);
		}
		else if (!(vtune % million))
		{
			Console.WriteLn("VTUNE: ERET was called %uM times", vtune / million);
		}

#endif

		if (cpuRegs.CP0.n.Status.b.ERL)
		{
			cpuRegs.pc = cpuRegs.CP0.n.ErrorEPC;
			cpuRegs.CP0.n.Status.b.ERL = 0;
		}
		else
		{
			cpuRegs.pc = cpuRegs.CP0.n.EPC;
			cpuRegs.CP0.n.Status.b.EXL = 0;
		}
		cpuUpdateOperationMode();
		cpuSetNextEventDelta(4);
		intSetBranch();
		if (trace_return)
			PSBBNTraceTlb(PSBBNTlbEvent::EretAfter, cpuRegs.CP0.n.BadVAddr, trace_cause);
	}

	void DI()
	{
		if (cpuRegs.CP0.n.Status.b._EDI || cpuRegs.CP0.n.Status.b.EXL ||
			cpuRegs.CP0.n.Status.b.ERL || (cpuRegs.CP0.n.Status.b.KSU == 0))
		{
			cpuRegs.CP0.n.Status.b.EIE = 0;
			// IRQs are disabled so no need to do a cpu exception/event test...
			//cpuSetNextEventDelta();
		}
	}

	void EI()
	{
		if (cpuRegs.CP0.n.Status.b._EDI || cpuRegs.CP0.n.Status.b.EXL ||
			cpuRegs.CP0.n.Status.b.ERL || (cpuRegs.CP0.n.Status.b.KSU == 0))
		{
			cpuRegs.CP0.n.Status.b.EIE = 1;
			// schedule an event test, which will check for and raise pending IRQs.
			cpuSetNextEventDelta(4);
		}
	}

} // namespace COP0
} // namespace OpcodeImpl
} // namespace Interpreter
} // namespace R5900
