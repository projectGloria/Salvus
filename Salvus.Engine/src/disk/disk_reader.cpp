#include "disk/disk_reader.hpp"

namespace salvus::disk {

common::Result<void> DiskReader::OpenReadOnly(const std::wstring& /*devicePath*/) {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "Read-only raw disk access is scheduled for Phase 2 (Storage Access).");
}

void DiskReader::Close() noexcept {
    isOpen_ = false;
    geometry_ = {};
}

common::Result<std::size_t> DiskReader::ReadSectors(
    std::uint64_t byteOffset,
    std::span<std::uint8_t> destination) const {
    if (!isOpen_) {
        return common::Result<std::size_t>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader is not open.");
    }
    const auto validation = ValidateReadRange(
        geometry_,
        ReadRange{byteOffset, static_cast<std::uint64_t>(destination.size())},
        true);
    if (!validation.ok()) {
        return common::Result<std::size_t>::Fail(validation.error().code, validation.error().message);
    }
    return common::Result<std::size_t>::Fail(
        common::StatusCode::NotImplemented,
        "Sector reading is scheduled for Phase 2 (Storage Access).");
}

} // namespace salvus::disk
