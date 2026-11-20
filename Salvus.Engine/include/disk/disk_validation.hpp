#pragma once

#include "common/result.hpp"

#include <cstdint>

namespace salvus::disk {

struct DiskGeometry {
    std::uint64_t totalBytes = 0;
    std::uint32_t bytesPerSector = 512;
};

struct ReadRange {
    std::uint64_t offsetBytes = 0;
    std::uint64_t lengthBytes = 0;
};

// Strict offset, overflow, and sector boundary validation helpers.
[[nodiscard]] bool IsPowerOfTwo(std::uint32_t value) noexcept;
[[nodiscard]] bool IsValidSectorSize(std::uint32_t bytesPerSector) noexcept;
[[nodiscard]] bool IsSectorAligned(std::uint64_t value, std::uint32_t bytesPerSector) noexcept;
[[nodiscard]] common::Result<std::uint64_t> AlignDownToSector(std::uint64_t value, std::uint32_t bytesPerSector) noexcept;
[[nodiscard]] common::Result<std::uint64_t> AlignUpToSector(std::uint64_t value, std::uint32_t bytesPerSector) noexcept;
[[nodiscard]] common::Result<void> ValidateReadRange(
    const DiskGeometry& geometry,
    const ReadRange& range,
    bool requireSectorAlignment = true) noexcept;

} // namespace salvus::disk
