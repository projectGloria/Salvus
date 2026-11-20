#include "filesystem/ntfs_parser.hpp"

namespace salvus::filesystem {

common::Result<void> NTFSParser::ParseBootSector(std::span<const std::uint8_t> /*bootSector*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "NTFS boot sector and MFT parsing is scheduled for Phase 3 (NTFS Recovery).");
}

} // namespace salvus::filesystem
