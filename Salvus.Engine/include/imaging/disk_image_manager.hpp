#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <string>

namespace salvus::imaging {

struct DiskImageOptions {
    std::wstring sourceDevicePath;
    std::wstring destinationImagePath;
    std::uint32_t bufferSizeBytes = 1024U * 1024U; // 1 MiB default
    std::uint32_t maxSectorRetries = 2U;           // Bounded retries for unstable media
    bool computeSha256 = true;
};

struct DiskImageResult {
    std::uint64_t totalBytesCopied = 0;
    std::uint64_t badSectorCount = 0;
    std::string sha256Hex;
    std::wstring badSectorMapPath;
};

class DiskImageManager {
public:
    // Sector-by-sector read-only disk imaging with bounded retries, bad-sector map, and SHA-256.
    [[nodiscard]] common::Result<DiskImageResult> CreateRawImage(const DiskImageOptions& options) const;
};

} // namespace salvus::imaging
