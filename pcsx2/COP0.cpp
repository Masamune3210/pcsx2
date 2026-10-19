// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "COP0.h"
#include "EEActiveTLB.h"
#include "vtlb.h"

#include <array>
#include <unordered_set>

// Diagnostic observer only. Existing EE COP0 trace settings are the opt-in.
// No guest memory reads: instruction_sample can be stale on fetch faults and JIT misses.
namespace
{
struct PSBBNTraceState
{
	u64 ihin_seen = 0;
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
}

void PSBBNResetTlbTrace()
{
	s_psbbn_trace = {};
}

bool PSBBNTlbTraceEnabled()
{
	return EmuConfig.Trace.Enabled && EmuConfig.Trace.EE.cop0;
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
}

void SyncActiveTLBMappings()
{
	EEActiveTLBPageMap desired =
		BuildEEActiveTLBPageMap(tlb, std::size(tlb), static_cast<u8>(cpuRegs.CP0.n.EntryHi));

	// Remove obsolete mappings first. Every removal also invalidates generated
	// guest code and fastmem aliases through the existing PCSX2 machinery.
	for (const auto& [page, old] : s_active_tlb_pages)
	{
		const auto match = desired.find(page);
		if (match == desired.end() || match->second.physicalAddress != old.physicalAddress ||
			match->second.scratchpad != old.scratchpad)
		{
			memClearPageAddr(page << 12);
			Cpu->Clear(page << 12, 0x400);
		}
	}
	std::unordered_set<u32> scratchBasesToUpdate;
	for (const auto& [page, now] : desired)
	{
		const auto old = s_active_tlb_pages.find(page);
		if (old != s_active_tlb_pages.end() && old->second.physicalAddress == now.physicalAddress &&
			old->second.scratchpad == now.scratchpad)
			continue;
		if (now.scratchpad)
		{
			// Preserve the existing 16KB scratchpad fastmem path; mapping it
			// one 4KB page at a time would unnecessarily force JIT slowmem.
			scratchBasesToUpdate.insert(page - (now.physicalAddress >> 12));
			continue;
		}
		memSetPageAddr(page << 12, now.physicalAddress);
		Cpu->Clear(page << 12, 0x400);
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
}

void ResetActiveTLBMappings()
{
	for (const auto& [page, mapping] : s_active_tlb_pages)
	{
		memClearPageAddr(page << 12);
		Cpu->Clear(page << 12, 0x400);
	}
	s_active_tlb_pages.clear();
}

void WriteCP0EntryHi(u32 value)
{
	const u8 previousAsid = static_cast<u8>(cpuRegs.CP0.n.EntryHi);
	cpuRegs.CP0.n.EntryHi = value;
	if (static_cast<u8>(value) != previousAsid)
		SyncActiveTLBMappings();
}

void MapTLB(const tlbs&, int)
{
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

		union
		{
			struct
			{
				u32 VPN2 : 19;
				u32 VPN2X : 2;
				u32 G : 3;
				u32 ASID : 8;
			} s;
			u32 u;
		} EntryHi32;

		EntryHi32.u = cpuRegs.CP0.n.EntryHi;

		cpuRegs.CP0.n.Index = 0xFFFFFFFF;
		for (i = 0; i < 48; i++)
		{
			if (tlb[i].VPN2() == ((~tlb[i].Mask()) & (EntryHi32.s.VPN2)) && ((tlb[i].isGlobal()) || ((tlb[i].EntryHi.ASID & 0xff) == EntryHi32.s.ASID)))
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
