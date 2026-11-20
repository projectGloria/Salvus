#pragma once

#include "common/result.hpp"

#include <string>

namespace salvus::scanner {

enum class ScanMode {
    Quick = 0,
    Deep,
    Partition
};

class ScanManager {
public:
    // Phase 3+: Coordinates asynchronous scan sessions and cancellation.
    [[nodiscard]] common::Result<std::string> StartScan(const std::wstring& targetPath, ScanMode mode) const;
    [[nodiscard]] common::Result<void> CancelScan(const std::string& sessionId) const;
};

} // namespace salvus::scanner
