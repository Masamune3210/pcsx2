// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "MemoryTypes.h"
#include "Memory.h"
#include "EEPerformanceProbe.h"
#include "x86/EERecCodeCache.h"
#include "x86/EERecSourceWitness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>

namespace
{
class EERecCodeCache : public testing::Test
{
protected:
	static constexpr u32 RAMSize = 0x20000;
	std::unique_ptr<BASEBLOCK[]> ram = std::make_unique<BASEBLOCK[]>(RAMSize / 4);
	std::array<uptr, 0x10000> physicalLUT{};
	BaseBlocks blocks;
	std::array<u8, 16> compilerCode{}, firstCode{}, secondCode{};
	std::array<u8, 16> dispatcherCode{};
	uptr compiler = reinterpret_cast<uptr>(compilerCode.data());

	void SetUp() override
	{
		blocks.SetJITCompile(compilerCode.data());
		for (u32 page = 0; page < RAMSize / 0x10000; page++)
			physicalLUT[page] = reinterpret_cast<uptr>(ram.get());
		for (u32 word = 0; word < RAMSize / 4; word++)
			ram[word].SetFnptr(compiler);
	}

	void AddBlock(u32 address, u32 words, uptr code, u32 hostBytes = 16)
	{
		BASEBLOCKEX* block = blocks.New(address, code);
		block->size = words;
		block->x86size = hostBytes;
		ram[address / 4].SetFnptr(code);
	}

	void Clear(u32 address, u32 words, BASEBLOCK* compiling = nullptr, BaseBlocks::Metrics* metrics = nullptr)
	{
		EERecClearPhysicalBlocks(blocks, physicalLUT.data(), compiling, compiler, address, words, metrics);
	}
};

TEST(EEPerformanceProbe, OptInCountersResetAndRecordOnlyWhenExplicitlyInvoked)
{
	using namespace EEPerformanceProbe;
	Configure(true);
	EXPECT_TRUE(Enabled());
	EXPECT_EQ(Read(compilations.calls), 0u);
	EXPECT_EQ(Read(tlbSyncs.calls), 0u);
	const auto started = Clock::now();
	compiledHostBytes.fetch_add(128, std::memory_order_relaxed);
	Record(compilations, started);
	Record(tlbSyncs, started);
	EXPECT_EQ(Read(compilations.calls), 1u);
	EXPECT_EQ(Read(tlbSyncs.calls), 1u);
	EXPECT_EQ(Read(compiledHostBytes), 128u);
	EXPECT_EQ(Read(physicalClearWork.calls), 0u);
	Configure(false);
	EXPECT_FALSE(Enabled());
	EXPECT_EQ(Read(compilations.calls), 0u);
	EXPECT_EQ(Read(tlbSyncs.calls), 0u);
	EXPECT_EQ(Read(compiledHostBytes), 0u);
}

TEST(EEPerformanceProbe, TLBSyncRouteAccountingIsDisjointAndResets)
{
	using namespace EEPerformanceProbe;
	Configure(true);
	const auto started = Clock::now();
	RecordTLBSync(tlbMemoHits, started, false, 0, 0, 0, 0, 31);
	RecordTLBSync(tlbSmallDeltas, started, true, 3, 1, 2, 0, 0);
	RecordTLBSync(tlbFullRebuilds, started, false, 0, 3, 5, 1, 7);
	EXPECT_EQ(Read(tlbSyncs.calls), 3u);
	EXPECT_EQ(Read(tlbMemoHits.calls), 1u);
	EXPECT_EQ(Read(tlbSmallDeltas.calls), 1u);
	EXPECT_EQ(Read(tlbFullRebuilds.calls), 1u);
	EXPECT_EQ(Read(tlbAsidSyncs), 1u);
	EXPECT_EQ(Read(tlbDeltaCandidatePages), 3u);
	EXPECT_EQ(Read(tlbDeltaPublishedPages), 0u);
	EXPECT_EQ(Read(tlbDeltaNoopPages), 0u);
	EXPECT_EQ(Read(tlbRemovedMappings), 4u);
	EXPECT_EQ(Read(tlbInstalledMappings), 7u);
	EXPECT_EQ(Read(tlbPermissionUpdates), 1u);
	EXPECT_EQ(Read(tlbUnchangedMappings), 38u);
	EXPECT_EQ(Read(tlbMemoHits.nanoseconds) + Read(tlbSmallDeltas.nanoseconds) +
		Read(tlbFullRebuilds.nanoseconds), Read(tlbSyncs.nanoseconds));
	Configure(false);
	EXPECT_EQ(Read(tlbSyncs.calls), 0u);
	EXPECT_EQ(Read(tlbMemoHits.calls), 0u);
	EXPECT_EQ(Read(tlbSmallDeltas.calls), 0u);
	EXPECT_EQ(Read(tlbFullRebuilds.calls), 0u);
	EXPECT_EQ(Read(tlbRemovedMappings), 0u);
	EXPECT_EQ(Read(tlbAsidSyncs), 0u);
	EXPECT_EQ(Read(tlbDeltaPublishedPages), 0u);
	EXPECT_EQ(Read(tlbDeltaNoopPages), 0u);
}

TEST(EERecCodeCacheCapacity, StandardPreservesOriginalLimitAndExtendedIsSelectable)
{
	for (const u32 setting : {64u, 128u, 256u, 512u, 1472u, 0u, 1024u, 2048u})
		EXPECT_EQ(HostMemoryMap::EErecGenerationSize(false, setting), 0x04000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true), 0x10000000u); // default
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 64), 0x04000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 128), 0x08000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 256), 0x10000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 512), 0x20000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 1472), 0x5c000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 0), 0x10000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 1024), 0x10000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 2048), 0x10000000u);
	EXPECT_EQ(HostMemoryMap::EErecGenerationSize(true, 1472), HostMemoryMap::EErecSize);
	EXPECT_EQ(HostMemoryMap::IOPrecOffset,
		HostMemoryMap::EErecOffset + HostMemoryMap::EErecSize);
	EXPECT_LT(HostMemoryMap::EErecGenerationSize(false), HostMemoryMap::EErecSize);
	EXPECT_LE(HostMemoryMap::SWrecOffset + HostMemoryMap::SWrecSize,
		HostMemoryMap::CodeSize);
	EXPECT_LE(static_cast<u64>(HostMemoryMap::MainSize) + HostMemoryMap::CodeSize,
		0x7e000000ull);
	EXPECT_GT(HostMemoryMap::IOPrecOffset, HostMemoryMap::EErecOffset +
		HostMemoryMap::EErecGenerationSize(true, 512));
}

