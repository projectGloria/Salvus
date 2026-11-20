# Testing Salvus

Salvus uses automated tests across both the native C++20 engine and the .NET 10 desktop/data layers.

## 1. C++20 Engine Tests (GoogleTest)

Build and run tests via CTest:

```powershell
cmake -S Salvus.Engine -B Salvus.Engine/build -G "Visual Studio 17 2022" -A x64 -DSALVUS_BUILD_TESTS=ON
cmake --build Salvus.Engine/build --config Release
ctest --test-dir Salvus.Engine/build -C Release --output-on-failure
```

Current Phase 1 test suites:
- `ipc_protocol_tests`: Length-prefixed framing, max-frame boundary enforcement, JSON envelope serialization/parsing, and command dispatching (`ping`, `get_engine_info`, `get_capabilities`, unknown command handling).
- `disk_validation_tests`: Strict disk offset validation, sector-alignment checks, overflow protection, and out-of-bounds rejection.

## 2. .NET 10 Tests (xUnit)

```powershell
dotnet test Salvus.sln -c Release
```

Current Phase 1 test suites:
- IPC framing and protocol contract round-trip tests (`Salvus.Shared`).
- SQLite schema initialization, preference persistence, and scan session repository tests (`Salvus.Data`).
- ViewModel and navigation tests (`Salvus.Desktop`).
- Live Named Pipe IPC integration test launching `Salvus.Engine.exe` when available.

## Safety Policy for Testing

Never test destructive recovery scenarios on personal or production storage. All file-system and recovery tests (Phases 2–6) must use disposable virtual disks or synthetic disk images.
