#include "common/ipc_protocol.hpp"
#include "common/version.hpp"

#include <charconv>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace salvus::common {

namespace {

std::string CurrentUtcIso8601() {
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

std::uint32_t GetCurrentProcessIdSafe() noexcept {
#ifdef _WIN32
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return 0;
#endif
}

std::size_t SkipWhitespace(std::string_view s, std::size_t pos) noexcept {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])) != 0) {
        ++pos;
    }
    return pos;
}

// Finds `"key"` followed by `:` at top-level JSON object scope and returns index after `:`.
std::optional<std::size_t> FindTopLevelJsonValueStart(std::string_view json, std::string_view key) {
    std::size_t i = SkipWhitespace(json, 0);
    if (i >= json.size() || json[i] != '{') {
        return std::nullopt;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (; i < json.size(); ++i) {
        const char ch = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '{' || ch == '[') {
            ++depth;
        } else if (ch == '}' || ch == ']') {
            --depth;
        } else if (ch == '"' && depth == 1) {
            const std::size_t keyStart = i + 1;
            std::size_t keyEnd = keyStart;
            bool keyEscaped = false;
            while (keyEnd < json.size()) {
                if (keyEscaped) {
                    keyEscaped = false;
                } else if (json[keyEnd] == '\\') {
                    keyEscaped = true;
                } else if (json[keyEnd] == '"') {
                    break;
                }
                ++keyEnd;
            }
            if (keyEnd >= json.size()) {
                return std::nullopt;
            }

            const std::string_view candidate = json.substr(keyStart, keyEnd - keyStart);
            const std::size_t afterKey = SkipWhitespace(json, keyEnd + 1);
            if (afterKey < json.size() && json[afterKey] == ':') {
                if (candidate == key) {
                    return SkipWhitespace(json, afterKey + 1);
                }
                i = afterKey;
                continue;
            }
            i = keyEnd;
        } else if (ch == '"') {
            inString = true;
        }
    }

    return std::nullopt;
}

Result<std::string> ExtractJsonStringValue(std::string_view json, std::size_t startPos) {
    if (startPos >= json.size() || json[startPos] != '"') {
        return Result<std::string>::Fail(StatusCode::ProtocolError, "Expected JSON string value.");
    }

    std::string out;
    std::size_t i = startPos + 1;
    while (i < json.size()) {
        const char ch = json[i++];
        if (ch == '"') {
            return Result<std::string>::Ok(std::move(out));
        }
        if (ch == '\\') {
            if (i >= json.size()) {
                return Result<std::string>::Fail(StatusCode::ProtocolError, "Unterminated JSON escape sequence.");
            }
            const char esc = json[i++];
            switch (esc) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                default:
                    return Result<std::string>::Fail(StatusCode::ProtocolError, "Unsupported JSON escape sequence.");
            }
        } else {
            out.push_back(ch);
        }
    }

    return Result<std::string>::Fail(StatusCode::ProtocolError, "Unterminated JSON string.");
}

Result<std::uint32_t> ExtractJsonUint32Value(std::string_view json, std::size_t startPos) {
    if (startPos >= json.size() || std::isdigit(static_cast<unsigned char>(json[startPos])) == 0) {
        return Result<std::uint32_t>::Fail(StatusCode::ProtocolError, "Expected unsigned integer JSON value.");
    }
    std::size_t endPos = startPos;
    while (endPos < json.size() && std::isdigit(static_cast<unsigned char>(json[endPos])) != 0) {
        ++endPos;
    }

    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(json.data() + startPos, json.data() + endPos, value);
    if (ec != std::errc{} || ptr != json.data() + endPos) {
        return Result<std::uint32_t>::Fail(StatusCode::ProtocolError, "Invalid uint32 JSON value.");
    }
    return Result<std::uint32_t>::Ok(value);
}

} // namespace

