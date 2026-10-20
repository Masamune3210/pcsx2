// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "DEV9/ATA/ATA.h"
#include "DEV9/DEV9.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
	constexpr size_t SectorSize = 512;
	using Bytes = std::vector<u8>;

	Bytes Pattern(size_t sectors, unsigned int salt)
	{
		Bytes bytes(sectors * SectorSize);
		for (size_t i = 0; i < bytes.size(); ++i)
			bytes[i] = static_cast<u8>((i * 37 + (i / SectorSize) * 19 + (i >> 8) * 7 + salt) ^ (i >> 3));
		return bytes;
	}

	class ATAImageTest : public testing::Test
	{
	protected:
		std::filesystem::path directory;
		std::filesystem::path image;
		Bytes initial;
		std::unique_ptr<ATA> drive;

		void SetUp() override
		{
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			for (unsigned int attempt = 0; attempt < 100; ++attempt)
			{
				directory = std::filesystem::temp_directory_path() /
					("pcsx2-ata-" + std::to_string(stamp) + "-" + std::to_string(attempt));
				if (std::filesystem::create_directory(directory))
					break;
				directory.clear();
			}
			ASSERT_FALSE(directory.empty());
			image = directory / "disposable.raw";
			initial = Pattern(600, 11);
			ASSERT_NO_FATAL_FAILURE(CreateImage(initial));
			ASSERT_NO_FATAL_FAILURE(Open());
		}

		void TearDown() override
		{
			Close();
			if (!directory.empty())
			{
				std::error_code error;
				std::filesystem::remove_all(directory, error);
				EXPECT_FALSE(error) << error.message();
			}
		}

		void CreateImage(const Bytes& bytes)
		{
			std::ofstream file(image, std::ios::binary | std::ios::trunc);
			ASSERT_TRUE(file.is_open());
			file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			file.close();
			ASSERT_TRUE(file.good());
		}

		void Open()
		{
			dev9.irqcause = 0;
			ATAHarness::irqCount = 0;
			EmuConfig.DEV9.HddEnable = true;
			drive = std::make_unique<ATA>();
			ASSERT_EQ(drive->Open(image.string()), 0);
			drive->Write(ATA_R_CONTROL, 0, 8);
		}

		void Close()
		{
			if (drive)
			{
				drive->Close();
				drive.reset();
			}
		}

		u16 Status() { return drive->Read(ATA_R_ALT_STATUS, 8); }
		bool IRQ() { return (dev9.irqcause & ATA_INTR_INTRQ) != 0; }
		void Acknowledge() { drive->Read(ATA_R_STATUS, 8); }

		bool WaitReady()
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (Status() & ATA_STAT_BUSY)
			{
				drive->Async(1);
				if (std::chrono::steady_clock::now() >= deadline)
					return false;
				std::this_thread::yield();
			}
			return true;
		}

		void Address(u32 lba, u8 count)
		{
			drive->Write(ATA_R_SELECT, 0xE0 | ((lba >> 24) & 0x0F), 8);
			drive->Write(ATA_R_NSECTOR, count, 8);
			drive->Write(ATA_R_SECTOR, lba & 0xFF, 8);
			drive->Write(ATA_R_LCYL, (lba >> 8) & 0xFF, 8);
			drive->Write(ATA_R_HCYL, (lba >> 16) & 0xFF, 8);
		}

		void Address48(u64 lba, u16 count)
		{
			drive->Write(ATA_R_SELECT, 0xE0, 8);
			drive->Write(ATA_R_NSECTOR, count >> 8, 8);
			drive->Write(ATA_R_SECTOR, (lba >> 24) & 0xFF, 8);
			drive->Write(ATA_R_LCYL, (lba >> 32) & 0xFF, 8);
			drive->Write(ATA_R_HCYL, (lba >> 40) & 0xFF, 8);
			drive->Write(ATA_R_NSECTOR, count & 0xFF, 8);
			drive->Write(ATA_R_SECTOR, lba & 0xFF, 8);
			drive->Write(ATA_R_LCYL, (lba >> 8) & 0xFF, 8);
			drive->Write(ATA_R_HCYL, (lba >> 16) & 0xFF, 8);
		}

		void Command(u8 command) { drive->Write(ATA_R_CMD, command, 8); }

		u32 TaskLba()
		{
			return drive->Read(ATA_R_SECTOR, 8) | (drive->Read(ATA_R_LCYL, 8) << 8) |
				(drive->Read(ATA_R_HCYL, 8) << 16) | ((drive->Read(ATA_R_SELECT, 8) & 15) << 24);
		}

		u64 TaskLba48()
		{
			u64 lba = drive->Read(ATA_R_SECTOR, 8) | (drive->Read(ATA_R_LCYL, 8) << 8) |
				(drive->Read(ATA_R_HCYL, 8) << 16);
			drive->Write(ATA_R_CONTROL, 0x80, 8);
			lba |= u64(drive->Read(ATA_R_SECTOR, 8)) << 24;
			lba |= u64(drive->Read(ATA_R_LCYL, 8)) << 32;
			lba |= u64(drive->Read(ATA_R_HCYL, 8)) << 40;
			EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 0);
			drive->Write(ATA_R_CONTROL, 0, 8);
			return lba;
		}

		void ExpectSuccess()
		{
			ASSERT_TRUE(WaitReady());
			EXPECT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), 0);
		}

		void ExpectAbort()
		{
			ASSERT_TRUE(WaitReady());
			EXPECT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_ERR);
			EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ABORT, 0);
		}

		void SetMultiple(u8 count)
		{
			drive->Write(ATA_R_NSECTOR, count, 8);
			Command(0xC6);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
		}

		void SetSmallChsGeometry()
		{
			drive->Write(ATA_R_SELECT, 0xA0, 8); // one head
			drive->Write(ATA_R_NSECTOR, 10, 8); // ten sectors/track, sixty cylinders
			Command(0x91);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
		}

		void AddressChs(u16 cylinder, u8 head, u8 sector, u8 count)
		{
			drive->Write(ATA_R_SELECT, 0xA0 | head, 8);
			drive->Write(ATA_R_NSECTOR, count, 8);
			drive->Write(ATA_R_SECTOR, sector, 8);
			drive->Write(ATA_R_LCYL, cylinder & 0xFF, 8);
			drive->Write(ATA_R_HCYL, cylinder >> 8, 8);
		}

		void Flush()
		{
			Command(0xE7);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
		}

		void CheckPower(u8 expected)
		{
			Command(0xE5);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), expected);
			Acknowledge();
		}

		void WriteSectors(u8 command, u32 lba, const Bytes& bytes, unsigned int blockSectors, bool extended = false)
		{
			ASSERT_EQ(bytes.size() % SectorSize, 0u);
			const size_t count = bytes.size() / SectorSize;
			ASSERT_GT(count, 0u);
			ASSERT_LE(count, extended ? 65536u : 256u);
			if (extended)
				Address48(lba, static_cast<u16>(count));
			else
				Address(lba, static_cast<u8>(count));
			const unsigned int irqStart = ATAHarness::irqCount;
			Command(command);
			ASSERT_TRUE(WaitReady());
			ASSERT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_DRQ);
			EXPECT_FALSE(IRQ()) << "PIO data-out's first DRQ must not interrupt";
			for (size_t sector = 0; sector < count; ++sector)
			{
				ASSERT_TRUE(WaitReady());
				ASSERT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_DRQ) << sector;
				for (size_t word = 0; word < SectorSize / 2; ++word)
				{
					const size_t offset = sector * SectorSize + word * 2;
					drive->Write(ATA_R_DATA, bytes[offset] | (bytes[offset + 1] << 8), 16);
					if (word + 1 != SectorSize / 2)
						ASSERT_NE(Status() & ATA_STAT_DRQ, 0) << "early DRQ end at word " << word;
				}
				ASSERT_TRUE(WaitReady());
				const bool endBlock = ((sector + 1) % blockSectors == 0) || (sector + 1 == count);
				EXPECT_EQ(IRQ(), endBlock) << "sector " << sector;
				if (endBlock)
					Acknowledge();
			}
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			EXPECT_EQ(ATAHarness::irqCount - irqStart, (count + blockSectors - 1) / blockSectors);
			EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 0);
			EXPECT_EQ(extended ? TaskLba48() : TaskLba(), lba + count);
		}

		void ReadSectors(u8 command, u32 lba, size_t count, unsigned int blockSectors, Bytes* bytes, bool extended = false)
		{
			if (extended)
				Address48(lba, static_cast<u16>(count));
			else
				Address(lba, static_cast<u8>(count));
			const unsigned int irqStart = ATAHarness::irqCount;
			Command(command);
			bytes->clear();
			for (size_t sector = 0; sector < count; ++sector)
			{
				ASSERT_TRUE(WaitReady());
				ASSERT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_DRQ) << sector;
				const bool startBlock = sector % blockSectors == 0;
				EXPECT_EQ(IRQ(), startBlock) << "sector " << sector;
				if (startBlock)
					Acknowledge();
				for (size_t word = 0; word < SectorSize / 2; ++word)
				{
					const u16 value = drive->Read(ATA_R_DATA, 16);
					bytes->push_back(value & 0xFF);
					bytes->push_back(value >> 8);
					if (word + 1 != SectorSize / 2)
						ASSERT_NE(Status() & ATA_STAT_DRQ, 0);
				}
			}
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			EXPECT_EQ(ATAHarness::irqCount - irqStart, (count + blockSectors - 1) / blockSectors);
			EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 0);
			EXPECT_EQ(extended ? TaskLba48() : TaskLba(), lba + count);
		}

		Bytes DiskBytes()
		{
			std::ifstream file(image, std::ios::binary);
			EXPECT_TRUE(file.is_open());
			return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}

		std::array<u16, 256> Identify()
		{
			Command(0xEC);
			std::array<u16, 256> words{};
			EXPECT_TRUE(WaitReady());
			EXPECT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_DRQ);
			Acknowledge();
			for (u16& word : words)
				word = drive->Read(ATA_R_DATA, 16);
			EXPECT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_BUSY | ATA_STAT_ERR), 0);
			return words;
		}
	};

	TEST_F(ATAImageTest, SingleSectorWritesAndAliasesPersistAfterCloseAndReopen)
	{
		for (u8 command : {0x30, 0x31})
		{
			SCOPED_TRACE(static_cast<unsigned int>(command));
			const Bytes payload = Pattern(1, command);
			const u32 lba = command;
			ASSERT_NO_FATAL_FAILURE(WriteSectors(command, lba, payload, 1));
			ASSERT_NO_FATAL_FAILURE(Flush());
			Close();
			std::copy(payload.begin(), payload.end(), initial.begin() + lba * SectorSize);
			EXPECT_EQ(DiskBytes(), initial) << "verify every byte, including adjacent sectors";
			ASSERT_NO_FATAL_FAILURE(Open());
			Bytes actual;
			ASSERT_NO_FATAL_FAILURE(ReadSectors(command == 0x30 ? 0x20 : 0x21, lba, 1, 1, &actual));
			EXPECT_EQ(actual, payload);
		}
	}

	TEST_F(ATAImageTest, ZeroCountTransfersExactly256Sectors)
	{
		const Bytes payload = Pattern(256, 0x98);
		ASSERT_NO_FATAL_FAILURE(WriteSectors(0x30, 17, payload, 1));
		ASSERT_NO_FATAL_FAILURE(Flush());
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 17, 256, 1, &actual));
		EXPECT_EQ(actual, payload);
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 17 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, Multiple128UsesTwoInterruptBlocksFor130SectorTail)
	{
		ASSERT_NO_FATAL_FAILURE(SetMultiple(128));
		const Bytes payload = Pattern(130, 0x57);
		ASSERT_NO_FATAL_FAILURE(WriteSectors(0xC5, 41, payload, 128));
		ASSERT_NO_FATAL_FAILURE(Flush());
		for (int repeat = 0; repeat < 2; ++repeat)
		{
			Bytes actual;
			ASSERT_NO_FATAL_FAILURE(ReadSectors(0xC4, 41, 130, 128, &actual));
			EXPECT_EQ(actual, payload);
		}
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 41 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
		ASSERT_NO_FATAL_FAILURE(Open());
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0xC4, 41, 130, 128, &actual));
		EXPECT_EQ(actual, payload);
	}

	TEST_F(ATAImageTest, Multiple128ZeroCountTransfersAndPersists256Sectors)
	{
		ASSERT_NO_FATAL_FAILURE(SetMultiple(128));
		const Bytes payload = Pattern(256, 0xC5);
		ASSERT_NO_FATAL_FAILURE(WriteSectors(0xC5, 11, payload, 128));
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0xC4, 11, 256, 128, &actual));
		EXPECT_EQ(actual, payload);
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 11 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, MultipleInvalidRangesAbortBeforeAnyDataTransfer)
	{
		ASSERT_NO_FATAL_FAILURE(SetMultiple(4));
		for (u8 command : {0xC4, 0xC5})
		{
			SCOPED_TRACE(static_cast<unsigned int>(command));
			for (const auto& range : std::array<std::array<u32, 2>, 3>{{{599, 2}, {596, 8}, {600, 1}}})
			{
				Address(range[0], range[1]);
				Command(command);
				ASSERT_NO_FATAL_FAILURE(ExpectAbort());
				EXPECT_TRUE(IRQ());
				EXPECT_EQ(TaskLba(), 600u);
				EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF);
				drive->Write(ATA_R_DATA, 0xA55A, 16);
				Acknowledge();
			}
		}
		ASSERT_NO_FATAL_FAILURE(Flush());
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, MultipleBackingReadErrorTransfersOneWholeErrorBlockWithoutStaleBytes)
	{
		for (u32 completeSectors : {0u, 1u, 4u, 5u, 8u, 9u})
		{
			SCOPED_TRACE(completeSectors);
			Close();
			ASSERT_NO_FATAL_FAILURE(CreateImage(initial));
			ASSERT_NO_FATAL_FAILURE(Open());
			ASSERT_NO_FATAL_FAILURE(SetMultiple(4));
			Bytes stale;
			ASSERT_NO_FATAL_FAILURE(ReadSectors(0xC4, 100, 10, 4, &stale));
			std::error_code error;
			std::filesystem::resize_file(image, completeSectors * SectorSize + 31, error);
			if (error)
				GTEST_SKIP() << "platform does not permit resizing an open image: " << error.message();
			Address(0, 10);
			Command(0xC4);
			const u32 errorBlockStart = completeSectors / 4 * 4;
			const u32 errorBlockEnd = std::min(errorBlockStart + 4, 10u);
			for (u32 sector = 0; sector < errorBlockEnd; ++sector)
			{
				ASSERT_TRUE(WaitReady());
				const u16 expectedStatus = ATA_STAT_DRQ | (sector >= errorBlockStart ? ATA_STAT_ERR : 0);
				ASSERT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_ERR), expectedStatus);
				EXPECT_EQ(IRQ(), sector % 4 == 0);
				if (sector % 4 == 0)
					Acknowledge();
				if (sector >= errorBlockStart)
					EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ECC, 0);
				for (u32 word = 0; word < SectorSize / 2; ++word)
				{
					const size_t offset = sector * SectorSize + word * 2;
					const u16 expected = sector < completeSectors ? initial[offset] | (initial[offset + 1] << 8) : 0;
					EXPECT_EQ(drive->Read(ATA_R_DATA, 16), expected);
				}
			}
			EXPECT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_ERR);
			EXPECT_EQ(TaskLba(), completeSectors);
			EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 10 - completeSectors);
			EXPECT_FALSE(IRQ());
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF);
			// A new command must discard the completed error-block continuation.
			EXPECT_NE(Identify()[47] & 0xFF, 0);
		}
	}

