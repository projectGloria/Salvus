#include "scanner/quick_scanner.hpp"

namespace salvus::scanner {

common::Result<void> QuickScanner::Start(const std::wstring& /*targetPath*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "Quick Scan is scheduled for Phase 3 (NTFS Recovery).");
}

} // namespace salvus::scanner
