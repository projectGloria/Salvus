#include "filesystem/fat32_parser.hpp"

namespace salvus::filesystem {

common::Result<void> FAT32Parser::ParseBootSector(std::span<const std::uint8_t> /*bootSector*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "FAT32 parsing is scheduled for Phase 5 (Additional File Systems).");
}

} // namespace salvus::filesystem
