#pragma once

#include <cstdint>
#include <string_view>

namespace salvus::common {

inline constexpr std::string_view kEngineName = "Salvus.Engine";
inline constexpr std::string_view kEngineVersion = "0.5.0";
inline constexpr std::string_view kDevelopmentPhase = "Phase 5 - Additional File Systems";
inline constexpr std::uint32_t kIpcProtocolVersion = 1;
inline constexpr std::uint32_t kMaxIpcFrameBytes = 16U * 1024U * 1024U; // 16 MiB

#ifdef _DEBUG
inline constexpr std::string_view kBuildConfiguration = "Debug";
#else
inline constexpr std::string_view kBuildConfiguration = "Release";
#endif

} // namespace salvus::common
