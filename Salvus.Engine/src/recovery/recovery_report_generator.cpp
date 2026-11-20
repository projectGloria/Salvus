#include "recovery/recovery_components.hpp"

namespace salvus::recovery {

common::Result<std::string> RecoveryReportGenerator::GenerateJsonReport(const std::string& /*sessionId*/) const {
    return common::Result<std::string>::Fail(
        common::StatusCode::NotImplemented,
        "Recovery report generation is scheduled for Phase 6 (Professional Features).");
}

} // namespace salvus::recovery
