#include "disk/disk_reader.hpp"
#include "recovery/recovery_components.hpp"
#include "scanner/deep_scanner.hpp"
#include "scanner/signature_database.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

using namespace salvus::scanner;
using namespace salvus::disk;
using namespace salvus::filesystem;
using namespace salvus::recovery;

class DeepScannerRawImageTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto tempDir = std::filesystem::temp_directory_path();
        imagePath_ = tempDir / "salvus_deep_scan_test_phase4.raw";
        recoverPath_ = tempDir / "salvus_carved_png_recovered.png";

        // 64 KiB RAW disk image with no file system at all
        std::vector<std::uint8_t> raw(64U * 1024U, 0x00U);

        // 1. Valid PNG at sector 2 (offset 1024): header (8B) + payload (120B) + IEND footer (8B) = 136 bytes
        pngExpected_ = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        for (int i = 0; i < 120; ++i) {
            pngExpected_.push_back(static_cast<std::uint8_t>(0x30 + (i % 50)));
        }
        const std::vector<std::uint8_t> pngFooter = {0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
        pngExpected_.insert(pngExpected_.end(), pngFooter.begin(), pngFooter.end());
        std::memcpy(raw.data() + 1024U, pngExpected_.data(), pngExpected_.size());

        // 2. Valid PDF at sector 8 (offset 4096): "%PDF-1.7\n...%%EOF"
        const std::string pdfStr = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n"
                                   "0123456789012345678901234567890123456789\n%%EOF";
        pdfExpectedSize_ = pdfStr.size();
        std::memcpy(raw.data() + 4096U, pdfStr.data(), pdfStr.size());

        // 3. Valid BMP at sector 16 (offset 8192): header "BM", total size = 256, pixel offset = 54
        std::vector<std::uint8_t> bmp(256U, 0x11U);
        bmp[0] = 'B';
        bmp[1] = 'M';
        bmp[2] = 0x00U; bmp[3] = 0x01U; bmp[4] = 0x00U; bmp[5] = 0x00U; // 256 LE
        bmp[10] = 54U; bmp[11] = 0U; bmp[12] = 0U; bmp[13] = 0U;        // pixel offset 54
        std::memcpy(raw.data() + 8192U, bmp.data(), bmp.size());

        // 4. Truncated JPEG at sector 32 (offset 16384): header FF D8 FF E0 with NO FF D9 footer anywhere
        raw[16384U] = 0xFFU;
        raw[16385U] = 0xD8U;
        raw[16386U] = 0xFFU;
        raw[16387U] = 0xE0U;

        std::ofstream out(imagePath_, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        out.close();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(imagePath_, ec);
        std::filesystem::remove(recoverPath_, ec);
    }

    std::filesystem::path imagePath_;
    std::filesystem::path recoverPath_;
    std::vector<std::uint8_t> pngExpected_;
    std::size_t pdfExpectedSize_ = 0;
};

TEST_F(DeepScannerRawImageTest, BuiltInSignatureDatabaseIsPopulatedAndExtensible) {
    SignatureDatabase db;
    EXPECT_GE(db.Count(), 25U);

    const std::size_t before = db.Count();
    db.RegisterSignature(FileSignature{
        "custom_test", "tst", "Custom", 0, {'T', 'S', 'T', '!'}, 0, {}, {}, 16, 1024, SizeStrategy::FixedMaxCap});
    EXPECT_EQ(db.Count(), before + 1U);
}

TEST_F(DeepScannerRawImageTest, CarvesValidSignaturesAndMarksTruncatedAsDamaged) {
    DiskReader reader;
    ASSERT_TRUE(reader.OpenReadOnly(imagePath_.wstring()).ok());

    DeepScanner scanner;
    const auto res = scanner.Scan(reader);
    ASSERT_TRUE(res.ok()) << res.error().message;
    EXPECT_EQ(res.value().bytesScanned, 64ULL * 1024ULL);

    const CarvedFileEntry* png = nullptr;
    const CarvedFileEntry* pdf = nullptr;
    const CarvedFileEntry* bmp = nullptr;
    const CarvedFileEntry* jpg = nullptr;

    for (const auto& f : res.value().files) {
        if (f.extension == "png") png = &f;
        if (f.extension == "pdf") pdf = &f;
        if (f.extension == "bmp") bmp = &f;
        if (f.extension == "jpg") jpg = &f;
    }

    ASSERT_NE(png, nullptr);
    EXPECT_EQ(png->diskOffsetBytes, 1024ULL);
    EXPECT_EQ(png->estimatedSizeBytes, pngExpected_.size());
    EXPECT_TRUE(png->footerOrSizeVerified);
    EXPECT_EQ(png->confidence, RecoveryConfidence::Low);

    ASSERT_NE(pdf, nullptr);
    EXPECT_EQ(pdf->diskOffsetBytes, 4096ULL);
    EXPECT_EQ(pdf->estimatedSizeBytes, pdfExpectedSize_);
    EXPECT_TRUE(pdf->footerOrSizeVerified);
    EXPECT_EQ(pdf->confidence, RecoveryConfidence::Low);

    ASSERT_NE(bmp, nullptr);
    EXPECT_EQ(bmp->diskOffsetBytes, 8192ULL);
    EXPECT_EQ(bmp->estimatedSizeBytes, 256ULL);
    EXPECT_TRUE(bmp->footerOrSizeVerified);
    EXPECT_EQ(bmp->confidence, RecoveryConfidence::Low);

    ASSERT_NE(jpg, nullptr);
    EXPECT_EQ(jpg->diskOffsetBytes, 16384ULL);
    EXPECT_FALSE(jpg->footerOrSizeVerified);
    EXPECT_EQ(jpg->confidence, RecoveryConfidence::Damaged);

    // Recover carved PNG via RecoveryWriter and verify exact byte-for-byte content
    RecoveryWriter writer;
    const std::vector<FileExtent> ext = {FileExtent{png->diskOffsetBytes, png->estimatedSizeBytes, false}};
    const auto writeRes = writer.WriteRecoveredFile(
        imagePath_.wstring(), recoverPath_.wstring(), false, {}, ext, png->estimatedSizeBytes);
    ASSERT_TRUE(writeRes.ok()) << writeRes.error().message;

    std::ifstream inPng(recoverPath_, std::ios::binary);
    std::vector<std::uint8_t> actual(
        (std::istreambuf_iterator<char>(inPng)), std::istreambuf_iterator<char>());
    EXPECT_EQ(actual, pngExpected_);
}

} // namespace