#if defined(__POSIX__)
	TEST_F(ATAImageTest, ExtendedMultipleRangesAndBackingReadErrorsFollowBlockProtocol)
	{
		Close();
		constexpr u64 sectors = (u64(1) << 28) + 1;
		std::error_code error;
		std::filesystem::resize_file(image, sectors * SectorSize, error);
		if (error)
			GTEST_SKIP() << "filesystem does not support this sparse logical size: " << error.message();
		ASSERT_NO_FATAL_FAILURE(Open());
		ASSERT_NO_FATAL_FAILURE(SetMultiple(4));
		for (u8 command : {0x29, 0x39})
		{
			Address48(sectors - 1, 2);
			Command(command);
			ASSERT_NO_FATAL_FAILURE(ExpectAbort());
			EXPECT_EQ(TaskLba48(), sectors);
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF);
			drive->Write(ATA_R_DATA, 0xA55A, 16);
			Acknowledge();
		}
		Bytes stale;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x29, 100, 10, 4, &stale, true));
		std::filesystem::resize_file(image, 5 * SectorSize + 31, error);
		ASSERT_FALSE(error) << error.message();
		Address48(0, 10);
		Command(0x29);
		for (u32 sector = 0; sector < 8; ++sector)
		{
			ASSERT_TRUE(WaitReady());
			ASSERT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_DRQ | (sector >= 4 ? ATA_STAT_ERR : 0));
			EXPECT_EQ(IRQ(), sector % 4 == 0);
			if (sector % 4 == 0)
				Acknowledge();
			for (u32 word = 0; word < SectorSize / 2; ++word)
			{
				const size_t offset = sector * SectorSize + word * 2;
				const u16 expected = sector < 5 ? initial[offset] | (initial[offset + 1] << 8) : 0;
				EXPECT_EQ(drive->Read(ATA_R_DATA, 16), expected);
			}
		}
		EXPECT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_ERR);
		EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ECC, 0);
		EXPECT_EQ(TaskLba48(), 5u);
		EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 5);
		EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF);
	}
