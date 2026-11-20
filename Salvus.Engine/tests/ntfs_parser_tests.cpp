#include "disk/disk_reader.hpp"
#include "filesystem/filesystem_detector.hpp"
#include "filesystem/ntfs_parser.hpp"
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
using namespace salvus::disk;
using namespace salvus::recovery;

void WriteLe16(std::vector<std::uint8_t>& buf, std::size_t off, std::uint16_t val) {
    buf[off] = static_cast<std::uint8_t>(val & 0xFFU);
    buf[off + 1U] = static_cast<std::uint8_t>((val >> 8U) & 0xFFU);
}

void WriteLe32(std::vector<std::uint8_t>& buf, std::size_t off, std::uint32_t val) {
    for (std::size_t i = 0; i < 4U; ++i) {
        buf[off + i] = static_cast<std::uint8_t>((val >> (i * 8U)) & 0xFFU);
    }
}

void WriteLe64(std::vector<std::uint8_t>& buf, std::size_t off, std::uint64_t val) {
    for (std::size_t i = 0; i < 8U; ++i) {
        buf[off + i] = static_cast<std::uint8_t>((val >> (i * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> MakeResidentAttr(
    std::uint32_t type,
    const std::vector<std::uint8_t>& value,
    const std::u16string& attrName = {}) {
    const std::size_t nameBytes = attrName.size() * 2U;
    const std::size_t nameOffset = 24U;
    const std::size_t valueOffset = (nameOffset + nameBytes + 7U) & ~std::size_t{7U};
    const std::size_t totalLen = (valueOffset + value.size() + 7U) & ~std::size_t{7U};

    std::vector<std::uint8_t> attr(totalLen, 0U);
    WriteLe32(attr, 0U, type);
    WriteLe32(attr, 4U, static_cast<std::uint32_t>(totalLen));
    attr[8U] = 0U; // Resident
    attr[9U] = static_cast<std::uint8_t>(attrName.size());
    WriteLe16(attr, 10U, static_cast<std::uint16_t>(nameOffset));
    WriteLe32(attr, 16U, static_cast<std::uint32_t>(value.size()));
    WriteLe16(attr, 20U, static_cast<std::uint16_t>(valueOffset));

    for (std::size_t i = 0; i < attrName.size(); ++i) {
        WriteLe16(attr, nameOffset + i * 2U, static_cast<std::uint16_t>(attrName[i]));
    }
    if (!value.empty()) {
        std::memcpy(attr.data() + valueOffset, value.data(), value.size());
    }
    return attr;
}

std::vector<std::uint8_t> MakeNonResidentDataAttr(
    std::uint64_t realSize,
    std::uint64_t allocSize,
    const std::vector<std::uint8_t>& dataRuns) {
    const std::size_t runOffset = 64U;
    const std::size_t totalLen = (runOffset + dataRuns.size() + 7U) & ~std::size_t{7U};

    std::vector<std::uint8_t> attr(totalLen, 0U);
    WriteLe32(attr, 0U, 0x80U); // $DATA
    WriteLe32(attr, 4U, static_cast<std::uint32_t>(totalLen));
    attr[8U] = 1U; // Non-resident
    WriteLe16(attr, 32U, static_cast<std::uint16_t>(runOffset));
    WriteLe64(attr, 40U, allocSize);
    WriteLe64(attr, 48U, realSize);
    WriteLe64(attr, 56U, realSize);
    std::memcpy(attr.data() + runOffset, dataRuns.data(), dataRuns.size());
    return attr;
}

std::vector<std::uint8_t> MakeFileNameValue(std::uint64_t parentRecord, const std::u16string& name) {
    std::vector<std::uint8_t> val(66U + name.size() * 2U, 0U);
    WriteLe64(val, 0U, parentRecord);
    // Windows FILETIME for 2026-01-15T12:00:00Z
    WriteLe64(val, 8U, 134129520000000000ULL);
    WriteLe64(val, 16U, 134129520000000000ULL);
    val[64U] = static_cast<std::uint8_t>(name.size());
    val[65U] = 1U; // Win32 namespace
    for (std::size_t i = 0; i < name.size(); ++i) {
        WriteLe16(val, 66U + i * 2U, static_cast<std::uint16_t>(name[i]));
    }
    return val;
}

std::vector<std::uint8_t> BuildMftRecord(
    std::uint16_t flags,
    const std::vector<std::vector<std::uint8_t>>& attrs,
    bool corruptUsa = false) {
    std::vector<std::uint8_t> rec(1024U, 0U);
    std::memcpy(rec.data(), "FILE", 4U);
    WriteLe16(rec, 4U, 48U);    // USA offset
    WriteLe16(rec, 6U, 3U);     // USA count: 1 USN + 2 sectors (512B each)
    WriteLe16(rec, 20U, 56U);   // First attribute offset
    WriteLe16(rec, 22U, flags); // Flags
    WriteLe32(rec, 28U, 1024U); // Allocated size

    std::size_t pos = 56U;
    for (const auto& a : attrs) {
        std::memcpy(rec.data() + pos, a.data(), a.size());
        pos += a.size();
    }
    WriteLe32(rec, pos, 0xFFFFFFFFU);
    WriteLe32(rec, 24U, static_cast<std::uint32_t>(pos + 4U));

    // Apply USA fixup values (USN = 0x0042)
    constexpr std::uint16_t kUsn = 0x0042U;
    WriteLe16(rec, 48U, kUsn);
    rec[50U] = rec[510U];
    rec[51U] = rec[511U];
    rec[52U] = rec[1022U];
    rec[53U] = rec[1023U];

    WriteLe16(rec, 510U, corruptUsa ? 0x9999U : kUsn);
    WriteLe16(rec, 1022U, kUsn);
    return rec;
}

class SyntheticNtfsTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto tempDir = std::filesystem::temp_directory_path();
        imagePath_ = tempDir / "salvus_synthetic_ntfs_phase3.raw";
        recoverDir_ = tempDir / "salvus_phase3_recovered";

        // 64 clusters * 4096 bytes/cluster = 256 KiB synthetic NTFS volume
        constexpr std::size_t kClusterSize = 4096U;
        constexpr std::size_t kTotalClusters = 64U;
        std::vector<std::uint8_t> vol(kTotalClusters * kClusterSize, 0U);

        // 1. Boot sector at offset 0
        std::memcpy(vol.data() + 3U, "NTFS    ", 8U);
        WriteLe16(vol, 11U, 512U);                           // Bytes per sector
        vol[13U] = 8U;                                       // Sectors per cluster (4096 bytes)
        WriteLe64(vol, 40U, (kTotalClusters * kClusterSize) / 512U); // Total sectors
        WriteLe64(vol, 48U, 4ULL);                           // $MFT starts at cluster 4
        WriteLe64(vol, 56U, 32ULL);                          // $MFTMirr at cluster 32
        vol[64U] = static_cast<std::uint8_t>(static_cast<std::int8_t>(-10)); // 2^10 = 1024 byte MFT records
        vol[510U] = 0x55U;
        vol[511U] = 0xAAU;

        const std::size_t mftBase = 4U * kClusterSize; // Cluster 4..11 = 8 clusters = 32 MFT records (0..31)

        // Record 0: $MFT itself (non-resident, 10 clusters = 40 records at cluster 4)
        const std::vector<std::uint8_t> mftRuns = {0x11U, 10U, 4U, 0x00U}; // len=10 clusters, lcn=+4
        auto rec0 = BuildMftRecord(0x0001U, {
            MakeResidentAttr(0x30U, MakeFileNameValue(5ULL, u"$MFT")),
            MakeNonResidentDataAttr(40ULL * 1024ULL, 40ULL * 1024ULL, mftRuns)
        });
        std::memcpy(vol.data() + mftBase + 0U * 1024U, rec0.data(), 1024U);

        // Record 5: Root directory "."
        auto rec5 = BuildMftRecord(0x0003U, {
            MakeResidentAttr(0x30U, MakeFileNameValue(5ULL, u"."))
        });
        std::memcpy(vol.data() + mftBase + 5U * 1024U, rec5.data(), 1024U);

        // Record 20: Subdirectory "\Documents" (active directory)
        auto rec20 = BuildMftRecord(0x0003U, {
            MakeResidentAttr(0x30U, MakeFileNameValue(5ULL, u"Documents"))
        });
        std::memcpy(vol.data() + mftBase + 20U * 1024U, rec20.data(), 1024U);

        // Record 21: Deleted RESIDENT file "\Documents\notes.txt"
        residentExpected_ = "Salvus NTFS resident recovery verification payload.";
        std::vector<std::uint8_t> resBytes(residentExpected_.begin(), residentExpected_.end());
        auto rec21 = BuildMftRecord(0x0000U, { // flags=0 => DELETED file
            MakeResidentAttr(0x30U, MakeFileNameValue(20ULL, u"notes.txt")),
            MakeResidentAttr(0x80U, resBytes)
        });
        std::memcpy(vol.data() + mftBase + 21U * 1024U, rec21.data(), 1024U);

        // Record 22: Deleted FRAGMENTED NON-RESIDENT file "\Documents\report.bin" (5000 bytes)
        // Fragment 1: 1 cluster (4096 bytes) at LCN 25
        // Fragment 2: 1 cluster (4096 bytes, 904 used) at LCN 20 (negative delta = -5!)
        // Plus an Alternate Data Stream ":Zone.Identifier"
        fragmentedExpected_.resize(5000U);
        for (std::size_t i = 0; i < 5000U; ++i) {
            fragmentedExpected_[i] = static_cast<std::uint8_t>((i * 17U + 5U) & 0xFFU);
        }
        std::memcpy(vol.data() + 25U * kClusterSize, fragmentedExpected_.data(), 4096U);
        std::memcpy(vol.data() + 20U * kClusterSize, fragmentedExpected_.data() + 4096U, 904U);

        // Run 1: len=1 (1B), lcn=+25 (1B) => 0x11, 0x01, 25
        // Run 2: len=1 (1B), delta=-5 (1B, 0xFB) => 0x11, 0x01, 0xFB
        const std::vector<std::uint8_t> fragRuns = {0x11U, 1U, 25U, 0x11U, 1U, 0xFBU, 0x00U};
        const std::vector<std::uint8_t> adsBytes = {'[', 'Z', 'o', 'n', 'e', ']'};
        auto rec22 = BuildMftRecord(0x0000U, { // flags=0 => DELETED file
            MakeResidentAttr(0x30U, MakeFileNameValue(20ULL, u"report.bin")),
            MakeNonResidentDataAttr(5000ULL, 8192ULL, fragRuns),
            MakeResidentAttr(0x80U, adsBytes, u"Zone.Identifier")
        });
        std::memcpy(vol.data() + mftBase + 22U * 1024U, rec22.data(), 1024U);

        // Record 23: Corrupted deleted file (USA fixup mismatch)
        auto rec23 = BuildMftRecord(0x0000U, {
            MakeResidentAttr(0x30U, MakeFileNameValue(20ULL, u"corrupt.dat")),
            MakeResidentAttr(0x80U, std::vector<std::uint8_t>{1, 2, 3, 4})
        }, /*corruptUsa=*/true);
        std::memcpy(vol.data() + mftBase + 23U * 1024U, rec23.data(), 1024U);

        std::ofstream out(imagePath_, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out.write(reinterpret_cast<const char*>(vol.data()), static_cast<std::streamsize>(vol.size()));
        out.close();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(imagePath_, ec);
        std::filesystem::remove_all(recoverDir_, ec);
    }

    std::filesystem::path imagePath_;
    std::filesystem::path recoverDir_;
    std::string residentExpected_;
    std::vector<std::uint8_t> fragmentedExpected_;
};

TEST_F(SyntheticNtfsTest, DetectsNtfsAndRecoversResidentAndFragmentedDeletedFiles) {
    DiskReader reader;
    ASSERT_TRUE(reader.OpenReadOnly(imagePath_.wstring()).ok());

    std::vector<std::uint8_t> boot(512U, 0U);
    ASSERT_TRUE(reader.ReadBytes(0ULL, boot).ok());

    FileSystemDetector detector;
    const auto fsType = detector.Detect(boot);
    ASSERT_TRUE(fsType.ok());
    EXPECT_EQ(fsType.value(), FileSystemType::Ntfs);

    NTFSParser parser;
    const auto scanRes = parser.ScanVolume(reader);
    ASSERT_TRUE(scanRes.ok()) << scanRes.error().message;
    EXPECT_EQ(scanRes.value().entries.size(), 3U);

    const NtfsFileEntry* notes = nullptr;
    const NtfsFileEntry* report = nullptr;
    const NtfsFileEntry* corrupt = nullptr;

    for (const auto& e : scanRes.value().entries) {
        if (e.fileName == "notes.txt") notes = &e;
        if (e.fileName == "report.bin") report = &e;
        if (e.fileName == "corrupt.dat") corrupt = &e;
    }

    ASSERT_NE(notes, nullptr);
    EXPECT_EQ(notes->fullPath, "\\Documents\\notes.txt");
    EXPECT_TRUE(notes->isDeleted);
    EXPECT_TRUE(notes->isResident);
    EXPECT_EQ(notes->confidence, RecoveryConfidence::High);

    ASSERT_NE(report, nullptr);
    EXPECT_EQ(report->fullPath, "\\Documents\\report.bin");
    EXPECT_TRUE(report->isDeleted);
    EXPECT_FALSE(report->isResident);
    EXPECT_EQ(report->fileSizeBytes, 5000ULL);
    EXPECT_EQ(report->dataExtents.size(), 2U);
    EXPECT_EQ(report->confidence, RecoveryConfidence::High);
    ASSERT_EQ(report->alternateDataStreams.size(), 1U);
    EXPECT_EQ(report->alternateDataStreams[0], "Zone.Identifier");

    ASSERT_NE(corrupt, nullptr);
    EXPECT_EQ(corrupt->confidence, RecoveryConfidence::Damaged);

    // Recover both resident and fragmented non-resident files to separate destination
    RecoveryWriter writer;
    const auto notesDest = recoverDir_ / "notes.txt";
    const auto writeNotes = writer.WriteRecoveredFile(
        imagePath_.wstring(), notesDest.wstring(),
        notes->isResident, notes->residentData, notes->dataExtents, notes->fileSizeBytes);
    ASSERT_TRUE(writeNotes.ok()) << writeNotes.error().message;
    EXPECT_EQ(writeNotes.value().bytesWritten, residentExpected_.size());

    std::ifstream inNotes(notesDest, std::ios::binary);
    std::string actualNotes((std::istreambuf_iterator<char>(inNotes)), std::istreambuf_iterator<char>());
    EXPECT_EQ(actualNotes, residentExpected_);

    const auto reportDest = recoverDir_ / "report.bin";
    const auto writeReport = writer.WriteRecoveredFile(
        imagePath_.wstring(), reportDest.wstring(),
        report->isResident, report->residentData, report->dataExtents, report->fileSizeBytes);
    ASSERT_TRUE(writeReport.ok()) << writeReport.error().message;
    EXPECT_EQ(writeReport.value().bytesWritten, 5000ULL);

    std::ifstream inReport(reportDest, std::ios::binary);
    std::vector<std::uint8_t> actualReport(
        (std::istreambuf_iterator<char>(inReport)), std::istreambuf_iterator<char>());
    EXPECT_EQ(actualReport, fragmentedExpected_);
}

TEST_F(SyntheticNtfsTest, RecoveryWriterRejectsSameSourceDestination) {
    const auto sameDrive = RecoveryWriter::ValidateSeparateDestination(L"\\\\.\\C:", L"C:\\recovered\\file.txt");
    EXPECT_FALSE(sameDrive.ok());

    const auto overwriteImage = RecoveryWriter::ValidateSeparateDestination(
        imagePath_.wstring(), imagePath_.wstring());
    EXPECT_FALSE(overwriteImage.ok());

    const auto separateDrive = RecoveryWriter::ValidateSeparateDestination(L"\\\\.\\C:", L"D:\\recovered\\file.txt");
    EXPECT_TRUE(separateDrive.ok());
}

} // namespace
