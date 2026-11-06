// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "SIO/Memcard/MemoryCardFile.h"

#include "SIO/Memcard/MemoryCardFolder.h"
#include "SIO/Sio.h"
#include <SIO/SioTypes.h>

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include <algorithm>
#include <array>
#include <chrono>

#include "Config.h"
#include "Host.h"
#include "VMManager.h"
#include "IconsPromptFont.h"

#include "fmt/format.h"

#include <map>

static constexpr int MCD_SIZE = 1024 * 8 * 16; // Legacy PSX card default size

static constexpr int MC2_MBSIZE = 1024 * 528 * 2; // Size of a single megabyte of card data

static constexpr int MC2_ERASE_SIZE = 528 * 16;

static const char* s_folder_mem_card_id_file = "_pcsx2_superblock";

bool FileMcd_Open = false;

// ECC code ported from mymc
// https://sourceforge.net/p/mymc-opl/code/ci/master/tree/ps2mc_ecc.py
// Public domain license

static u32 CalculateECC(const u8* buf)
{
	const u8 parity_table[256] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1,
		0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0,
		1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1,
		0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0,
		1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1,
		0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0,
		1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0,
		1, 0, 1, 1, 0};

	const u8 column_parity_mask[256] = {0, 7, 22, 17, 37, 34, 51, 52, 52, 51, 34, 37, 17, 22,
		7, 0, 67, 68, 85, 82, 102, 97, 112, 119, 119, 112, 97, 102, 82, 85, 68, 67, 82, 85, 68, 67, 119, 112,
		97, 102, 102, 97, 112, 119, 67, 68, 85, 82, 17, 22, 7, 0, 52, 51, 34, 37, 37, 34, 51, 52, 0, 7, 22, 17,
		97, 102, 119, 112, 68, 67, 82, 85, 85, 82, 67, 68, 112, 119, 102, 97, 34, 37, 52, 51, 7, 0, 17, 22,
		22, 17, 0, 7, 51, 52, 37, 34, 51, 52, 37, 34, 22, 17, 0, 7, 7, 0, 17, 22, 34, 37, 52, 51, 112, 119, 102,
		97, 85, 82, 67, 68, 68, 67, 82, 85, 97, 102, 119, 112, 112, 119, 102, 97, 85, 82, 67, 68, 68, 67, 82,
		85, 97, 102, 119, 112, 51, 52, 37, 34, 22, 17, 0, 7, 7, 0, 17, 22, 34, 37, 52, 51, 34, 37, 52, 51, 7, 0,
		17, 22, 22, 17, 0, 7, 51, 52, 37, 34, 97, 102, 119, 112, 68, 67, 82, 85, 85, 82, 67, 68, 112, 119, 102,
		97, 17, 22, 7, 0, 52, 51, 34, 37, 37, 34, 51, 52, 0, 7, 22, 17, 82, 85, 68, 67, 119, 112, 97, 102, 102,
		97, 112, 119, 67, 68, 85, 82, 67, 68, 85, 82, 102, 97, 112, 119, 119, 112, 97, 102, 82, 85, 68, 67,
		0, 7, 22, 17, 37, 34, 51, 52, 52, 51, 34, 37, 17, 22, 7, 0};

	u8 column_parity = 0x77;
	u8 line_parity_0 = 0x7F;
	u8 line_parity_1 = 0x7F;

	for (int i = 0; i < 128; i++)
	{
		u8 b = buf[i];
		column_parity ^= column_parity_mask[b];
		if (parity_table[b])
		{
			line_parity_0 ^= ~i;
			line_parity_1 ^= i;
		}
	}

	return column_parity | ((line_parity_0 & 0x7F) << 8) | (line_parity_1 << 16);
}

static bool ConvertNoECCtoRAW(const char* file_in, const char* file_out)
{
	auto fin = FileSystem::OpenManagedCFile(file_in, "rb");
	if (!fin)
		return false;

	auto fout = FileSystem::OpenManagedCFile(file_out, "wb");
	if (!fout)
		return false;

	const s64 size = FileSystem::FSize64(fin.get());
	u8 buffer[512];

	for (s64 i = 0; i < (size / 512); i++)
	{
		if (std::fread(buffer, sizeof(buffer), 1, fin.get()) != 1 ||
			std::fwrite(buffer, sizeof(buffer), 1, fout.get()) != 1)
		{
			return false;
		}

		for (int j = 0; j < 4; j++)
		{
			u32 checksum = CalculateECC(&buffer[j * 128]);
			if (std::fwrite(&checksum, 3, 1, fout.get()) != 1)
				return false;
		}

		// unwritten spare reads back erased
		const u32 unused = 0xffffffff;
		if (std::fwrite(&unused, sizeof(unused), 1, fout.get()) != 1)
			return false;
	}

	if (std::fflush(fout.get()) != 0)
		return false;

	return true;
}

static bool ConvertRAWtoNoECC(const char* file_in, const char* file_out)
{
	auto fin = FileSystem::OpenManagedCFile(file_in, "rb");
	if (!fin)
		return false;

	auto fout = FileSystem::OpenManagedCFile(file_out, "wb");
	if (!fout)
		return false;

	const s64 size = FileSystem::FSize64(fin.get());
	u8 buffer[512];
	u8 checksum[16];

	for (s64 i = 0; i < (size / 528); i++)
	{
		if (std::fread(buffer, sizeof(buffer), 1, fin.get()) != 1 ||
			std::fwrite(buffer, sizeof(buffer), 1, fout.get()) != 1 ||
			std::fread(checksum, sizeof(checksum), 1, fin.get()) != 1)
		{
			return false;
		}
	}

	if (std::fflush(fout.get()) != 0)
		return false;

	return true;
}

