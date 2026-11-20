#include "disk/disk_reader.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

using namespace salvus::disk;
using salvus::common::StatusCode;

class DiskReaderImageTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto tempDir = std::filesystem::temp_directory_path();
        imagePath_ = tempDir / "salvus_test_disk_image_phase2.raw";

        // Create a 16-sector (8192-byte) synthetic raw disk image with deterministic byte pattern
        std::vector<std::uint8_t> imageData(16U * 512U, 0U);
        for (std::size_t sector = 0; sector < 16U; ++sector) {
            for (std::size_t b = 0; b < 512U; ++b) {
                imageData[sector * 512U + b] = static_cast<std::uint8_t>((sector * 31U + b) & 0xFFU);
            }
        }

        std::ofstream out(imagePath_, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out.write(reinterpret_cast<const char*>(imageData.data()), static_cast<std::streamsize>(imageData.size()));
        out.close();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(imagePath_, ec);
    }

    std::filesystem::path imagePath_;
};

TEST_F(DiskReaderImageTest, OpensReadOnlyAndReadsAlignedSectors) {
    DiskReader reader;
    const auto openRes = reader.OpenReadOnly(imagePath_.wstring(), 512U);
    ASSERT_TRUE(openRes.ok()) << openRes.error().message;
    EXPECT_TRUE(reader.is_open());
    EXPECT_FALSE(reader.is_raw_device());
    EXPECT_EQ(reader.geometry().totalBytes, 8192ULL);
    EXPECT_EQ(reader.geometry().bytesPerSector, 512U);

    std::array<std::uint8_t, 1024> buf{};
    const auto readRes = reader.ReadSectors(1024ULL, buf);
    ASSERT_TRUE(readRes.ok()) << readRes.error().message;
    EXPECT_EQ(readRes.value(), 1024U);

    // Sector 2 starts at offset 1024
    EXPECT_EQ(buf[0], static_cast<std::uint8_t>((2U * 31U + 0U) & 0xFFU));
    EXPECT_EQ(buf[511], static_cast<std::uint8_t>((2U * 31U + 511U) & 0xFFU));
    // Sector 3 starts at buf[512]
    EXPECT_EQ(buf[512], static_cast<std::uint8_t>((3U * 31U + 0U) & 0xFFU));
}

TEST_F(DiskReaderImageTest, ReadsArbitraryByteRange) {
    DiskReader reader;
    ASSERT_TRUE(reader.OpenReadOnly(imagePath_.wstring(), 512U).ok());

    std::array<std::uint8_t, 37> buf{};
    const auto readRes = reader.ReadBytes(500ULL, buf);
    ASSERT_TRUE(readRes.ok()) << readRes.error().message;
    EXPECT_EQ(readRes.value(), 37U);

    // Byte 500 is in sector 0, offset 500
    EXPECT_EQ(buf[0], static_cast<std::uint8_t>((0U * 31U + 500U) & 0xFFU));
    // Byte 512 is in sector 1, offset 0 (index 12 in buf)
    EXPECT_EQ(buf[12], static_cast<std::uint8_t>((1U * 31U + 0U) & 0xFFU));
}

TEST_F(DiskReaderImageTest, RejectsOutOfBoundsAndUnalignedSectorReads) {
    DiskReader reader;
    ASSERT_TRUE(reader.OpenReadOnly(imagePath_.wstring(), 512U).ok());

    std::array<std::uint8_t, 512> buf{};
    const auto unaligned = reader.ReadSectors(100ULL, buf);
    EXPECT_FALSE(unaligned.ok());
    EXPECT_EQ(unaligned.error().code, StatusCode::InvalidArgument);

    const auto outOfBounds = reader.ReadSectors(8192ULL, buf);
    EXPECT_FALSE(outOfBounds.ok());
    EXPECT_EQ(outOfBounds.error().code, StatusCode::OutOfBounds);
}

TEST_F(DiskReaderImageTest, FailsCleanlyForMissingFile) {
    DiskReader reader;
    const auto res = reader.OpenReadOnly(L"C:\\nonexistent_salvus_test_image_99999.raw");
    EXPECT_FALSE(res.ok());
    EXPECT_FALSE(reader.is_open());
}

} // namespace
