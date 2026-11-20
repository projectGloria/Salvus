#pragma once

#include "common/result.hpp"
#include "disk/disk_reader.hpp"
#include "recovery/recovery_components.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace salvus::filesystem {

enum class RecoveryConfidence {
    High = 0,
    Medium = 1,
    Low = 2,
    Damaged = 3,
    Unrecoverable = 4
};

[[nodiscard]] std::string RecoveryConfidenceToString(RecoveryConfidence confidence);

struct NtfsBootSector {
    std::uint16_t bytesPerSector = 0;
    std::uint8_t sectorsPerCluster = 0;
    std::uint32_t clusterSizeBytes = 0;
    std::uint64_t totalSectors = 0;
    std::uint64_t totalVolumeBytes = 0;
    std::uint64_t mftStartCluster = 0;
    std::uint64_t mftMirrorStartCluster = 0;
    std::uint32_t mftRecordSizeBytes = 0;
    std::uint64_t volumeSerialNumber = 0;
};

struct NtfsFileEntry {
    std::uint64_t recordNumber = 0;
    std::uint64_t parentRecordNumber = 0;
    std::string fileName;
    std::string fullPath;
    bool isDeleted = false;
    bool isDirectory = false;
    bool isResident = false;
    std::uint64_t fileSizeBytes = 0;
    std::string createdUtc;
    std::string modifiedUtc;
    RecoveryConfidence confidence = RecoveryConfidence::Unrecoverable;
    std::string confidenceReason;
    std::vector<std::uint8_t> residentData;
    std::vector<recovery::FileExtent> dataExtents;
    std::vector<std::string> alternateDataStreams;
};

struct NtfsScanOptions {
    bool includeActiveFiles = false;
    std::uint64_t maxMftRecords = 250000ULL;
};

struct NtfsScanSummary {
    NtfsBootSector bootSector{};
    std::uint64_t mftRecordsScanned = 0;
    std::vector<NtfsFileEntry> entries;
};

class NTFSParser {
public:
    // Parses and validates a 512-byte NTFS boot sector.
    [[nodiscard]] common::Result<NtfsBootSector> ParseBootSector(std::span<const std::uint8_t> bootSector) const;

    // Parses a single raw MFT record (applies USA fixup on a local copy, extracts metadata & data runs).
    [[nodiscard]] common::Result<NtfsFileEntry> ParseMftRecord(
        std::uint64_t recordNumber,
        std::span<const std::uint8_t> rawRecord,
        const NtfsBootSector& bootSector) const;

    // Scans an NTFS volume or raw NTFS image via a read-only DiskReader and reconstructs paths.
    [[nodiscard]] common::Result<NtfsScanSummary> ScanVolume(
        const disk::DiskReader& reader,
        const NtfsScanOptions& options = {}) const;
};

} // namespace salvus::filesystem
