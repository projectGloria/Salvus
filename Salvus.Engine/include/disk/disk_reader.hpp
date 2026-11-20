#pragma once

#include "common/result.hpp"
#include "disk/disk_validation.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace salvus::disk {

class DiskReader {
public:
    DiskReader() = default;
    ~DiskReader();

    DiskReader(const DiskReader&) = delete;
    DiskReader& operator=(const DiskReader&) = delete;

    // Opens a physical disk, logical volume, or disk image file strictly in read-only mode.
    [[nodiscard]] common::Result<void> OpenReadOnly(
        const std::wstring& deviceOrImagePath,
        std::uint32_t defaultImageSectorSize = 512U);
    void Close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] bool is_raw_device() const noexcept { return isRawDevice_; }
    [[nodiscard]] const DiskGeometry& geometry() const noexcept { return geometry_; }

    // Sector-aligned read with strict offset, overflow, and capacity validation.
    [[nodiscard]] common::Result<std::size_t> ReadSectors(
        std::uint64_t byteOffset,
        std::span<std::uint8_t> destination) const;

    // Arbitrary byte-range read; transparently aligns to sector boundaries on raw devices.
    [[nodiscard]] common::Result<std::size_t> ReadBytes(
        std::uint64_t byteOffset,
        std::span<std::uint8_t> destination) const;

private:
    void* handle_ = nullptr; // Native Win32 HANDLE (opaque in header to keep windows.h isolated)
    bool isRawDevice_ = false;
    DiskGeometry geometry_{};
};

} // namespace salvus::disk
