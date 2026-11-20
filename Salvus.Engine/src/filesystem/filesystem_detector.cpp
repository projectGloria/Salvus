#include "filesystem/filesystem_detector.hpp"

#include <cstring>

namespace salvus::filesystem {

std::string FileSystemTypeToString(FileSystemType type) {
    switch (type) {
        case FileSystemType::Ntfs:  return "NTFS";
        case FileSystemType::Fat32: return "FAT32";
        case FileSystemType::ExFat: return "exFAT";
        case FileSystemType::ReFs:  return "ReFS (Experimental)";
        case FileSystemType::Raw:   return "RAW";
        default:                    return "Unknown";
    }
}

common::Result<FileSystemType> FileSystemDetector::Detect(std::span<const std::uint8_t> bootSector) const {
    if (bootSector.size() < 512U) {
        return common::Result<FileSystemType>::Fail(
            common::StatusCode::InvalidArgument, "Boot sector buffer must be at least 512 bytes.");
    }

    const bool hasBootSignature = (bootSector[510] == 0x55U && bootSector[511] == 0xAAU);

    // NTFS: OEM ID "NTFS    " at offset 3
    if (hasBootSignature && std::memcmp(bootSector.data() + 3, "NTFS    ", 8) == 0) {
        return common::Result<FileSystemType>::Ok(FileSystemType::Ntfs);
    }

    // exFAT: OEM ID "EXFAT   " at offset 3
    if (hasBootSignature && std::memcmp(bootSector.data() + 3, "EXFAT   ", 8) == 0) {
        return common::Result<FileSystemType>::Ok(FileSystemType::ExFat);
    }

    // ReFS (experimental): OEM ID "ReFS\0\0\0\0" at offset 3
    if (std::memcmp(bootSector.data() + 3, "ReFS\0\0\0\0", 8) == 0) {
        return common::Result<FileSystemType>::Ok(FileSystemType::ReFs);
    }

    // FAT32: FileSystemType label "FAT32   " at offset 82
    if (hasBootSignature && std::memcmp(bootSector.data() + 82, "FAT32   ", 8) == 0) {
        return common::Result<FileSystemType>::Ok(FileSystemType::Fat32);
    }

    return common::Result<FileSystemType>::Ok(FileSystemType::Raw);
}

} // namespace salvus::filesystem