#endif

	TEST_F(ATAImageTest, LastSectorIsReadableAndWritable)
	{
		const Bytes payload = Pattern(1, 0xF3);
		ASSERT_NO_FATAL_FAILURE(WriteSectors(0x30, 599, payload, 1));
		ASSERT_NO_FATAL_FAILURE(Flush());
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 599, 1, 1, &actual));
		EXPECT_EQ(actual, payload);
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 599 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, OneSectorImageHasOneAccessibleSector)
	{
		Close();
		initial = Pattern(1, 0x56);
		ASSERT_NO_FATAL_FAILURE(CreateImage(initial));
		ASSERT_NO_FATAL_FAILURE(Open());
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 0, 1, 1, &actual));
		EXPECT_EQ(actual, initial);
		const Bytes payload = Pattern(1, 0x75);
		ASSERT_NO_FATAL_FAILURE(WriteSectors(0x30, 0, payload, 1));
		Close();
		EXPECT_EQ(DiskBytes(), payload);
	}

	TEST_F(ATAImageTest, EmptyImageAndOutOfRangeStartReturnIdNotFoundWithoutData)
	{
		for (size_t sectors : {size_t(600), size_t(0)})
		{
			Close();
			const Bytes content = Pattern(sectors, 1);
			ASSERT_NO_FATAL_FAILURE(CreateImage(content));
			ASSERT_NO_FATAL_FAILURE(Open());
			for (u8 command : {0x20, 0x30})
			{
				Address(static_cast<u32>(sectors), 1);
				Command(command);
				ASSERT_TRUE(WaitReady());
				EXPECT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_ERR);
				EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ID, 0);
				Acknowledge();
			}
			Close();
			EXPECT_EQ(DiskBytes(), content);
		}
	}

	TEST_F(ATAImageTest, CrossingEofTransfersValidPrefixAndReportsFirstInvalidSector)
	{
		const Bytes payload = Pattern(1, 0xAC);
		for (u8 command : {0x20, 0x30})
		{
			Address(599, 2);
			Command(command);
			ASSERT_TRUE(WaitReady());
			ASSERT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_DRQ);
			Acknowledge();
			for (unsigned int word = 0; word < SectorSize / 2; ++word)
			{
				if (command == 0x20)
				{
					const size_t offset = 599 * SectorSize + word * 2;
					EXPECT_EQ(drive->Read(ATA_R_DATA, 16), initial[offset] | (initial[offset + 1] << 8));
				}
				else
					drive->Write(ATA_R_DATA, payload[word * 2] | (payload[word * 2 + 1] << 8), 16);
			}
			ASSERT_TRUE(WaitReady());
			EXPECT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_ERR);
			EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ID, 0);
			EXPECT_EQ(TaskLba(), 600u);
			EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 1);
			Acknowledge();
		}
		ASSERT_NO_FATAL_FAILURE(Flush());
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 599 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, SrstDiscardsIncompleteSectorAndAllowsFreshWrite)
	{
		Address(17, 2);
		Command(0x30);
		ASSERT_TRUE(WaitReady());
		ASSERT_NE(Status() & ATA_STAT_DRQ, 0);
		for (unsigned int i = 0; i < 73; ++i)
			drive->Write(ATA_R_DATA, 0xCC33, 16);
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		ASSERT_TRUE(WaitReady());
		EXPECT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_ERR), 0);
		EXPECT_FALSE(IRQ());
		const Bytes payload = Pattern(1, 99);
		ASSERT_NO_FATAL_FAILURE(WriteSectors(0x30, 23, payload, 1));
		ASSERT_NO_FATAL_FAILURE(Flush());
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 23 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial) << "partial command must not leak into a later write";
	}

	TEST_F(ATAImageTest, SrstPreservesAcceptedMultipleBlockAndDiscardsPartialNextBlock)
	{
		ASSERT_NO_FATAL_FAILURE(SetMultiple(2));
		const Bytes payload = Pattern(2, 0xB2);
		Address(13, 4);
		Command(0xC5);
		ASSERT_TRUE(WaitReady());
		ASSERT_NE(Status() & ATA_STAT_DRQ, 0);
		for (size_t i = 0; i < payload.size(); i += 2)
			drive->Write(ATA_R_DATA, payload[i] | (payload[i + 1] << 8), 16);
		ASSERT_TRUE(WaitReady());
		ASSERT_TRUE(IRQ());
		Acknowledge();
		for (unsigned int i = 0; i < 33; ++i)
			drive->Write(ATA_R_DATA, 0xE771, 16);
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		ASSERT_NO_FATAL_FAILURE(Flush());
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 13 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, CachedWriteIsVisibleToImmediateReadWithoutFlushOrAsyncPump)
	{
		const Bytes payload = Pattern(3, 0x34);
		ASSERT_NO_FATAL_FAILURE(WriteSectors(0x30, 19, payload, 1));
		Bytes actual;
		// WriteSectors pumps only while BSY. The cached completion may leave work
		// queued or in flight; the next command must observe it in either case.
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 19, 3, 1, &actual));
		EXPECT_EQ(actual, payload);
		ASSERT_NO_FATAL_FAILURE(Flush());
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 19 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, DisablingWriteCacheFlushesPriorAcknowledgedWrites)
	{
		const Bytes payload = Pattern(2, 0xDC);
		ASSERT_NO_FATAL_FAILURE(WriteSectors(0x30, 37, payload, 1));
		drive->Write(ATA_R_FEATURE, 0x82, 8);
		Command(0xEF);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		std::copy(payload.begin(), payload.end(), initial.begin() + 37 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial) << "separate reader sees bytes before ATA Close";
		EXPECT_EQ(Identify()[85] & (1 << 5), 0);
	}

	TEST_F(ATAImageTest, TruncatedBackingReadReturnsGuestErrorWithoutDataOrHostAbort)
	{
		std::error_code error;
		std::filesystem::resize_file(image, SectorSize * 2, error);
		if (error)
			GTEST_SKIP() << "platform does not permit resizing an open image: " << error.message();
		Address(4, 1);
		Command(0x20);
		ASSERT_TRUE(WaitReady());
		EXPECT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_ERR);
		EXPECT_NE(drive->Read(ATA_R_ERROR, 8), 0);
		EXPECT_TRUE(IRQ());
		EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF);
	}

	TEST_F(ATAImageTest, UnsupportedDataWidthsDoNotAdvanceEitherPioDirection)
	{
		const auto expected = Identify();
		Command(0xEC);
		Acknowledge();
		EXPECT_EQ(drive->Read(ATA_R_DATA, 8), 0xFFFF);
		EXPECT_EQ(drive->Read(ATA_R_DATA, 32), 0xFFFF);
		for (u16 word : expected)
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), word);
		const Bytes payload = Pattern(1, 0x72);
		Address(59, 1);
		Command(0x30);
		ASSERT_TRUE(WaitReady());
		drive->Write(ATA_R_DATA, 0xFFFF, 8);
		drive->Write(ATA_R_DATA, 0xFFFF, 32);
		for (size_t offset = 0; offset < payload.size(); offset += 2)
			drive->Write(ATA_R_DATA, payload[offset] | (payload[offset + 1] << 8), 16);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 59 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, PartialDmaResetDoesNotLeakIntoFreshShorterDmaWrite)
	{
		Bytes abandoned = Pattern(2, 0xD6);
		Address(7, 2);
		Command(0xCA);
		ASSERT_TRUE(WaitReady());
		ASSERT_TRUE(drive->dmaReady);
		ASSERT_EQ(drive->WriteDMAFromFIFO(abandoned.data(), 73), 73);
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		EXPECT_FALSE(drive->dmaReady);
		EXPECT_EQ(drive->WriteDMAFromFIFO(abandoned.data(), 73), 0);
		EXPECT_EQ(drive->ReadDMAToFIFO(abandoned.data(), 73), 0);
		Bytes payload = Pattern(1, 0xA2);
		Address(23, 1);
		Command(0xCA);
		ASSERT_TRUE(WaitReady());
		ASSERT_TRUE(drive->dmaReady);
		ASSERT_EQ(drive->WriteDMAFromFIFO(payload.data(), payload.size()), payload.size());
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		ASSERT_NO_FATAL_FAILURE(Flush());
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 23 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, DmaAlternateOpcodesTransferActualBytes)
	{
		Bytes payload = Pattern(2, 0x9C);
		Address(29, 2);
		Command(0xCB);
		ASSERT_TRUE(WaitReady());
		ASSERT_TRUE(drive->dmaReady);
		ASSERT_EQ(drive->WriteDMAFromFIFO(payload.data(), 133), 133);
		ASSERT_EQ(drive->WriteDMAFromFIFO(payload.data() + 133, payload.size() - 133), payload.size() - 133);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Address(29, 2);
		Command(0xC9);
		ASSERT_TRUE(WaitReady());
		ASSERT_TRUE(drive->dmaReady);
		Bytes actual(payload.size());
		ASSERT_EQ(drive->ReadDMAToFIFO(actual.data(), actual.size()), actual.size());
		EXPECT_EQ(actual, payload);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Close();
		std::copy(payload.begin(), payload.end(), initial.begin() + 29 * SectorSize);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, SelectingAbsentDeviceDoesNotExecuteCommandsOrClearPendingIrq)
	{
		const auto expected = Identify();
		Command(0xEC);
		ASSERT_TRUE(IRQ());
		// Device selection must remain possible while DRQ is asserted.
		drive->Write(ATA_R_SELECT, 0xF0, 8);
		EXPECT_EQ(Status(), 0);
		EXPECT_FALSE(IRQ());
		EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF);
		drive->Read(ATA_R_STATUS, 8);
		Command(0x30);
		drive->Write(ATA_R_DATA, 0xF00D, 16);
		drive->Write(ATA_R_SELECT, 0xE0, 8);
		EXPECT_TRUE(IRQ());
		EXPECT_NE(Status() & ATA_STAT_DRQ, 0);
		Acknowledge();
		for (u16 word : expected)
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), word);
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, NienMasksLineButRetainsPendingReadInterrupt)
	{
		drive->Write(ATA_R_CONTROL, 2, 8);
		Address(7, 1);
		Command(0x20);
		ASSERT_TRUE(WaitReady());
		ASSERT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_DRQ);
		EXPECT_FALSE(IRQ());
		drive->Write(ATA_R_CONTROL, 0, 8);
		EXPECT_TRUE(IRQ());
		drive->Read(ATA_R_ALT_STATUS, 8);
		EXPECT_TRUE(IRQ());
		Acknowledge();
		EXPECT_FALSE(IRQ());
		for (unsigned int i = 0; i < SectorSize / 2; ++i)
			drive->Read(ATA_R_DATA, 16);
	}

	TEST_F(ATAImageTest, SeekAcceptsLastSectorAndRejectsFirstSectorBeyondImage)
	{
		Address(599, 1);
		Command(0x70);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_NE(Status() & ATA_STAT_SEEK, 0);
		Acknowledge();
		Address(600, 1);
		Command(0x70);
		ASSERT_TRUE(WaitReady());
		EXPECT_NE(Status() & ATA_STAT_ERR, 0);
		EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ID, 0);
	}

	TEST_F(ATAImageTest, MultipleModeAcceptsSupportedPowersAndPreservesSettingOnAbort)
	{
		for (u8 count : {0, 2, 4, 8, 16, 32, 64, 128})
		{
			SCOPED_TRACE(static_cast<unsigned int>(count));
			ASSERT_NO_FATAL_FAILURE(SetMultiple(count));
			EXPECT_EQ(Identify()[59] & 0xFF, count);
		}
		for (u8 count : {1, 3, 129, 255})
		{
			SCOPED_TRACE(static_cast<unsigned int>(count));
			drive->Write(ATA_R_NSECTOR, count, 8);
			Command(0xC6);
			ASSERT_NO_FATAL_FAILURE(ExpectAbort());
			Acknowledge();
			EXPECT_EQ(Identify()[59] & 0xFF, 128);
		}
		ASSERT_NO_FATAL_FAILURE(SetMultiple(0));
		Address(10, 1);
		Command(0xC5);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
	}

	TEST_F(ATAImageTest, InitParametersUsesAllHeadBitsAndRejectsZeroSectors)
	{
		drive->Write(ATA_R_SELECT, 0xEF, 8);
		drive->Write(ATA_R_NSECTOR, 63, 8);
		Command(0x91);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		auto words = Identify();
		EXPECT_EQ(words[55], 16);
		EXPECT_EQ(words[56], 63);
		drive->Write(ATA_R_NSECTOR, 0, 8);
		Command(0x91);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		words = Identify();
		EXPECT_EQ(words[55], 16);
		EXPECT_EQ(words[56], 63);
		EXPECT_EQ(words[53] & 1, 0);
		for (u8 command : {0x20, 0x30, 0xC4, 0xC5})
		{
			Address(10, 1);
			Command(command);
			ASSERT_TRUE(WaitReady());
			EXPECT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_ERR);
			EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ID, 0);
			EXPECT_EQ(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ABORT, 0);
			Acknowledge();
		}
		Acknowledge();
		drive->Write(ATA_R_NSECTOR, 63, 8);
		Command(0x91);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 10, 1, 1, &actual));
		EXPECT_EQ(actual, Bytes(initial.begin() + 10 * SectorSize, initial.begin() + 11 * SectorSize));
	}

	TEST_F(ATAImageTest, ChsFirstAndLastSectorShareActualBackingBytesWithLba)
	{
		ASSERT_NO_FATAL_FAILURE(SetSmallChsGeometry());
		const auto words = Identify();
		EXPECT_EQ(words[54], 60);
		EXPECT_EQ(words[55], 1);
		EXPECT_EQ(words[56], 10);
		for (u32 lba : {0u, 599u})
		{
			const Bytes payload = Pattern(1, lba + 0x63);
			AddressChs(lba / 10, 0, lba % 10 + 1, 1);
			Command(0x30);
			ASSERT_TRUE(WaitReady());
			ASSERT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_DRQ);
			EXPECT_FALSE(IRQ());
			for (size_t offset = 0; offset < payload.size(); offset += 2)
				drive->Write(ATA_R_DATA, payload[offset] | (payload[offset + 1] << 8), 16);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 0);
			Acknowledge();
			AddressChs(lba / 10, 0, lba % 10 + 1, 1);
			Command(0x20);
			ASSERT_TRUE(WaitReady());
			ASSERT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_DRQ);
			Acknowledge();
			for (size_t offset = 0; offset < payload.size(); offset += 2)
				EXPECT_EQ(drive->Read(ATA_R_DATA, 16), payload[offset] | (payload[offset + 1] << 8));
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Bytes actual;
			ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, lba, 1, 1, &actual));
			EXPECT_EQ(actual, payload);
			std::copy(payload.begin(), payload.end(), initial.begin() + lba * SectorSize);
		}
		ASSERT_NO_FATAL_FAILURE(Flush());
		Close();
		EXPECT_EQ(DiskBytes(), initial);
		ASSERT_NO_FATAL_FAILURE(Open());
		ASSERT_NO_FATAL_FAILURE(SetSmallChsGeometry());
		AddressChs(59, 0, 10, 1);
		Command(0x20);
		ASSERT_TRUE(WaitReady());
		ASSERT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_DRQ);
		for (size_t offset = 599 * SectorSize; offset < initial.size(); offset += 2)
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), initial[offset] | (initial[offset + 1] << 8));
	}

	TEST_F(ATAImageTest, ChsInvalidSectorHeadOrCylinderReturnsIdNotFound)
	{
		ASSERT_NO_FATAL_FAILURE(SetSmallChsGeometry());
		constexpr std::array<std::array<u16, 3>, 4> addresses{{{0, 0, 0}, {0, 0, 11}, {0, 1, 1}, {60, 0, 1}}};
		for (const auto& address : addresses)
		{
			for (u8 command : {0x20, 0x30})
			{
				AddressChs(address[0], address[1], address[2], 1);
				Command(command);
				ASSERT_TRUE(WaitReady());
				EXPECT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_ERR);
				EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ID, 0);
				Acknowledge();
			}
		}
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, UnsupportedFeaturesAndReservedTransferModesAbort)
	{
		for (u8 feature : {0x01, 0x05, 0x31, 0x55, 0x5D, 0x5E, 0x81, 0x85, 0x95, 0xAA, 0xDD, 0xDE, 0xFF})
		{
			SCOPED_TRACE(static_cast<unsigned int>(feature));
			drive->Write(ATA_R_FEATURE, feature, 8);
			Command(0xEF);
			ASSERT_NO_FATAL_FAILURE(ExpectAbort());
			Acknowledge();
		}
		for (u8 mode : {0x01, 0x02, 0x07, 0x0D, 0x0F, 0x23, 0x27, 0x47})
		{
			SCOPED_TRACE(static_cast<unsigned int>(mode));
			drive->Write(ATA_R_FEATURE, 0x03, 8);
			drive->Write(ATA_R_NSECTOR, mode, 8);
			Command(0xEF);
			ASSERT_NO_FATAL_FAILURE(ExpectAbort());
			Acknowledge();
		}
	}

	TEST_F(ATAImageTest, TransferModeSelectorsAcceptOnlyImplementedValues)
	{
		for (unsigned int selector = 0; selector <= 0xFF; ++selector)
		{
			SCOPED_TRACE(selector);
			const bool supported = selector == 0 || (selector >= 0x08 && selector <= 0x0C) ||
				(selector >= 0x20 && selector <= 0x22) || (selector >= 0x40 && selector <= 0x46);
			drive->Write(ATA_R_FEATURE, 3, 8);
			drive->Write(ATA_R_NSECTOR, selector, 8);
			Command(0xEF);
			if (supported)
				ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			else
				ASSERT_NO_FATAL_FAILURE(ExpectAbort());
			Acknowledge();
		}
	}

	TEST_F(ATAImageTest, FeatureReversionControlsSoftwareResetOfVolatileSettings)
	{
		ASSERT_NO_FATAL_FAILURE(SetSmallChsGeometry());
		ASSERT_NO_FATAL_FAILURE(SetMultiple(8));
		for (const auto& setting : std::array<std::array<u8, 2>, 4>{{{3, 0x09}, {3, 0x21}, {0x82, 0}, {0x66, 0}}})
		{
			drive->Write(ATA_R_FEATURE, setting[0], 8);
			drive->Write(ATA_R_NSECTOR, setting[1], 8);
			Command(0xEF);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
		}
		drive->Write(ATA_R_NSECTOR, 1, 8);
		Command(0xE3);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		ASSERT_TRUE(WaitReady());
		auto words = Identify();
		EXPECT_EQ(words[55], 1);
		EXPECT_EQ(words[56], 10);
		EXPECT_EQ(words[59] & 0xFF, 8);
		EXPECT_EQ(words[51] >> 8, 1);
		EXPECT_EQ(words[63] & 0x0700, 0x0200);
		EXPECT_EQ(words[85] & (1 << 5), 0);
		drive->Async(36864000u * 5);
		ASSERT_NO_FATAL_FAILURE(CheckPower(0));

		drive->Write(ATA_R_FEATURE, 0xCC, 8);
		Command(0xEF);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		ASSERT_TRUE(WaitReady());
		words = Identify();
		EXPECT_EQ(words[55], 16);
		EXPECT_EQ(words[56], 63);
		EXPECT_EQ(words[59] & 0xFF, 128);
		EXPECT_EQ(words[51] >> 8, 2);
		EXPECT_EQ(words[63] & 0x0700, 0x0100);
		EXPECT_NE(words[85] & (1 << 5), 0);
		drive->Async(36864000u * 6);
		ASSERT_NO_FATAL_FAILURE(CheckPower(0xFF));
	}

	TEST_F(ATAImageTest, HardwareResetRestoresDefaultsAndReenablesFeatureReversion)
	{
		for (u8 feature : {0x82, 0x66})
		{
			drive->Write(ATA_R_FEATURE, feature, 8);
			Command(0xEF);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
		}
		drive->ATA_HardReset();
		ASSERT_TRUE(WaitReady());
		auto words = Identify();
		EXPECT_NE(words[85] & (1 << 5), 0);
		EXPECT_EQ(words[51] >> 8, 2);
		EXPECT_EQ(words[63] & 0x0700, 0x0100);
		EXPECT_EQ(words[88] & 0x7F00, 0);
		drive->Write(ATA_R_FEATURE, 0x82, 8);
		Command(0xEF);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		ASSERT_TRUE(WaitReady());
		EXPECT_NE(Identify()[85] & (1 << 5), 0);
	}

	TEST_F(ATAImageTest, SoftwareResetAlwaysPreservesSelectedUltraDmaMode)
	{
		for (u8 reversion : {0x66, 0xCC})
		{
			SCOPED_TRACE(static_cast<unsigned int>(reversion));
			for (const auto& setting : std::array<std::array<u8, 2>, 2>{{{3, 0x46}, {reversion, 0}}})
			{
				drive->Write(ATA_R_FEATURE, setting[0], 8);
				drive->Write(ATA_R_NSECTOR, setting[1], 8);
				Command(0xEF);
				ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
				Acknowledge();
			}
			drive->Write(ATA_R_CONTROL, 4, 8);
			drive->Write(ATA_R_CONTROL, 0, 8);
			ASSERT_TRUE(WaitReady());
			const auto words = Identify();
			EXPECT_EQ(words[63] & 0x0700, 0);
			EXPECT_EQ(words[88] & 0x7F00, 0x4000);
		}
	}

	TEST_F(ATAImageTest, PioModeSelectionPreservesDmaAndAdvertisesOnlySupportedModes)
	{
		for (u8 dma : {0x20, 0x21, 0x22, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46})
		{
			SCOPED_TRACE(static_cast<unsigned int>(dma));
			drive->Write(ATA_R_FEATURE, 3, 8);
			drive->Write(ATA_R_NSECTOR, dma, 8);
			Command(0xEF);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
			const auto before = Identify();
			const unsigned int dmaWord = dma < 0x40 ? 63 : 88;
			EXPECT_NE(before[dmaWord] & (1 << (8 + (dma & 7))), 0);
			for (u8 pio : {0x00, 0x08, 0x09, 0x0A, 0x0B, 0x0C})
			{
				drive->Write(ATA_R_FEATURE, 3, 8);
				drive->Write(ATA_R_NSECTOR, pio, 8);
				Command(0xEF);
				ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
				Acknowledge();
				const auto after = Identify();
				EXPECT_EQ(after[63], before[63]);
				EXPECT_EQ(after[88], before[88]);
				EXPECT_EQ(after[51] >> 8, pio == 0 ? 2 : std::min(pio & 7, 2));
			}
		}
	}

	// Prospective HPA regressions: source first; compilation/execution is prohibited.
	class ATAHpaImageTest : public ATAImageTest
	{
	protected:
		void SetUp() override
		{
			ATAImageTest::SetUp();
			ASSERT_FALSE(HasFatalFailure());
			Close();
			initial = Pattern(6048, 39); // Six complete native 16-head/63-sector cylinders.
			ASSERT_NO_FATAL_FAILURE(CreateImage(initial));
			ASSERT_NO_FATAL_FAILURE(Open());
		}

		void Native(bool chs = false)
		{
			drive->Write(ATA_R_SELECT, chs ? 0xA0 : 0xE0, 8);
			Command(0xF8);
		}

		void Limit(u32 sectors, bool nonvolatile = false)
		{
			Native();
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
			Address(sectors - 1, nonvolatile ? 1 : 0); // Rev18 bit0=1 preserves the limit.
			Command(0xF9);
		}

		u32 Capacity()
		{
			const auto words = Identify();
			return u32(words[60]) | (u32(words[61]) << 16);
		}

		void SoftReset()
		{
			drive->Write(ATA_R_CONTROL, 4, 8);
			drive->Write(ATA_R_CONTROL, 0, 8);
		}
	};

	TEST_F(ATAHpaImageTest, NativeMaximumRemainsNativeAndLimitOnlyHidesSectors)
	{
		const auto before = Identify();
		EXPECT_NE(before[82] & (1 << 10), 0);
		EXPECT_EQ(before[83] & (1 << 8), 0); // No later SET MAX security extension.
		Limit(2016);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_TRUE(IRQ());
		Acknowledge();
		EXPECT_EQ(TaskLba(), 2015u);
		const auto after = Identify();
		EXPECT_EQ(after[1], 2);
		EXPECT_EQ(after[3], before[3]);
		EXPECT_EQ(after[6], before[6]);
		EXPECT_EQ(after[54], 2);
		EXPECT_EQ(after[57], 2016);
		EXPECT_EQ(Capacity(), 2016u);
		Native();
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_EQ(TaskLba(), 6047u);
		Acknowledge();
		Bytes read;
		ReadSectors(0x20, 2015, 1, 1, &read);
		EXPECT_EQ(read, Bytes(initial.begin() + 2015 * SectorSize, initial.begin() + 2016 * SectorSize));
		for (u8 command : {0x20, 0x30, 0x40, 0xC8, 0xCA})
		{
			Address(2016, 1);
			Command(command);
			ASSERT_TRUE(WaitReady());
			EXPECT_NE(Status() & ATA_STAT_ERR, 0);
			EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ID, 0);
			Acknowledge();
		}
		Close();
		EXPECT_EQ(DiskBytes(), initial);
		EXPECT_EQ(std::filesystem::file_size(image), initial.size());
	}

	TEST_F(ATAHpaImageTest, SuccessfulNativeCommandMustImmediatelyPrecedeEachSet)
	{
		Address(2015, 0);
		Command(0xF9);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		Native();
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Identify(); // An intervening command consumes the authorization.
		Address(2015, 0);
		Command(0xF9);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		Limit(2016);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Address(3023, 0);
		Command(0xF9); // The same successful F8 cannot authorize a second F9.
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		EXPECT_EQ(Capacity(), 2016u);
	}

	TEST_F(ATAHpaImageTest, InvalidLimitDoesNotChangeIdentityOrNativeCapacity)
	{
		const auto before = Identify();
		Limit(6049);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		EXPECT_EQ(Identify(), before);
		Native();
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		AddressChs(16384, 15, 63, 0);
		Command(0xF9);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		EXPECT_EQ(Identify(), before);
	}

	TEST_F(ATAHpaImageTest, VolatileLimitSurvivesSoftwareResetAndHardResetRestoresNative)
	{
		Limit(2016);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		SoftReset();
		EXPECT_EQ(Capacity(), 2016u);
		drive->ATA_HardReset();
		EXPECT_EQ(Capacity(), 6048u);
		EXPECT_FALSE(std::filesystem::exists(image.string() + ".ata-hpa"));
	}

	TEST_F(ATAHpaImageTest, NonvolatileLimitPersistsAndQuotaResetsOnlyOnHardwareReset)
	{
		Limit(2016, true);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Limit(3024); // A volatile expansion leaves the saved limit intact.
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		SoftReset();
		EXPECT_EQ(Capacity(), 3024u);
		Limit(4032, true);
		ASSERT_TRUE(WaitReady());
		EXPECT_NE(Status() & ATA_STAT_ERR, 0);
		EXPECT_EQ(drive->Read(ATA_R_ERROR, 8), ATA_ERR_ID);
		Acknowledge();
		EXPECT_EQ(Capacity(), 3024u);
		drive->ATA_HardReset();
		EXPECT_EQ(Capacity(), 2016u);
		Limit(4032, true);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Close();
		ASSERT_NO_FATAL_FAILURE(Open());
		EXPECT_EQ(Capacity(), 4032u);
		Limit(6048, true); // Reopen is a new power cycle.
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAHpaImageTest, ChsUsesNativeHeadAndSectorFieldsAfterTranslationChange)
	{
		SetSmallChsGeometry(); // Current translation is 1 head, 10 sectors.
		Native(true);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_EQ(drive->Read(ATA_R_LCYL, 8), 5);
		EXPECT_EQ(drive->Read(ATA_R_HCYL, 8), 0);
		EXPECT_EQ(drive->Read(ATA_R_SELECT, 8) & 15, 15);
		EXPECT_EQ(drive->Read(ATA_R_SECTOR, 8), 63);
		Acknowledge();
		AddressChs(1, 15, 63, 0);
		Command(0xF9);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		const auto words = Identify();
		EXPECT_EQ(words[1], 2);
		EXPECT_EQ(words[54], 201);
		EXPECT_EQ(words[55], 1);
		EXPECT_EQ(words[56], 10);
		EXPECT_EQ(Capacity(), 2016u);
		for (auto invalid : {std::array<u8, 2>{14, 63}, std::array<u8, 2>{15, 62}})
		{
			Native(true);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
			AddressChs(1, invalid[0], invalid[1], 0);
			Command(0xF9);
			ASSERT_NO_FATAL_FAILURE(ExpectAbort());
			Acknowledge();
		}
	}

	TEST_F(ATAHpaImageTest, OneSectorLimitRemainsUsableAndDoesNotAlterNativeGeometry)
	{
		Limit(1);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		const auto words = Identify();
		EXPECT_EQ(words[1], 0);
		EXPECT_EQ(words[3], 16);
		EXPECT_EQ(words[6], 63);
		EXPECT_EQ(Capacity(), 1u);
		Bytes read;
		ReadSectors(0x20, 0, 1, 1, &read);
		EXPECT_EQ(read, Bytes(initial.begin(), initial.begin() + SectorSize));
		Limit(6048);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_EQ(Capacity(), 6048u);
	}

	TEST_F(ATAHpaImageTest, FailedMetadataCommitDoesNotChangeLimitOrConsumeQuota)
	{
		const std::filesystem::path temporary = image.string() + ".ata-hpa.tmp";
		ASSERT_TRUE(std::filesystem::create_directory(temporary));
		Limit(2016, true);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		EXPECT_EQ(Capacity(), 6048u);
		EXPECT_FALSE(std::filesystem::exists(image.string() + ".ata-hpa"));
		ASSERT_TRUE(std::filesystem::remove(temporary));
		Limit(3024, true);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		EXPECT_EQ(Capacity(), 3024u);
	}

	TEST_F(ATAHpaImageTest, CorruptOrTrailingMetadataRefusesOpenWithoutChangingImage)
	{
		Limit(2016, true);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Close();
		const auto metadata = image.string() + ".ata-hpa";
		std::ifstream source(metadata, std::ios::binary);
		const Bytes good(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>{});
		source.close();
		ASSERT_FALSE(good.empty());
		for (int corruption : {0, 1, 2})
		{
			Bytes bad = good;
			if (corruption == 0)
				bad[0] ^= 1;
			else if (corruption == 1)
				bad.back() ^= 1;
			else
				bad.push_back(0);
			std::ofstream file(metadata, std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(bad.data()), bad.size());
			file.close();
			drive = std::make_unique<ATA>();
			EXPECT_NE(drive->Open(image.string()), 0);
			Close();
			EXPECT_EQ(DiskBytes(), initial);
		}
	}

	TEST_F(ATAHpaImageTest, ChangedNativeImageSizeRefusesSavedLimit)
	{
		Limit(2016, true);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Close();
		std::filesystem::resize_file(image, initial.size() + SectorSize);
		drive = std::make_unique<ATA>();
		EXPECT_NE(drive->Open(image.string()), 0);
		Close();
		EXPECT_EQ(std::filesystem::file_size(image), initial.size() + SectorSize);
	}

	TEST_F(ATAHpaImageTest, FailedNativeReadDoesNotAuthorizeSetMax)
	{
		Close();
		initial = Pattern(600, 29); // No complete default CHS cylinder.
		ASSERT_NO_FATAL_FAILURE(CreateImage(initial));
		ASSERT_NO_FATAL_FAILURE(Open());
		Native();
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Native(true);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		Address(299, 0);
		Command(0xF9);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		EXPECT_EQ(Capacity(), 600u);
	}

	TEST_F(ATAHpaImageTest, NotApplicableFeatureAndCountBitsDoNotBecomeSecurityCommands)
	{
		Native();
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Address(2015, 0xFE); // Only bit0 applies: this is volatile.
		drive->Write(ATA_R_FEATURE, 0xFF, 8); // Features is not applicable in ATA-4.
		Command(0xF9);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		EXPECT_EQ(Capacity(), 2016u);
		drive->ATA_HardReset();
		EXPECT_EQ(Capacity(), 6048u);
	}

	TEST_F(ATAHpaImageTest, Lba48ProfileDefersHpaWithoutReducingNativeCapacity)
	{
		Close();
		const u64 sectors = (1ULL << 28) + 4096;
		std::filesystem::resize_file(image, sectors * SectorSize); // Disposable sparse image.
		ASSERT_NO_FATAL_FAILURE(Open());
		const auto before = Identify();
		EXPECT_EQ(before[82] & (1 << 10), 0);
		EXPECT_EQ(before[85] & (1 << 10), 0);
		EXPECT_NE(before[83] & (1 << 10), 0);
		for (u8 command : {0xF8, 0xF9, 0x27, 0x37})
		{
			Address(2015, 0);
			Command(command);
			ASSERT_NO_FATAL_FAILURE(ExpectAbort());
			Acknowledge();
		}
		EXPECT_EQ(Identify(), before);
		EXPECT_FALSE(std::filesystem::exists(image.string() + ".ata-hpa"));
		Close();
		EXPECT_EQ(std::filesystem::file_size(image), sectors * SectorSize);
	}

	// Prospective SMART regressions: no compiler or C++ execution is authorized.
	class ATASmartImageTest : public ATAImageTest
	{
	protected:
		void Smart(u8 feature, u8 sector = 0, u8 count = 0)
		{
			drive->Write(ATA_R_FEATURE, feature, 8);
			drive->Write(ATA_R_SECTOR, sector, 8);
			drive->Write(ATA_R_NSECTOR, count, 8);
			drive->Write(ATA_R_LCYL, 0x4F, 8);
			drive->Write(ATA_R_HCYL, 0xC2, 8);
			Command(0xB0);
		}

		Bytes SmartData()
		{
			Smart(0xD0);
			EXPECT_TRUE(WaitReady());
			EXPECT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_DRQ);
			EXPECT_TRUE(IRQ());
			Acknowledge();
			Bytes data(SectorSize);
			for (size_t word = 0; word < SectorSize / 2; ++word)
			{
				const u16 value = drive->Read(ATA_R_DATA, 16);
				data[word * 2] = value & 0xFF;
				data[word * 2 + 1] = value >> 8;
			}
			EXPECT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), 0);
			EXPECT_FALSE(IRQ());
			return data;
		}

		static u64 SmartValue(const Bytes& data, size_t offset, size_t count)
		{
			u64 value = 0;
			for (size_t i = 0; i < count; ++i)
				value |= u64(data[offset + i]) << (i * 8);
			return value;
		}
	};

	TEST_F(ATASmartImageTest, ReadDataHasChecksumHonestCapabilitiesAndNoPhysicalAttributes)
	{
		const Bytes data = SmartData();
		u8 checksum = 0;
		for (u8 byte : data)
			checksum += byte;
		EXPECT_EQ(checksum, 0);
		EXPECT_EQ(data[362], 0); // Never scanned, rather than fabricated success.
		EXPECT_EQ(data[367], 1); // Offline only; no later self-test/log claims.
		EXPECT_EQ(SmartValue(data, 368, 2), 2); // Event autosave, no power-mode claim.
		for (size_t i = 2; i < 362; ++i)
			EXPECT_EQ(data[i], 0); // No invented temperature or physical attributes.
		for (size_t i = 370; i < 386; ++i)
			EXPECT_EQ(data[i], 0);
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATASmartImageTest, OfflineScanReadsBoundedChunksWithoutChangingGuestTaskfile)
	{
		Smart(0xD4, 0xA5, 0x37); // ATA-4 Sector Number is not applicable.
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		const u32 before = TaskLba();
		const auto irqBefore = ATAHarness::irqCount;
		drive->Async(36864000);
		EXPECT_EQ(TaskLba(), before);
		EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 0x37);
		EXPECT_EQ(ATAHarness::irqCount, irqBefore);
		const Bytes data = SmartData();
		EXPECT_GT(SmartValue(data, 416, 8), 0);
		EXPECT_LE(SmartValue(data, 416, 8), 128);
		EXPECT_NE(data[362], 2); // 600 sectors cannot finish in one bounded pump.
	}

	TEST_F(ATASmartImageTest, OfflineCompletionPersistsEvenWhenAttributeAutosaveDisabled)
	{
		Smart(0xD2, 0, 0);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Smart(0xD4);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		for (unsigned int chunk = 0; chunk < 8; ++chunk)
			drive->Async(36864000);
		const Bytes complete = SmartData();
		EXPECT_EQ(complete[362], 2);
		EXPECT_EQ(SmartValue(complete, 404, 4), 1);
		EXPECT_EQ(SmartValue(complete, 416, 8), initial.size() / SectorSize);
		Close();
		ASSERT_NO_FATAL_FAILURE(Open());
		EXPECT_EQ(SmartData(), complete);
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATASmartImageTest, OfflineReadFailureIsPersistedAndReportedThroughStatus)
	{
		std::filesystem::resize_file(image, SectorSize);
		Smart(0xD4);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		drive->Async(36864000);
		const Bytes failed = SmartData();
		EXPECT_EQ(failed[362], 6);
		EXPECT_EQ(SmartValue(failed, 408, 4), 1);
		EXPECT_LT(SmartValue(failed, 424, 8), initial.size() / SectorSize);
		Smart(0xDA);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_EQ(drive->Read(ATA_R_LCYL, 8), 0xF4);
		EXPECT_EQ(drive->Read(ATA_R_HCYL, 8), 0x2C);
		Acknowledge();
		Close();
		ASSERT_NO_FATAL_FAILURE(Open());
		EXPECT_EQ(SmartData()[362], 6);
	}

	TEST_F(ATASmartImageTest, SaveAttributesPersistsRealInterruptedScanState)
	{
		Smart(0xD2, 0, 0);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Smart(0xD4);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		drive->Async(36864000);
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		Smart(0xD3);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		const Bytes saved = SmartData();
		EXPECT_EQ(saved[362], 5);
		EXPECT_EQ(SmartValue(saved, 400, 4), 1);
		EXPECT_GT(SmartValue(saved, 416, 8), 0);
		Close();
		ASSERT_NO_FATAL_FAILURE(Open());
		EXPECT_EQ(SmartData(), saved);
	}

	TEST_F(ATASmartImageTest, SaveAttributesAbortsWithoutTruncatingExistingTemporaryFile)
	{
		const std::string temporary = image.string() + ".ata-smart.tmp";
		{
			std::ofstream file(temporary, std::ios::binary);
			file << "preserve-smart-sentinel";
		}
		Smart(0xD3);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		std::ifstream file(temporary, std::ios::binary);
		const std::string actual{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		EXPECT_EQ(actual, "preserve-smart-sentinel");
	}

	TEST_F(ATASmartImageTest, StandbySuspendsAndMediaCommandResumesOfflineScan)
	{
		Smart(0xD4);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		drive->Async(36864000);
		Command(0xE0);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		for (unsigned int chunk = 0; chunk < 8; ++chunk)
			drive->Async(36864000);
		Bytes media;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 0, 1, 1, &media));
		const Bytes suspended = SmartData();
		EXPECT_EQ(suspended[362], 4);
		EXPECT_LT(SmartValue(suspended, 416, 8), initial.size() / SectorSize);
		for (unsigned int chunk = 0; chunk < 8; ++chunk)
			drive->Async(36864000);
		EXPECT_EQ(SmartData()[362], 2);
	}

	TEST_F(ATASmartImageTest, DisableAndResetAbortOfflineWithoutFabricatedSuccess)
	{
		Smart(0xD4);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Smart(0xD9);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		for (unsigned int chunk = 0; chunk < 8; ++chunk)
			drive->Async(36864000);
		Smart(0xD8);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		EXPECT_EQ(SmartData()[362], 5);
		Smart(0xD4);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		EXPECT_EQ(SmartData()[362], 5);
	}

	TEST_F(ATASmartImageTest, ResetAutosaveIsControlledByPersistedD2Setting)
	{
		for (u8 count : {0x00, 0xF1})
		{
			Smart(0xD2, 0, count);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
			Smart(0xD4);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
			drive->Async(36864000);
			drive->Write(ATA_R_CONTROL, 4, 8);
			drive->Write(ATA_R_CONTROL, 0, 8);
			EXPECT_EQ(std::filesystem::exists(image.string() + ".ata-smart"), count == 0xF1);
		}
	}

	TEST_F(ATASmartImageTest, ChecksumCorruptionInFullSizeSmartStateRejectsOpen)
	{
		Smart(0xD3);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Close();
		const std::string sidecar = image.string() + ".ata-smart";
		ASSERT_EQ(std::filesystem::file_size(sidecar), SectorSize);
		{
			std::fstream file(sidecar, std::ios::binary | std::ios::in | std::ios::out);
			ASSERT_TRUE(file.is_open());
			file.seekp(400);
			file.put(0x7F);
		}
		drive = std::make_unique<ATA>();
		EXPECT_NE(drive->Open(image.string()), 0);
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATASmartImageTest, CorruptSmartStateRejectsOpenWithoutChangingImage)
	{
		Close();
		std::ofstream file(image.string() + ".ata-smart", std::ios::binary);
		file << "invalid-smart-state";
		file.close();
		drive = std::make_unique<ATA>();
		EXPECT_NE(drive->Open(image.string()), 0);
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, SmartAutosaveUsesSectorCountRatherThanSectorNumber)
	{
		for (u8 count : {0x00, 0xF1, 0x7E})
		{
			drive->Write(ATA_R_FEATURE, 0xD2, 8);
			drive->Write(ATA_R_NSECTOR, count, 8);
			drive->Write(ATA_R_SECTOR, count == 0x7E ? 0 : 0x7E, 8);
			drive->Write(ATA_R_LCYL, 0x4F, 8);
			drive->Write(ATA_R_HCYL, 0xC2, 8);
			Command(0xB0);
			if (count == 0x7E)
				ASSERT_NO_FATAL_FAILURE(ExpectAbort());
			else
				ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
		}
	}

	TEST_F(ATAImageTest, SmartEnableSettingPersistsAcrossCloseAndReopen)
	{
		EXPECT_NE(Identify()[85] & 1, 0);
		drive->Write(ATA_R_FEATURE, 0xD9, 8);
		drive->Write(ATA_R_LCYL, 0x4F, 8);
		drive->Write(ATA_R_HCYL, 0xC2, 8);
		Command(0xB0);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Close();
		ASSERT_NO_FATAL_FAILURE(Open());
		EXPECT_EQ(Identify()[85] & 1, 0);
		drive->Write(ATA_R_FEATURE, 0xDA, 8);
		drive->Write(ATA_R_LCYL, 0x4F, 8);
		drive->Write(ATA_R_HCYL, 0xC2, 8);
		Command(0xB0);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		drive->Write(ATA_R_FEATURE, 0xD8, 8);
		Command(0xB0);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Close();
		ASSERT_NO_FATAL_FAILURE(Open());
		EXPECT_NE(Identify()[85] & 1, 0);
	}

	TEST_F(ATAImageTest, SmartAutosavePersistsInVersionedSidecar)
	{
		for (u8 count : {0x00, 0xF1})
		{
			drive->Write(ATA_R_FEATURE, 0xD2, 8);
			drive->Write(ATA_R_NSECTOR, count, 8);
			drive->Write(ATA_R_LCYL, 0x4F, 8);
			drive->Write(ATA_R_HCYL, 0xC2, 8);
			Command(0xB0);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
			Close();
			std::ifstream file(image.string() + ".ata-settings", std::ios::binary);
			ASSERT_TRUE(file.is_open());
			const Bytes actual{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
			const Bytes expected{'P', 'C', 'S', 'X', '2', 'A', 1, u8(count == 0 ? 1 : 3)};
			EXPECT_EQ(actual, expected);
			file.close();
			ASSERT_NO_FATAL_FAILURE(Open());
		}
	}

	TEST_F(ATAImageTest, CorruptExistingSettingsRejectOpenWithoutChangingImage)
	{
		Close();
		std::ofstream file(image.string() + ".ata-settings", std::ios::binary);
		file << "invalid-settings";
		file.close();
		ASSERT_TRUE(file.good());
		drive = std::make_unique<ATA>();
		EXPECT_NE(drive->Open(image.string()), 0);
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, ExistingSettingsTemporaryFileIsPreservedAndCommandAborts)
	{
		const std::string temporary = image.string() + ".ata-settings.tmp";
		{
			std::ofstream file(temporary, std::ios::binary);
			file << "preexisting-sentinel";
			file.close();
			ASSERT_TRUE(file.good());
		}
		drive->Write(ATA_R_FEATURE, 0xD9, 8);
		drive->Write(ATA_R_LCYL, 0x4F, 8);
		drive->Write(ATA_R_HCYL, 0xC2, 8);
		Command(0xB0);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		EXPECT_NE(Identify()[85] & 1, 0);
		std::ifstream file(temporary, std::ios::binary);
		const std::string actual{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		EXPECT_EQ(actual, "preexisting-sentinel");
	}

	TEST_F(ATAImageTest, PowerImmediateCommandsReportIdleStandbyAndWakeForMedia)
	{
		ASSERT_NO_FATAL_FAILURE(CheckPower(0xFF));
		Command(0xE1);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		ASSERT_NO_FATAL_FAILURE(CheckPower(0x80));
		Command(0xE0);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		ASSERT_NO_FATAL_FAILURE(CheckPower(0));
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 7, 1, 1, &actual));
		ASSERT_NO_FATAL_FAILURE(CheckPower(0xFF));
	}

	TEST_F(ATAImageTest, IdleFiveSecondTimerIsNotRestartedByCheckPower)
	{
		drive->Write(ATA_R_NSECTOR, 1, 8);
		Command(0xE3);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		drive->Async(36864000u * 4);
		ASSERT_NO_FATAL_FAILURE(CheckPower(0x80));
		drive->Async(36864000u);
		ASSERT_NO_FATAL_FAILURE(CheckPower(0));
	}

	TEST_F(ATAImageTest, StandbyTimerEncodingsExpireAtProgrammedBoundary)
	{
		constexpr std::array<std::array<u32, 2>, 8> settings{{
			{1, 5}, {240, 1200}, {241, 1800}, {251, 19800},
			{252, 1260}, {253, 36000}, {255, 1275}, {0, 0}}};
		for (const auto& setting : settings)
		{
			SCOPED_TRACE(setting[0]);
			drive->Write(ATA_R_NSECTOR, setting[0], 8);
			Command(0xE3);
			ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
			Acknowledge();
			if (setting[1] == 0)
			{
				drive->Async(36864000u * 6);
				ASSERT_NO_FATAL_FAILURE(CheckPower(0x80));
				continue;
			}
			u64 remaining = static_cast<u64>(setting[1]) * 36864000 - 1;
			while (remaining != 0)
			{
				// UINT32_MAX is the internal no-time completion-pump sentinel.
				const u32 cycles = static_cast<u32>(std::min<u64>(remaining, 0xFFFFFFFEu));
				drive->Async(cycles);
				remaining -= cycles;
			}
			ASSERT_NO_FATAL_FAILURE(CheckPower(0x80));
			drive->Async(1);
			ASSERT_NO_FATAL_FAILURE(CheckPower(0));
		}
	}

	TEST_F(ATAImageTest, StandbyTimerRestartsForMediaAndRejectsReservedCount)
	{
		drive->Write(ATA_R_NSECTOR, 1, 8);
		Command(0xE2);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		ASSERT_NO_FATAL_FAILURE(CheckPower(0));
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 11, 1, 1, &actual));
		drive->Async(36864000u * 4);
		ASSERT_NO_FATAL_FAILURE(CheckPower(0xFF));
		drive->Async(36864000u);
		ASSERT_NO_FATAL_FAILURE(CheckPower(0));
		drive->Write(ATA_R_NSECTOR, 254, 8);
		Command(0xE3);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
	}

	TEST_F(ATAImageTest, SleepIgnoresCommandsUntilSrst)
	{
		Command(0xE6);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		const auto count = drive->Read(ATA_R_NSECTOR, 8);
		const auto irqCount = ATAHarness::irqCount;
		Address(7, 1);
		Command(0x20);
		Command(0xE5);
		EXPECT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), 0);
		EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), count);
		EXPECT_EQ(ATAHarness::irqCount, irqCount);
		drive->Write(ATA_R_CONTROL, 4, 8);
		EXPECT_NE(Status() & ATA_STAT_BUSY, 0);
		drive->Write(ATA_R_CONTROL, 0, 8);
		ASSERT_TRUE(WaitReady());
		ASSERT_NO_FATAL_FAILURE(CheckPower(0));
		Bytes actual;
		ASSERT_NO_FATAL_FAILURE(ReadSectors(0x20, 7, 1, 1, &actual));
		EXPECT_EQ(actual, Bytes(initial.begin() + 7 * SectorSize, initial.begin() + 8 * SectorSize));
	}

	TEST_F(ATAImageTest, IdentifyDoesNotAdvertiseUnavailableSmartOrExtendedFlush)
	{
		const auto words = Identify();
		EXPECT_EQ(words[84] & 3, 0);
		EXPECT_EQ(words[87] & 3, 0);
		EXPECT_EQ(words[83] & (1 << 13), 0);
		EXPECT_EQ(words[86] & (1 << 13), 0);
		EXPECT_EQ(words[88] & 0x7F, 0x7F);
		EXPECT_EQ(words[49] & (1 << 10), 0);
	}

