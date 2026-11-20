#include "scanner/signature_database.hpp"

#include <cstring>

namespace salvus::scanner {

SignatureDatabase::SignatureDatabase() {
    LoadBuiltInSignatures();
}

void SignatureDatabase::RegisterSignature(FileSignature signature) {
    signatures_.push_back(std::move(signature));
}

const FileSignature* SignatureDatabase::MatchHeader(std::span<const std::uint8_t> sectorWindow) const noexcept {
    for (const auto& sig : signatures_) {
        if (sig.headerBytes.empty()) {
            continue;
        }
        if (sig.headerOffset + sig.headerBytes.size() > sectorWindow.size()) {
            continue;
        }
        if (std::memcmp(sectorWindow.data() + sig.headerOffset, sig.headerBytes.data(), sig.headerBytes.size()) != 0) {
            continue;
        }
        if (!sig.secondaryBytes.empty()) {
            if (sig.secondaryOffset + sig.secondaryBytes.size() > sectorWindow.size()) {
                continue;
            }
            if (std::memcmp(
                    sectorWindow.data() + sig.secondaryOffset,
                    sig.secondaryBytes.data(),
                    sig.secondaryBytes.size()) != 0) {
                continue;
            }
        }
        return &sig;
    }
    return nullptr;
}

void SignatureDatabase::LoadBuiltInSignatures() {
    // --- IMAGES ---
    RegisterSignature(FileSignature{
        "jpg", "jpg", "Images", 0, {0xFF, 0xD8, 0xFF}, 0, {}, {0xFF, 0xD9},
        128, 32ULL * 1024ULL * 1024ULL, SizeStrategy::FooterSearch});

    RegisterSignature(FileSignature{
        "png", "png", "Images", 0, {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}, 0, {},
        {0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82},
        64, 32ULL * 1024ULL * 1024ULL, SizeStrategy::FooterSearch});

    RegisterSignature(FileSignature{
        "gif89a", "gif", "Images", 0, {'G', 'I', 'F', '8', '9', 'a'}, 0, {}, {0x00, 0x3B},
        32, 32ULL * 1024ULL * 1024ULL, SizeStrategy::FooterSearch});

    RegisterSignature(FileSignature{
        "gif87a", "gif", "Images", 0, {'G', 'I', 'F', '8', '7', 'a'}, 0, {}, {0x00, 0x3B},
        32, 32ULL * 1024ULL * 1024ULL, SizeStrategy::FooterSearch});

    RegisterSignature(FileSignature{
        "bmp", "bmp", "Images", 0, {'B', 'M'}, 0, {}, {},
        54, 64ULL * 1024ULL * 1024ULL, SizeStrategy::BmpHeaderSize});

    RegisterSignature(FileSignature{
        "tiff_le", "tiff", "Images", 0, {0x49, 0x49, 0x2A, 0x00}, 0, {}, {},
        64, 64ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "tiff_be", "tiff", "Images", 0, {0x4D, 0x4D, 0x00, 0x2A}, 0, {}, {},
        64, 64ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "webp", "webp", "Images", 0, {'R', 'I', 'F', 'F'}, 8, {'W', 'E', 'B', 'P'}, {},
        32, 32ULL * 1024ULL * 1024ULL, SizeStrategy::RiffHeaderSize});

    RegisterSignature(FileSignature{
        "ico", "ico", "Images", 0, {0x00, 0x00, 0x01, 0x00}, 0, {}, {},
        64, 4ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    // --- VIDEO ---
    RegisterSignature(FileSignature{
        "mp4", "mp4", "Video", 4, {'f', 't', 'y', 'p'}, 0, {}, {},
        256, 256ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "avi", "avi", "Video", 0, {'R', 'I', 'F', 'F'}, 8, {'A', 'V', 'I', ' '}, {},
        256, 512ULL * 1024ULL * 1024ULL, SizeStrategy::RiffHeaderSize});

    RegisterSignature(FileSignature{
        "mkv", "mkv", "Video", 0, {0x1A, 0x45, 0xDF, 0xA3}, 0, {}, {},
        256, 512ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "wmv", "wmv", "Video", 0, {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11}, 0, {}, {},
        256, 256ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "mpeg", "mpg", "Video", 0, {0x00, 0x00, 0x01, 0xBA}, 0, {}, {},
        256, 256ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    // --- AUDIO ---
    RegisterSignature(FileSignature{
        "mp3_id3", "mp3", "Audio", 0, {'I', 'D', '3'}, 0, {}, {},
        128, 32ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "wav", "wav", "Audio", 0, {'R', 'I', 'F', 'F'}, 8, {'W', 'A', 'V', 'E'}, {},
        44, 128ULL * 1024ULL * 1024ULL, SizeStrategy::RiffHeaderSize});

    RegisterSignature(FileSignature{
        "flac", "flac", "Audio", 0, {'f', 'L', 'a', 'C'}, 0, {}, {},
        128, 128ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "ogg", "ogg", "Audio", 0, {'O', 'g', 'g', 'S'}, 0, {}, {},
        128, 64ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    // --- DOCUMENTS ---
    RegisterSignature(FileSignature{
        "pdf", "pdf", "Documents", 0, {'%', 'P', 'D', 'F', '-'}, 0, {}, {'%', '%', 'E', 'O', 'F'},
        64, 64ULL * 1024ULL * 1024ULL, SizeStrategy::FooterSearch});

    RegisterSignature(FileSignature{
        "ole2_doc", "doc", "Documents", 0, {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1}, 0, {}, {},
        512, 32ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "rtf", "rtf", "Documents", 0, {'{', '\\', 'r', 't', 'f'}, 0, {}, {'}'},
        32, 16ULL * 1024ULL * 1024ULL, SizeStrategy::FooterSearch});

    // --- ARCHIVES (also covers Office OpenXML DOCX/XLSX/PPTX containers) ---
    RegisterSignature(FileSignature{
        "zip", "zip", "Archives", 0, {0x50, 0x4B, 0x03, 0x04}, 0, {}, {0x50, 0x4B, 0x05, 0x06},
        30, 128ULL * 1024ULL * 1024ULL, SizeStrategy::ZipEocdSearch});

    RegisterSignature(FileSignature{
        "rar", "rar", "Archives", 0, {0x52, 0x61, 0x72, 0x21, 0x1A, 0x07}, 0, {}, {},
        64, 128ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "7z", "7z", "Archives", 0, {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}, 0, {}, {},
        64, 128ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "gzip", "gz", "Archives", 0, {0x1F, 0x8B, 0x08}, 0, {}, {},
        32, 64ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    RegisterSignature(FileSignature{
        "tar", "tar", "Archives", 257, {'u', 's', 't', 'a', 'r'}, 0, {}, {},
        512, 128ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});

    // --- OTHER ---
    RegisterSignature(FileSignature{
        "pe_exe", "exe", "Other", 0, {'M', 'Z'}, 0, {}, {},
        512, 64ULL * 1024ULL * 1024ULL, SizeStrategy::FixedMaxCap});
}

} // namespace salvus::scanner
