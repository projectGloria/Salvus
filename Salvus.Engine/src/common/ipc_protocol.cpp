#include "common/ipc_protocol.hpp"
#include "common/version.hpp"
#include "device/device_manager.hpp"
#include "disk/disk_reader.hpp"
#include "filesystem/exfat_parser.hpp"
#include "filesystem/fat32_parser.hpp"
#include "filesystem/filesystem_detector.hpp"
#include "filesystem/ntfs_parser.hpp"
#include "partition/partition_analyzer.hpp"
#include "recovery/recovery_components.hpp"
#include "scanner/deep_scanner.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace salvus::common {

struct CommandDispatcher::SessionStore {
    struct CachedNtfsSession {
        std::wstring targetPath;
        std::unordered_map<std::uint64_t, salvus::filesystem::NtfsFileEntry> entriesByRecord;
    };

    mutable std::mutex mutex;
    mutable std::atomic<std::uint64_t> nextSessionSeq{1};
    mutable std::unordered_map<std::string, CachedNtfsSession> sessions;
};

CommandDispatcher::CommandDispatcher() : sessions_(std::make_unique<SessionStore>()) {}
CommandDispatcher::~CommandDispatcher() = default;

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

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
#ifdef _WIN32
    const int required = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), required);
    return wide;
#else
    return std::wstring(utf8.begin(), utf8.end());
#endif
}

std::size_t SkipWhitespace(std::string_view s, std::size_t pos) noexcept {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])) != 0) {
        ++pos;
    }
    return pos;
}

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

Result<std::uint64_t> ExtractJsonUint64Value(std::string_view json, std::size_t startPos) {
    if (startPos >= json.size() || std::isdigit(static_cast<unsigned char>(json[startPos])) == 0) {
        return Result<std::uint64_t>::Fail(StatusCode::ProtocolError, "Expected unsigned integer JSON value.");
    }
    std::size_t endPos = startPos;
    while (endPos < json.size() && std::isdigit(static_cast<unsigned char>(json[endPos])) != 0) {
        ++endPos;
    }

    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(json.data() + startPos, json.data() + endPos, value);
    if (ec != std::errc{} || ptr != json.data() + endPos) {
        return Result<std::uint64_t>::Fail(StatusCode::ProtocolError, "Invalid uint64 JSON value.");
    }
    return Result<std::uint64_t>::Ok(value);
}

std::optional<bool> ExtractJsonBoolValue(std::string_view json, std::size_t startPos) {
    if (json.substr(startPos, 4U) == "true") {
        return true;
    }
    if (json.substr(startPos, 5U) == "false") {
        return false;
    }
    return std::nullopt;
}

std::string ExtractJsonObjectSlice(std::string_view json, std::size_t startPos) {
    if (startPos >= json.size() || json[startPos] != '{') {
        return "{}";
    }
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = startPos; i < json.size(); ++i) {
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
        if (ch == '"') {
            inString = true;
        } else if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return std::string(json.substr(startPos, i - startPos + 1U));
            }
        }
    }
    return "{}";
}

std::string Base64Encode(std::span<const std::uint8_t> data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2U) / 3U) * 4U);
    std::size_t i = 0;
    while (i + 3U <= data.size()) {
        const std::uint32_t triple =
            (static_cast<std::uint32_t>(data[i]) << 16U) |
            (static_cast<std::uint32_t>(data[i + 1U]) << 8U) |
            static_cast<std::uint32_t>(data[i + 2U]);
        out.push_back(kTable[(triple >> 18U) & 0x3FU]);
        out.push_back(kTable[(triple >> 12U) & 0x3FU]);
        out.push_back(kTable[(triple >> 6U) & 0x3FU]);
        out.push_back(kTable[triple & 0x3FU]);
        i += 3U;
    }
    const std::size_t rem = data.size() - i;
    if (rem == 1U) {
        const std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16U;
        out.push_back(kTable[(v >> 18U) & 0x3FU]);
        out.push_back(kTable[(v >> 12U) & 0x3FU]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2U) {
        const std::uint32_t v =
            (static_cast<std::uint32_t>(data[i]) << 16U) |
            (static_cast<std::uint32_t>(data[i + 1U]) << 8U);
        out.push_back(kTable[(v >> 18U) & 0x3FU]);
        out.push_back(kTable[(v >> 12U) & 0x3FU]);
        out.push_back(kTable[(v >> 6U) & 0x3FU]);
        out.push_back('=');
    }
    return out;
}

