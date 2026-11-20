#pragma once

#include "common/result.hpp"
#include "disk/disk_reader.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace salvus::recovery {

struct FileExtent {
    std::uint64_t diskOffsetBytes = 0;
    std::uint64_t lengthBytes = 0;
    bool isSparse = false;
};

struct RecoveryWriteResult {
    std::uint64_t bytesWritten = 0;
    std::wstring destinationPath;
};

class FileReconstructor {
public:
    // Validates extents against disk capacity and reconstructs bytes into an output buffer (bounded).
    [[nodiscard]] common::Result<std::vector<std::uint8_t>> ReconstructToBuffer(
        const disk::DiskReader& reader,
        const std::vector<FileExtent>& extents,
        std::uint64_t fileSizeBytes,
        std::uint64_t maxBufferBytes = 64ULL * 1024ULL * 1024ULL) const;
};

class RecoveryWriter {
public:
    // Enforces that destination path is not on the same source volume/device or equal to the source image.
    [[nodiscard]] static common::Result<void> ValidateSeparateDestination(
        const std::wstring& sourceDeviceOrImagePath,
        const std::wstring& destinationFilePath);

    // Streams recovered resident or non-resident file contents to a validated separate destination path.
    [[nodiscard]] common::Result<RecoveryWriteResult> WriteRecoveredFile(
        const std::wstring& sourceDeviceOrImagePath,
        const std::wstring& destinationFilePath,
        bool isResident,
        std::span<const std::uint8_t> residentData,
        const std::vector<FileExtent>& extents,
        std::uint64_t fileSizeBytes) const;
};

class IntegrityChecker {
public:
    // Phase 6: Computes SHA-256 checksums and structural integrity validation.
    [[nodiscard]] common::Result<std::string> ComputeSha256(const std::wstring& filePath) const;
};

class RecoveryReportGenerator {
public:
    // Phase 6: Generates structured recovery audit reports.
    [[nodiscard]] common::Result<std::string> GenerateJsonReport(const std::string& sessionId) const;
};

} // namespace salvus::recovery