// --------------------------------------------------------------------------------------
//  FileMemoryCard
// --------------------------------------------------------------------------------------
// Provides thread-safe direct file IO mapping.
//
class FileMemoryCard
{
protected:
	std::FILE* m_file[8] = {};
	s64 m_fileSize[8] = {};
	std::string m_filenames[8] = {};
	// The image is loaded lazily once per open session. Only the emulation thread mutates it.
	std::vector<u8> m_image[8];
	std::vector<u8> m_dirtyPages[8];
	std::string m_diskPaths[8];
	FILESYSTEM_STAT_DATA m_diskStat[8] = {};
	u64 m_diskHashes[8] = {};
	bool m_loaded[8] = {};
	bool m_dirty[8] = {};
	bool m_failed[8] = {};
	bool m_checksumChanged[8] = {};
	std::chrono::steady_clock::time_point m_lastAccess[8] = {};
	std::chrono::steady_clock::time_point m_firstDirty[8] = {};
	u64 m_chksum[8] = {};
	bool m_ispsx[8] = {};
	u32 m_chkaddr = 0;

public:
	FileMemoryCard();
	~FileMemoryCard();

	void Lock();
	void Unlock();

	void Open();
	void Close();
	void NextFrame(uint slot);

	s32 IsPresent(uint slot);
	void GetSizeInfo(uint slot, McdSizeInfo& outways);
	bool IsPSX(uint slot);
	s32 Read(uint slot, u8* dest, u32 adr, int size);
	s32 Save(uint slot, const u8* src, u32 adr, int size);
	s32 EraseBlock(uint slot, u32 adr);
	u64 GetCRC(uint slot);

protected:
	static constexpr size_t DIRTY_PAGE_SIZE = 528;
	static constexpr auto WRITEBACK_IDLE = std::chrono::seconds(2);
	static constexpr auto WRITEBACK_MAX_DELAY = std::chrono::seconds(15);

	static u64 HashBytes(const u8* data, size_t size, u64 hash = 14695981039346656037ull);
	bool EnsureLoaded(uint slot);
	bool CheckBackingUnchanged(uint slot);
	bool CheckBackingContents(uint slot);
	bool IsImageSane(uint slot) const;
	void MarkDirty(uint slot, size_t offset, size_t length);
	void FailCard(uint slot, const char* reason);
	bool Flush(uint slot);
	bool Seek(std::FILE* f, u32 adr);
	bool Create(const char* mcdFile, uint sizeInMB);
};

uint FileMcd_GetMtapPort(uint slot)
{
	switch (slot)
	{
		case 0:
		case 2:
		case 3:
		case 4:
			return 0;
		case 1:
		case 5:
		case 6:
		case 7:
			return 1;

			jNO_DEFAULT
	}

	return 0; // technically unreachable.
}

// Returns the multitap slot number, range 1 to 3 (slot 0 refers to the standard
// 1st and 2nd player slots).
uint FileMcd_GetMtapSlot(uint slot)
{
	switch (slot)
	{
		case 0:
		case 1:
			pxFail("Invalid parameter in call to GetMtapSlot -- specified slot is one of the base slots, not a Multitap slot.");
			break;

		case 2:
		case 3:
		case 4:
			return slot - 1;
		case 5:
		case 6:
		case 7:
			return slot - 4;

			jNO_DEFAULT
	}

	return 0; // technically unreachable.
}

bool FileMcd_IsMultitapSlot(uint slot)
{
	return (slot > 1);
}

std::string FileMcd_GetDefaultName(uint slot)
{
	if (FileMcd_IsMultitapSlot(slot))
		return StringUtil::StdStringFromFormat("Mcd-Multitap%u-Slot%02u.ps2", FileMcd_GetMtapPort(slot) + 1, FileMcd_GetMtapSlot(slot) + 1);
	else
		return StringUtil::StdStringFromFormat("Mcd%03u.ps2", slot + 1);
}

FileMemoryCard::FileMemoryCard()
{
	for (u8 slot = 0; slot < 8; slot++)
	{
		m_fileSize[slot] = -1;
	}
}

FileMemoryCard::~FileMemoryCard() = default;

void FileMemoryCard::Open()
{
	for (int slot = 0; slot < 8; ++slot)
	{
		m_filenames[slot] = {};
		m_diskPaths[slot].clear();
		m_image[slot].clear();
		m_dirtyPages[slot].clear();
		m_loaded[slot] = false;
		m_dirty[slot] = false;
		m_failed[slot] = false;
		m_checksumChanged[slot] = false;
		m_chksum[slot] = 0;

		if (EmuConfig.Mcd[slot].Type != MemoryCardType::File)
			continue;

		if (FileMcd_IsMultitapSlot(slot))
		{
			if (!EmuConfig.Pad.MultitapPort0_Enabled && (FileMcd_GetMtapPort(slot) == 0))
				continue;
			if (!EmuConfig.Pad.MultitapPort1_Enabled && (FileMcd_GetMtapPort(slot) == 1))
				continue;
		}

		const std::string fname = EmuConfig.FullpathToMcd(slot);

		if (!EmuConfig.Mcd[slot].Enabled || fname.empty())
		{
			Console.WriteLnFmt("McdSlot {} [File]: [disabled/empty filename]", slot);
			continue;
		}

		if (FileSystem::GetPathFileSize(fname.c_str()) <= 0)
		{
			if (!Create(fname.c_str(), 8))
			{
				Host::ReportErrorAsync(TRANSLATE_SV("MemoryCard", "Memory Card Creation Failed"),
					fmt::format(TRANSLATE_FS("MemoryCard", "Could not create the memory card:\n{}"),
						fname));
			}
		}

		if (fname.ends_with(".bin") || fname.ends_with(".mc2"))
		{
			std::string newname(fname + "x");
			if (!ConvertNoECCtoRAW(fname.c_str(), newname.c_str()))
			{
				Console.Error("Could convert memory card: %s", fname.c_str());
				FileSystem::DeleteFilePath(newname.c_str());
				continue;
			}

			// store the original filename
			m_file[slot] = FileSystem::OpenSharedCFile(newname.c_str(), "r+b", FileSystem::FileShareMode::DenyWrite);
			m_diskPaths[slot] = std::move(newname);
		}
		else
		{
			m_file[slot] = FileSystem::OpenSharedCFile(fname.c_str(), "r+b", FileSystem::FileShareMode::DenyWrite);
			m_diskPaths[slot] = fname;
		}

		if (!m_file[slot])
		{
			Host::ReportErrorAsync(TRANSLATE_SV("MemoryCard", "Memory Card Read Failed"),
				fmt::format(TRANSLATE_FS("MemoryCard", "Unable to access memory card:\n\n{}\n\n"
													   "Another instance of PCSX2 may be using this memory card "
													   "or the memory card is stored in a write-protected folder.\n"
													   "Close any other instances of PCSX2, or restart your computer.\n"),
					fname));
		}
		else // Load checksum
		{
			m_fileSize[slot] = FileSystem::FSize64(m_file[slot]);

			Console.WriteLnFmt(Color_Green, "McdSlot {} [File]: {} [{} MB, {}]", slot, Path::GetFileName(fname),
				(m_fileSize[slot] + (MCD_SIZE + 1)) / MC2_MBSIZE,
				FileMcd_IsMemoryCardFormatted(m_file[slot]) ? "Formatted" : "UNFORMATTED");

			m_filenames[slot] = std::move(fname);
			m_ispsx[slot] = m_fileSize[slot] == 0x20000;
			m_chkaddr = 0x210;
			// The full image and checksum are loaded on first guest access.
		}
	}
}

