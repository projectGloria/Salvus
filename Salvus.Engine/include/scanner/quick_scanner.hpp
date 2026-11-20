#pragma once

#include "common/result.hpp"

#include <string>

namespace salvus::scanner {

class QuickScanner {
public:
    // Phase 3/5: Fast metadata-based scan for recently deleted files.
    [[nodiscard]] common::Result<void> Start(const std::wstring& targetPath) const;
};

} // namespace salvus::scanner
