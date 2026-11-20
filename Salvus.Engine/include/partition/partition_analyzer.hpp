#pragma once

#include "common/result.hpp"
#include "disk/disk_reader.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace salvus::partition {

enum class PartitionScheme {
    Unknown = 0,
    Mbr,
    Gpt
};

[[nodiscard]] std::string PartitionSchemeToString(PartitionScheme scheme);

struct PartitionEntry {
    std::uint32_t index = 0;
    std::uint64_t startOffsetBytes = 0;
    std::uint64_t lengthBytes = 0;
    std::string name;
    std::string typeDescription;
    std::string detectedFileSystem;
    bool isLostCandidate = false;
    bool isFromBackupGpt = false;
};

struct PartitionAnalysisResult {
    PartitionScheme scheme = PartitionScheme::Unknown;
    bool primaryGptValid = false;
    bool backupGptValid = false;
    std::vector<PartitionEntry> partitions;
};

class PartitionAnalyzer {
public:
    // Strictly non-destructive MBR/GPT analysis, backup GPT fallback, and lost partition detection.
    [[nodiscard]] common::Result<PartitionAnalysisResult> Analyze(
        const disk::DiskReader& reader,
        bool scanForLostPartitions = true) const;
};

} // namespace salvus::partition
