#pragma once

#include <string_view>

namespace salvus::common {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static void Log(LogLevel level, std::string_view component, std::string_view message);
    static void Info(std::string_view component, std::string_view message);
    static void Warning(std::string_view component, std::string_view message);
    static void Error(std::string_view component, std::string_view message);
};

} // namespace salvus::common
