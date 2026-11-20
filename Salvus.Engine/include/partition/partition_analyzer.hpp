#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace salvus::partition {

enum class PartitionScheme {
    Unknown = 0,
    Mbr,
    Gpt
};

struct PartitionEntry {
    std::uint32_t index = 0;
    std::uint64_t startOffsetBytes = 0;
    std::uint64_t lengthBytes = 0;
    std::string typeDescription;
    bool isLostCandidate = false;
};

class PartitionAnalyzer {
public:
    // Phase 5: Non-destructive MBR/GPT analysis and lost partition detection.
    [[nodiscard]] common::Result<std::vector<PartitionEntry>> AnalyzePartitions(const std::wstring& devicePath) const;
};

} // namespace salvus::partition
