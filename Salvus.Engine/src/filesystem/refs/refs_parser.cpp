#include "filesystem/refs_parser.hpp"

namespace salvus::filesystem {

common::Result<void> ReFSParser::InspectVolumeHeader(std::span<const std::uint8_t> /*header*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "ReFS support is experimental and not yet implemented.");
}

} // namespace salvus::filesystem
