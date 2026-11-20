#include "scanner/scan_manager.hpp"

namespace salvus::scanner {

common::Result<std::string> ScanManager::StartScan(const std::wstring& /*targetPath*/, ScanMode /*mode*/) const {
    return common::Result<std::string>::Fail(
        common::StatusCode::NotImplemented,
        "Scan execution is scheduled for Phase 3+.");
}

common::Result<void> ScanManager::CancelScan(const std::string& /*sessionId*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "Scan cancellation is scheduled for Phase 3+.");
}

} // namespace salvus::scanner