#if defined(__POSIX__)
	// POSIX truncate extends a file with holes, without allocating a multi-TiB
	// image. Do not enable these fixtures on Windows without sparse-file setup.
	TEST_F(ATAImageTest, ExtendedPioWritesPersistWithHighLbaAndHighSectorCount)
	{
		Close();
		const u64 sectors = (u64(1) << 28) + 1024;
		std::error_code error;
		std::filesystem::resize_file(image, sectors * SectorSize, error);
		if (error)
			GTEST_SKIP() << "filesystem does not support this sparse logical size: " << error.message();
		ASSERT_NO_FATAL_FAILURE(Open());
		for (u8 command : {0x34, 0x39})
		{
			const u32 lba = (u32(1) << 28) + (command == 0x34 ? 3 : 400);
			const size_t count = command == 0x34 ? 257 : 130;
			const unsigned int block = command == 0x34 ? 1 : 128;
			const Bytes payload = Pattern(count, command);
			ASSERT_NO_FATAL_FAILURE(WriteSectors(command, lba, payload, block, true));
			ASSERT_NO_FATAL_FAILURE(Flush());
			Close();
			std::ifstream file(image, std::ios::binary);
			file.seekg(u64(lba) * SectorSize);
			Bytes disk(payload.size());
			file.read(reinterpret_cast<char*>(disk.data()), disk.size());
			ASSERT_EQ(file.gcount(), disk.size());
			EXPECT_EQ(disk, payload);
			file.close();
			ASSERT_NO_FATAL_FAILURE(Open());
			Bytes actual;
			ASSERT_NO_FATAL_FAILURE(ReadSectors(command == 0x34 ? 0x24 : 0x29, lba, count, block, &actual, true));
			EXPECT_EQ(actual, payload);
		}
		EXPECT_EQ(std::filesystem::file_size(image), sectors * SectorSize);
	}

	TEST_F(ATAImageTest, LargeSparseIdentifySaturatesLba28CapacityBeforeNarrowing)
	{
		Close();
		const u64 sectors = (u64(1) << 32) + 7;
		std::error_code error;
		std::filesystem::resize_file(image, sectors * SectorSize, error);
		if (error)
			GTEST_SKIP() << "filesystem does not support this sparse logical size: " << error.message();
		ASSERT_NO_FATAL_FAILURE(Open());
		const auto words = Identify();
		EXPECT_EQ(u32(words[60]) | (u32(words[61]) << 16), 0x0FFFFFFFu);
		EXPECT_EQ(u64(words[100]) | (u64(words[101]) << 16) | (u64(words[102]) << 32) | (u64(words[103]) << 48), sectors);
		EXPECT_NE(words[83] & (1 << 13), 0);
		EXPECT_NE(words[86] & (1 << 13), 0);
	}

	TEST_F(ATAImageTest, DmaReadGrowsPast256SectorsAndReturnsRealImageBytes)
	{
		Close();
		std::error_code error;
		std::filesystem::resize_file(image, ((u64(1) << 28) + 1) * SectorSize, error);
		if (error)
			GTEST_SKIP() << "filesystem does not support this sparse logical size: " << error.message();
		ASSERT_NO_FATAL_FAILURE(Open());
		drive->Write(ATA_R_SELECT, 0xE0, 8);
		// LBA48 high task file first, followed by low task file: count 0x0101.
		drive->Write(ATA_R_NSECTOR, 1, 8);
		drive->Write(ATA_R_SECTOR, 0, 8);
		drive->Write(ATA_R_LCYL, 0, 8);
		drive->Write(ATA_R_HCYL, 0, 8);
		drive->Write(ATA_R_NSECTOR, 1, 8);
		drive->Write(ATA_R_SECTOR, 0, 8);
		drive->Write(ATA_R_LCYL, 0, 8);
		drive->Write(ATA_R_HCYL, 0, 8);
		Command(0x25);
		ASSERT_TRUE(WaitReady());
		ASSERT_TRUE(drive->dmaReady);
		ASSERT_EQ(Status() & (ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_DRQ);
		Bytes actual(257 * SectorSize);
		ASSERT_EQ(drive->ReadDMAToFIFO(actual.data(), 113), 113);
		ASSERT_EQ(drive->ReadDMAToFIFO(actual.data() + 113, actual.size() - 113), actual.size() - 113);
		EXPECT_EQ(actual, Bytes(initial.begin(), initial.begin() + actual.size()));
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_FALSE(drive->dmaReady);
		EXPECT_TRUE(IRQ());
	}
#endif

	TEST_F(ATAImageTest, DiagnosticBufferRoundTripTransfersExactlyOneSectorWithoutDiskWrites)
	{
		const Bytes payload = Pattern(1, 0xD7);
		// Buffer commands ignore sector count and address, including an invalid LBA.
		Address(0x0FFFFFFF, 0);
		const auto interrupts = ATAHarness::irqCount;
		Command(0xE8);
		ASSERT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_DRQ);
		EXPECT_FALSE(IRQ());
		for (size_t i = 0; i < payload.size(); i += 2)
		{
			drive->Write(ATA_R_DATA, payload[i] | (payload[i + 1] << 8), 16);
			if (i + 2 != payload.size())
			{
				ASSERT_NE(Status() & ATA_STAT_DRQ, 0);
				ASSERT_FALSE(IRQ());
			}
		}
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_TRUE(IRQ());
		Acknowledge();
		drive->Write(ATA_R_DATA, 0xBAD0, 16); // Past the accepted sector: no effect.
		Command(0xE4);
		ASSERT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_ERR), ATA_STAT_DRQ);
		EXPECT_TRUE(IRQ());
		Acknowledge();
		for (size_t i = 0; i < payload.size(); i += 2)
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), payload[i] | (payload[i + 1] << 8));
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_FALSE(IRQ());
		EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF);
		EXPECT_EQ(ATAHarness::irqCount - interrupts, 2u);
		EXPECT_EQ(TaskLba(), 0x0FFFFFFFu);
		EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 0);
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, ResetCancelsPartialDiagnosticBufferWithoutWritingImage)
	{
		Command(0xE4);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort()); // No accepted WRITE BUFFER yet.
		Acknowledge();
		Command(0xE8);
		ASSERT_NE(Status() & ATA_STAT_DRQ, 0);
		for (int i = 0; i < 73; ++i)
			drive->Write(ATA_R_DATA, 0xCAFE, 16);
		drive->Write(ATA_R_CONTROL, 4, 8);
		drive->Write(ATA_R_CONTROL, 0, 8);
		Command(0xE4);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
		Command(0xE8);
		for (int i = 0; i < 256; ++i)
			drive->Write(ATA_R_DATA, static_cast<u16>(i ^ 0xA55A), 16);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Acknowledge();
		Command(0xE4);
		for (int i = 0; i < 256; ++i)
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), static_cast<u16>(i ^ 0xA55A));
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		Close();
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, DiagnosticBufferIrqMasksAndUnsupportedWidthsPreserveData)
	{
		drive->Write(ATA_R_CONTROL, 2, 8);
		Command(0xE8);
		drive->Write(ATA_R_DATA, 0xFF, 8);
		EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF); // Wrong direction.
		for (int i = 0; i < 256; ++i)
			drive->Write(ATA_R_DATA, static_cast<u16>(i + 1), 16);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_FALSE(IRQ());
		drive->Write(ATA_R_CONTROL, 0, 8);
		EXPECT_TRUE(IRQ());
		Acknowledge();
		Command(0xE4);
		Acknowledge();
		EXPECT_EQ(drive->Read(ATA_R_DATA, 8), 0xFFFF);
		for (int i = 0; i < 256; ++i)
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), i + 1);
		ASSERT_NO_FATAL_FAILURE(ExpectSuccess());
		EXPECT_EQ(Identify()[82] & (3 << 12), 3 << 12);
	}

	TEST_F(ATAImageTest, ClosedImageCannotResumeOrStartATransfer)
	{
		Address(17, 2);
		Command(0x30);
		ASSERT_NE(Status() & ATA_STAT_DRQ, 0);
		drive->Write(ATA_R_DATA, 0xCAFE, 16);
		drive->Close();
		for (u8 command : {0x20, 0x30, 0xC8, 0xCA, 0xE7})
		{
			Address(17, 1);
			Command(command);
			drive->Write(ATA_R_DATA, 0xBEEF, 16);
			drive->Async(1);
			EXPECT_EQ(Status() & (ATA_STAT_BUSY | ATA_STAT_DRQ), 0);
			EXPECT_EQ(drive->Read(ATA_R_DATA, 16), 0xFFFF);
		}
		EXPECT_EQ(DiskBytes(), initial);
	}

	TEST_F(ATAImageTest, ClearingHobInControlSelectsLowTaskFileBytes)
	{
		drive->Write(ATA_R_NSECTOR, 0x9A, 8);
		drive->Write(ATA_R_NSECTOR, 0x23, 8);
		drive->Write(ATA_R_SECTOR, 0xBC, 8);
		drive->Write(ATA_R_SECTOR, 0x45, 8);
		drive->Write(ATA_R_CONTROL, 0x80, 8);
		EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 0x9A);
		EXPECT_EQ(drive->Read(ATA_R_SECTOR, 8), 0xBC);
		drive->Write(ATA_R_CONTROL, 0, 8);
		EXPECT_EQ(drive->Read(ATA_R_NSECTOR, 8), 0x23);
		EXPECT_EQ(drive->Read(ATA_R_SECTOR, 8), 0x45);
	}
}
