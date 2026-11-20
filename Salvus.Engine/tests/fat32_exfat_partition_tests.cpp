#include "disk/disk_reader.hpp"
#include "filesystem/exfat_parser.hpp"
#include "filesystem/fat32_parser.hpp"
#include "filesystem/filesystem_detector.hpp"
#include "partition/partition_analyzer.hpp"
#include "recovery/recovery_components.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace salvus::filesystem;
using namespace salvus::partition;
using namespace salvus::disk;
using namespace salvus::recovery;

void WriteLe16(std::vector<std::uint8_t>& buf, std::size_t off, std::uint16_t v) {
    buf[off] = static_cast<std::uint8_t>(v & 0xFFU);
    buf[off + 1U] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}

void WriteLe32(std::vector<std::uint8_t>& buf, std::size_t off, std::uint32_t v) {
    for (std::size_t i = 0; i < 4U; ++i) {
        buf[off + i] = static_cast<std::uint8_t>((v >> (i * 8U)) & 0xFFU);
    }
}

void WriteLe64(std::vector<std::uint8_t>& buf, std::size_t off, std::uint64_t v) {
    for (std::size_t i = 0; i < 8U; ++i) {
        buf[off + i] = static_cast<std::uint8_t>((v >> (i * 8U)) & 0xFFU);
    }
}

