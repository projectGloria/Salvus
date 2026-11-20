#include "recovery/recovery_components.hpp"
#include "common/ipc_protocol.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace salvus::recovery {

common::Result<std::string> RecoveryReportGenerator::GenerateJsonReport(const std::string& sessionId) const {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utcTm{};
#ifdef _WIN32
    gmtime_s(&utcTm, &nowTime);
#else
    gmtime_r(&nowTime, &utcTm);
#endif
    std::ostringstream ts;
    ts << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%SZ");

    std::ostringstream oss;
    oss << "{"
        << "\"generator\":\"Salvus.Engine\","
        << "\"sessionId\":\"" << common::EscapeJsonString(sessionId) << "\","
        << "\"generatedUtc\":\"" << ts.str() << "\""
        << "}";
    return common::Result<std::string>::Ok(oss.str());
}

} // namespace salvus::recovery
