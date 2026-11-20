#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace salvus::common {

struct IpcRequest {
    std::string id;
    std::uint32_t protocolVersion = 0;
    std::string command;
    std::string payloadJson = "{}";
};

struct IpcResponse {
    std::string id;
    std::uint32_t protocolVersion = 1;
    bool ok = false;
    std::string errorCode;
    std::string errorMessage;
    std::string payloadJson = "{}";
};

struct DispatchResult {
    IpcResponse response;
    bool shutdownRequested = false;
};

// Length-prefixed binary framing (4-byte little-endian uint32 length + UTF-8 JSON payload)
Result<std::vector<std::uint8_t>> EncodeIpcFrame(std::string_view jsonPayload);
Result<std::uint32_t> DecodeIpcFrameLength(std::span<const std::uint8_t> headerBytes);

// Safe JSON escaping and envelope serialization/parsing
std::string EscapeJsonString(std::string_view input);
Result<IpcRequest> ParseIpcRequest(std::string_view json);
std::string SerializeIpcResponse(const IpcResponse& response);

// Dispatches Phase 1 engine IPC commands
class CommandDispatcher {
public:
    [[nodiscard]] DispatchResult Dispatch(const IpcRequest& request) const;
};

} // namespace salvus::common
