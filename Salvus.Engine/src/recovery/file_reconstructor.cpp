#include "recovery/recovery_components.hpp"

namespace salvus::recovery {

common::Result<void> FileReconstructor::Reconstruct(const std::vector<FileExtent>& /*extents*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "File reconstruction is scheduled for Phase 3 (NTFS Recovery).");
}

} // namespace salvus::recovery