TEST(EERecSourceWitness, SeparateVirtualPagesMayHaveNoncontiguousPhysicalSource)
{
	EERecSourceWitnessSnapshot snapshot;
	snapshot.complete = true;
	ASSERT_TRUE(EERecWitnessAppendWord(snapshot, 0x0fb6dffcu, 0x01ed8ffcu, 0x12345678u, false));
	ASSERT_TRUE(EERecWitnessAppendWord(snapshot, 0x0fb6e000u, 0x00028000u, 0x87654321u, true));
	ASSERT_TRUE(EERecWitnessAppendWord(snapshot, 0x0fb6e004u, 0x00028004u, 0x13572468u, true));
	ASSERT_EQ(snapshot.pageCount, 2u);
	EXPECT_EQ(snapshot.pages[0].guestAddress, 0x0fb6dffcu);
	EXPECT_EQ(snapshot.pages[0].physicalAddress, 0x01ed8ffcu);
	EXPECT_EQ(snapshot.pages[0].words, 1u);
	EXPECT_FALSE(snapshot.pages[0].writable);
	EXPECT_EQ(snapshot.pages[1].guestAddress, 0x0fb6e000u);
	EXPECT_EQ(snapshot.pages[1].physicalAddress, 0x00028000u);
	EXPECT_EQ(snapshot.pages[1].words, 2u);
	EXPECT_TRUE(snapshot.pages[1].writable);
}

TEST(EERecSourceWitness, BoundsSourcePageInventoryWithoutGuessingExtraPFNs)
{
	EERecSourceWitnessSnapshot snapshot;
	for (u32 page = 0; page < EERecSourceWitnessSnapshot::MaxPages; ++page)
		ASSERT_TRUE(EERecWitnessAppendWord(snapshot, 0x0fb6d000u + page * 0x1000u,
			0x01ed8000u + page * 0x3000u, page, true));
	ASSERT_EQ(snapshot.pageCount, EERecSourceWitnessSnapshot::MaxPages);
	EXPECT_FALSE(EERecWitnessAppendWord(snapshot, 0x0fb75000u, 0x01f00000u, 9, true));
	EXPECT_EQ(snapshot.pageCount, EERecSourceWitnessSnapshot::MaxPages);
}

TEST(EERecSourceWitness, RejectsUnexpectedPhysicalStepInsideSameVirtualPage)
{
	EERecSourceWitnessSnapshot snapshot;
	ASSERT_TRUE(EERecWitnessAppendWord(snapshot, 0x0fb6d000u, 0x01ed8000u, 0x1234u, true));
	EXPECT_FALSE(EERecWitnessAppendWord(snapshot, 0x0fb6d004u, 0x01ef0004u, 0x5678u, true));
	EXPECT_FALSE(EERecWitnessAppendWord(snapshot, 0x0fb6d004u, 0x01ed8004u, 0x5678u, false));
	EXPECT_EQ(snapshot.pageCount, 1u);
	EXPECT_EQ(snapshot.pages[0].words, 1u);
}

TEST(EERecSourceWitness, ClassifiesPhysicalByteChangesBeforeMappingStatus)
{
	EXPECT_EQ(EERecClassifySourceWitness(true, true, false, false, false),
		EERecSourceWitnessVerdict::PhysicalBytesChanged);
	EXPECT_EQ(EERecClassifySourceWitness(true, true, true, false, false),
		EERecSourceWitnessVerdict::PhysicalBytesUnchangedVirtualUnmapped);
	EXPECT_EQ(EERecClassifySourceWitness(true, true, true, true, false),
		EERecSourceWitnessVerdict::PhysicalBytesUnchangedMappingChanged);
	EXPECT_EQ(EERecClassifySourceWitness(true, true, true, true, true),
		EERecSourceWitnessVerdict::PhysicalBytesUnchangedMappingSame);
}

TEST(EERecSourceWitness, IncompleteEvidenceNeverClaimsPhysicalBytesUnchanged)
{
	EXPECT_EQ(EERecClassifySourceWitness(false, true, true, true, true),
		EERecSourceWitnessVerdict::Incomplete);
	EXPECT_EQ(EERecClassifySourceWitness(true, false, true, true, true),
		EERecSourceWitnessVerdict::Incomplete);
	EXPECT_STREQ(EERecSourceWitnessVerdictName(
		EERecSourceWitnessVerdict::PhysicalBytesUnchangedMappingSame),
		"same-bytes-mapping-same");
}