void FileMemoryCard::Close()
{
	for (int slot = 0; slot < 8; ++slot)
	{
		if (!m_file[slot])
			continue;

		// Preserve the legacy checksum write, but include it in the final batched flush.
		if (m_loaded[slot] && m_checksumChanged[slot] && !m_ispsx[slot] &&
			m_image[slot].size() >= (m_chkaddr + sizeof(m_chksum[slot])))
		{
			std::memcpy(m_image[slot].data() + m_chkaddr, &m_chksum[slot], sizeof(m_chksum[slot]));
			MarkDirty(slot, m_chkaddr, sizeof(m_chksum[slot]));
		}

		if (m_dirty[slot] && !m_failed[slot])
			Flush(slot);

		std::fclose(m_file[slot]);
		m_file[slot] = nullptr;

		// Never overwrite the non-ECC source if persistence has failed.
		if (!m_failed[slot] && (m_filenames[slot].ends_with(".bin") || m_filenames[slot].ends_with(".mc2")))
		{
			const std::string name_in(m_filenames[slot] + 'x');
			if (ConvertRAWtoNoECC(name_in.c_str(), m_filenames[slot].c_str()))
				FileSystem::DeleteFilePath(name_in.c_str());
		}

		m_filenames[slot] = {};
		m_diskPaths[slot].clear();
		m_image[slot].clear();
		m_dirtyPages[slot].clear();
		m_loaded[slot] = false;
		m_dirty[slot] = false;
		m_failed[slot] = false;
		m_checksumChanged[slot] = false;
		m_fileSize[slot] = -1;
	}
}

// Returns FALSE if the seek failed (is outside the bounds of the file).
bool FileMemoryCard::Seek(std::FILE* f, u32 adr)
{
	return (FileSystem::FSeek64(f, adr, SEEK_SET) == 0);
}

// returns FALSE if an error occurred (either permission denied or disk full)
bool FileMemoryCard::Create(const char* mcdFile, uint sizeInMB)
{
	//int enc[16] = {0x77,0x7f,0x7f,0x77,0x7f,0x7f,0x77,0x7f,0x7f,0x77,0x7f,0x7f,0,0,0,0};

	Console.WriteLn("(FileMcd) Creating new %uMB memory card: %s", sizeInMB, mcdFile);

	auto fp = FileSystem::OpenManagedCFile(mcdFile, "wb");
	if (!fp)
		return false;

	u8 buf[MC2_ERASE_SIZE];
	std::memset(buf, 0xff, sizeof(buf));

	for (uint i = 0; i < (MC2_MBSIZE * sizeInMB) / sizeof(buf); i++)
	{
		if (std::fwrite(buf, sizeof(buf), 1, fp.get()) != 1)
			return false;
	}
	return true;
}

s32 FileMemoryCard::IsPresent(uint slot)
{
	return m_file[slot] != nullptr;
}

void FileMemoryCard::GetSizeInfo(uint slot, McdSizeInfo& outways)
{
	outways.SectorSize = 512; // 0x0200
	outways.EraseBlockSizeInSectors = 16; // 0x0010
	outways.Xor = 18; // 0x12, XOR 02 00 00 10

	pxAssert(m_file[slot]);
	if (m_file[slot])
		outways.McdSizeInSectors = static_cast<u32>(m_fileSize[slot]) / (outways.SectorSize + outways.EraseBlockSizeInSectors);
	else
		outways.McdSizeInSectors = 0x4000;

	u8* pdata = (u8*)&outways.McdSizeInSectors;
	outways.Xor ^= pdata[0] ^ pdata[1] ^ pdata[2] ^ pdata[3];
}

bool FileMemoryCard::IsPSX(uint slot)
{
	return m_ispsx[slot];
}

u64 FileMemoryCard::HashBytes(const u8* data, size_t size, u64 hash)
{
	for (size_t i = 0; i < size; ++i)
		hash = (hash ^ data[i]) * 1099511628211ull;
	return hash;
}

bool FileMemoryCard::EnsureLoaded(uint slot)
{
	if (m_loaded[slot])
		return true;
	if (!m_file[slot] || m_failed[slot] || m_fileSize[slot] <= 0 ||
		m_fileSize[slot] > (64ll * MC2_MBSIZE))
		return false;

	FILESYSTEM_STAT_DATA before, after;
	if (!FileSystem::StatFile(m_diskPaths[slot].c_str(), &before) || before.Size != m_fileSize[slot] ||
		!Seek(m_file[slot], 0))
	{
		FailCard(slot, "Could not inspect the backing image before loading");
		return false;
	}

	std::vector<u8> image(static_cast<size_t>(m_fileSize[slot]));
	if (std::fread(image.data(), 1, image.size(), m_file[slot]) != image.size() ||
		!FileSystem::StatFile(m_diskPaths[slot].c_str(), &after) ||
		before.Size != after.Size || before.CreationTime != after.CreationTime ||
		before.ModificationTime != after.ModificationTime)
	{
		FailCard(slot, "Backing image changed or could not be read during initial load");
		return false;
	}

	m_diskHashes[slot] = HashBytes(image.data(), image.size());
	m_diskStat[slot] = after;
	m_image[slot] = std::move(image);
	m_dirtyPages[slot].assign((m_image[slot].size() + DIRTY_PAGE_SIZE - 1) / DIRTY_PAGE_SIZE, 0);
	m_loaded[slot] = true;
	m_lastAccess[slot] = std::chrono::steady_clock::now();

	if (!m_ispsx[slot] && m_image[slot].size() >= m_chkaddr + sizeof(m_chksum[slot]))
		std::memcpy(&m_chksum[slot], m_image[slot].data() + m_chkaddr, sizeof(m_chksum[slot]));

	return true;
}

