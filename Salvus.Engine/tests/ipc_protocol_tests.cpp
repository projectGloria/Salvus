#include "common/ipc_protocol.hpp"
#include "common/version.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

using namespace salvus::common;

TEST(IpcFramingTests, EncodeAndDecodeRoundTrip) {
    const std::string payload = R"({"id":"req-1","protocolVersion":1,"command":"ping"})";
    const auto encoded = EncodeIpcFrame(payload);
    ASSERT_TRUE(encoded.ok());
    ASSERT_EQ(encoded.value().size(), 4U + payload.size());

    const auto decodedLen = DecodeIpcFrameLength(
        std::span<const std::uint8_t>(encoded.value().data(), 4U));
    ASSERT_TRUE(decodedLen.ok());
    EXPECT_EQ(decodedLen.value(), payload.size());
}

TEST(IpcFramingTests, RejectsEmptyPayload) {
    const auto encoded = EncodeIpcFrame("");
    EXPECT_FALSE(encoded.ok());
    EXPECT_EQ(encoded.error().code, StatusCode::InvalidArgument);
}

TEST(IpcFramingTests, RejectsZeroAndOversizedFrameHeaders) {
    const std::array<std::uint8_t, 4> zeroHeader{0, 0, 0, 0};
    const auto zeroRes = DecodeIpcFrameLength(zeroHeader);
    EXPECT_FALSE(zeroRes.ok());
    EXPECT_EQ(zeroRes.error().code, StatusCode::ProtocolError);

    const std::uint32_t oversized = kMaxIpcFrameBytes + 1U;
    const std::array<std::uint8_t, 4> oversizedHeader{
        static_cast<std::uint8_t>(oversized & 0xFFU),
        static_cast<std::uint8_t>((oversized >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((oversized >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((oversized >> 24U) & 0xFFU)};
    const auto overRes = DecodeIpcFrameLength(oversizedHeader);
    EXPECT_FALSE(overRes.ok());
    EXPECT_EQ(overRes.error().code, StatusCode::OutOfBounds);
}

TEST(IpcProtocolTests, ParsesValidRequestAndDispatchesPing) {
    const std::string json = R"({"id":"abc-123","protocolVersion":1,"command":"ping","payload":{}})";
    const auto parsed = ParseIpcRequest(json);
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value().id, "abc-123");
    EXPECT_EQ(parsed.value().protocolVersion, 1U);
    EXPECT_EQ(parsed.value().command, "ping");

    CommandDispatcher dispatcher;
    const auto dispatch = dispatcher.Dispatch(parsed.value());
    EXPECT_TRUE(dispatch.response.ok);
    EXPECT_FALSE(dispatch.shutdownRequested);
    EXPECT_EQ(dispatch.response.id, "abc-123");

    const std::string serialized = SerializeIpcResponse(dispatch.response);
    EXPECT_NE(serialized.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(serialized.find("\"status\":\"ready\""), std::string::npos);
}

TEST(IpcProtocolTests, DispatchesEngineInfoAndCapabilities) {
    CommandDispatcher dispatcher;

    IpcRequest infoReq{"info-1", kIpcProtocolVersion, "get_engine_info", "{}"};
    const auto infoRes = dispatcher.Dispatch(infoReq);
    EXPECT_TRUE(infoRes.response.ok);
    EXPECT_NE(infoRes.response.payloadJson.find("\"engineName\":\"Salvus.Engine\""), std::string::npos);

    IpcRequest capReq{"cap-1", kIpcProtocolVersion, "get_capabilities", "{}"};
    const auto capRes = dispatcher.Dispatch(capReq);
    EXPECT_TRUE(capRes.response.ok);
    EXPECT_NE(capRes.response.payloadJson.find("\"ntfsRecovery\":{\"implemented\":false"), std::string::npos);
}

TEST(IpcProtocolTests, RejectsUnknownCommandAndUnsupportedProtocol) {
    CommandDispatcher dispatcher;

    IpcRequest unknownReq{"u-1", kIpcProtocolVersion, "format_disk", "{}"};
    const auto unknownRes = dispatcher.Dispatch(unknownReq);
    EXPECT_FALSE(unknownRes.response.ok);
    EXPECT_EQ(unknownRes.response.errorCode, "ERR_UNKNOWN_COMMAND");

    IpcRequest badProtoReq{"p-1", 99U, "ping", "{}"};
    const auto badProtoRes = dispatcher.Dispatch(badProtoReq);
    EXPECT_FALSE(badProtoRes.response.ok);
    EXPECT_EQ(badProtoRes.response.errorCode, "ERR_UNSUPPORTED_PROTOCOL");
}

TEST(IpcProtocolTests, ShutdownSetsShutdownFlag) {
    CommandDispatcher dispatcher;
    IpcRequest shutdownReq{"s-1", kIpcProtocolVersion, "shutdown", "{}"};
    const auto res = dispatcher.Dispatch(shutdownReq);
    EXPECT_TRUE(res.response.ok);
    EXPECT_TRUE(res.shutdownRequested);
}

} // namespace
