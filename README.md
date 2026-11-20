<p align="center">
  <img src="Salvus.png" alt="Salvus Logo" width="160" />
</p>

# Salvus

**Recover what matters.**

Salvus is an open-source, professional-grade Windows data recovery application designed for Windows 10 and Windows 11 (x64). It combines a dedicated C++20 recovery engine with a dark-first .NET 10 WPF desktop application connected over secured Windows Named Pipes.

> **Current Status: Phase 5 — Additional File Systems & Partition Recovery Complete**  
> The monorepo foundation, C++20 recovery engine, .NET 10 WPF desktop shell, secured Named Pipe IPC, SQLite persistence, official branding, native storage enumeration, strictly read-only disk reader, NTFS/FAT32/exFAT Quick Scan & recovery, signature-based Deep Scan with safe non-executing previews, and non-destructive MBR/GPT & lost partition recovery are implemented and tested. Disk imaging, SHA-256 verification, recovery reports, and release packaging are scheduled for Phases 6–7.

## Core Principles

- **Data integrity first:** Source storage is opened strictly read-only during scanning.
- **Non-destructive by design:** Salvus never formats, repairs, or initializes a storage device automatically, and never writes recovered files to the source device.
- **Process separation:** The native C++20 recovery engine (`Salvus.Engine.exe`) runs as an isolated process separate from the desktop interface (`Salvus.Desktop.exe`).
- **Transparent results:** Every discovered file will receive an explicit confidence classification backed by real structural evidence—never simulated or placeholder data.

## Architecture Overview

| Component | Technology | Responsibility |
| --- | --- | --- |
| `Salvus.Engine` | C++20, CMake, MSVC, GoogleTest | Native disk access, file-system parsing, signature carving, reconstruction, disk imaging |
| `Salvus.Shared` | C# (.NET 10) | Shared data contracts, Named Pipe framing, and IPC protocol definitions |
| `Salvus.Data` | C# (.NET 10), SQLite | Scan sessions, configurations, discovered metadata, recovery history, user preferences |
| `Salvus.Desktop` | C# (.NET 10), WPF, MVVM, DI | Dark-first desktop interface, device selection, scan results, previews, settings |
| `Salvus.Tests` | C# (.NET 10), xUnit | Automated unit and integration tests for shared contracts, IPC, data layer, and ViewModels |

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for detailed architectural documentation.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Build Instructions](docs/BUILD.md)
- [Testing Guide](docs/TESTING.md)
- [Dependency Inventory](docs/DEPENDENCIES.md)
- [Security Policy](SECURITY.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

## License

Salvus is free software licensed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the full text.
