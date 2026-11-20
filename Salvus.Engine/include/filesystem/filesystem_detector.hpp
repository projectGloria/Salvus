#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace salvus::filesystem {

enum class FileSystemType {
    Unknown = 0,
    Ntfs,
    Fat32,
    ExFat,
    ReFs,
    Raw
};

[[nodiscard]] std::string FileSystemTypeToString(FileSystemType type);

class FileSystemDetector {
public:
    // Inspects a volume boot sector (>= 512 bytes) to identify the file system.
    [[nodiscard]] common::Result<FileSystemType> Detect(std::span<const std::uint8_t> bootSector) const;
};

} // namespace salvus::filesystem
