#include "common/ipc_server.hpp"
#include "common/logger.hpp"
#include "common/version.hpp"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::wstring ParsePipeName(int argc, wchar_t* argv[]) {
    std::wstring pipeName = L"\\\\.\\pipe\\Salvus.Engine";
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--pipe" && (i + 1) < argc) {
            pipeName = argv[++i];
            if (pipeName.rfind(L"\\\\.\\pipe\\", 0) != 0) {
                pipeName = L"\\\\.\\pipe\\" + pipeName;
            }
        }
    }
    return pipeName;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--version") {
            std::cout << salvus::common::kEngineName << " "
                      << salvus::common::kEngineVersion << " ("
                      << salvus::common::kDevelopmentPhase << ")\n";
            return 0;
        }
    }

    const std::wstring pipeName = ParsePipeName(argc, argv);
    salvus::common::Logger::Info("Main", "Starting Salvus.Engine process.");

    salvus::common::NamedPipeServer server(pipeName);
    const auto result = server.Run();
    if (!result.ok()) {
        salvus::common::Logger::Error("Main", result.error().message);
        return 1;
    }

    salvus::common::Logger::Info("Main", "Salvus.Engine exited cleanly.");
    return 0;
}
