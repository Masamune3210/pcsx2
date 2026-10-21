// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "MemoryTypes.h"
#include "x86/EERecCodeCache.h"

#include <gtest/gtest.h>

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

	void AddBlock(u32 address, u32 words, uptr code)
	{
		blocks.New(address, code)->size = words;
		ram[address / 4].SetFnptr(code);
	}

	void Clear(u32 address, u32 words, BASEBLOCK* compiling = nullptr)
	{
		EERecClearPhysicalBlocks(blocks, physicalLUT.data(), compiling, compiler, address, words);
	}
};

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
