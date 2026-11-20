#include "common/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace salvus::common {

namespace {

std::mutex g_logMutex;

std::string_view LevelToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
    }
    return "INFO";
}

std::string CurrentUtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utcTm{};
#ifdef _WIN32
    gmtime_s(&utcTm, &nowTime);
#else
    gmtime_r(&nowTime, &utcTm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

} // namespace

void Logger::Log(LogLevel level, std::string_view component, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cerr << "[" << CurrentUtcTimestamp() << "] "
              << "[" << LevelToString(level) << "] "
              << "[" << component << "] "
              << message << '\n';
    std::cerr.flush();
}

void Logger::Info(std::string_view component, std::string_view message) {
    Log(LogLevel::Info, component, message);
}

void Logger::Warning(std::string_view component, std::string_view message) {
    Log(LogLevel::Warning, component, message);
}

void Logger::Error(std::string_view component, std::string_view message) {
    Log(LogLevel::Error, component, message);
}

} // namespace salvus::common