bool FileMemoryCard::CheckBackingUnchanged(uint slot)
{
	// Metadata checks are inexpensive enough to perform before every guest write.
	// The entire image is independently checked again immediately before writeback.
	FILESYSTEM_STAT_DATA now;
	return FileSystem::StatFile(m_diskPaths[slot].c_str(), &now) &&
		now.Size == m_diskStat[slot].Size &&
		now.CreationTime == m_diskStat[slot].CreationTime &&
		now.ModificationTime == m_diskStat[slot].ModificationTime;
}

bool FileMemoryCard::CheckBackingContents(uint slot)
{
	if (!Seek(m_file[slot], 0))
		return false;

	std::array<u8, 64 * 1024> buffer;
	u64 hash = 14695981039346656037ull;
	size_t remaining = m_image[slot].size();
	while (remaining != 0)
	{
		const size_t len = std::min(buffer.size(), remaining);
		if (std::fread(buffer.data(), 1, len, m_file[slot]) != len)
			return false;
		hash = HashBytes(buffer.data(), len, hash);
		remaining -= len;
	}
	return hash == m_diskHashes[slot];
}

bool FileMemoryCard::IsImageSane(uint slot) const
{
	// Check structural safety, not full filesystem consistency: games legitimately
	// leave intermediate FAT/directory states while formatting or saving a card.
	const size_t size = m_image[slot].size();
	return m_loaded[slot] && m_fileSize[slot] > 0 &&
		size == static_cast<size_t>(m_fileSize[slot]) &&
		(m_ispsx[slot] ? size == MCD_SIZE : (size % 528 == 0)) &&
		m_dirtyPages[slot].size() == ((size + DIRTY_PAGE_SIZE - 1) / DIRTY_PAGE_SIZE);
}

void FileMemoryCard::MarkDirty(uint slot, size_t offset, size_t length)
{
	if (length == 0)
		return;

	const auto now = std::chrono::steady_clock::now();
	if (!m_dirty[slot])
		m_firstDirty[slot] = now;
	m_dirty[slot] = true;
	m_lastAccess[slot] = now;
	const size_t first = offset / DIRTY_PAGE_SIZE;
	const size_t last = (offset + length - 1) / DIRTY_PAGE_SIZE;
	std::fill(m_dirtyPages[slot].begin() + first, m_dirtyPages[slot].begin() + last + 1, 1);
}

void FileMemoryCard::FailCard(uint slot, const char* reason)
{
	if (m_failed[slot])
		return;
	m_failed[slot] = true;

	// A conflicted/failed image must not be written over the existing card.
	// Save a standalone recovery image in the local cache if guest writes are pending.
	std::string recovery;
	if (m_dirty[slot] && !m_image[slot].empty() && !EmuFolders::Cache.empty() &&
		FileSystem::CreateDirectoryPath(EmuFolders::Cache.c_str(), false))
	{
		const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
		recovery = Path::Combine(EmuFolders::Cache,
			fmt::format("memcard-recovery-slot{}-{}{}", slot + 1, stamp, m_ispsx[slot] ? ".mcr" : ".ps2"));
		// Match the final-close checksum semantics in the recovery file without
		// altering the guest-visible in-memory sector.
		std::vector<u8> recoveryImage(m_image[slot]);
		if (m_checksumChanged[slot] && !m_ispsx[slot] &&
			recoveryImage.size() >= m_chkaddr + sizeof(m_chksum[slot]))
		{
			std::memcpy(recoveryImage.data() + m_chkaddr, &m_chksum[slot], sizeof(m_chksum[slot]));
		}
		if (!FileSystem::WriteBinaryFile(recovery.c_str(), recoveryImage.data(), recoveryImage.size()))
			recovery.clear();
	}
	Host::ReportErrorAsync("Memory Card Image Writeback Stopped",
		fmt::format("{}: {}. Further writeback was stopped. A previous partial write may have reached the backing image.{}",
			m_filenames[slot], reason,
			recovery.empty() ? " No local recovery image could be created." :
			fmt::format(" A recovery copy was saved to: {}", recovery)));
}

bool FileMemoryCard::Flush(uint slot)
{
	if (!m_dirty[slot])
		return true;
	if (m_failed[slot] || !IsImageSane(slot))
	{
		FailCard(slot, "Pending memory card image is not structurally valid");
		return false;
	}

	// Detect external modifications even if timestamps have only one-second resolution.
	// This is a sequential read, once per batch, rather than per guest command.
	if (!CheckBackingUnchanged(slot) || !CheckBackingContents(slot))
	{
		FailCard(slot, "Backing image was externally modified or could not be verified");
		return false;
	}

	const auto& image = m_image[slot];
	const auto& dirty = m_dirtyPages[slot];
	for (size_t page = 0; page < dirty.size();)
	{
		if (!dirty[page])
		{
			++page;
			continue;
		}
		const size_t begin = page++;
		while (page < dirty.size() && dirty[page])
			++page;
		const size_t offset = begin * DIRTY_PAGE_SIZE;
		const size_t count = std::min(page * DIRTY_PAGE_SIZE, image.size()) - offset;
		if (FileSystem::FSeek64(m_file[slot], static_cast<s64>(offset), SEEK_SET) != 0 ||
			std::fwrite(image.data() + offset, 1, count, m_file[slot]) != count)
		{
			FailCard(slot, "Could not write dirty memory card sectors");
			return false;
		}
	}
	if (std::fflush(m_file[slot]) != 0)
	{
		FailCard(slot, "Could not flush memory card sectors to the filesystem");
		return false;
	}

	FILESYSTEM_STAT_DATA now;
	if (!FileSystem::StatFile(m_diskPaths[slot].c_str(), &now) || now.Size != m_fileSize[slot])
	{
		FailCard(slot, "Could not verify the card size after writeback");
		return false;
	}

	m_diskStat[slot] = now;
	m_diskHashes[slot] = HashBytes(image.data(), image.size());
	std::fill(m_dirtyPages[slot].begin(), m_dirtyPages[slot].end(), 0);
	m_dirty[slot] = false;

	static auto lastNotification = std::chrono::time_point<std::chrono::system_clock>();
	const auto currentTime = std::chrono::system_clock::now();
	if (currentTime - lastNotification > std::chrono::seconds(5))
	{
		Host::AddIconOSDMessage(fmt::format("MemoryCardSave{}", slot), ICON_PF_MEMORY_CARD,
			fmt::format(TRANSLATE_FS("MemoryCard", "Memory Card '{}' was saved to storage."),
				Path::GetFileName(m_filenames[slot])), Host::OSD_INFO_DURATION);
		lastNotification = currentTime;
	}
	return true;
}

