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
    ~DiskReader() = default;

    DiskReader(const DiskReader&) = delete;
    DiskReader& operator=(const DiskReader&) = delete;

    // Phase 2: Strictly read-only native Windows raw disk / image access.
    [[nodiscard]] common::Result<void> OpenReadOnly(const std::wstring& devicePath);
    void Close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return isOpen_; }
    [[nodiscard]] const DiskGeometry& geometry() const noexcept { return geometry_; }

    [[nodiscard]] common::Result<std::size_t> ReadSectors(
        std::uint64_t byteOffset,
        std::span<std::uint8_t> destination) const;

private:
    bool isOpen_ = false;
    DiskGeometry geometry_{};
};

} // namespace salvus::disk
