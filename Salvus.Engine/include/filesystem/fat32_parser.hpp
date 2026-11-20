#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <span>

namespace salvus::filesystem {

class FAT32Parser {
public:
    // Phase 5: FAT32 boot sector, FAT table analysis, cluster chain and LFN reconstruction.
    [[nodiscard]] common::Result<void> ParseBootSector(std::span<const std::uint8_t> bootSector) const;
};

} // namespace salvus::filesystem
