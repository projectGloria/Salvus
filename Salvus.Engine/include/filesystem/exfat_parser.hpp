#pragma once

#include "common/result.hpp"
#include "disk/disk_reader.hpp"
#include "filesystem/ntfs_parser.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace salvus::filesystem {

struct ExFatBootSector {
    std::uint32_t bytesPerSector = 0;
    std::uint32_t sectorsPerCluster = 0;
    std::uint32_t clusterSizeBytes = 0;
    std::uint64_t volumeLengthSectors = 0;
    std::uint64_t totalVolumeBytes = 0;
    std::uint32_t fatOffsetSectors = 0;
    std::uint32_t fatLengthSectors = 0;
    std::uint32_t clusterHeapOffsetSectors = 0;
    std::uint32_t clusterCount = 0;
    std::uint32_t rootDirectoryCluster = 0;
    std::uint64_t fatStartOffsetBytes = 0;
    std::uint64_t clusterHeapOffsetBytes = 0;
};

struct ExFatScanSummary {
    ExFatBootSector bootSector{};
    std::uint64_t directoriesScanned = 0;
    std::vector<NtfsFileEntry> entries;
};

class ExFATParser {
public:
    [[nodiscard]] common::Result<ExFatBootSector> ParseBootSector(std::span<const std::uint8_t> bootSector) const;

    [[nodiscard]] common::Result<ExFatScanSummary> ScanVolume(
        const disk::DiskReader& reader,
        bool includeActiveFiles = false) const;
};

} // namespace salvus::filesystem