TEST_F(EERecCodeCache, EvictionObserverReportsOnlyBlocksActuallyRemoved)
{
	AddBlock(0x3000, 8, reinterpret_cast<uptr>(firstCode.data()), 11);
	AddBlock(0x3010, 4, reinterpret_cast<uptr>(secondCode.data()), 17);
	BASEBLOCKEX* compiling = blocks.New(0x3018, reinterpret_cast<uptr>(dispatcherCode.data()));
	compiling->size = 2;
	AddBlock(0x6000, 4, reinterpret_cast<uptr>(secondCode.data()), 23);
	struct Observed
	{
		std::array<u32, 4> starts{};
		std::array<u32, 4> sizes{};
		u32 count = 0;
	} observed;
	const auto observer = +[](const BASEBLOCKEX& block, void* pointer) {
		auto* out = static_cast<Observed*>(pointer);
		ASSERT_LT(out->count, out->starts.size());
		out->starts[out->count] = block.startpc;
		out->sizes[out->count] = block.x86size;
		++out->count;
	};
	BaseBlocks::Metrics metrics;
	EERecClearPhysicalBlocks(blocks, physicalLUT.data(), &ram[0x3018 / 4],
		compiler, 0x301c, 1, &metrics, observer, &observed);
	EXPECT_EQ(observed.count, 2u);
	EXPECT_EQ(metrics.removedBlocks, 2u);
	EXPECT_EQ(metrics.retiredHostBytes, 28u);
	EXPECT_NE(blocks.Get(0x3018), nullptr); // compiling block never reported
	EXPECT_NE(blocks.Get(0x6000), nullptr); // unrelated block never reported
	std::array<u32, 2> found{observed.starts[0], observed.starts[1]};
	std::sort(found.begin(), found.end());
	EXPECT_EQ(found[0], 0x3000u);
	EXPECT_EQ(found[1], 0x3010u);
	const u32 oldCount = observed.count;
	EERecClearPhysicalBlocks(blocks, physicalLUT.data(), &ram[0x3018 / 4],
		compiler, 0x301c, 1, &metrics, observer, &observed);
	EXPECT_EQ(observed.count, oldCount); // no repeat event for already-removed blocks
}

TEST_F(EERecCodeCache, EvictionObserverOptionalAndDoesNotChangeRemovalResults)
{
	AddBlock(0x8000, 4, reinterpret_cast<uptr>(firstCode.data()), 24);
	BaseBlocks::Metrics metrics;
	EERecClearPhysicalBlocks(blocks, physicalLUT.data(), nullptr, compiler, 0x8004, 1, &metrics);
	EXPECT_EQ(metrics.removedBlocks, 1u);
	EXPECT_EQ(metrics.retiredHostBytes, 24u);
	EXPECT_EQ(blocks.Get(0x8000), nullptr);
}

TEST_F(EERecCodeCache, MutationMetricsCountRemovedOverlapsButPreserveCompilingAndLiveBytes)
{
	AddBlock(0x3000, 8, reinterpret_cast<uptr>(firstCode.data()), 11);
	AddBlock(0x3010, 4, reinterpret_cast<uptr>(secondCode.data()), 17);
	BASEBLOCKEX* compiling = blocks.New(0x3018, reinterpret_cast<uptr>(dispatcherCode.data()));
	compiling->size = 2;
	compiling->x86size = 13;
	AddBlock(0x6000, 4, reinterpret_cast<uptr>(secondCode.data()), 23);
	const auto before = blocks.GetLiveMetrics();
	EXPECT_EQ(before.blocks, 4U);
	EXPECT_EQ(before.hostBytes, 64U);

	BaseBlocks::Metrics metrics;
	Clear(0x301c, 1, &ram[0x3018 / 4], &metrics);
	EXPECT_EQ(metrics.removedBlocks, 2U);
	EXPECT_EQ(metrics.retiredHostBytes, 28U);
	EXPECT_EQ(metrics.dispatchWrites, 2U);
	EXPECT_EQ(metrics.dispatchWriteAttempts, 10U);
	EXPECT_EQ(metrics.dispatchWriteSkips, 8U);
	EXPECT_EQ(metrics.dispatchWriteAttempts, metrics.dispatchWrites + metrics.dispatchWriteSkips);
	EXPECT_NE(blocks.Get(0x3018), nullptr);
	const auto after = blocks.GetLiveMetrics();
	EXPECT_EQ(after.blocks, 2U);
	EXPECT_EQ(after.hostBytes, 36U);
	EXPECT_EQ(after.hostBytes + metrics.retiredHostBytes, before.hostBytes);
	// Repeating an empty clear does not retire bytes or write dispatch entries.
	const u64 attempts = metrics.dispatchWriteAttempts;
	Clear(0x301c, 1, &ram[0x3018 / 4], &metrics);
	EXPECT_EQ(metrics.removedBlocks, 2U);
	EXPECT_EQ(metrics.retiredHostBytes, 28U);
	EXPECT_EQ(metrics.dispatchWriteAttempts, attempts);
}

TEST_F(EERecCodeCache, LinkMutationMetricsCoverPendingPublishRemoveAndRepeatedPatch)
{
	BaseBlocks::Metrics metrics;
	s32 jump = static_cast<s32>(compiler - reinterpret_cast<uptr>(&jump + 1));
	blocks.Link(0x3000, &jump, &metrics);
	EXPECT_EQ(metrics.linkPatchSkips, 1U);
	BASEBLOCKEX* block = blocks.New(0x3000, reinterpret_cast<uptr>(firstCode.data()), &metrics);
	block->size = 4;
	block->x86size = 9;
	blocks.Link(0x3000, &jump, &metrics);
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 2U);
	blocks.Remove(0, 0, &metrics);
	EXPECT_EQ(jump, static_cast<s32>(compiler - reinterpret_cast<uptr>(&jump + 1)));
	EXPECT_EQ(metrics.linkPatchAttempts, 5U);
	EXPECT_EQ(metrics.linkPatchWrites, 2U);
	EXPECT_EQ(metrics.linkPatchSkips, 3U);
	EXPECT_EQ(metrics.removedBlocks, 1U);
	EXPECT_EQ(metrics.retiredHostBytes, 9U);
	// Records remain valid until the arena/reset lifecycle discards them.
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 2U);
	block = blocks.New(0x3000, reinterpret_cast<uptr>(secondCode.data()), &metrics);
	block->size = 4;
	EXPECT_EQ(jump, static_cast<s32>(block->fnptr - reinterpret_cast<uptr>(&jump + 1)));
	EXPECT_EQ(metrics.linkPatchAttempts, 7U);
	EXPECT_EQ(metrics.linkPatchWrites, 3U);
	EXPECT_EQ(metrics.linkPatchSkips, 4U);
}

