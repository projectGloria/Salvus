#include "filesystem/filesystem_detector.hpp"

namespace salvus::filesystem {

common::Result<FileSystemType> FileSystemDetector::Detect(std::span<const std::uint8_t> /*bootSector*/) const {
    return common::Result<FileSystemType>::Fail(
        common::StatusCode::NotImplemented,
        "File-system detection is scheduled for Phase 3 (NTFS) and Phase 5 (FAT32/exFAT).");
}

} // namespace salvus::filesystem
