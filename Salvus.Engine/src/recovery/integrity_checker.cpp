#include "recovery/recovery_components.hpp"

namespace salvus::recovery {

common::Result<std::string> IntegrityChecker::ComputeSha256(const std::wstring& /*filePath*/) const {
    return common::Result<std::string>::Fail(
        common::StatusCode::NotImplemented,
        "SHA-256 integrity checking is scheduled for Phase 6 (Professional Features).");
}

} // namespace salvus::recovery