// Extended fine mode: do not leave retired-source rel32 sites in the
// target's relink multimap, but do make their bytes safe for in-flight returns.
// DispatcherReg is essential here: JITCompile requires an invalid target LUT
// entry, which is NOT guaranteed when only the source was retired.
TEST_F(EERecCodeCache, RetiredSourceDetachesToDispatcherButLiveOrUnknownSourcesStillRelink)
{
	const uptr source = reinterpret_cast<uptr>(firstCode.data());
	const uptr target = reinterpret_cast<uptr>(secondCode.data());
	const uptr dispatcher = reinterpret_cast<uptr>(dispatcherCode.data());
	AddBlock(0x3000, 4, source);
	AddBlock(0x6000, 4, target);
	blocks.SetRetiredSourceDispatcher(dispatcher);

	BaseBlocks::Metrics metrics;
	s32 owned = 0, unknownOwner = 0;
	blocks.Link(0x6000, &owned, &metrics, source);
	blocks.Link(0x6000, &unknownOwner, &metrics);
	ASSERT_EQ(blocks.GetLiveMetrics().linkRecords, 2u);
	EXPECT_EQ(owned, static_cast<s32>(target - (reinterpret_cast<uptr>(&owned) + sizeof(s32))));
	blocks.Remove(0, 0, &metrics); // Retire only the 0x3000 source.
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 1u);
	EXPECT_EQ(metrics.retiredSourceLinksDetached, 1u);
	EXPECT_EQ(metrics.retiredSourceDispatchWrites, 1u);
	EXPECT_EQ(owned, static_cast<s32>(dispatcher - (reinterpret_cast<uptr>(&owned) + sizeof(s32))));
	EXPECT_EQ(unknownOwner, static_cast<s32>(target - (reinterpret_cast<uptr>(&unknownOwner) + sizeof(s32))));

	blocks.Remove(0, 0, &metrics); // Retire the target.
	EXPECT_EQ(unknownOwner, static_cast<s32>(compiler - (reinterpret_cast<uptr>(&unknownOwner) + sizeof(s32))));
	const uptr replacement = reinterpret_cast<uptr>(firstCode.data() + 8);
	blocks.New(0x6000, replacement, &metrics);
	EXPECT_EQ(unknownOwner, static_cast<s32>(replacement - (reinterpret_cast<uptr>(&unknownOwner) + sizeof(s32))));
	EXPECT_EQ(owned, static_cast<s32>(dispatcher - (reinterpret_cast<uptr>(&owned) + sizeof(s32))));
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 1u);
	EXPECT_EQ(metrics.retiredSourceLinksDetached, 1u);
}

TEST_F(EERecCodeCache, BatchSourceRetirementHandlesSelfLinksAndMultipleOwners)
{
	const uptr sourceA = reinterpret_cast<uptr>(firstCode.data());
	const uptr sourceB = reinterpret_cast<uptr>(secondCode.data());
	const uptr target = reinterpret_cast<uptr>(dispatcherCode.data());
	const uptr safeDispatcher = reinterpret_cast<uptr>(compilerCode.data() + 8);
	AddBlock(0x3000, 4, sourceA);
	AddBlock(0x3010, 4, sourceB);
	AddBlock(0x6000, 4, target);
	blocks.SetRetiredSourceDispatcher(safeDispatcher);

	s32 aToTarget = 0, bToTarget = 0, aToSelf = 0;
	BaseBlocks::Metrics metrics;
	blocks.Link(0x6000, &aToTarget, &metrics, sourceA);
	blocks.Link(0x6000, &bToTarget, &metrics, sourceB);
	blocks.Link(0x3000, &aToSelf, &metrics, sourceA);
	ASSERT_EQ(blocks.GetLiveMetrics().linkRecords, 3u);
	blocks.Remove(0, 1, &metrics);
	EXPECT_EQ(metrics.retiredSourceLinksDetached, 3u);
	EXPECT_EQ(metrics.retiredSourceDispatchWrites, 3u);
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 0u);
	for (const s32* site : std::array<const s32*, 3>{&aToTarget, &bToTarget, &aToSelf})
		EXPECT_EQ(*site, static_cast<s32>(safeDispatcher - (reinterpret_cast<uptr>(site) + sizeof(s32))));
	blocks.Remove(0, 0, &metrics);
	blocks.New(0x6000, sourceA, &metrics);
	for (const s32* site : std::array<const s32*, 3>{&aToTarget, &bToTarget, &aToSelf})
		EXPECT_EQ(*site, static_cast<s32>(safeDispatcher - (reinterpret_cast<uptr>(site) + sizeof(s32))));
}

TEST_F(EERecCodeCache, FineStaticLinkHelperForwardsOwnerToRetirementIndex)
{
	const uptr source = reinterpret_cast<uptr>(firstCode.data());
	const uptr target = reinterpret_cast<uptr>(secondCode.data());
	const uptr dispatcher = reinterpret_cast<uptr>(dispatcherCode.data());
	AddBlock(0x3000, 4, source);
	AddBlock(0x6000, 4, target);
	blocks.SetRetiredSourceDispatcher(dispatcher);
	s32 jump = 0;
	EERecLinkStaticBlock(blocks, true, 0x00403000, 0x00403040, 0x6000, &jump,
		dispatcher, nullptr, source);
	ASSERT_EQ(blocks.GetLiveMetrics().linkRecords, 1u);
	blocks.Remove(0, 0);
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 0u);
	EXPECT_EQ(jump, static_cast<s32>(dispatcher - (reinterpret_cast<uptr>(&jump) + sizeof(s32))));
}