void FileMemoryCard::NextFrame(uint slot)
{
	if (!m_dirty[slot] || m_failed[slot])
		return;
	const auto now = std::chrono::steady_clock::now();
	if (now - m_lastAccess[slot] >= WRITEBACK_IDLE ||
		now - m_firstDirty[slot] >= WRITEBACK_MAX_DELAY)
		Flush(slot);
}

s32 FileMemoryCard::Read(uint slot, u8* dest, u32 adr, int size)
{
	if (!m_file[slot])
	{
		DevCon.Error("(FileMcd) Ignoring attempted read from disabled slot.");
		if (size > 0)
			std::memset(dest, 0, size);
		return 1;
	}
	if (size < 0 || !EnsureLoaded(slot) ||
		static_cast<u64>(adr) + static_cast<u64>(size) > m_image[slot].size())
		return 0;

	if (size > 0)
		std::memcpy(dest, m_image[slot].data() + adr, size);
	m_lastAccess[slot] = std::chrono::steady_clock::now();
	return 1;
}

s32 FileMemoryCard::Save(uint slot, const u8* src, u32 adr, int size)
{
	if (!m_file[slot])
	{
		DevCon.Error("(FileMcd) Ignoring attempted save/write to disabled slot.");
		return 1;
	}
	if (size < 0 || !EnsureLoaded(slot) || m_failed[slot] ||
		static_cast<u64>(adr) + static_cast<u64>(size) > m_image[slot].size())
		return 0;
	if (size == 0)
		return 1;
	if (!CheckBackingUnchanged(slot))
	{
		FailCard(slot, "Backing image changed before a guest write");
		return 0;
	}

	u8* data = m_image[slot].data() + adr;
	if (m_ispsx[slot])
	{
		std::memcpy(data, src, size);
	}
	else
	{
		for (int i = 0; i < size; ++i)
		{
			if ((data[i] & src[i]) != src[i])
				Console.Warning("(FileMcd) Warning: writing to uncleared data. (%d) [%08X]", slot, adr);
			data[i] &= src[i];
		}

		if (adr == m_chkaddr)
			Console.Warning("(FileMcd) Warning: checksum sector overwritten. (%d)", slot);

		for (int i = 0; i + 8 <= size; i += 8)
		{
			u64 word;
			std::memcpy(&word, data + i, sizeof(word));
			m_chksum[slot] ^= word;
		}
		m_checksumChanged[slot] = true;
	}

	MarkDirty(slot, adr, size);
	return 1;
}

s32 FileMemoryCard::EraseBlock(uint slot, u32 adr)
{
	if (!m_file[slot])
	{
		DevCon.Error("MemoryCard: Ignoring erase for disabled slot.");
		return 1;
	}
	if (!EnsureLoaded(slot) || m_failed[slot] ||
		static_cast<u64>(adr) + MC2_ERASE_SIZE > m_image[slot].size())
		return 0;
	if (!CheckBackingUnchanged(slot))
	{
		FailCard(slot, "Backing image changed before a guest erase");
		return 0;
	}

	std::memset(m_image[slot].data() + adr, 0xff, MC2_ERASE_SIZE);
	MarkDirty(slot, adr, MC2_ERASE_SIZE);
	return 1;
}

u64 FileMemoryCard::GetCRC(uint slot)
{
	if (!m_file[slot] || !EnsureLoaded(slot))
		return 0;
	if (!m_ispsx[slot])
		return m_chksum[slot];

	// Preserve the original PS1 CRC behavior, including its full-chunk limit.
	constexpr size_t chunkSize = sizeof(u64) * 528 * 8;
	const size_t bytes = (m_image[slot].size() / chunkSize) * chunkSize;
	u64 result = 0;
	for (size_t i = 0; i < bytes; i += sizeof(u64))
	{
		u64 word;
		std::memcpy(&word, m_image[slot].data() + i, sizeof(word));
		result ^= word;
	}
	return result;
}

// --------------------------------------------------------------------------------------
//  MemoryCard Component API Bindings
// --------------------------------------------------------------------------------------
namespace Mcd
{
	FileMemoryCard impl; // class-based implementations we refer to when API is invoked
	FolderMemoryCardAggregator implFolder;
}; // namespace Mcd

uint FileMcd_ConvertToSlot(uint port, uint slot)
{
	if (slot == 0)
		return port;
	if (port == 0)
		return slot + 1; // multitap 1
	return slot + 4; // multitap 2
}

void FileMcd_SetType()
{
	// detect inserted memory card types
	for (uint slot = 0; slot < 8; ++slot)
	{
		if (EmuConfig.Mcd[slot].Filename.empty())
		{
			EmuConfig.Mcd[slot].Type = MemoryCardType::Empty;
		}
		else if (EmuConfig.Mcd[slot].Enabled)
		{
			MemoryCardType type = MemoryCardType::File; // default to file if we can't find anything at the path so it gets auto-generated

			const std::string path(EmuConfig.FullpathToMcd(slot));
			if (FileSystem::DirectoryExists(path.c_str()))
				type = MemoryCardType::Folder;

			EmuConfig.Mcd[slot].Type = type;
		}
	}
}

void FileMcd_EmuOpen()
{
	if (FileMcd_Open)
		return;
	FileMcd_Open = true;


	Mcd::impl.Open();
	Mcd::implFolder.SetFiltering(EmuConfig.McdFolderAutoManage);
	Mcd::implFolder.Open();
}

