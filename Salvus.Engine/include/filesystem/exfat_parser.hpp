#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <span>

namespace salvus::filesystem {

class ExFATParser {
public:
    // Phase 5: exFAT VBR, allocation bitmap, and directory entry reconstruction.
    [[nodiscard]] common::Result<void> ParseBootSector(std::span<const std::uint8_t> bootSector) const;
};

} // namespace salvus::filesystem
