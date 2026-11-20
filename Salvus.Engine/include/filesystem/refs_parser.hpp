#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <span>

namespace salvus::filesystem {

class ReFSParser {
public:
    // Experimental: ReFS volume identification only until full validation is implemented.
    [[nodiscard]] common::Result<void> InspectVolumeHeader(std::span<const std::uint8_t> header) const;
};

} // namespace salvus::filesystem
