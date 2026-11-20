#include "partition/partition_analyzer.hpp"

namespace salvus::partition {

common::Result<std::vector<PartitionEntry>> PartitionAnalyzer::AnalyzePartitions(
    const std::wstring& /*devicePath*/) const {
    return common::Result<std::vector<PartitionEntry>>::Fail(
        common::StatusCode::NotImplemented,
        "Partition analysis (MBR/GPT and lost partition detection) is scheduled for Phase 5.");
}

} // namespace salvus::partition
