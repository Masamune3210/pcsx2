// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Deliberately unregistered while compilation is prohibited. Link with the real
// PCSX2 DEV9.cpp; do not substitute a copy of its FIFO implementation.
#include "DEV9/DEV9.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>

void IOPReadFIFO();
void IOPWriteFIFO();

namespace
{
constexpr int capacity = SPD_DBUF_AVAIL_MAX * 512;

u8 Pattern(int i)
{
	return static_cast<u8>(0x29 + i * 37 + (i >> 8) * 13);
}

class DEV9FIFO : public testing::Test
{
protected:
	dev9Struct saved{};
	std::array<u8, capacity> memory{};

	void SetUp() override
	{
		saved = dev9;
		dev9 = {};
		std::fill_n(dev9.fifo, capacity, 0xe7);
		dev9.dma_iop_ptr = memory.data();
		dev9.xfr_ctrl = SPD_XFR_DMAEN;
	}

	void TearDown() override
	{
		dev9 = saved;
	}
};

TEST_F(DEV9FIFO, WrappedWritePreservesBothSegmentsAndUntouchedBytes)
{
	for (const int tail : {1, 17, 512})
	{
		SCOPED_TRACE(tail);
		std::fill_n(dev9.fifo, capacity, 0xe7);
		for (int i = 0; i < capacity; i++)
			memory[i] = Pattern(i);
		const int base = capacity - tail;
		// Two equal segments also give the unfixed implementation a clean
		// data-mismatch failure without overwriting fields after the ring.
		const int length = tail * 2;
		dev9.fifo_bytes_read = base;
		dev9.fifo_bytes_write = base;
		dev9.dma_iop_size = length;
		dev9.dma_iop_transfered = 0;
		dev9.xfr_ctrl |= SPD_XFR_WRITE;

		IOPWriteFIFO();

		ASSERT_EQ(dev9.dma_iop_transfered, length);
		EXPECT_EQ(dev9.fifo_bytes_write - dev9.fifo_bytes_read, length);
		for (int i = 0; i < capacity; i++)
		{
			const int source = (i - base + capacity) % capacity;
			EXPECT_EQ(dev9.fifo[i], source < length ? Pattern(source) : 0xe7) << i;
		}
	}
}

TEST_F(DEV9FIFO, WrappedReadPreservesOrderAndRespectsResidualLength)
{
	constexpr int base = capacity - 17;
	constexpr int length = 1024;
	for (int i = 0; i < length; i++)
		dev9.fifo[(base + i) % capacity] = Pattern(i);
	memory.fill(0xe7);
	dev9.fifo_bytes_read = base;
	dev9.fifo_bytes_write = base + length;
	dev9.dma_iop_size = 12 + length;
	dev9.dma_iop_transfered = 12;

	IOPReadFIFO();

	EXPECT_EQ(dev9.dma_iop_transfered, 12 + length);
	EXPECT_EQ(dev9.fifo_bytes_read, dev9.fifo_bytes_write);
	for (int i = 0; i < capacity; i++)
		EXPECT_EQ(memory[i], (i >= 12 && i < 12 + length) ? Pattern(i - 12) : 0xe7) << i;
}

TEST_F(DEV9FIFO, FullFIFOLeavesPendingWriteUntouched)
{
	dev9.xfr_ctrl |= SPD_XFR_WRITE;
	dev9.fifo_bytes_write = capacity;
	dev9.dma_iop_size = 512;
	IOPWriteFIFO();
	EXPECT_EQ(dev9.dma_iop_transfered, 0);
	EXPECT_EQ(dev9.fifo_bytes_write, capacity);
	EXPECT_TRUE(std::all_of(std::begin(dev9.fifo), std::end(dev9.fifo), [](u8 v) { return v == 0xe7; }));
}

TEST_F(DEV9FIFO, IndependentCounterResetCannotBecomeNegativeCopyLength)
{
	// A write-counter-only reset can leave read > write. Preserve this
	// invalid state until software resets both; never fabricate DMA progress.
	for (const bool write : {false, true})
	{
		dev9.xfr_ctrl = SPD_XFR_DMAEN | (write ? SPD_XFR_WRITE : 0);
		dev9.fifo_bytes_read = 512;
		dev9.fifo_bytes_write = 0;
		dev9.dma_iop_size = 512;
		dev9.dma_iop_transfered = 0;
		memory.fill(0xe7);
		if (write)
			IOPWriteFIFO();
		else
			IOPReadFIFO();
		EXPECT_EQ(dev9.fifo_bytes_read, 512);
		EXPECT_EQ(dev9.fifo_bytes_write, 0);
		EXPECT_EQ(dev9.dma_iop_transfered, 0);
		EXPECT_TRUE(std::all_of(memory.begin(), memory.end(), [](u8 v) { return v == 0xe7; }));
		EXPECT_TRUE(std::all_of(std::begin(dev9.fifo), std::end(dev9.fifo), [](u8 v) { return v == 0xe7; }));
	}
}

TEST_F(DEV9FIFO, InvalidResidualCannotBecomeNegativeCopyLength)
{
	for (const bool write : {false, true})
	{
		for (const auto residual : {std::array{511, 512}, std::array{-1, 0}, std::array{512, -1}})
		{
			dev9.xfr_ctrl = SPD_XFR_DMAEN | (write ? SPD_XFR_WRITE : 0);
			dev9.fifo_bytes_read = 0;
			dev9.fifo_bytes_write = 512;
			dev9.dma_iop_size = residual[0];
			dev9.dma_iop_transfered = residual[1];
			memory.fill(0xe7);
			if (write)
				IOPWriteFIFO();
			else
				IOPReadFIFO();
			EXPECT_EQ(dev9.fifo_bytes_read, 0);
			EXPECT_EQ(dev9.fifo_bytes_write, 512);
			EXPECT_EQ(dev9.dma_iop_transfered, residual[1]);
			EXPECT_TRUE(std::all_of(memory.begin(), memory.end(), [](u8 v) { return v == 0xe7; }));
			EXPECT_TRUE(std::all_of(std::begin(dev9.fifo), std::end(dev9.fifo), [](u8 v) { return v == 0xe7; }));
		}
	}
}

TEST_F(DEV9FIFO, CounterRebasePreservesRingPositionAtSignedLimit)
{
	constexpr int base = std::numeric_limits<int>::max() - 511;
	static_assert(base % capacity == capacity - 512);
	for (int i = 0; i < 1024; i++)
		memory[i] = Pattern(i);
	dev9.xfr_ctrl |= SPD_XFR_WRITE;
	dev9.fifo_bytes_read = base;
	dev9.fifo_bytes_write = base;
	dev9.dma_iop_size = 1024;

	IOPWriteFIFO();

	ASSERT_EQ(dev9.dma_iop_transfered, 1024);
	EXPECT_GE(dev9.fifo_bytes_read, 0);
	EXPECT_GE(dev9.fifo_bytes_write, 0);
	EXPECT_EQ(dev9.fifo_bytes_read % capacity, base % capacity);
	EXPECT_EQ(dev9.fifo_bytes_write - dev9.fifo_bytes_read, 1024);
	for (int i = 0; i < 1024; i++)
		EXPECT_EQ(dev9.fifo[(base % capacity + i) % capacity], Pattern(i)) << i;
}

TEST_F(DEV9FIFO, ResetDiscardsPendingIOPTransferAndBufferedBytes)
{
	dev9.fifo_bytes_read = 12;
	dev9.fifo_bytes_write = 512;
	dev9.dma_iop_size = 1024;
	dev9.dma_iop_transfered = 512;

	DEV9ResetATAFIFO();

	EXPECT_EQ(dev9.fifo_bytes_read, 0);
	EXPECT_EQ(dev9.fifo_bytes_write, 0);
	EXPECT_EQ(dev9.dma_iop_ptr, nullptr);
	EXPECT_EQ(dev9.dma_iop_size, 0);
	EXPECT_EQ(dev9.dma_iop_transfered, 0);
}
} // namespace
