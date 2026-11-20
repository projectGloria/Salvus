#include "common/ipc_server.hpp"
#include "common/logger.hpp"
#include "common/version.hpp"

#include <array>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#endif

namespace salvus::common {

namespace {

#ifdef _WIN32

struct ScopedHandle {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~ScopedHandle() {
        if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
            ::CloseHandle(handle);
        }
    }
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE h) : handle(h) {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
};

struct ScopedSecurityDescriptor {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ~ScopedSecurityDescriptor() {
        if (descriptor != nullptr) {
            ::LocalFree(descriptor);
        }
    }
};

bool BuildCurrentUserPipeSecurity(ScopedSecurityDescriptor& outSd, SECURITY_ATTRIBUTES& outSa) {
    HANDLE rawToken = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        return false;
    }
    ScopedHandle token(rawToken);

    DWORD requiredBytes = 0;
    ::GetTokenInformation(token.handle, TokenUser, nullptr, 0, &requiredBytes);
    if (requiredBytes == 0) {
        return false;
    }

    std::vector<std::uint8_t> buffer(requiredBytes);
    if (!::GetTokenInformation(token.handle, TokenUser, buffer.data(), requiredBytes, &requiredBytes)) {
        return false;
    }

    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR sidString = nullptr;
    if (!::ConvertSidToStringSidW(tokenUser->User.Sid, &sidString)) {
        return false;
    }

    // Grant full pipe access strictly to the current user SID and SYSTEM.
    std::wstring sddl = L"D:(A;;GA;;;SY)(A;;GA;;;";
    sddl += sidString;
    sddl += L")";
    ::LocalFree(sidString);

    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &outSd.descriptor, nullptr)) {
        return false;
    }

    outSa.nLength = sizeof(SECURITY_ATTRIBUTES);
    outSa.lpSecurityDescriptor = outSd.descriptor;
    outSa.bInheritHandle = FALSE;
    return true;
}

bool ReadExactBytes(HANDLE pipe, std::uint8_t* destination, std::size_t count) {
    std::size_t totalRead = 0;
    while (totalRead < count) {
        const auto remaining = static_cast<DWORD>(count - totalRead);
        DWORD bytesRead = 0;
        const BOOL ok = ::ReadFile(pipe, destination + totalRead, remaining, &bytesRead, nullptr);
        if (!ok || bytesRead == 0) {
            return false;
        }
        totalRead += bytesRead;
    }
    return true;
}

bool WriteExactBytes(HANDLE pipe, const std::uint8_t* source, std::size_t count) {
    std::size_t totalWritten = 0;
    while (totalWritten < count) {
        const auto remaining = static_cast<DWORD>(count - totalWritten);
        DWORD bytesWritten = 0;
        const BOOL ok = ::WriteFile(pipe, source + totalWritten, remaining, &bytesWritten, nullptr);
        if (!ok || bytesWritten == 0) {
            return false;
        }
        totalWritten += bytesWritten;
    }
    return true;
}

#endif // _WIN32

} // namespace

NamedPipeServer::NamedPipeServer(std::wstring pipeName)
    : pipeName_(std::move(pipeName)) {}

void NamedPipeServer::RequestStop() noexcept {
    stopRequested_.store(true);
}

Result<void> NamedPipeServer::Run() {
#ifndef _WIN32
    return Result<void>::Fail(StatusCode::NotImplemented, "NamedPipeServer is only supported on Windows.");
#else
    ScopedSecurityDescriptor sd;
    SECURITY_ATTRIBUTES sa{};
    SECURITY_ATTRIBUTES* saPtr = nullptr;
    if (BuildCurrentUserPipeSecurity(sd, sa)) {
        saPtr = &sa;
    } else {
        Logger::Warning("IpcServer", "Failed to build restricted SDDL for Named Pipe; falling back to default security.");
    }

    ScopedHandle pipe(::CreateNamedPipeW(
        pipeName_.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,
        65536,
        65536,
        0,
        saPtr));

    if (pipe.handle == INVALID_HANDLE_VALUE) {
        return Result<void>::Fail(
            StatusCode::IoError,
            "CreateNamedPipeW failed with Win32 error " + std::to_string(::GetLastError()));
    }

    Logger::Info("IpcServer", "Named pipe server listening.");

    while (!stopRequested_.load()) {
        const BOOL connected = ::ConnectNamedPipe(pipe.handle, nullptr)
                                   ? TRUE
                                   : (::GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);
        if (!connected) {
            Logger::Warning("IpcServer", "ConnectNamedPipe failed; retrying.");
            continue;
        }

        while (!stopRequested_.load()) {
            std::array<std::uint8_t, 4> header{};
            if (!ReadExactBytes(pipe.handle, header.data(), header.size())) {
                break; // Client disconnected
            }

            const auto lenRes = DecodeIpcFrameLength(header);
            if (!lenRes.ok()) {
                IpcResponse errResp{};
                errResp.protocolVersion = kIpcProtocolVersion;
                errResp.ok = false;
                errResp.errorCode = "ERR_INVALID_FRAME";
                errResp.errorMessage = lenRes.error().message;
                const auto encoded = EncodeIpcFrame(SerializeIpcResponse(errResp));
                if (encoded.ok()) {
                    WriteExactBytes(pipe.handle, encoded.value().data(), encoded.value().size());
                }
                break;
            }

            std::string payload(lenRes.value(), '\0');
            if (!ReadExactBytes(pipe.handle, reinterpret_cast<std::uint8_t*>(payload.data()), payload.size())) {
                break;
            }

            const auto reqRes = ParseIpcRequest(payload);
            DispatchResult dispatch{};
            if (!reqRes.ok()) {
                dispatch.response.protocolVersion = kIpcProtocolVersion;
                dispatch.response.ok = false;
                dispatch.response.errorCode = "ERR_MALFORMED_REQUEST";
                dispatch.response.errorMessage = reqRes.error().message;
            } else {
                dispatch = dispatcher_.Dispatch(reqRes.value());
            }

            const std::string responseJson = SerializeIpcResponse(dispatch.response);
            const auto encoded = EncodeIpcFrame(responseJson);
            if (!encoded.ok()) {
                Logger::Error("IpcServer", "Failed to encode IPC response frame.");
                break;
            }

            if (!WriteExactBytes(pipe.handle, encoded.value().data(), encoded.value().size())) {
                break;
            }

            ::FlushFileBuffers(pipe.handle);

            if (dispatch.shutdownRequested) {
                Logger::Info("IpcServer", "Shutdown command received; terminating server loop.");
                stopRequested_.store(true);
                break;
            }
        }

        ::DisconnectNamedPipe(pipe.handle);
    }

    return Result<void>::Ok();
#endif
}

} // namespace salvus::common
