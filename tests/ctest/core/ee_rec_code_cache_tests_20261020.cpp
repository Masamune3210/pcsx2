// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
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
} // namespace
