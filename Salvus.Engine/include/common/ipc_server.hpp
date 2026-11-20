#pragma once

#include "common/ipc_protocol.hpp"
#include "common/result.hpp"

#include <atomic>
#include <string>

namespace salvus::common {

class NamedPipeServer {
public:
    explicit NamedPipeServer(std::wstring pipeName);
    ~NamedPipeServer() = default;

    NamedPipeServer(const NamedPipeServer&) = delete;
    NamedPipeServer& operator=(const NamedPipeServer&) = delete;

    // Runs the synchronous single-client-per-connection server loop until shutdown or Stop().
    Result<void> Run();
    void RequestStop() noexcept;

    [[nodiscard]] const std::wstring& pipe_name() const noexcept {
        return pipeName_;
    }

private:
    std::wstring pipeName_;
    CommandDispatcher dispatcher_;
    std::atomic<bool> stopRequested_{false};
};

} // namespace salvus::common
