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
		Address(10, 1);
		Command(0x20);
		ASSERT_TRUE(WaitReady());
		EXPECT_EQ(Status() & (ATA_STAT_ERR | ATA_STAT_DRQ), ATA_STAT_ERR);
		EXPECT_NE(drive->Read(ATA_R_ERROR, 8) & ATA_ERR_ID, 0);
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
		drive->Write(ATA_R_FEATURE, 0xFF, 8);
		Command(0xEF);
		ASSERT_NO_FATAL_FAILURE(ExpectAbort());
		Acknowledge();
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
				EXPECT_LE(after[51] >> 8, 2);
			}
		}
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
