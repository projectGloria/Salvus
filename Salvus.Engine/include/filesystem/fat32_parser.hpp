#pragma once

#include "common/result.hpp"
#include "disk/disk_reader.hpp"
#include "filesystem/ntfs_parser.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace salvus::filesystem {

struct Fat32BootSector {
    std::uint16_t bytesPerSector = 0;
    std::uint8_t sectorsPerCluster = 0;
    std::uint16_t reservedSectors = 0;
    std::uint8_t numFats = 0;
    std::uint32_t totalSectors = 0;
    std::uint32_t sectorsPerFat = 0;
    std::uint32_t rootCluster = 0;
    std::uint32_t clusterSizeBytes = 0;
    std::uint64_t fatStartOffsetBytes = 0;
    std::uint64_t dataStartOffsetBytes = 0;
    std::uint64_t totalVolumeBytes = 0;
    std::uint32_t totalClusters = 0;
};

struct Fat32ScanSummary {
    Fat32BootSector bootSector{};
    std::uint64_t directoriesScanned = 0;
    std::vector<NtfsFileEntry> entries; // Unified file entry contract for recovery & previews
};

class FAT32Parser {
public:
    [[nodiscard]] common::Result<Fat32BootSector> ParseBootSector(std::span<const std::uint8_t> bootSector) const;

    [[nodiscard]] common::Result<Fat32ScanSummary> ScanVolume(
        const disk::DiskReader& reader,
        bool includeActiveFiles = false) const;
};

} // namespace salvus::filesystem