std::uint32_t ComputeCrc32Test(const std::uint8_t* data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

TEST(Fat32Tests, ScansAndRecoversDeletedLongFileNameEntry) {
    const auto tempPath = std::filesystem::temp_directory_path() / "salvus_fat32_phase5.raw";
    const auto recPath = std::filesystem::temp_directory_path() / "salvus_fat32_recovered.txt";

    // Layout: 512B sectors, 1 sector/cluster, reserved=2, numFats=1, sectorsPerFat=2, totalSectors=64
    // FAT @ offset 1024 (2 sectors), Data @ offset 2048 (cluster 2 = root dir, cluster 3 = deleted file)
    std::vector<std::uint8_t> vol(64U * 512U, 0U);
    WriteLe16(vol, 11U, 512U);
    vol[13U] = 1U;
    WriteLe16(vol, 14U, 2U);
    vol[16U] = 1U;
    WriteLe32(vol, 32U, 64U);
    WriteLe32(vol, 36U, 2U);
    WriteLe32(vol, 44U, 2U); // rootCluster = 2
    std::memcpy(vol.data() + 82U, "FAT32   ", 8U);
    vol[510U] = 0x55U;
    vol[511U] = 0xAAU;

    // FAT[2] = EOC (root dir); FAT[3] = 0 (cleared on deletion)
    WriteLe32(vol, 1024U + 2U * 4U, 0x0FFFFFFFU);

    // Root directory @ cluster 2 (offset 2048):
    // Entry 0: Deleted LFN entry for "Report2026.txt" (14 chars => fits in 2 LFN entries, let's use "Notes2026.txt" = 13 chars = 1 LFN entry!)
    const std::u16string lfn = u"Notes2026.txt";
    const std::size_t dirBase = 2048U;
    vol[dirBase + 0U] = 0xE5U; // Deleted LFN
    vol[dirBase + 11U] = 0x0FU; // LFN attribute
    static constexpr std::size_t kLfnOffs[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for (std::size_t i = 0; i < 13U; ++i) {
        WriteLe16(vol, dirBase + kLfnOffs[i], static_cast<std::uint16_t>(lfn[i]));
    }

    // Entry 1: Deleted 8.3 entry pointing to cluster 3
    const std::string payload = "Salvus FAT32 deleted LFN file recovery test payload.";
    const std::size_t e1 = dirBase + 32U;
    std::memcpy(vol.data() + e1, "\xE5OTES20 TXT", 11U);
    vol[e1 + 11U] = 0x20U; // Archive
    WriteLe16(vol, e1 + 26U, 3U); // Start cluster low = 3
    WriteLe32(vol, e1 + 28U, static_cast<std::uint32_t>(payload.size()));

    // File data at cluster 3 (offset 2048 + 512 = 2560)
    std::memcpy(vol.data() + 2560U, payload.data(), payload.size());

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(vol.data()), static_cast<std::streamsize>(vol.size()));
    }

    DiskReader reader;
    ASSERT_TRUE(reader.OpenReadOnly(tempPath.wstring()).ok());

    FileSystemDetector detector;
    const auto fs = detector.Detect(std::span<const std::uint8_t>(vol.data(), 512U));
    ASSERT_TRUE(fs.ok());
    EXPECT_EQ(fs.value(), FileSystemType::Fat32);

    FAT32Parser parser;
    const auto scan = parser.ScanVolume(reader, false);
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    ASSERT_EQ(scan.value().entries.size(), 1U);
    EXPECT_EQ(scan.value().entries[0].fileName, "Notes2026.txt");
    EXPECT_TRUE(scan.value().entries[0].isDeleted);
    EXPECT_EQ(scan.value().entries[0].confidence, RecoveryConfidence::Medium);

    RecoveryWriter writer;
    const auto w = writer.WriteRecoveredFile(
        tempPath.wstring(), recPath.wstring(),
        false, {}, scan.value().entries[0].dataExtents, scan.value().entries[0].fileSizeBytes);
    ASSERT_TRUE(w.ok()) << w.error().message;

    std::ifstream in(recPath, std::ios::binary);
    std::string actual((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(actual, payload);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    std::filesystem::remove(recPath, ec);
}

TEST(ExFatTests, ScansAndRecoversDeletedExFatEntrySet) {
    const auto tempPath = std::filesystem::temp_directory_path() / "salvus_exfat_phase5.raw";
    const auto recPath = std::filesystem::temp_directory_path() / "salvus_exfat_recovered.csv";

    // 512B sectors (shift=9), 1 sector/cluster (shift=0), FAT @ sector 4 (len 2), Heap @ sector 8, 32 clusters
    std::vector<std::uint8_t> vol(64U * 512U, 0U);
    std::memcpy(vol.data() + 3U, "EXFAT   ", 8U);
    WriteLe64(vol, 72U, 64ULL); // volumeLengthSectors
    WriteLe32(vol, 80U, 4U);    // fatOffsetSectors
    WriteLe32(vol, 84U, 2U);    // fatLengthSectors
    WriteLe32(vol, 88U, 8U);    // clusterHeapOffsetSectors
    WriteLe32(vol, 92U, 32U);   // clusterCount
    WriteLe32(vol, 96U, 2U);    // rootDirectoryCluster = 2
    vol[108U] = 9U;             // bytesPerSectorShift = 9 (512)
    vol[109U] = 0U;             // sectorsPerClusterShift = 0 (1)
    vol[510U] = 0x55U;
    vol[511U] = 0xAAU;

    // Root dir @ cluster 2 = sector 8 = offset 4096
    const std::size_t dirBase = 4096U;
    const std::string payload = "col1,col2\n100,200\n";
    const std::u16string name = u"budget.csv";

    // Entry 0: Deleted File Directory Entry (0x05), secondaryCount = 2
    vol[dirBase + 0U] = 0x05U;
    vol[dirBase + 1U] = 2U;

    // Entry 1: Deleted Stream Extension (0x40), NoFatChain (0x02), firstCluster = 3, dataLength = payload.size()
    vol[dirBase + 32U + 0U] = 0x40U;
    vol[dirBase + 32U + 1U] = 0x02U;
    vol[dirBase + 32U + 3U] = static_cast<std::uint8_t>(name.size());
    WriteLe32(vol, dirBase + 32U + 20U, 3U);
    WriteLe64(vol, dirBase + 32U + 24U, payload.size());

    // Entry 2: Deleted File Name Entry (0x41)
    vol[dirBase + 64U + 0U] = 0x41U;
    for (std::size_t i = 0; i < name.size(); ++i) {
        WriteLe16(vol, dirBase + 64U + 2U + i * 2U, static_cast<std::uint16_t>(name[i]));
    }

    // File data at cluster 3 (offset 4096 + 512 = 4608)
    std::memcpy(vol.data() + 4608U, payload.data(), payload.size());

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(vol.data()), static_cast<std::streamsize>(vol.size()));
    }

    DiskReader reader;
    ASSERT_TRUE(reader.OpenReadOnly(tempPath.wstring()).ok());

    ExFATParser parser;
    const auto scan = parser.ScanVolume(reader, false);
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    ASSERT_EQ(scan.value().entries.size(), 1U);
    EXPECT_EQ(scan.value().entries[0].fileName, "budget.csv");
    EXPECT_TRUE(scan.value().entries[0].isDeleted);
    EXPECT_EQ(scan.value().entries[0].confidence, RecoveryConfidence::High);

    RecoveryWriter writer;
    const auto w = writer.WriteRecoveredFile(
        tempPath.wstring(), recPath.wstring(),
        false, {}, scan.value().entries[0].dataExtents, scan.value().entries[0].fileSizeBytes);
    ASSERT_TRUE(w.ok()) << w.error().message;

    std::ifstream in(recPath, std::ios::binary);
    std::string actual((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(actual, payload);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    std::filesystem::remove(recPath, ec);
}

TEST(PartitionAnalyzerTests, FallsBackToBackupGptAndDetectsLostPartition) {
    const auto tempPath = std::filesystem::temp_directory_path() / "salvus_gpt_lost_phase5.raw";

    // 512 KiB disk image (1024 sectors of 512B)
    constexpr std::size_t kTotalSectors = 1024U;
    std::vector<std::uint8_t> disk(kTotalSectors * 512U, 0U);

    // Primary GPT at LBA 1 is intentionally zeroed/damaged!
    // Backup GPT Partition Entry Array at LBA 1000 (2 entries, 128B each):
    const std::size_t entryBase = 1000U * 512U;
    disk[entryBase + 0U] = 0xEBU; // Non-zero Type GUID
    WriteLe64(disk, entryBase + 32U, 34ULL);   // firstLba = 34
    WriteLe64(disk, entryBase + 40U, 100ULL);  // lastLba = 100
    const std::u16string gptName = u"DataPart";
    for (std::size_t i = 0; i < gptName.size(); ++i) {
        WriteLe16(disk, entryBase + 56U + i * 2U, static_cast<std::uint16_t>(gptName[i]));
    }

    // Backup GPT Header at final LBA (1023)
    const std::size_t hdrBase = (kTotalSectors - 1U) * 512U;
    std::memcpy(disk.data() + hdrBase, "EFI PART", 8U);
    WriteLe32(disk, hdrBase + 12U, 92U);      // headerSize = 92
    WriteLe64(disk, hdrBase + 72U, 1000ULL);  // entryArrayLba = 1000
    WriteLe32(disk, hdrBase + 80U, 2U);       // numEntries = 2
    WriteLe32(disk, hdrBase + 84U, 128U);     // entrySize = 128
    const std::uint32_t crc = ComputeCrc32Test(disk.data() + hdrBase, 92U);
    WriteLe32(disk, hdrBase + 16U, crc);

    // Plant an unallocated / lost NTFS boot sector at 256 KiB (offset 262144)
    const std::size_t lostOff = 262144U;
    std::memcpy(disk.data() + lostOff + 3U, "NTFS    ", 8U);
    WriteLe16(disk, lostOff + 11U, 512U);
    disk[lostOff + 13U] = 8U;
    WriteLe64(disk, lostOff + 40U, 200ULL); // 200 sectors = 100 KiB
    WriteLe64(disk, lostOff + 48U, 4ULL);
    disk[lostOff + 64U] = static_cast<std::uint8_t>(static_cast<std::int8_t>(-10));
    disk[lostOff + 510U] = 0x55U;
    disk[lostOff + 511U] = 0xAAU;

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(disk.data()), static_cast<std::streamsize>(disk.size()));
    }

    DiskReader reader;
    ASSERT_TRUE(reader.OpenReadOnly(tempPath.wstring()).ok());

    PartitionAnalyzer analyzer;
    const auto res = analyzer.Analyze(reader, true);
    ASSERT_TRUE(res.ok()) << res.error().message;
    EXPECT_EQ(res.value().scheme, PartitionScheme::Gpt);
    EXPECT_FALSE(res.value().primaryGptValid);
    EXPECT_TRUE(res.value().backupGptValid);

    bool foundBackupGptPart = false;
    bool foundLostNtfs = false;
    for (const auto& p : res.value().partitions) {
        if (p.isFromBackupGpt && p.name == "DataPart") {
            foundBackupGptPart = true;
            EXPECT_EQ(p.startOffsetBytes, 34ULL * 512ULL);
        }
        if (p.isLostCandidate && p.detectedFileSystem == "NTFS") {
            foundLostNtfs = true;
            EXPECT_EQ(p.startOffsetBytes, lostOff);
            EXPECT_EQ(p.lengthBytes, 200ULL * 512ULL);
        }
    }
    EXPECT_TRUE(foundBackupGptPart);
    EXPECT_TRUE(foundLostNtfs);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
}

} // namespace
