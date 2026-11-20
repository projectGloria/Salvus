#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace salvus::recovery {

struct FileExtent {
    std::uint64_t diskOffsetBytes = 0;
    std::uint64_t lengthBytes = 0;
};

class FileReconstructor {
public:
    // Phase 3+: Reconstructs file byte streams from validated extents.
    [[nodiscard]] common::Result<void> Reconstruct(const std::vector<FileExtent>& extents) const;
};

class RecoveryWriter {
public:
    // Phase 3+: Writes recovered files strictly to a separate destination volume.
    [[nodiscard]] common::Result<void> WriteRecoveredFile(
        const std::wstring& sourceDevicePath,
        const std::wstring& destinationFilePath) const;
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
