#pragma once

#include "common/result.hpp"
#include "disk/disk_reader.hpp"
#include "filesystem/ntfs_parser.hpp"
#include "scanner/signature_database.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace salvus::scanner {

struct CarvedFileEntry {
    std::uint64_t carvedId = 0;
    std::string fileName;
    std::string category;
    std::string extension;
    std::uint64_t diskOffsetBytes = 0;
    std::uint64_t estimatedSizeBytes = 0;
    bool footerOrSizeVerified = false;
    filesystem::RecoveryConfidence confidence = filesystem::RecoveryConfidence::Low;
    std::string confidenceReason;
};

struct DeepScanOptions {
    std::uint32_t stepBytes = 512U;
    std::uint64_t maxFiles = 50000ULL;
    std::uint64_t maxScanBytes = 0ULL; // 0 = entire device/image
};

struct DeepScanSummary {
    std::uint64_t bytesScanned = 0;
    std::vector<CarvedFileEntry> files;
};

class DeepScanner {
public:
    explicit DeepScanner(SignatureDatabase database = SignatureDatabase{});

    // Strictly read-only bounded streaming signature carving.
    [[nodiscard]] common::Result<DeepScanSummary> Scan(
        const disk::DiskReader& reader,
        const DeepScanOptions& options = {}) const;

private:
    SignatureDatabase database_;
};

} // namespace salvus::scanner
