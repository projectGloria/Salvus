#include "common/ipc_protocol.hpp"
#include "disk/disk_reader.hpp"
#include "filesystem/exfat_parser.hpp"
#include "filesystem/fat32_parser.hpp"
#include "filesystem/filesystem_detector.hpp"
#include "filesystem/ntfs_parser.hpp"
#include "partition/partition_analyzer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace {

using namespace salvus::filesystem;
using namespace salvus::partition;
using namespace salvus::disk;
using namespace salvus::common;

TEST(CorruptionResilienceTests, RejectsZeroAndAllFFBootSectorsSafely) {
    const std::vector<std::uint8_t> zeros(512U, 0x00U);
    const std::vector<std::uint8_t> ones(512U, 0xFFU);

    FileSystemDetector detector;
    EXPECT_EQ(detector.Detect(zeros).value(), FileSystemType::Raw);
    EXPECT_EQ(detector.Detect(ones).value(), FileSystemType::Raw);

    NTFSParser ntfs;
    EXPECT_FALSE(ntfs.ParseBootSector(zeros).ok());
    EXPECT_FALSE(ntfs.ParseBootSector(ones).ok());

    FAT32Parser fat32;
    EXPECT_FALSE(fat32.ParseBootSector(zeros).ok());
    EXPECT_FALSE(fat32.ParseBootSector(ones).ok());

    ExFATParser exfat;
    EXPECT_FALSE(exfat.ParseBootSector(zeros).ok());
    EXPECT_FALSE(exfat.ParseBootSector(ones).ok());
}

TEST(CorruptionResilienceTests, RejectsNtfsBootSectorOverflowAndInvalidExponents) {
    std::vector<std::uint8_t> bpb(512U, 0U);
    std::memcpy(bpb.data() + 3U, "NTFS    ", 8U);
    bpb[11U] = 0x00U; bpb[12U] = 0x02U; // bytesPerSector = 512
    bpb[13U] = 3U;                       // invalid non-power-of-two sectorsPerCluster
    bpb[510U] = 0x55U; bpb[511U] = 0xAAU;

    NTFSParser ntfs;
    EXPECT_FALSE(ntfs.ParseBootSector(bpb).ok());

    // Overflow totalSectors = UINT64_MAX
    bpb[13U] = 8U;
    std::memset(bpb.data() + 40U, 0xFF, 8U);
    bpb[64U] = static_cast<std::uint8_t>(static_cast<std::int8_t>(-10));
    EXPECT_FALSE(ntfs.ParseBootSector(bpb).ok());

    // Invalid negative MFT record size exponent (-99)
    std::memset(bpb.data() + 40U, 0x00, 8U);
    bpb[40U] = 0x00U; bpb[41U] = 0x10U; // 4096 sectors
    bpb[64U] = static_cast<std::uint8_t>(static_cast<std::int8_t>(-99));
    EXPECT_FALSE(ntfs.ParseBootSector(bpb).ok());
}

TEST(CorruptionResilienceTests, HandlesZeroLengthAndOversizedMftAttributesWithoutHanging) {
    NtfsBootSector bpb{};
    bpb.bytesPerSector = 512U;
    bpb.sectorsPerCluster = 8U;
    bpb.clusterSizeBytes = 4096U;
    bpb.totalSectors = 2048ULL;
    bpb.totalVolumeBytes = 1024ULL * 1024ULL;
    bpb.mftRecordSizeBytes = 1024U;

    // Craft MFT record with attrLength = 0 (infinite loop trap if unchecked)
    std::vector<std::uint8_t> rec(1024U, 0U);
    std::memcpy(rec.data(), "FILE", 4U);
    rec[4U] = 48U; rec[6U] = 3U;
    rec[20U] = 56U; // firstAttrOffset = 56
    rec[24U] = 0x00U; rec[25U] = 0x04U; // usedSize = 1024
    rec[56U] = 0x80U; // $DATA with attrLength = 0!

    NTFSParser ntfs;
    const auto res = ntfs.ParseMftRecord(100ULL, rec, bpb);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value().confidence, RecoveryConfidence::Damaged);

    // Craft MFT record with attrLength = UINT32_MAX (out-of-bounds read trap)
    std::memset(rec.data() + 60U, 0xFF, 4U);
    const auto res2 = ntfs.ParseMftRecord(101ULL, rec, bpb);
    ASSERT_TRUE(res2.ok());
    EXPECT_EQ(res2.value().confidence, RecoveryConfidence::Damaged);
}

TEST(CorruptionResilienceTests, RejectsCorruptedGptHeaderCrc32) {
    const auto tempPath = std::filesystem::temp_directory_path() / "salvus_corrupt_gpt_phase7.raw";
    std::vector<std::uint8_t> disk(128U * 512U, 0U);

    // Write "EFI PART" at LBA 1 with an invalid CRC32
    std::memcpy(disk.data() + 512U, "EFI PART", 8U);
    disk[512U + 12U] = 92U; // headerSize = 92
    disk[512U + 16U] = 0xDE; disk[512U + 17U] = 0xAD; // bogus CRC

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(disk.data()), static_cast<std::streamsize>(disk.size()));
    }

    DiskReader reader;
    ASSERT_TRUE(reader.OpenReadOnly(tempPath.wstring()).ok());

    PartitionAnalyzer analyzer;
    const auto res = analyzer.Analyze(reader, false);
    ASSERT_TRUE(res.ok());
    EXPECT_FALSE(res.value().primaryGptValid);
    EXPECT_FALSE(res.value().backupGptValid);
    EXPECT_EQ(res.value().scheme, PartitionScheme::Unknown);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
}

TEST(CorruptionResilienceTests, RejectsMalformedIpcJsonSafely) {
    EXPECT_FALSE(ParseIpcRequest("").ok());
    EXPECT_FALSE(ParseIpcRequest("not-json").ok());
    EXPECT_FALSE(ParseIpcRequest(R"({"id":"unterminated)").ok());
    EXPECT_FALSE(ParseIpcRequest(R"({"id":"1","protocolVersion":9999999999999999999999999,"command":"ping"})").ok());
}

} // namespace