TEST_F(EERecCodeCache, NativeOwnerHintsDoNotChangeOriginalLinkLifetime)
{
	const uptr source = reinterpret_cast<uptr>(firstCode.data());
	const uptr target = reinterpret_cast<uptr>(secondCode.data());
	AddBlock(0x3000, 4, source);
	AddBlock(0x6000, 4, target);
	BaseBlocks::Metrics metrics;
	s32 jump = 0;
	blocks.Link(0x6000, &jump, &metrics, source);
	blocks.Remove(0, 0, &metrics);
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 1u);
	EXPECT_EQ(metrics.retiredSourceLinksDetached, 0u);
	EXPECT_EQ(jump, static_cast<s32>(target - (reinterpret_cast<uptr>(&jump) + sizeof(s32))));
	blocks.Remove(0, 0, &metrics);
	EXPECT_EQ(jump, static_cast<s32>(compiler - (reinterpret_cast<uptr>(&jump) + sizeof(s32))));
	const uptr replacement = reinterpret_cast<uptr>(dispatcherCode.data());
	blocks.New(0x6000, replacement, &metrics);
	EXPECT_EQ(jump, static_cast<s32>(replacement - (reinterpret_cast<uptr>(&jump) + sizeof(s32))));
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 1u);
}

TEST_F(EERecCodeCache, FineSourceIndexDoesNotOutliveCodeArenaReset)
{
	const uptr source = reinterpret_cast<uptr>(firstCode.data());
	const uptr dispatcher = reinterpret_cast<uptr>(dispatcherCode.data());
	AddBlock(0x3000, 4, source);
	blocks.SetRetiredSourceDispatcher(dispatcher);
	ASSERT_TRUE(blocks.HasRetiredSourceDispatcher());
	s32 oldJump = 0;
	blocks.Link(0x4000, &oldJump, nullptr, source);
	ASSERT_EQ(blocks.GetLiveMetrics().linkRecords, 1u);
	blocks.Reset();
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 0u);
	// The runtime must arm its new dispatcher AFTER this reset.
	EXPECT_FALSE(blocks.HasRetiredSourceDispatcher());
	blocks.SetRetiredSourceDispatcher(dispatcher);
	ASSERT_TRUE(blocks.HasRetiredSourceDispatcher());
	AddBlock(0x3000, 4, source);
	BaseBlocks::Metrics metrics;
	blocks.Remove(0, 0, &metrics);
	EXPECT_EQ(metrics.retiredSourceLinksDetached, 0u);
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 0u);
}

TEST_F(EERecCodeCache, DispatcherLinkUsesSameIdempotentPatchAccounting)
{
	BaseBlocks::Metrics metrics;
	s32 jump = 0;
	const uptr dispatcher = reinterpret_cast<uptr>(dispatcherCode.data());
	EERecLinkStaticBlock(blocks, true, 0x4000, 0x5000, 0x6000, &jump, dispatcher, &metrics);
	EERecLinkStaticBlock(blocks, true, 0x4000, 0x5000, 0x6000, &jump, dispatcher, &metrics);
	EXPECT_EQ(jump, static_cast<s32>(dispatcher - reinterpret_cast<uptr>(&jump + 1)));
	EXPECT_EQ(metrics.linkPatchAttempts, 2U);
	EXPECT_EQ(metrics.linkPatchWrites, 1U);
	EXPECT_EQ(metrics.linkPatchSkips, 1U);
	EXPECT_EQ(blocks.GetLiveMetrics().linkRecords, 0U);
}

TEST_F(EERecCodeCache, CacheAndMetricsResetHaveSeparateExplicitLifetimes)
{
	BaseBlocks::Metrics metrics;
	AddBlock(0x3000, 4, reinterpret_cast<uptr>(firstCode.data()), 19);
	s32 jump = 0;
	blocks.Link(0x3000, &jump, &metrics);
	Clear(0x3000, 1, nullptr, &metrics);
	AddBlock(0x6000, 4, reinterpret_cast<uptr>(secondCode.data()), 23);
	blocks.Reset();
	const auto live = blocks.GetLiveMetrics();
	EXPECT_EQ(live.blocks, 0U);
	EXPECT_EQ(live.hostBytes, 0U);
	EXPECT_EQ(live.linkRecords, 0U);
	EXPECT_EQ(metrics.removedBlocks, 1U);
	metrics.Reset();
	EXPECT_EQ(metrics.removedBlocks, 0U);
	EXPECT_EQ(metrics.retiredHostBytes, 0U);
	EXPECT_EQ(metrics.dispatchWriteAttempts, 0U);
	EXPECT_EQ(metrics.dispatchWrites, 0U);
	EXPECT_EQ(metrics.dispatchWriteSkips, 0U);
	EXPECT_EQ(metrics.linkPatchAttempts, 0U);
	EXPECT_EQ(metrics.linkPatchWrites, 0U);
	EXPECT_EQ(metrics.linkPatchSkips, 0U);
}

TEST_F(EERecCodeCache, LargeClearRangeDoesNotWrapBeforeFindingPhysicalBlocks)
{
	AddBlock(0x3000, 4, reinterpret_cast<uptr>(firstCode.data()));
	BaseBlocks::Metrics metrics;
	Clear(0x3000, 0x40000000, nullptr, &metrics);
	EXPECT_EQ(blocks.Get(0x3000), nullptr);
	EXPECT_EQ(metrics.removedBlocks, 1U);
	EXPECT_EQ(ram[0x3000 / 4].GetFnptr(), compiler);
}

