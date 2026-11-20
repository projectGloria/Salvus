#include "filesystem/exfat_parser.hpp"

namespace salvus::filesystem {

common::Result<void> ExFATParser::ParseBootSector(std::span<const std::uint8_t> /*bootSector*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "exFAT parsing is scheduled for Phase 5 (Additional File Systems).");
}

} // namespace salvus::filesystem