Result<std::vector<std::uint8_t>> EncodeIpcFrame(std::string_view jsonPayload) {
    if (jsonPayload.empty()) {
        return Result<std::vector<std::uint8_t>>::Fail(
            StatusCode::InvalidArgument, "IPC payload must not be empty.");
    }
    if (jsonPayload.size() > kMaxIpcFrameBytes) {
        return Result<std::vector<std::uint8_t>>::Fail(
            StatusCode::OutOfBounds, "IPC payload exceeds maximum allowed frame size.");
    }

    const auto length = static_cast<std::uint32_t>(jsonPayload.size());
    std::vector<std::uint8_t> frame(4U + jsonPayload.size());
    frame[0] = static_cast<std::uint8_t>(length & 0xFFU);
    frame[1] = static_cast<std::uint8_t>((length >> 8U) & 0xFFU);
    frame[2] = static_cast<std::uint8_t>((length >> 16U) & 0xFFU);
    frame[3] = static_cast<std::uint8_t>((length >> 24U) & 0xFFU);

    for (std::size_t i = 0; i < jsonPayload.size(); ++i) {
        frame[4U + i] = static_cast<std::uint8_t>(jsonPayload[i]);
    }
    return Result<std::vector<std::uint8_t>>::Ok(std::move(frame));
}

Result<std::uint32_t> DecodeIpcFrameLength(std::span<const std::uint8_t> headerBytes) {
    if (headerBytes.size() != 4U) {
        return Result<std::uint32_t>::Fail(
            StatusCode::ProtocolError, "IPC frame header must be exactly 4 bytes.");
    }

    const std::uint32_t length =
        static_cast<std::uint32_t>(headerBytes[0]) |
        (static_cast<std::uint32_t>(headerBytes[1]) << 8U) |
        (static_cast<std::uint32_t>(headerBytes[2]) << 16U) |
        (static_cast<std::uint32_t>(headerBytes[3]) << 24U);

    if (length == 0U) {
        return Result<std::uint32_t>::Fail(
            StatusCode::ProtocolError, "IPC frame length must be greater than zero.");
    }
    if (length > kMaxIpcFrameBytes) {
        return Result<std::uint32_t>::Fail(
            StatusCode::OutOfBounds, "IPC frame length exceeds maximum allowed size.");
    }
    return Result<std::uint32_t>::Ok(length);
}

