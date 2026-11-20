#include "imaging/disk_image_manager.hpp"
#include "recovery/recovery_components.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace salvus::imaging;
using namespace salvus::recovery;

TEST(IntegrityCheckerTests, MatchesKnownNistSha256Vector) {
    const auto tempFile = std::filesystem::temp_directory_path() / "salvus_sha256_nist_abc.txt";
    {
        std::ofstream out(tempFile, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out.write("abc", 3);
    }

    IntegrityChecker checker;
    const auto sha = checker.ComputeSha256(tempFile.wstring());
    ASSERT_TRUE(sha.ok()) << sha.error().message;
    EXPECT_EQ(sha.value(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    std::error_code ec;
    std::filesystem::remove(tempFile, ec);
}

TEST(DiskImageManagerTests, CreatesExactRawImageWithMatchingSha256AndRejectsOverwrite) {
    const auto srcFile = std::filesystem::temp_directory_path() / "salvus_img_src_phase6.raw";
    const auto dstFile = std::filesystem::temp_directory_path() / "salvus_img_dst_phase6.raw";

    std::vector<std::uint8_t> data(16U * 512U, 0U);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>((i * 29U + 7U) & 0xFFU);
    }
    {
        std::ofstream out(srcFile, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    DiskImageManager mgr;

    // Must reject overwriting source image
    DiskImageOptions badOpts{};
    badOpts.sourceDevicePath = srcFile.wstring();
    badOpts.destinationImagePath = srcFile.wstring();
    EXPECT_FALSE(mgr.CreateRawImage(badOpts).ok());

    // Valid separate-destination imaging
    DiskImageOptions opts{};
    opts.sourceDevicePath = srcFile.wstring();
    opts.destinationImagePath = dstFile.wstring();
    opts.computeSha256 = true;
    const auto res = mgr.CreateRawImage(opts);
    ASSERT_TRUE(res.ok()) << res.error().message;
    EXPECT_EQ(res.value().totalBytesCopied, data.size());
    EXPECT_EQ(res.value().badSectorCount, 0ULL);
    EXPECT_EQ(res.value().sha256Hex.size(), 64U);

    IntegrityChecker checker;
    const auto srcSha = checker.ComputeSha256(srcFile.wstring());
    ASSERT_TRUE(srcSha.ok());
    EXPECT_EQ(srcSha.value(), res.value().sha256Hex);

    std::error_code ec;
    std::filesystem::remove(srcFile, ec);
    std::filesystem::remove(dstFile, ec);
}

} // namespace