std::string LowercaseExtension(const std::string& fileName) {
    const auto dot = fileName.rfind('.');
    if (dot == std::string::npos) {
        return {};
    }
    std::string ext = fileName.substr(dot + 1U);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
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

    auto protoRes = ExtractJsonUint64Value(json, *protoPos);
    if (!protoRes.ok()) {
        return Result<IpcRequest>::Fail(protoRes.error().code, protoRes.error().message);
    }

    IpcRequest req;
    req.id = std::move(idRes.value());
    req.command = std::move(cmdRes.value());
    req.protocolVersion = static_cast<std::uint32_t>(protoRes.value());

    const auto payloadPos = FindTopLevelJsonValueStart(json, "payload");
    req.payloadJson = payloadPos ? ExtractJsonObjectSlice(json, *payloadPos) : "{}";
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
                << "\"processId\":" << GetCurrentProcessIdSafe() << ","
                << "\"isElevated\":" << (salvus::device::IsProcessElevated() ? "true" : "false")
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
            "\"deviceEnumeration\":{\"implemented\":true,\"phase\":\"Phase 2\",\"notes\":\"Active native Windows physical disk and logical volume enumeration.\"},"
            "\"rawDiskReadOnly\":{\"implemented\":true,\"phase\":\"Phase 2\",\"notes\":\"Active strictly read-only sector/byte reader for disks, volumes, and raw images.\"},"
            "\"ntfsRecovery\":{\"implemented\":true,\"phase\":\"Phase 3\",\"notes\":\"Active NTFS boot sector, MFT parsing, deleted file detection, fragmented extent reconstruction, and separate-destination recovery.\"},"
            "\"deepSignatureCarving\":{\"implemented\":true,\"phase\":\"Phase 4\",\"notes\":\"Active signature database (27+ built-in file types), bounded streaming carving, boundary detection, and safe non-executing previews.\"},"
            "\"fat32ExfatGptMbr\":{\"implemented\":true,\"phase\":\"Phase 5\",\"notes\":\"Active FAT32 (LFN + clusters), exFAT (entry sets), MBR, GPT (primary & backup CRC32), and non-destructive lost partition detection.\"},"
            "\"diskImaging\":{\"implemented\":false,\"phase\":\"Phase 6\",\"notes\":\"Scheduled for Phase 6 (Professional Features).\"}"
            "}";
        return result;
    }

    if (request.command == "list_devices") {
        salvus::device::DeviceManager devMgr;
        const auto disksRes = devMgr.EnumeratePhysicalDisks();
        if (!disksRes.ok()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_DEVICE_ENUMERATION";
            result.response.errorMessage = disksRes.error().message;
            return result;
        }

        std::ostringstream payload;
        payload << "{\"isElevated\":" << (salvus::device::IsProcessElevated() ? "true" : "false")
                << ",\"devices\":[";
        const auto& disks = disksRes.value();
        for (std::size_t i = 0; i < disks.size(); ++i) {
            const auto& d = disks[i];
            if (i > 0) {
                payload << ",";
            }
            payload << "{"
                    << "\"physicalDiskIndex\":" << d.physicalDiskIndex << ","
                    << "\"devicePath\":\"" << EscapeJsonString(d.devicePath) << "\","
                    << "\"deviceName\":\"" << EscapeJsonString(d.deviceName) << "\","
                    << "\"model\":\"" << EscapeJsonString(d.model) << "\","
                    << "\"manufacturer\":\"" << EscapeJsonString(d.manufacturer) << "\","
                    << "\"connectionType\":\"" << EscapeJsonString(d.connectionType) << "\","
                    << "\"partitionLayout\":\"" << EscapeJsonString(d.partitionLayout) << "\","
                    << "\"healthStatus\":\"" << EscapeJsonString(d.healthStatus) << "\","
                    << "\"capacityBytes\":" << d.capacityBytes << ","
                    << "\"sectorSizeBytes\":" << d.sectorSizeBytes << ","
                    << "\"deviceType\":\"" << EscapeJsonString(salvus::device::StorageDeviceTypeToString(d.deviceType)) << "\","
                    << "\"isPhysicalDisk\":" << (d.isPhysicalDisk ? "true" : "false") << ","
                    << "\"isRemovable\":" << (d.isRemovable ? "true" : "false") << ","
                    << "\"isSsd\":" << (d.isSsd ? "true" : "false") << ","
                    << "\"rawAccessAvailable\":" << (d.rawAccessAvailable ? "true" : "false")
                    << "}";
        }
        payload << "]}";
        result.response.ok = true;
        result.response.payloadJson = payload.str();
        return result;
    }

    if (request.command == "list_volumes") {
        salvus::device::DeviceManager devMgr;
        const auto volsRes = devMgr.EnumerateVolumes();
        if (!volsRes.ok()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_VOLUME_ENUMERATION";
            result.response.errorMessage = volsRes.error().message;
            return result;
        }

        std::ostringstream payload;
        payload << "{\"volumes\":[";
        const auto& vols = volsRes.value();
        for (std::size_t i = 0; i < vols.size(); ++i) {
            const auto& v = vols[i];
            if (i > 0) {
                payload << ",";
            }
            payload << "{"
                    << "\"volumePath\":\"" << EscapeJsonString(v.volumePath) << "\","
                    << "\"driveLetter\":\"" << EscapeJsonString(v.driveLetter) << "\","
                    << "\"label\":\"" << EscapeJsonString(v.label) << "\","
                    << "\"fileSystem\":\"" << EscapeJsonString(v.fileSystem) << "\","
                    << "\"totalBytes\":" << v.totalBytes << ","
                    << "\"availableBytes\":" << v.availableBytes << ","
                    << "\"physicalDiskIndex\":" << v.physicalDiskIndex << ","
                    << "\"partitionOffsetBytes\":" << v.partitionOffsetBytes << ","
                    << "\"partitionLengthBytes\":" << v.partitionLengthBytes << ","
                    << "\"isRemovable\":" << (v.isRemovable ? "true" : "false")
                    << "}";
        }
        payload << "]}";
        result.response.ok = true;
        result.response.payloadJson = payload.str();
        return result;
    }

    if (request.command == "quick_scan" || request.command == "quick_scan_ntfs") {
        const auto targetPos = FindTopLevelJsonValueStart(request.payloadJson, "targetPath");
        if (!targetPos) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "quick_scan requires payload.targetPath.";
            return result;
        }
        const auto targetRes = ExtractJsonStringValue(request.payloadJson, *targetPos);
        if (!targetRes.ok() || targetRes.value().empty()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "Invalid payload.targetPath.";
            return result;
        }

        bool includeActive = false;
        std::uint64_t maxRecords = 250000ULL;
        if (const auto incPos = FindTopLevelJsonValueStart(request.payloadJson, "includeActive")) {
            if (const auto incBool = ExtractJsonBoolValue(request.payloadJson, *incPos)) {
                includeActive = *incBool;
            }
        }
        if (const auto maxPos = FindTopLevelJsonValueStart(request.payloadJson, "maxRecords")) {
            const auto maxRes = ExtractJsonUint64Value(request.payloadJson, *maxPos);
            if (maxRes.ok() && maxRes.value() > 0ULL) {
                maxRecords = maxRes.value();
            }
        }

        const std::wstring wideTarget = Utf8ToWide(targetRes.value());
        salvus::disk::DiskReader reader;
        const auto openRes = reader.OpenReadOnly(wideTarget);
        if (!openRes.ok()) {
            result.response.ok = false;
            result.response.errorCode =
                openRes.error().code == StatusCode::AccessDenied ? "ERR_ACCESS_DENIED" : "ERR_OPEN_TARGET";
            result.response.errorMessage = openRes.error().message;
            return result;
        }

        std::vector<std::uint8_t> bootSector(512U, 0U);
        const auto bootRead = reader.ReadBytes(0ULL, bootSector);
        if (!bootRead.ok() || bootRead.value() < 512U) {
            result.response.ok = false;
            result.response.errorCode = "ERR_READ_BOOT";
            result.response.errorMessage = "Failed to read volume boot sector.";
            return result;
        }

        salvus::filesystem::FileSystemDetector detector;
        const auto fsRes = detector.Detect(bootSector);
        const auto fsType = fsRes.ok() ? fsRes.value() : salvus::filesystem::FileSystemType::Raw;

        std::string fsName = salvus::filesystem::FileSystemTypeToString(fsType);
        std::uint64_t scannedCount = 0;
        std::uint32_t clusterBytes = reader.geometry().bytesPerSector;
        std::vector<salvus::filesystem::NtfsFileEntry> entries;

        if (fsType == salvus::filesystem::FileSystemType::Ntfs) {
            salvus::filesystem::NTFSParser parser;
            salvus::filesystem::NtfsScanOptions scanOpts{includeActive, maxRecords};
            const auto scanRes = parser.ScanVolume(reader, scanOpts);
            if (!scanRes.ok()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_NTFS_SCAN";
                result.response.errorMessage = scanRes.error().message;
                return result;
            }
            scannedCount = scanRes.value().mftRecordsScanned;
            clusterBytes = scanRes.value().bootSector.clusterSizeBytes;
            entries = scanRes.value().entries;
        } else if (fsType == salvus::filesystem::FileSystemType::Fat32) {
            salvus::filesystem::FAT32Parser parser;
            const auto scanRes = parser.ScanVolume(reader, includeActive);
            if (!scanRes.ok()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_FAT32_SCAN";
                result.response.errorMessage = scanRes.error().message;
                return result;
            }
            scannedCount = scanRes.value().directoriesScanned;
            clusterBytes = scanRes.value().bootSector.clusterSizeBytes;
            entries = scanRes.value().entries;
        } else if (fsType == salvus::filesystem::FileSystemType::ExFat) {
            salvus::filesystem::ExFATParser parser;
            const auto scanRes = parser.ScanVolume(reader, includeActive);
            if (!scanRes.ok()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_EXFAT_SCAN";
                result.response.errorMessage = scanRes.error().message;
                return result;
            }
            scannedCount = scanRes.value().directoriesScanned;
            clusterBytes = scanRes.value().bootSector.clusterSizeBytes;
            entries = scanRes.value().entries;
        } else {
            result.response.ok = false;
            result.response.errorCode = "ERR_UNSUPPORTED_FS";
            result.response.errorMessage =
                "Unsupported or RAW file system (" + fsName + ") for Quick Scan. Use Deep Scan for raw signature carving.";
            return result;
        }

        const std::uint64_t seq = sessions_->nextSessionSeq.fetch_add(1ULL);
        const std::string sessionId = "quick-" + std::to_string(GetCurrentProcessIdSafe()) + "-" + std::to_string(seq);

        SessionStore::CachedNtfsSession cached{};
        cached.targetPath = wideTarget;

        std::ostringstream payload;
        payload << "{"
                << "\"sessionId\":\"" << EscapeJsonString(sessionId) << "\","
                << "\"fileSystem\":\"" << EscapeJsonString(fsName) << "\","
                << "\"mftRecordsScanned\":" << scannedCount << ","
                << "\"clusterSizeBytes\":" << clusterBytes << ","
                << "\"files\":[";

        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            if (i > 0) {
                payload << ",";
            }
            payload << "{"
                    << "\"recordNumber\":" << e.recordNumber << ","
                    << "\"fileName\":\"" << EscapeJsonString(e.fileName) << "\","
                    << "\"fullPath\":\"" << EscapeJsonString(e.fullPath) << "\","
                    << "\"sizeBytes\":" << e.fileSizeBytes << ","
                    << "\"isDeleted\":" << (e.isDeleted ? "true" : "false") << ","
                    << "\"isResident\":" << (e.isResident ? "true" : "false") << ","
                    << "\"extentCount\":" << e.dataExtents.size() << ","
                    << "\"createdUtc\":\"" << EscapeJsonString(e.createdUtc) << "\","
                    << "\"modifiedUtc\":\"" << EscapeJsonString(e.modifiedUtc) << "\","
                    << "\"confidence\":\"" << EscapeJsonString(salvus::filesystem::RecoveryConfidenceToString(e.confidence)) << "\","
                    << "\"confidenceReason\":\"" << EscapeJsonString(e.confidenceReason) << "\","
                    << "\"alternateDataStreams\":[";
            for (std::size_t a = 0; a < e.alternateDataStreams.size(); ++a) {
                if (a > 0) {
                    payload << ",";
                }
                payload << "\"" << EscapeJsonString(e.alternateDataStreams[a]) << "\"";
            }
            payload << "]}";

            cached.entriesByRecord.emplace(e.recordNumber, e);
        }
        payload << "]}";

        {
            std::lock_guard<std::mutex> lock(sessions_->mutex);
            sessions_->sessions[sessionId] = std::move(cached);
        }

        result.response.ok = true;
        result.response.payloadJson = payload.str();
        return result;
    }

    if (request.command == "analyze_partitions") {
        const auto targetPos = FindTopLevelJsonValueStart(request.payloadJson, "targetPath");
        if (!targetPos) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "analyze_partitions requires payload.targetPath.";
            return result;
        }
        const auto targetRes = ExtractJsonStringValue(request.payloadJson, *targetPos);
        if (!targetRes.ok() || targetRes.value().empty()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "Invalid payload.targetPath.";
            return result;
        }

        salvus::disk::DiskReader reader;
        const auto openRes = reader.OpenReadOnly(Utf8ToWide(targetRes.value()));
        if (!openRes.ok()) {
            result.response.ok = false;
            result.response.errorCode =
                openRes.error().code == StatusCode::AccessDenied ? "ERR_ACCESS_DENIED" : "ERR_OPEN_TARGET";
            result.response.errorMessage = openRes.error().message;
            return result;
        }

        salvus::partition::PartitionAnalyzer analyzer;
        const auto partRes = analyzer.Analyze(reader, true);
        if (!partRes.ok()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_PARTITION_ANALYSIS";
            result.response.errorMessage = partRes.error().message;
            return result;
        }

        const auto& analysis = partRes.value();
        std::ostringstream payload;
        payload << "{"
                << "\"scheme\":\"" << EscapeJsonString(salvus::partition::PartitionSchemeToString(analysis.scheme)) << "\","
                << "\"primaryGptValid\":" << (analysis.primaryGptValid ? "true" : "false") << ","
                << "\"backupGptValid\":" << (analysis.backupGptValid ? "true" : "false") << ","
                << "\"partitions\":[";

        for (std::size_t i = 0; i < analysis.partitions.size(); ++i) {
            const auto& p = analysis.partitions[i];
            if (i > 0) {
                payload << ",";
            }
            payload << "{"
                    << "\"index\":" << p.index << ","
                    << "\"startOffsetBytes\":" << p.startOffsetBytes << ","
                    << "\"lengthBytes\":" << p.lengthBytes << ","
                    << "\"name\":\"" << EscapeJsonString(p.name) << "\","
                    << "\"typeDescription\":\"" << EscapeJsonString(p.typeDescription) << "\","
                    << "\"detectedFileSystem\":\"" << EscapeJsonString(p.detectedFileSystem) << "\","
                    << "\"isLostCandidate\":" << (p.isLostCandidate ? "true" : "false") << ","
                    << "\"isFromBackupGpt\":" << (p.isFromBackupGpt ? "true" : "false")
                    << "}";
        }
        payload << "]}";
        result.response.ok = true;
        result.response.payloadJson = payload.str();
        return result;
    }

    if (request.command == "recover_file") {
        const auto sessPos = FindTopLevelJsonValueStart(request.payloadJson, "sessionId");
        const auto recPos = FindTopLevelJsonValueStart(request.payloadJson, "recordNumber");
        const auto destPos = FindTopLevelJsonValueStart(request.payloadJson, "destinationPath");

        if (!sessPos || !recPos || !destPos) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "recover_file requires sessionId, recordNumber, and destinationPath.";
            return result;
        }

        const auto sessRes = ExtractJsonStringValue(request.payloadJson, *sessPos);
        const auto recRes = ExtractJsonUint64Value(request.payloadJson, *recPos);
        const auto destRes = ExtractJsonStringValue(request.payloadJson, *destPos);
        if (!sessRes.ok() || !recRes.ok() || !destRes.ok() || destRes.value().empty()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "Malformed recover_file arguments.";
            return result;
        }

        std::wstring sourceTarget;
        salvus::filesystem::NtfsFileEntry fileEntry{};
        {
            std::lock_guard<std::mutex> lock(sessions_->mutex);
            const auto sIt = sessions_->sessions.find(sessRes.value());
            if (sIt == sessions_->sessions.end()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_SESSION_NOT_FOUND";
                result.response.errorMessage = "Scan session not found in engine memory.";
                return result;
            }
            const auto eIt = sIt->second.entriesByRecord.find(recRes.value());
            if (eIt == sIt->second.entriesByRecord.end()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_RECORD_NOT_FOUND";
                result.response.errorMessage = "File record not found in scan session.";
                return result;
            }
            sourceTarget = sIt->second.targetPath;
            fileEntry = eIt->second;
        }

        salvus::recovery::RecoveryWriter writer;
        const auto writeRes = writer.WriteRecoveredFile(
            sourceTarget,
            Utf8ToWide(destRes.value()),
            fileEntry.isResident,
            fileEntry.residentData,
            fileEntry.dataExtents,
            fileEntry.fileSizeBytes);
        if (!writeRes.ok()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_RECOVERY_WRITE";
            result.response.errorMessage = writeRes.error().message;
            return result;
        }

        std::ostringstream payload;
        payload << "{"
                << "\"bytesWritten\":" << writeRes.value().bytesWritten << ","
                << "\"destinationPath\":\"" << EscapeJsonString(destRes.value()) << "\","
                << "\"fileName\":\"" << EscapeJsonString(fileEntry.fileName) << "\""
                << "}";
        result.response.ok = true;
        result.response.payloadJson = payload.str();
        return result;
    }

    if (request.command == "deep_scan") {
        const auto targetPos = FindTopLevelJsonValueStart(request.payloadJson, "targetPath");
        if (!targetPos) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "deep_scan requires payload.targetPath.";
            return result;
        }
        const auto targetRes = ExtractJsonStringValue(request.payloadJson, *targetPos);
        if (!targetRes.ok() || targetRes.value().empty()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "Invalid payload.targetPath.";
            return result;
        }

        salvus::scanner::DeepScanOptions scanOpts{};
        if (const auto maxPos = FindTopLevelJsonValueStart(request.payloadJson, "maxFiles")) {
            const auto maxRes = ExtractJsonUint64Value(request.payloadJson, *maxPos);
            if (maxRes.ok() && maxRes.value() > 0ULL) {
                scanOpts.maxFiles = maxRes.value();
            }
        }

        const std::wstring wideTarget = Utf8ToWide(targetRes.value());
        salvus::disk::DiskReader reader;
        const auto openRes = reader.OpenReadOnly(wideTarget);
        if (!openRes.ok()) {
            result.response.ok = false;
            result.response.errorCode =
                openRes.error().code == StatusCode::AccessDenied ? "ERR_ACCESS_DENIED" : "ERR_OPEN_TARGET";
            result.response.errorMessage = openRes.error().message;
            return result;
        }

        salvus::scanner::DeepScanner scanner;
        const auto scanRes = scanner.Scan(reader, scanOpts);
        if (!scanRes.ok()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_DEEP_SCAN";
            result.response.errorMessage = scanRes.error().message;
            return result;
        }

        const std::uint64_t seq = sessions_->nextSessionSeq.fetch_add(1ULL);
        const std::string sessionId = "deep-" + std::to_string(GetCurrentProcessIdSafe()) + "-" + std::to_string(seq);

        SessionStore::CachedNtfsSession cached{};
        cached.targetPath = wideTarget;

        std::ostringstream payload;
        payload << "{"
                << "\"sessionId\":\"" << EscapeJsonString(sessionId) << "\","
                << "\"fileSystem\":\"RAW (Signature Carving)\","
                << "\"mftRecordsScanned\":" << scanRes.value().bytesScanned << ","
                << "\"clusterSizeBytes\":" << reader.geometry().bytesPerSector << ","
                << "\"files\":[";

        const auto& carved = scanRes.value().files;
        for (std::size_t i = 0; i < carved.size(); ++i) {
            const auto& c = carved[i];
            if (i > 0) {
                payload << ",";
            }
            const std::string virtualPath = "\\[Carved]\\" + c.category + "\\" + c.fileName;
            payload << "{"
                    << "\"recordNumber\":" << c.carvedId << ","
                    << "\"fileName\":\"" << EscapeJsonString(c.fileName) << "\","
                    << "\"fullPath\":\"" << EscapeJsonString(virtualPath) << "\","
                    << "\"sizeBytes\":" << c.estimatedSizeBytes << ","
                    << "\"isDeleted\":true,"
                    << "\"isResident\":false,"
                    << "\"extentCount\":1,"
                    << "\"createdUtc\":\"\","
                    << "\"modifiedUtc\":\"\","
                    << "\"confidence\":\"" << EscapeJsonString(salvus::filesystem::RecoveryConfidenceToString(c.confidence)) << "\","
                    << "\"confidenceReason\":\"" << EscapeJsonString(c.confidenceReason) << "\","
                    << "\"alternateDataStreams\":[]"
                    << "}";

            salvus::filesystem::NtfsFileEntry cachedEntry{};
            cachedEntry.recordNumber = c.carvedId;
            cachedEntry.fileName = c.fileName;
            cachedEntry.fullPath = virtualPath;
            cachedEntry.isDeleted = true;
            cachedEntry.isResident = false;
            cachedEntry.fileSizeBytes = c.estimatedSizeBytes;
            cachedEntry.confidence = c.confidence;
            cachedEntry.confidenceReason = c.confidenceReason;
            cachedEntry.dataExtents.push_back(salvus::recovery::FileExtent{
                c.diskOffsetBytes, c.estimatedSizeBytes, false});
            cached.entriesByRecord.emplace(c.carvedId, std::move(cachedEntry));
        }
        payload << "]}";

        {
            std::lock_guard<std::mutex> lock(sessions_->mutex);
            sessions_->sessions[sessionId] = std::move(cached);
        }

        result.response.ok = true;
        result.response.payloadJson = payload.str();
        return result;
    }

    if (request.command == "get_file_preview") {
        const auto sessPos = FindTopLevelJsonValueStart(request.payloadJson, "sessionId");
        const auto recPos = FindTopLevelJsonValueStart(request.payloadJson, "recordNumber");
        if (!sessPos || !recPos) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "get_file_preview requires sessionId and recordNumber.";
            return result;
        }
        const auto sessRes = ExtractJsonStringValue(request.payloadJson, *sessPos);
        const auto recRes = ExtractJsonUint64Value(request.payloadJson, *recPos);
        if (!sessRes.ok() || !recRes.ok()) {
            result.response.ok = false;
            result.response.errorCode = "ERR_INVALID_ARGUMENT";
            result.response.errorMessage = "Malformed get_file_preview arguments.";
            return result;
        }

        std::wstring sourceTarget;
        salvus::filesystem::NtfsFileEntry fileEntry{};
        {
            std::lock_guard<std::mutex> lock(sessions_->mutex);
            const auto sIt = sessions_->sessions.find(sessRes.value());
            if (sIt == sessions_->sessions.end()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_SESSION_NOT_FOUND";
                result.response.errorMessage = "Scan session not found.";
                return result;
            }
            const auto eIt = sIt->second.entriesByRecord.find(recRes.value());
            if (eIt == sIt->second.entriesByRecord.end()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_RECORD_NOT_FOUND";
                result.response.errorMessage = "File record not found.";
                return result;
            }
            sourceTarget = sIt->second.targetPath;
            fileEntry = eIt->second;
        }

        const std::string ext = LowercaseExtension(fileEntry.fileName);
        const bool isImage = (ext == "jpg" || ext == "jpeg" || ext == "png" ||
                              ext == "gif" || ext == "bmp" || ext == "ico");
        const bool isText = (ext == "txt" || ext == "log" || ext == "md" || ext == "json" ||
                             ext == "xml" || ext == "csv" || ext == "ini" || ext == "cfg" ||
                             ext == "rtf" || ext == "c" || ext == "cpp" || ext == "h" || ext == "cs");
        const bool isPdf = (ext == "pdf");

        if (!isImage && !isText && !isPdf) {
            result.response.ok = true;
            result.response.payloadJson =
                "{\"previewKind\":\"unsupported\",\"textContent\":\"Safe preview is supported for images, plain text, and PDF metadata only. Recovered files are never executed.\",\"base64Bytes\":\"\",\"bytesRead\":0,\"truncated\":false}";
            return result;
        }

        const std::uint64_t previewLimit = isImage ? (512ULL * 1024ULL) : (64ULL * 1024ULL);
        const std::uint64_t bytesToRead = (std::min)(fileEntry.fileSizeBytes, previewLimit);
        const bool truncated = fileEntry.fileSizeBytes > previewLimit;

        std::vector<std::uint8_t> previewBytes;
        if (fileEntry.isResident) {
            const auto count = static_cast<std::size_t>((std::min)(
                static_cast<std::uint64_t>(fileEntry.residentData.size()), bytesToRead));
            previewBytes.assign(fileEntry.residentData.begin(), fileEntry.residentData.begin() + count);
        } else if (bytesToRead > 0ULL) {
            salvus::disk::DiskReader reader;
            const auto openRes = reader.OpenReadOnly(sourceTarget);
            if (!openRes.ok()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_OPEN_TARGET";
                result.response.errorMessage = openRes.error().message;
                return result;
            }
            salvus::recovery::FileReconstructor reconstructor;
            const auto bufRes = reconstructor.ReconstructToBuffer(
                reader, fileEntry.dataExtents, bytesToRead, previewLimit);
            if (!bufRes.ok()) {
                result.response.ok = false;
                result.response.errorCode = "ERR_PREVIEW_READ";
                result.response.errorMessage = bufRes.error().message;
                return result;
            }
            previewBytes = std::move(bufRes.value());
        }

        std::ostringstream payload;
        if (isImage) {
            payload << "{"
                    << "\"previewKind\":\"image\","
                    << "\"textContent\":\"\","
                    << "\"base64Bytes\":\"" << Base64Encode(previewBytes) << "\","
                    << "\"bytesRead\":" << previewBytes.size() << ","
                    << "\"truncated\":" << (truncated ? "true" : "false")
                    << "}";
        } else if (isPdf) {
            std::string headerLine = "PDF Document (non-executing metadata preview)";
            if (previewBytes.size() >= 8U && std::memcmp(previewBytes.data(), "%PDF-", 5U) == 0) {
                std::size_t nl = 5U;
                while (nl < previewBytes.size() && nl < 24U && previewBytes[nl] != '\r' && previewBytes[nl] != '\n') {
                    ++nl;
                }
                headerLine = "PDF Header: " + std::string(reinterpret_cast<const char*>(previewBytes.data()), nl) +
                             " | Declared size: " + std::to_string(fileEntry.fileSizeBytes) + " bytes";
            }
            payload << "{"
                    << "\"previewKind\":\"text\","
                    << "\"textContent\":\"" << EscapeJsonString(headerLine) << "\","
                    << "\"base64Bytes\":\"\","
                    << "\"bytesRead\":" << previewBytes.size() << ","
                    << "\"truncated\":" << (truncated ? "true" : "false")
                    << "}";
        } else {
            std::string sanitized;
            sanitized.reserve(previewBytes.size());
            for (const std::uint8_t b : previewBytes) {
                if (b == '\r' || b == '\n' || b == '\t' || (b >= 0x20U && b <= 0x7EU)) {
                    sanitized.push_back(static_cast<char>(b));
                } else {
                    sanitized.push_back('.');
                }
            }
            payload << "{"
                    << "\"previewKind\":\"text\","
                    << "\"textContent\":\"" << EscapeJsonString(sanitized) << "\","
                    << "\"base64Bytes\":\"\","
                    << "\"bytesRead\":" << previewBytes.size() << ","
                    << "\"truncated\":" << (truncated ? "true" : "false")
                    << "}";
        }

        result.response.ok = true;
        result.response.payloadJson = payload.str();
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
