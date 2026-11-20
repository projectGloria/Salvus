#pragma once

#include "common/result.hpp"

#include <string>

namespace salvus::scanner {

class DeepScanner {
public:
    // Phase 4: Signature-based carving when file-system metadata is missing or damaged.
    [[nodiscard]] common::Result<void> Start(const std::wstring& targetPath) const;
};

} // namespace salvus::scanner