void FileMcd_EmuClose()
{
	if (!FileMcd_Open)
		return;
	FileMcd_Open = false;
	Mcd::implFolder.Close();
	Mcd::impl.Close();
}

void FileMcd_CancelEject()
{
	AutoEject::ClearAll();
}

void FileMcd_Reopen(std::string new_serial)
{
	Console.WriteLn("Reopening memory cards...");
	FileMcd_EmuClose();
	FileMcd_SetType();
	sioSetGameSerial(new_serial);
	FileMcd_EmuOpen();
}

static bool FileMcd_IsAutoEjecting()
{
	for (size_t port = 0; port < SIO::PORTS; ++port)
	{
		for (size_t slot = 0; slot < SIO::SLOTS; ++slot)
		{
			if (mcds[port][slot].autoEjectTicks > 0)
			{
				return true; // Auto-eject is active
			}
		}
	}
	return false; // No auto-eject active
}

void FileMcd_Swap()
{
	if (MemcardBusy::IsBusy())
	{
		Host::AddIconOSDMessage("MemoryCardSwap_Busy", ICON_PF_MEMORY_CARD, TRANSLATE_SV("MemoryCardSwap_Busy", "Memory cards are busy. Can't swap right now."));
		return;
	}

	// Check if auto-eject is active
	if (FileMcd_IsAutoEjecting())
	{
		Host::AddIconOSDMessage("MemoryCardSwap_AutoEject", ICON_PF_MEMORY_CARD, TRANSLATE_SV("MemoryCardSwap_AutoEject", "Memory cards are being auto-ejected. Can't swap right now."));
		return;
	}

	const std::string card1Filename = Host::GetStringSettingValue("MemoryCards", "Slot1_Filename");
	const std::string card2Filename = Host::GetStringSettingValue("MemoryCards", "Slot2_Filename");

	// Copy each McdOptions to local memory
	Pcsx2Config::McdOptions firstSlot = EmuConfig.Mcd[0];
	Pcsx2Config::McdOptions secondSlot = EmuConfig.Mcd[1];

	if (!firstSlot.Enabled || !secondSlot.Enabled || card1Filename.empty() || card2Filename.empty())
	{
		Host::AddIconOSDMessage("MemoryCardSwap_EmptySlot", ICON_PF_MEMORY_CARD, TRANSLATE_SV("MemoryCard_EmptySlot", "Both slots must have a card selected to swap."));
		return;
	}

	// Swap them
	Host::SetBaseStringSettingValue("MemoryCards", "Slot1_Filename", card2Filename.c_str());
	Host::SetBaseStringSettingValue("MemoryCards", "Slot2_Filename", card1Filename.c_str());
	Host::CommitBaseSettingChanges();
	VMManager::ApplySettings();
	EmuConfig.Mcd[0] = secondSlot;
	EmuConfig.Mcd[1] = firstSlot;

	// Reopen them
	FileMcd_EmuClose();
	FileMcd_SetType();
	FileMcd_EmuOpen();
	AutoEject::SetAll();
	Host::AddIconOSDMessage("MemoryCardSwap", ICON_PF_MEMORY_CARD, fmt::format(TRANSLATE_FS("MemoryCardSwap", "Memory Cards have been swapped.\nSlot 1: {}\nSlot 2: {}"), EmuConfig.Mcd[0].Filename, EmuConfig.Mcd[1].Filename), Host::OSD_INFO_DURATION);
}

s32 FileMcd_IsPresent(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.IsPresent(combinedSlot);
		case MemoryCardType::Folder:
			return Mcd::implFolder.IsPresent(combinedSlot);
		default:
			return false;
	}
}

void FileMcd_GetSizeInfo(uint port, uint slot, McdSizeInfo* outways)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			Mcd::impl.GetSizeInfo(combinedSlot, *outways);
			break;
		case MemoryCardType::Folder:
			Mcd::implFolder.GetSizeInfo(combinedSlot, *outways);
			break;
		default:
			return;
	}
}

bool FileMcd_IsPSX(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.IsPSX(combinedSlot);
		case MemoryCardType::Folder:
			return Mcd::implFolder.IsPSX(combinedSlot);
		default:
			return false;
	}
}

s32 FileMcd_Read(uint port, uint slot, u8* dest, u32 adr, int size)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.Read(combinedSlot, dest, adr, size);
		case MemoryCardType::Folder:
			return Mcd::implFolder.Read(combinedSlot, dest, adr, size);
		default:
			return 0;
	}
}

s32 FileMcd_Save(uint port, uint slot, const u8* src, u32 adr, int size)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.Save(combinedSlot, src, adr, size);
		case MemoryCardType::Folder:
			return Mcd::implFolder.Save(combinedSlot, src, adr, size);
		default:
			return 0;
	}
}

s32 FileMcd_EraseBlock(uint port, uint slot, u32 adr)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.EraseBlock(combinedSlot, adr);
		case MemoryCardType::Folder:
			return Mcd::implFolder.EraseBlock(combinedSlot, adr);
		default:
			return 0;
	}
}

u64 FileMcd_GetCRC(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.GetCRC(combinedSlot);
		case MemoryCardType::Folder:
			return Mcd::implFolder.GetCRC(combinedSlot);
		default:
			return 0;
	}
}

void FileMcd_NextFrame(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			Mcd::impl.NextFrame(combinedSlot);
			break;
		case MemoryCardType::Folder:
			Mcd::implFolder.NextFrame(combinedSlot);
			break;
		default:
			return;
	}
}

int FileMcd_ReIndex(uint port, uint slot, const std::string& filter)
{
	const int combinedSlot = FileMcd_ConvertToSlot(port, slot);

	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		//case MemoryCardType::File:
		//	return Mcd::impl.ReIndex( combinedSlot, filter );
		//	break;
		case MemoryCardType::Folder:
		{
			const bool enableFiltering = EmuConfig.McdFolderAutoManage && !filter.empty();
			if (!Mcd::implFolder.ReIndex(combinedSlot, enableFiltering, enableFiltering ? filter : std::string()))
				return -1;
			break;
		}
		default:
			return -1;
			break;
	}

	return combinedSlot;
}

// --------------------------------------------------------------------------------------
//  Library API Implementations
// --------------------------------------------------------------------------------------

