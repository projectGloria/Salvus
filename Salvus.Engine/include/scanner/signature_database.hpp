#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace salvus::scanner {

enum class SizeStrategy {
    FooterSearch = 0,
    BmpHeaderSize,
    RiffHeaderSize,
    ZipEocdSearch,
    FixedMaxCap
};

struct FileSignature {
    std::string id;
    std::string extension;
    std::string category;
    std::size_t headerOffset = 0;
    std::vector<std::uint8_t> headerBytes;
    std::size_t secondaryOffset = 0;
    std::vector<std::uint8_t> secondaryBytes;
    std::vector<std::uint8_t> footerBytes;
    std::uint64_t minSizeBytes = 16;
    std::uint64_t maxExpectedSizeBytes = 16ULL * 1024ULL * 1024ULL;
    SizeStrategy sizeStrategy = SizeStrategy::FooterSearch;
};

class SignatureDatabase {
public:
    SignatureDatabase();

    void RegisterSignature(FileSignature signature);

    [[nodiscard]] const std::vector<FileSignature>& Signatures() const noexcept {
        return signatures_;
    }

    [[nodiscard]] std::size_t Count() const noexcept {
        return signatures_.size();
    }

    // Returns pointer to first matching signature at the start of `sectorWindow`, or nullptr.
    [[nodiscard]] const FileSignature* MatchHeader(std::span<const std::uint8_t> sectorWindow) const noexcept;

private:
    void LoadBuiltInSignatures();
    std::vector<FileSignature> signatures_;
};

} // namespace salvus::scanner