TEST_F(EERecCodeCache, HostByteAccountingDoesNotTruncateAtFourGiB)
{
	AddBlock(0x3000, 4, reinterpret_cast<uptr>(firstCode.data()), 0xfffffff0u);
	AddBlock(0x3010, 4, reinterpret_cast<uptr>(secondCode.data()), 0xfffffff0u);
	// Deliberately synthetic metadata sizes exercise the aggregate width;
	// retirement never reads or frees the claimed host-code byte ranges.
	EXPECT_EQ(blocks.GetLiveMetrics().hostBytes, u64{0x1ffffffe0});
	BaseBlocks::Metrics metrics;
	Clear(0x3000, 8, nullptr, &metrics);
	EXPECT_EQ(metrics.retiredHostBytes, u64{0x1ffffffe0});
	EXPECT_EQ(blocks.GetLiveMetrics().hostBytes, 0U);
}

TEST_F(EERecCodeCache, PhysicalBlockLookupExtentDoesNotWrapAtAddressSpaceEnd)
{
	BASEBLOCKEX* block = blocks.New(0xfffffffcu, reinterpret_cast<uptr>(firstCode.data()));
	block->size = 2;
	EXPECT_EQ(blocks.Get(0xfffffffcu), block);
	EXPECT_EQ(blocks.Get(0), nullptr);
}

TEST(EERecLinkPatch, UnalignedImmediateRetainsExactDisplacementBitsAndAdjacentBytes)
{
	std::array<u8, 8> storage;
	storage.fill(0xab);
	s32* jump = reinterpret_cast<s32*>(storage.data() + 1);
	const uptr target = reinterpret_cast<uptr>(jump) + sizeof(s32) + u32{0xf1234567};
	BaseBlocks::Metrics metrics;
	BaseBlocks::PatchLink(jump, target, &metrics);
	BaseBlocks::PatchLink(jump, target, &metrics);
	u32 actual;
	std::memcpy(&actual, storage.data() + 1, sizeof(actual));
	EXPECT_EQ(actual, 0xf1234567u);
	EXPECT_EQ(storage[0], 0xab);
	EXPECT_EQ(storage[5], 0xab);
	EXPECT_EQ(storage[6], 0xab);
	EXPECT_EQ(storage[7], 0xab);
	EXPECT_EQ(metrics.linkPatchAttempts, 2U);
	EXPECT_EQ(metrics.linkPatchWrites, 1U);
	EXPECT_EQ(metrics.linkPatchSkips, 1U);
}

// A physical code page remains a cache object even if no guest virtual page
// currently maps it. Its dispatch entry and incoming links must both be reset.
TEST_F(EERecCodeCache, RemovingPhysicalBlockResetsDispatchAndIncomingLink)
{
	const uptr code = reinterpret_cast<uptr>(firstCode.data());
	AddBlock(0x3000, 4, code);
	s32 incoming = 0;
	blocks.Link(0x3000, &incoming);
	ASSERT_EQ(incoming, static_cast<s32>(code - reinterpret_cast<uptr>(&incoming + 1)));
	Clear(0x3000, 0x400);
	EXPECT_EQ(ram[0x3000 / 4].GetFnptr(), compiler);
	EXPECT_EQ(blocks.Get(0x3000), nullptr);
	EXPECT_EQ(incoming, static_cast<s32>(compiler - reinterpret_cast<uptr>(&incoming + 1)));
}

TEST_F(EERecCodeCache, PhysicalPageClearPreservesOtherPhysicalPage)
{
	const uptr first = reinterpret_cast<uptr>(firstCode.data());
	const uptr second = reinterpret_cast<uptr>(secondCode.data());
	AddBlock(0x3000, 4, first);
	AddBlock(0x6000, 4, second);
	Clear(0x3000, 0x400);
	EXPECT_EQ(ram[0x3000 / 4].GetFnptr(), compiler);
	EXPECT_EQ(ram[0x6000 / 4].GetFnptr(), second);
	ASSERT_NE(blocks.Get(0x6000), nullptr);
	EXPECT_EQ(blocks.Get(0x6000)->fnptr, second);
}

TEST_F(EERecCodeCache, WriteInsideBlockInvalidatesItsStart)
{
	AddBlock(0x3000, 8, reinterpret_cast<uptr>(firstCode.data()));
	Clear(0x3010, 1);
	EXPECT_EQ(ram[0x3000 / 4].GetFnptr(), compiler);
	EXPECT_EQ(blocks.Get(0x3000), nullptr);
}

TEST_F(EERecCodeCache, OverlappingBlockEntriesAreAllCleared)
{
	AddBlock(0x3000, 8, reinterpret_cast<uptr>(firstCode.data()));
	AddBlock(0x3010, 4, reinterpret_cast<uptr>(secondCode.data()));
	Clear(0x3014, 1);
	EXPECT_EQ(ram[0x3000 / 4].GetFnptr(), compiler);
	EXPECT_EQ(ram[0x3010 / 4].GetFnptr(), compiler);
	EXPECT_EQ(blocks.Get(0x3000), nullptr);
	EXPECT_EQ(blocks.Get(0x3010), nullptr);
}

TEST_F(EERecCodeCache, CompilingBlockSurvivesOverlapInvalidation)
{
	const uptr first = reinterpret_cast<uptr>(firstCode.data());
	const uptr second = reinterpret_cast<uptr>(secondCode.data());
	AddBlock(0x3000, 8, first);
	// A block under compilation has metadata but no published dispatch entry.
	blocks.New(0x3010, second)->size = 4;
	Clear(0x3014, 1, &ram[0x3010 / 4]);
	EXPECT_EQ(blocks.Get(0x3000), nullptr);
	ASSERT_NE(blocks.Get(0x3010), nullptr);
	EXPECT_EQ(blocks.Get(0x3010)->fnptr, second);
	EXPECT_EQ(ram[0x3010 / 4].GetFnptr(), compiler);
}

