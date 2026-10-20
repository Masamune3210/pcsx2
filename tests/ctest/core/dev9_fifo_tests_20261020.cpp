// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Registered in core_test against the real PCSX2 DEV9.cpp. Compilation and
// execution remain unrun; do not substitute a copy of the FIFO implementation.
#include "DEV9/DEV9.h"
#include "DEV9/ATA/ATA.h"
#include "Config.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
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
	const u32 generation = dev9.fifo_generation;
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
	EXPECT_NE(dev9.fifo_generation, generation);
}
// Run12 requested 0x3000, 0x7000 and 0x8000-byte IOP DMA8 transfers.
// Exercise the real ATA_ReadDMA -> SPEED FIFO -> IOPReadFIFO path, rather
// than calling FIFO helpers on a manually populated ring.
TEST_F(DEV9FIFO, AtaReadDMA8DrainsRequestsLargerThanThePhysicalFIFO)
{
	namespace fs = std::filesystem;
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path folder = fs::temp_directory_path() /
		("pcsx2-dev9-dma8-" + std::to_string(stamp));
	ASSERT_TRUE(fs::create_directory(folder));
	const fs::path path = folder / "temporary.raw";

	struct ScopedHDDTest
	{
		fs::path folder;
		bool previousEnabled;
		~ScopedHDDTest()
		{
			EmuConfig.DEV9.HddEnable = previousEnabled;
			std::error_code ec;
			fs::remove_all(folder, ec);
		}
	} cleanup{folder, EmuConfig.DEV9.HddEnable};

	constexpr int sectorSize = 512;
	constexpr int imageSectors = 128;
	std::vector<u8> disk(imageSectors * sectorSize);
	for (int i = 0; i < static_cast<int>(disk.size()); ++i)
		disk[i] = Pattern(i);
	{
		std::ofstream file(path, std::ios::binary);
		ASSERT_TRUE(file.is_open());
		file.write(reinterpret_cast<const char*>(disk.data()), disk.size());
		file.close();
		ASSERT_TRUE(file.good());
	}

	EmuConfig.DEV9.HddEnable = true;
	auto drive = std::make_unique<ATA>();
	dev9.ata = drive.get();
	ASSERT_EQ(drive->Open(path.string()), 0);
	drive->Write(ATA_R_CONTROL, 0, 8);

	for (const int bytes : {0x3000, 0x7000, 0x8000})
	{
		SCOPED_TRACE(bytes);
		ASSERT_EQ(bytes % sectorSize, 0);
		std::vector<u8> output(bytes, 0xe7);
		DEV9ResetATAFIFO();
		dev9.irqcause = 0;
		dev9.dma_iop_ptr = output.data();
		dev9.dma_iop_size = bytes;
		dev9.dma_iop_transfered = 0;
		dev9.xfr_ctrl = SPD_XFR_DMAEN;
		dev9.if_ctrl = SPD_IF_ATA_DMAEN | SPD_IF_READ;

		const int sectors = bytes / sectorSize;
		drive->Write(ATA_R_SELECT, 0xe0, 8);
		drive->Write(ATA_R_NSECTOR, sectors, 8);
		drive->Write(ATA_R_SECTOR, 4, 8);
		drive->Write(ATA_R_LCYL, 0, 8);
		drive->Write(ATA_R_HCYL, 0, 8);
		drive->Write(ATA_R_CMD, 0xc8, 8); // READ DMA, image sector 4.

		EXPECT_EQ(dev9.dma_iop_transfered, bytes);
		EXPECT_EQ(dev9.dma_iop_ptr, nullptr) << "DMA8 completion not signaled";
		EXPECT_EQ(dev9.fifo_bytes_read, dev9.fifo_bytes_write);
		EXPECT_TRUE(std::equal(output.begin(), output.end(), disk.begin() + 4 * sectorSize))
			<< "Short, repeated or out-of-order FIFO bytes";
		EXPECT_EQ(drive->Read(ATA_R_ALT_STATUS, 8) & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), 0);
	}

	drive->Close();
	dev9.ata = nullptr;
}

} // namespace
