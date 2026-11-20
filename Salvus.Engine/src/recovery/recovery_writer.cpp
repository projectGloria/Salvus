#include "recovery/recovery_components.hpp"

namespace salvus::recovery {

common::Result<void> RecoveryWriter::WriteRecoveredFile(
    const std::wstring& /*sourceDevicePath*/,
    const std::wstring& /*destinationFilePath*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "Recovery file writing is scheduled for Phase 3 (NTFS Recovery).");
}

} // namespace salvus::recovery
