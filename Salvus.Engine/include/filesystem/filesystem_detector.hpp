#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <span>

namespace salvus::filesystem {

enum class FileSystemType {
    Unknown = 0,
    Ntfs,
    Fat32,
    ExFat,
    ReFs,
    Raw
};

class FileSystemDetector {
public:
    // Phase 3/5: Inspect boot sector / volume header to identify file system.
    [[nodiscard]] common::Result<FileSystemType> Detect(std::span<const std::uint8_t> bootSector) const;
};

} // namespace salvus::filesystem