std::string EscapeJsonString(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8U);
    for (const char ch : input) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20U) {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(static_cast<unsigned char>(ch));
                    out += oss.str();
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

Result<IpcRequest> ParseIpcRequest(std::string_view json) {
    if (json.empty() || json.size() > kMaxIpcFrameBytes) {
        return Result<IpcRequest>::Fail(StatusCode::ProtocolError, "Invalid IPC JSON payload size.");
    }

    const auto idPos = FindTopLevelJsonValueStart(json, "id");
    const auto cmdPos = FindTopLevelJsonValueStart(json, "command");
    const auto protoPos = FindTopLevelJsonValueStart(json, "protocolVersion");

    if (!idPos || !cmdPos || !protoPos) {
        return Result<IpcRequest>::Fail(
            StatusCode::ProtocolError, "IPC request missing required fields (id, protocolVersion, command).");
    }

    auto idRes = ExtractJsonStringValue(json, *idPos);
    if (!idRes.ok()) {
        return Result<IpcRequest>::Fail(idRes.error().code, idRes.error().message);
    }

    auto cmdRes = ExtractJsonStringValue(json, *cmdPos);
    if (!cmdRes.ok()) {
        return Result<IpcRequest>::Fail(cmdRes.error().code, cmdRes.error().message);
    }

    auto protoRes = ExtractJsonUint32Value(json, *protoPos);
    if (!protoRes.ok()) {
        return Result<IpcRequest>::Fail(protoRes.error().code, protoRes.error().message);
    }

    IpcRequest req;
    req.id = std::move(idRes.value());
    req.command = std::move(cmdRes.value());
    req.protocolVersion = protoRes.value();
    req.payloadJson = "{}";
    return Result<IpcRequest>::Ok(std::move(req));
}

std::string SerializeIpcResponse(const IpcResponse& response) {
    std::ostringstream oss;
    oss << "{"
        << "\"id\":\"" << EscapeJsonString(response.id) << "\","
        << "\"protocolVersion\":" << response.protocolVersion << ","
        << "\"ok\":" << (response.ok ? "true" : "false") << ","
        << "\"errorCode\":\"" << EscapeJsonString(response.errorCode) << "\","
        << "\"errorMessage\":\"" << EscapeJsonString(response.errorMessage) << "\","
        << "\"payload\":" << (response.payloadJson.empty() ? "{}" : response.payloadJson)
        << "}";
    return oss.str();
}

DispatchResult CommandDispatcher::Dispatch(const IpcRequest& request) const {
    DispatchResult result{};
    result.response.id = request.id;
    result.response.protocolVersion = kIpcProtocolVersion;

    if (request.protocolVersion != kIpcProtocolVersion) {
        result.response.ok = false;
        result.response.errorCode = "ERR_UNSUPPORTED_PROTOCOL";
        result.response.errorMessage = "Unsupported IPC protocol version.";
        return result;
    }

    if (request.command == "ping") {
        result.response.ok = true;
        std::ostringstream payload;
        payload << "{"
                << "\"status\":\"ready\","
                << "\"processId\":" << GetCurrentProcessIdSafe() << ","
                << "\"utcTimestamp\":\"" << CurrentUtcIso8601() << "\""
                << "}";
        result.response.payloadJson = payload.str();
        return result;
    }

    if (request.command == "get_engine_info") {
        result.response.ok = true;
        std::ostringstream payload;
        payload << "{"
                << "\"engineName\":\"" << kEngineName << "\","
                << "\"engineVersion\":\"" << kEngineVersion << "\","
                << "\"developmentPhase\":\"" << kDevelopmentPhase << "\","
                << "\"protocolVersion\":" << kIpcProtocolVersion << ","
                << "\"buildConfiguration\":\"" << kBuildConfiguration << "\","
                << "\"platform\":\"Windows x64\","
                << "\"processId\":" << GetCurrentProcessIdSafe()
                << "}";
        result.response.payloadJson = payload.str();
        return result;
    }

    if (request.command == "get_capabilities") {
        result.response.ok = true;
        result.response.payloadJson =
            "{"
            "\"ipcNamedPipes\":{\"implemented\":true,\"phase\":\"Phase 1\",\"notes\":\"Active length-prefixed JSON over secured Windows Named Pipes.\"},"
            "\"diskOffsetValidation\":{\"implemented\":true,\"phase\":\"Phase 1\",\"notes\":\"Active bounds, overflow, and sector alignment validation.\"},"
            "\"deviceEnumeration\":{\"implemented\":false,\"phase\":\"Phase 2\",\"notes\":\"Scheduled for Phase 2 (Storage Access).\"},"
            "\"rawDiskReadOnly\":{\"implemented\":false,\"phase\":\"Phase 2\",\"notes\":\"Scheduled for Phase 2 (Storage Access).\"},"
            "\"ntfsRecovery\":{\"implemented\":false,\"phase\":\"Phase 3\",\"notes\":\"Scheduled for Phase 3 (NTFS Recovery).\"},"
            "\"deepSignatureCarving\":{\"implemented\":false,\"phase\":\"Phase 4\",\"notes\":\"Scheduled for Phase 4 (Deep Scanning).\"},"
            "\"fat32ExfatGptMbr\":{\"implemented\":false,\"phase\":\"Phase 5\",\"notes\":\"Scheduled for Phase 5 (Additional File Systems).\"},"
            "\"diskImaging\":{\"implemented\":false,\"phase\":\"Phase 6\",\"notes\":\"Scheduled for Phase 6 (Professional Features).\"}"
            "}";
        return result;
    }

    if (request.command == "shutdown") {
        result.response.ok = true;
        result.response.payloadJson = "{\"shuttingDown\":true}";
        result.shutdownRequested = true;
        return result;
    }

    result.response.ok = false;
    result.response.errorCode = "ERR_UNKNOWN_COMMAND";
    result.response.errorMessage = "Unknown or unimplemented IPC command: " + request.command;
    return result;
}

} // namespace salvus::common
