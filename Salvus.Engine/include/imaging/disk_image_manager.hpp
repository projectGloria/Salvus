#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <string>

namespace salvus::imaging {

struct DiskImageOptions {
    std::wstring sourceDevicePath;
    std::wstring destinationImagePath;
    std::uint32_t bufferSizeBytes = 1024U * 1024U;
    std::uint32_t maxSectorRetries = 2;
    bool computeSha256 = true;
};

class DiskImageManager {
public:
    // Phase 6: Sector-by-sector disk imaging with bounded retries and SHA-256 verification.
    [[nodiscard]] common::Result<void> CreateRawImage(const DiskImageOptions& options) const;
};

} // namespace salvus::imaging
