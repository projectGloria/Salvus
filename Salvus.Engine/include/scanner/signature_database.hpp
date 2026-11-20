#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace salvus::scanner {

struct FileSignature {
    std::string extension;
    std::string category;
    std::vector<std::uint8_t> headerBytes;
    std::vector<std::uint8_t> footerBytes;
    std::uint64_t maxExpectedSizeBytes = 0;
};

class SignatureDatabase {
public:
    // Phase 4: Extensible file-carving signature catalog.
    [[nodiscard]] std::size_t Count() const noexcept { return signatures_.size(); }

private:
    std::vector<FileSignature> signatures_;
};

} // namespace salvus::scanner
