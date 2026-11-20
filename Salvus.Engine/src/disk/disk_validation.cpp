#include "disk/disk_validation.hpp"

#include <limits>

namespace salvus::disk {

bool IsPowerOfTwo(std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

bool IsValidSectorSize(std::uint32_t bytesPerSector) noexcept {
    // Standard physical/logical sector sizes: 512 bytes up to 64 KiB, power of two.
    return bytesPerSector >= 512U && bytesPerSector <= 65536U && IsPowerOfTwo(bytesPerSector);
}

bool IsSectorAligned(std::uint64_t value, std::uint32_t bytesPerSector) noexcept {
    if (!IsValidSectorSize(bytesPerSector)) {
        return false;
    }
    return (value % static_cast<std::uint64_t>(bytesPerSector)) == 0U;
}

common::Result<std::uint64_t> AlignDownToSector(std::uint64_t value, std::uint32_t bytesPerSector) noexcept {
    if (!IsValidSectorSize(bytesPerSector)) {
        return common::Result<std::uint64_t>::Fail(
            common::StatusCode::InvalidArgument, "Invalid sector size.");
    }
    const std::uint64_t sector = bytesPerSector;
    return common::Result<std::uint64_t>::Ok(value - (value % sector));
}

common::Result<std::uint64_t> AlignUpToSector(std::uint64_t value, std::uint32_t bytesPerSector) noexcept {
    if (!IsValidSectorSize(bytesPerSector)) {
        return common::Result<std::uint64_t>::Fail(
            common::StatusCode::InvalidArgument, "Invalid sector size.");
    }
    const std::uint64_t sector = bytesPerSector;
    const std::uint64_t remainder = value % sector;
    if (remainder == 0U) {
        return common::Result<std::uint64_t>::Ok(value);
    }
    const std::uint64_t delta = sector - remainder;
    if (value > (std::numeric_limits<std::uint64_t>::max)() - delta) {
        return common::Result<std::uint64_t>::Fail(
            common::StatusCode::OutOfBounds, "Sector alignment overflow.");
    }
    return common::Result<std::uint64_t>::Ok(value + delta);
}

common::Result<void> ValidateReadRange(
    const DiskGeometry& geometry,
    const ReadRange& range,
    bool requireSectorAlignment) noexcept {
    if (!IsValidSectorSize(geometry.bytesPerSector)) {
        return common::Result<void>::Fail(
            common::StatusCode::InvalidArgument, "Invalid disk sector size.");
    }
    if (geometry.totalBytes == 0U) {
        return common::Result<void>::Fail(
            common::StatusCode::InvalidArgument, "Disk total size must be greater than zero.");
    }
    if (range.lengthBytes == 0U) {
        return common::Result<void>::Fail(
            common::StatusCode::InvalidArgument, "Read length must be greater than zero.");
    }
    if (requireSectorAlignment) {
        if (!IsSectorAligned(range.offsetBytes, geometry.bytesPerSector)) {
            return common::Result<void>::Fail(
                common::StatusCode::InvalidArgument, "Read offset is not aligned to sector boundary.");
        }
        if (!IsSectorAligned(range.lengthBytes, geometry.bytesPerSector)) {
            return common::Result<void>::Fail(
                common::StatusCode::InvalidArgument, "Read length is not a multiple of sector size.");
        }
    }
    if (range.offsetBytes > geometry.totalBytes) {
        return common::Result<void>::Fail(
            common::StatusCode::OutOfBounds, "Read offset exceeds disk capacity.");
    }
    if (range.lengthBytes > geometry.totalBytes - range.offsetBytes) {
        return common::Result<void>::Fail(
            common::StatusCode::OutOfBounds, "Read range exceeds disk capacity.");
    }
    return common::Result<void>::Ok();
}

} // namespace salvus::disk
