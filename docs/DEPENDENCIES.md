# Dependency Inventory

Salvus minimizes third-party dependencies to reduce attack surface and simplify auditing and licensing compliance (GPL-3.0-or-later).

## C++20 Recovery Engine (`Salvus.Engine`)

| Dependency | Version / Source | License | Purpose |
| --- | --- | --- | --- |
| Windows SDK (Win32 API) | System / MSVC | Proprietary (OS API) | Named Pipes, security descriptors, raw storage access |
| GoogleTest | v1.15.2 (CMake FetchContent, test-only) | BSD-3-Clause | Unit testing framework (`SALVUS_BUILD_TESTS=ON` only; not linked into production binaries) |

No third-party JSON or networking libraries are linked into `Salvus.Engine.exe` in Phase 1; the IPC envelope parser/serializer is self-contained, bounded, and audited.

## .NET 10 Solution (`Salvus.Shared`, `Salvus.Data`, `Salvus.Desktop`, `Salvus.Tests`)

| Package | Version | License | Project(s) | Purpose |
| --- | --- | --- | --- | --- |
| `Microsoft.Extensions.DependencyInjection` | 10.0.15 | MIT | `Salvus.Desktop` | Dependency injection container (required by spec) |
| `Microsoft.Data.Sqlite` | 10.0.15 | MIT | `Salvus.Data` | SQLite database access (required by spec) |
| `SQLitePCLRaw.lib.e_sqlite3` | 2.1.14 | Apache-2.0 | `Salvus.Data` | Patched native SQLite3 runtime library (mitigates GHSA-2m69-gcr7-jv3q) |
| `xunit` | 2.9.3 | Apache-2.0 | `Salvus.Tests` | Unit testing framework (test-only) |
| `xunit.runner.visualstudio` | 3.0.2 | Apache-2.0 | `Salvus.Tests` | Visual Studio / `dotnet test` adapter (test-only) |
| `Microsoft.NET.Test.Sdk` | 17.13.0 | MIT | `Salvus.Tests` | .NET test host (test-only) |