TEST_F(EERecCodeCache, ClearingAcross64KiBBoundaryUsesContiguousPhysicalEntries)
{
	AddBlock(0xfff0, 4, reinterpret_cast<uptr>(firstCode.data()));
	AddBlock(0x10000, 4, reinterpret_cast<uptr>(secondCode.data()));
	Clear(0xfff0, 8);
	EXPECT_EQ(ram[0xfff0 / 4].GetFnptr(), compiler);
	EXPECT_EQ(ram[0x10000 / 4].GetFnptr(), compiler);
	EXPECT_EQ(blocks.Get(0xfff0), nullptr);
	EXPECT_EQ(blocks.Get(0x10000), nullptr);
}

TEST_F(EERecCodeCache, ZeroLengthClearPreservesBlock)
{
	const uptr first = reinterpret_cast<uptr>(firstCode.data());
	AddBlock(0x3000, 4, first);
	Clear(0x3000, 0);
	EXPECT_EQ(ram[0x3000 / 4].GetFnptr(), first);
	EXPECT_NE(blocks.Get(0x3000), nullptr);
}

TEST_F(EERecCodeCache, CrossPageUserTargetCannotBypassVirtualDispatch)
{
	const uptr code = reinterpret_cast<uptr>(firstCode.data());
	const uptr dispatcher = reinterpret_cast<uptr>(dispatcherCode.data());
	AddBlock(0x6000, 4, code);
	s32 jump = 0;
	EERecLinkStaticBlock(blocks, true, 0x00403fbc, 0x00404040, 0x6000, &jump, dispatcher);
	EXPECT_EQ(jump, static_cast<s32>(dispatcher - reinterpret_cast<uptr>(&jump + 1)));
}

TEST_F(EERecCodeCache, PendingCrossPageTargetCannotAcquireStalePhysicalLink)
{
	const uptr dispatcher = reinterpret_cast<uptr>(dispatcherCode.data());
	s32 jump = 0;
	EERecLinkStaticBlock(blocks, true, 0x00403000, 0x00404040, 0x6000, &jump, dispatcher);
	// Another alias later compiles the old PFN. It must not patch this branch,
	// whose target VPN may now belong to a different ASID/PFN or be invalid.
	AddBlock(0x6000, 4, reinterpret_cast<uptr>(firstCode.data()));
	EXPECT_EQ(jump, static_cast<s32>(dispatcher - reinterpret_cast<uptr>(&jump + 1)));
}

TEST_F(EERecCodeCache, SamePageUserTargetKeepsPhysicalLink)
{
	const uptr code = reinterpret_cast<uptr>(firstCode.data());
	AddBlock(0x6080, 4, code);
	s32 jump = 0;
	EERecLinkStaticBlock(blocks, true, 0x00403000, 0x00403080, 0x6080, &jump,
		reinterpret_cast<uptr>(dispatcherCode.data()));
	EXPECT_EQ(jump, static_cast<s32>(code - reinterpret_cast<uptr>(&jump + 1)));
}

TEST_F(EERecCodeCache, FixedKernelTargetKeepsPhysicalLink)
{
	const uptr code = reinterpret_cast<uptr>(firstCode.data());
	AddBlock(0x6000, 4, code);
	s32 jump = 0;
	EERecLinkStaticBlock(blocks, true, 0x00403000, 0x80006000, 0x6000, &jump,
		reinterpret_cast<uptr>(dispatcherCode.data()));
	EXPECT_EQ(jump, static_cast<s32>(code - reinterpret_cast<uptr>(&jump + 1)));
}

TEST_F(EERecCodeCache, NativeCrossPageTargetKeepsPhysicalLink)
{
	const uptr code = reinterpret_cast<uptr>(firstCode.data());
	AddBlock(0x6000, 4, code);
	s32 jump = 0;
	EERecLinkStaticBlock(blocks, false, 0x00403000, 0x00404040, 0x6000, &jump,
		reinterpret_cast<uptr>(dispatcherCode.data()));
	EXPECT_EQ(jump, static_cast<s32>(code - reinterpret_cast<uptr>(&jump + 1)));
}

TEST_F(EERecCodeCache, NextPageDelaySlotCannotMakeCrossPageTargetLocal)
{
	const uptr dispatcher = reinterpret_cast<uptr>(dispatcherCode.data());
	s32 jump = 0;
	// The source is the saved virtual block start, even if the compiler's
	// current PC has advanced through a slot at 00404000 to 00404004.
	EERecLinkStaticBlock(blocks, true, 0x00403ff0, 0x00404008, 0x6008, &jump, dispatcher);
	EXPECT_EQ(jump, static_cast<s32>(dispatcher - reinterpret_cast<uptr>(&jump + 1)));
}

TEST_F(EERecCodeCache, UnmappedGuestPageRejectsReadablePhysicalOpcodes)
{
	const std::array<u32, 2> physicalCode{0, 0};
	const uptr unmappedPage = reinterpret_cast<uptr>(ram.get()) - 0x00400000u * 2;
	EXPECT_EQ(EERecGetInstructionPtr(true, 0x00404040, unmappedPage, ram.get(), physicalCode.data()), nullptr);
}

TEST_F(EERecCodeCache, MappedGuestPageKeepsPhysicalOpcodes)
{
	const std::array<u32, 2> physicalCode{0x8fbc0020, 0x1440001f};
	const uptr mappedPage = reinterpret_cast<uptr>(ram.get()) - 0x00400000u * 2 + 0x10000;
	EXPECT_EQ(EERecGetInstructionPtr(true, 0x00403fbc, mappedPage, ram.get(), physicalCode.data()), physicalCode.data());
}