static MemoryCardFileType GetMemoryCardFileTypeFromSize(s64 size)
{
	// Handle both ecc and non ecc versions
	if (size == (8 * MC2_MBSIZE) || size == _8mb)
		return MemoryCardFileType::PS2_8MB;
	else if (size == (16 * MC2_MBSIZE) || size == _16mb)
		return MemoryCardFileType::PS2_16MB;
	else if (size == (32 * MC2_MBSIZE) || size == _32mb)
		return MemoryCardFileType::PS2_32MB;
	else if (size == (64 * MC2_MBSIZE) || size == _64mb)
		return MemoryCardFileType::PS2_64MB;
	else if (size == MCD_SIZE)
		return MemoryCardFileType::PS1;
	else
		return MemoryCardFileType::Unknown;
}

static bool FileMcd_IsFolder(const std::string& path)
{
	const std::string superblock_path(Path::Combine(path, s_folder_mem_card_id_file));
	return FileSystem::FileExists(superblock_path.c_str());
}

bool FileMcd_IsMemoryCardFormatted(const std::string& path)
{
	auto fp = FileSystem::OpenManagedSharedCFile(path.c_str(), "rb", FileSystem::FileShareMode::DenyNone);
	if (!fp)
		return false;

	return FileMcd_IsMemoryCardFormatted(fp.get());
}

bool FileMcd_IsMemoryCardFormatted(std::FILE* fp)
{
	static const char formatted_psx[] = "MC";
	static const char formatted_string[] = "Sony PS2 Memory Card Format";
	static constexpr size_t read_length = sizeof(formatted_string) - 1;

	const s64 pos = FileSystem::FTell64(fp);

	u8 data[read_length];
	const bool okay = (FileSystem::FSeek64(fp, 0, SEEK_SET) == 0 && std::fread(data, read_length, 1, fp) == 1);
	FileSystem::FSeek64(fp, pos, SEEK_SET);
	if (!okay)
		return false;

	return (std::memcmp(data, formatted_string, sizeof(formatted_string) - 1) == 0 ||
			std::memcmp(data, formatted_psx, sizeof(formatted_psx) - 1) == 0);
}

static bool IsNoECCSize(s64 size)
{
	return (size == _8mb || size == _16mb || size == _32mb || size == _64mb);
}

bool FileMcd_InsertECC(std::vector<u8>& buffer)
{
	if (!IsNoECCSize(static_cast<s64>(buffer.size())))
		return false;

	const size_t pages = buffer.size() / FolderMemoryCard::PageSize;
	std::vector<u8> raw(pages * FolderMemoryCard::PageSizeRaw, 0xff);

	for (size_t page = 0; page < pages; page++)
	{
		const u8* src = &buffer[page * FolderMemoryCard::PageSize];
		u8* dst = &raw[page * FolderMemoryCard::PageSizeRaw];
		std::memcpy(dst, src, FolderMemoryCard::PageSize);

		for (int j = 0; j < 4; j++)
		{
			const u32 checksum = CalculateECC(&src[j * 128]);
			std::memcpy(&dst[FolderMemoryCard::PageSize + (j * 3)], &checksum, 3);
		}
	}

	buffer = std::move(raw);
	return true;
}

std::vector<AvailableMcdInfo> FileMcd_GetAvailableCards(bool include_in_use_cards)
{
	std::vector<FILESYSTEM_FIND_DATA> files;
	FileSystem::FindFiles(EmuFolders::MemoryCards.c_str(), "*",
		FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_FOLDERS | FILESYSTEM_FIND_HIDDEN_FILES, &files);

	std::vector<AvailableMcdInfo> mcds;
	mcds.reserve(files.size());

	for (FILESYSTEM_FIND_DATA& fd : files)
	{
		std::string basename(Path::GetFileName(fd.FileName));
		if (!include_in_use_cards)
		{
			bool in_use = false;
			for (size_t i = 0; i < std::size(EmuConfig.Mcd); i++)
			{
				if (EmuConfig.Mcd[i].Filename == basename)
				{
					in_use = true;
					break;
				}
			}
			if (in_use)
				continue;
		}

		// We only want relevant file types.
		if (!(fd.FileName.ends_with(".ps2") || fd.FileName.ends_with(".mcr") ||
				fd.FileName.ends_with(".mcd") || fd.FileName.ends_with(".bin") ||
				fd.FileName.ends_with(".mc2")))
			continue;

		if (fd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY)
		{
			if (!FileMcd_IsFolder(fd.FileName))
				continue;

			FolderMemoryCard sourceFolderMemoryCard;
			Pcsx2Config::McdOptions config;
			config.Enabled = true;
			config.Type = MemoryCardType::Folder;
			sourceFolderMemoryCard.Open(fd.FileName, config, (8 * 1024 * 1024) / FolderMemoryCard::ClusterSize, true, "");

			mcds.push_back({std::move(basename), std::move(fd.FileName), fd.ModificationTime,
				MemoryCardType::Folder, MemoryCardFileType::Unknown, 0u, sourceFolderMemoryCard.IsFormatted()});
			sourceFolderMemoryCard.Close(false);
		}
		else
		{
			if (fd.Size < MCD_SIZE)
				continue;

			const bool formatted = FileMcd_IsMemoryCardFormatted(fd.FileName);
			mcds.push_back({std::move(basename), std::move(fd.FileName), fd.ModificationTime,
				MemoryCardType::File, GetMemoryCardFileTypeFromSize(fd.Size),
				static_cast<u32>(fd.Size), formatted});
		}
	}

	std::sort(mcds.begin(), mcds.end(), [](auto& a, auto& b) { return a.name < b.name; });
	return mcds;
}

std::optional<AvailableMcdInfo> FileMcd_GetCardInfo(const std::string_view name)
{
	std::optional<AvailableMcdInfo> ret;

	std::string basename(name);
	std::string path(Path::Combine(EmuFolders::MemoryCards, basename));

	FILESYSTEM_STAT_DATA sd;
	if (!FileSystem::StatFile(path.c_str(), &sd))
		return ret;

	if (sd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY)
	{
		if (FileMcd_IsFolder(path))
		{
			ret = {std::move(basename), std::move(path), sd.ModificationTime,
				MemoryCardType::Folder, MemoryCardFileType::Unknown, 0u, true};
		}
	}
	else
	{
		if (sd.Size >= MCD_SIZE)
		{
			const bool formatted = FileMcd_IsMemoryCardFormatted(path);
			ret = {std::move(basename), std::move(path), sd.ModificationTime,
				MemoryCardType::File, GetMemoryCardFileTypeFromSize(sd.Size),
				static_cast<u32>(sd.Size), formatted};
		}
	}

	return ret;
}

