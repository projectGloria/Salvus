# Salvus Architecture

## 1. High-Level Process Model

Salvus strictly separates the user interface from low-level storage access and recovery logic:

```text
+-----------------------------------+       Windows Named Pipe        +-----------------------------------+
|      Salvus.Desktop.exe           | <=============================> |        Salvus.Engine.exe          |
|  (.NET 10 WPF, MVVM, DI)          |  Length-prefixed UTF-8 JSON     |  (C++20, MSVC, Windows APIs)      |
|  - Navigation & Views             |  PIPE_REJECT_REMOTE_CLIENTS     |  - DeviceManager (Phase 2)        |
|  - IPC Client & Process Manager   |  Current-user DACL              |  - DiskReader (Phase 2)           |
|  - SQLite Persistence (Salvus.Data)|                                |  - FileSystem Parsers (Phase 3/5) |
+-----------------------------------+                                 |  - Scanners & Recovery (Phase 3-6)|
                                                                      +-----------------------------------+
```

### Why Separate Processes?

1. **Fault isolation:** Malformed or corrupted file-system metadata parsed in native C++20 cannot corrupt the UI process or lose persisted session state.
2. **Controlled privilege elevation:** Only the engine process requires elevated raw-disk access when scanning physical disks or raw volumes.
3. **Independent testability:** `Salvus.Engine` can be exercised directly via GoogleTest and command-line/IPC integration tests without launching WPF.

## 2. Repository Layout

- `Salvus.ico` / `Salvus.png`: Official branding assets kept directly in the repository root and linked by both `Salvus.Desktop` and `Salvus.Engine`.
- `Salvus.Engine/`: C++20 recovery engine built with CMake + MSVC, organized into independent modules (`common`, `device`, `disk`, `filesystem/{ntfs,fat32,exfat,refs}`, `partition`, `scanner`, `recovery`, `imaging`) and `tests/`.
- `Salvus.Shared/`: .NET 10 class library containing shared IPC protocol contracts, framing helpers, and domain DTOs.
- `Salvus.Data/`: .NET 10 SQLite persistence layer storing scan sessions, scan configurations, discovered file metadata, recovery history, user preferences, and recovery reports (never recovered file contents).
- `Salvus.Desktop/`: .NET 10 WPF desktop application using MVVM and `Microsoft.Extensions.DependencyInjection`.
- `Salvus.Tests/`: .NET 10 xUnit test suite.

## 3. Named Pipe IPC Protocol (v1)

### Transport & Security

- **Pipe naming:** `\\.\pipe\Salvus.Engine.<instanceId>` (passed to `Salvus.Engine.exe --pipe <name>`), defaulting to `\\.\pipe\Salvus.Engine`.
- **Access control:** Created with `PIPE_REJECT_REMOTE_CLIENTS` and a security descriptor granting access only to the current user SID (owner), preventing other local or remote users from connecting.
- **Framing:** Every message consists of a 4-byte unsigned little-endian length prefix (`uint32_t`) followed by UTF-8 JSON bytes. Maximum frame size is bounded at **16 MiB** (`16,777,216` bytes); oversized or zero-length frames are rejected immediately.

### Message Envelopes

Request:
```json
{
  "id": "a1b2c3d4-...",
  "protocolVersion": 1,
  "command": "ping",
  "payload": {}
}
```

Response:
```json
{
  "id": "a1b2c3d4-...",
  "protocolVersion": 1,
  "ok": true,
  "errorCode": "",
  "errorMessage": "",
  "payload": {}
}
```

### Phase 1 Commands

| Command | Purpose |
| --- | --- |
| `ping` | Verifies engine liveness; returns engine PID, status, and UTC timestamp. |
| `get_engine_info` | Returns engine version, protocol version, compiler/platform metadata, and build configuration. |
| `get_capabilities` | Reports exact implementation status of every engine module so the UI never presents unimplemented features as active. |
| `shutdown` | Requests graceful termination of the engine process. |

## 4. Data Persistence (`Salvus.Data`)

SQLite is initialized in `%LocalAppData%\Salvus\salvus.db` (configurable for tests via custom path or `:memory:`). Tables created in Phase 1:

- `scan_sessions`
- `scan_configurations`
- `discovered_files` (metadata only; no file contents)
- `recovery_history`
- `recovery_reports`
- `user_preferences`

## 5. Branding Integration

- `Salvus.ico` is referenced from the repository root (`..\Salvus.ico`) as `ApplicationIcon` and WPF `Resource` in `Salvus.Desktop.csproj`, and compiled into `Salvus.Engine.exe` via `Salvus.Engine/src/app.rc`.
- `Salvus.png` is referenced from the repository root (`..\Salvus.png`) as a WPF `Resource` in `Salvus.Desktop.csproj` and displayed with `Stretch="Uniform"` (preserving original aspect ratio and transparency) on the Dashboard and About Salvus views.