TEST_F(EERecCodeCache, NativeBootAliasDoesNotRequireGuestMapping)
{
	const std::array<u32, 2> physicalCode{0x03e00008, 0};
	const uptr unmappedPage = reinterpret_cast<uptr>(ram.get()) - 0x00400000u * 2;
	EXPECT_EQ(EERecGetInstructionPtr(false, 0x00404040, unmappedPage, ram.get(), physicalCode.data()), physicalCode.data());
}

TEST(EERecDispatchMode, FirstGuestTransitionRequiresDiscardingNativeLinks)
{
	u32 mode = 0;
	bool reset = false;
	EERecEnterGuestTLBDispatch(mode, reset);
	EXPECT_EQ(mode, 1U);
	EXPECT_TRUE(reset);
}

TEST(EERecDispatchMode, ExistingGuestDispatchDoesNotRequestAnotherReset)
{
	u32 mode = 1;
	bool reset = false;
	EERecEnterGuestTLBDispatch(mode, reset);
	EXPECT_EQ(mode, 1U);
	EXPECT_FALSE(reset);
}

TEST(EERecDispatchMode, InternalCacheResetPreservesGuestDispatch)
{
	u32 mode = 1;
	EERecResetDispatchMode(mode, false);
	EXPECT_EQ(mode, 1U);
}

TEST(EERecDispatchMode, ExternalProviderCacheResetRestoresNativePolicy)
{
	u32 mode = 1;
	EERecResetDispatchMode(mode, true);
	EXPECT_EQ(mode, 0U);
}
TEST(EERecPhysicalPagePresence, SkipNeverCompiledPages)
{
	EERecCompiledPhysicalPages<0x20000> pages;
	EXPECT_FALSE(pages.MayContainCode(0x3000, 0x400));
	pages.Mark(0x7000);
	EXPECT_FALSE(pages.MayContainCode(0x3000, 0x400));
	EXPECT_TRUE(pages.MayContainCode(0x7000, 0x400));
}

TEST(EERecPhysicalPagePresence, RetainCodeCrossingPageBoundary)
{
	EERecCompiledPhysicalPages<0x20000> pages;
	pages.Mark(0x4000);
	EXPECT_TRUE(pages.MayContainCode(0x3ffc, 2));
	EXPECT_FALSE(pages.MayContainCode(0x5000, 1));
}

TEST(EERecPhysicalPagePresence, ResetDiscardsSeenPages)
{
	EERecCompiledPhysicalPages<0x20000> pages;
	pages.Mark(0x11000);
	EXPECT_TRUE(pages.MayContainCode(0x11000, 0x400));
	pages.Reset();
	EXPECT_FALSE(pages.MayContainCode(0x11000, 0x400));
}

TEST(EERecPhysicalPagePresence, CoversEntirePotentialRamWindow)
{
	// ExposedRam is runtime-selected (retail/development RAM), so bitmap
	// storage must use the compile-time allocation ceiling instead.
	EERecCompiledPhysicalPages<Ps2MemSize::TotalRam> pages;
	constexpr u32 lastPage = Ps2MemSize::TotalRam - 0x1000;
	EXPECT_FALSE(pages.MayContainCode(lastPage, 0x400));
	pages.Mark(lastPage);
	EXPECT_TRUE(pages.MayContainCode(lastPage, 0x400));
	pages.Reset();
	EXPECT_FALSE(pages.MayContainCode(lastPage, 0x400));
}

TEST(EERecPhysicalPagePresence, NonRamAndOverflowingRangesRemainConservative)
{
	EERecCompiledPhysicalPages<0x20000> pages;
	EXPECT_FALSE(pages.MayContainCode(0x1000, 0));
	EXPECT_TRUE(pages.MayContainCode(0x20000, 1));
	EXPECT_TRUE(pages.MayContainCode(0x1fffc, 2));
	EXPECT_TRUE(pages.MayContainCode(0xfffffffc, 2));
	EXPECT_TRUE(pages.MayContainCode(0x1000, 0xffffffff));
}

TEST_F(EERecCodeCache, PresenceFilterRetainsOverlappingBlockAndIncomingLinkInvalidation)
{
	EERecCompiledPhysicalPages<RAMSize> pages;
	pages.Mark(0x3000);
	AddBlock(0x3000, 8, reinterpret_cast<uptr>(firstCode.data()));
	AddBlock(0x3010, 4, reinterpret_cast<uptr>(secondCode.data()));
	s32 incoming = 0;
	blocks.Link(0x3000, &incoming);
	ASSERT_TRUE(pages.MayContainCode(0x3014, 1));
	Clear(0x3014, 1);
	EXPECT_EQ(blocks.Get(0x3000), nullptr);
	EXPECT_EQ(blocks.Get(0x3010), nullptr);
	EXPECT_EQ(incoming, static_cast<s32>(compiler - reinterpret_cast<uptr>(&incoming + 1)));
	// An individual clear must not erase the generation's seen bit.
	EXPECT_TRUE(pages.MayContainCode(0x3000, 0x400));
}

TEST(EERecPhysicalPagePresence, NoncontiguousGuestPFNsStillProtectPhysicalBlockMetadata)
{
	EERecCompiledPhysicalPages<0x20000> pages;
	// Guest source at the final instruction of one page followed by a
	// separately mapped next page. The physical block metadata nevertheless
	// describes the contiguous range starting at 0x1ffc.
	pages.Mark(0x1ffcu);
	pages.Mark(0x6000u);
	EXPECT_FALSE(pages.MayContainCode(0x2000u, 1)); // original false negative
	pages.MarkMetadataExtent(0x1ffcu, 12u);
	EXPECT_TRUE(pages.MayContainCode(0x2000u, 1));
	EXPECT_TRUE(pages.MayContainCode(0x6000u, 1));
}

} // namespace
