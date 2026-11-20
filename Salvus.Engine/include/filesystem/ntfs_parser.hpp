#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <span>

namespace salvus::filesystem {

class NTFSParser {
public:
    // Phase 3: NTFS boot sector, MFT parsing, deleted entry detection, data run reconstruction.
    [[nodiscard]] common::Result<void> ParseBootSector(std::span<const std::uint8_t> bootSector) const;
};

} // namespace salvus::filesystem