bool FileMcd_CreateNewCard(const std::string_view name, MemoryCardType type, MemoryCardFileType file_type)
{
	const std::string full_path(Path::Combine(EmuFolders::MemoryCards, name));

	if (type == MemoryCardType::Folder)
	{
		Console.WriteLn("(FileMcd) Creating new PS2 folder memory card: '%.*s'", static_cast<int>(name.size()), name.data());

		Error error;
		if (!FileSystem::CreateDirectoryPath(full_path.c_str(), false, &error))
		{
			Host::ReportErrorAsync("Memory Card Creation Failed",
				fmt::format("Failed to create directory. The error was:\n{}", error.GetDescription()));
			return false;
		}

		// write the superblock
		auto fp = FileSystem::OpenManagedCFile(Path::Combine(full_path, s_folder_mem_card_id_file).c_str(), "wb", &error);
		if (!fp)
		{
			Host::ReportErrorAsync("Memory Card Creation Failed", fmt::format("Failed to create superblock. The error was:\n{}", error.GetDescription()));
			return false;
		}

		return true;
	}

	if (type == MemoryCardType::File)
	{
		if (file_type <= MemoryCardFileType::Unknown || file_type >= MemoryCardFileType::MaxCount)
			return false;

		static constexpr std::array<u32, static_cast<size_t>(MemoryCardFileType::MaxCount)> sizes = {{0, 8 * MC2_MBSIZE, 16 * MC2_MBSIZE, 32 * MC2_MBSIZE, 64 * MC2_MBSIZE, MCD_SIZE}};

		const bool isPSX = (type == MemoryCardType::File && file_type == MemoryCardFileType::PS1);
		const u32 size = sizes[static_cast<u32>(file_type)];
		if (!isPSX && size == 0)
			return false;

		Error error;
		auto fp = FileSystem::OpenManagedCFile(full_path.c_str(), "wb", &error);
		if (!fp)
		{
			Host::ReportErrorAsync(TRANSLATE_SV("MemoryCard", "Memory Card Creation Failed"),
				fmt::format(TRANSLATE_FS("MemoryCard", "Failed to create memory card. The error was:\n{}"), error.GetDescription()));
			return false;
		}

		if (!isPSX)
		{
			Console.WriteLn("(FileMcd) Creating new PS2 %uMB memory card: '%s'", size / MC2_MBSIZE, full_path.c_str());

			// PS2 Memory Card
			u8 buf[MC2_ERASE_SIZE];
			std::memset(buf, 0xff, sizeof(buf));

			const u32 count = size / sizeof(buf);
			for (uint i = 0; i < count; i++)
			{
				if (std::fwrite(buf, sizeof(buf), 1, fp.get()) != 1)
				{
					Host::ReportErrorAsync("Memory Card Creation Failed",
						fmt::format("Failed to write memory card file:\n{}", full_path));
					return false;
				}
			}

			return true;
		}
		else
		{
			Console.WriteLn("(FileMcd) Creating new PSX 128 KiB memory card: '%s'", full_path.c_str());

			// PSX Memory Card; 8192 is the size in bytes of a single block of a PSX memory card (8 KiB).
			u8 buf[8192];
			std::memset(buf, 0xff, sizeof(buf));

			// PSX cards consist of 16 blocks, each 8 KiB in size.
			for (uint i = 0; i < 16; i++)
			{
				if (std::fwrite(buf, sizeof(buf), 1, fp.get()) != 1)
				{
					Host::ReportErrorAsync("Memory Card Creation Failed",
						fmt::format("Failed to write memory card file:\n{}", full_path));
					return false;
				}
			}

			return true;
		}
	}

	return false;
}

bool FileMcd_RenameCard(const std::string_view name, const std::string_view new_name)
{
	const std::string name_path(Path::Combine(EmuFolders::MemoryCards, name));
	const std::string new_name_path(Path::Combine(EmuFolders::MemoryCards, new_name));

	FILESYSTEM_STAT_DATA sd, new_sd;
	if (!FileSystem::StatFile(name_path.c_str(), &sd) || FileSystem::StatFile(new_name_path.c_str(), &new_sd))
	{
		Console.Error("(FileMcd) New name already exists, or old name does not");
		return false;
	}

	Console.WriteLn("(FileMcd) Renaming memory card '%.*s' to '%.*s'",
		static_cast<int>(name.size()), name.data(),
		static_cast<int>(new_name.size()), new_name.data());

	if (!FileSystem::RenamePath(name_path.c_str(), new_name_path.c_str()))
	{
		Console.Error("(FileMcd) Failed to rename '%s' to '%s'", name_path.c_str(), new_name_path.c_str());
		return false;
	}

	return true;
}

bool FileMcd_DeleteCard(const std::string_view name)
{
	const std::string name_path(Path::Combine(EmuFolders::MemoryCards, name));

	FILESYSTEM_STAT_DATA sd;
	if (!FileSystem::StatFile(name_path.c_str(), &sd))
	{
		Console.Error("(FileMcd) Can't stat '%s' for deletion", name_path.c_str());
		return false;
	}

	Console.WriteLn("(FileMcd) Deleting memory card '%.*s'", static_cast<int>(name.size()), name.data());

	if (sd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY)
	{
		// must be a folder memcard, so do a recursive delete (scary)
		if (!FileSystem::RecursiveDeleteDirectory(name_path.c_str()))
		{
			Console.Error("(FileMcd) Failed to recursively delete '%s'", name_path.c_str());
			return false;
		}
	}
	else
	{
		if (!FileSystem::DeleteFilePath(name_path.c_str()))
		{
			Console.Error("(FileMcd) Failed to delete file '%s'", name_path.c_str());
			return false;
		}
	}

	return true;
}
