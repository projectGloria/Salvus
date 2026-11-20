#include "scanner/deep_scanner.hpp"

namespace salvus::scanner {

common::Result<void> DeepScanner::Start(const std::wstring& /*targetPath*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "Deep Scan (signature-based file carving) is scheduled for Phase 4.");
}

} // namespace salvus::scanner
