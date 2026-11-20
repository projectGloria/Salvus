# Changelog

All notable changes to Salvus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added — Phase 1: Foundation
- Established monorepo structure (`Salvus.Engine`, `Salvus.Shared`, `Salvus.Data`, `Salvus.Desktop`, `Salvus.Tests`).
- Configured C++20 recovery engine (`Salvus.Engine`) with CMake, MSVC, Windows resource icon embedding, and GoogleTest.
- Implemented length-prefixed JSON over Windows Named Pipes (`PIPE_REJECT_REMOTE_CLIENTS`, user-restricted DACL) with `ping`, `get_engine_info`, `get_capabilities`, and `shutdown` commands.
- Implemented disk offset and sector boundary validation utilities in `Salvus.Engine`.
- Created .NET 10 solution (`Salvus.sln`) with shared IPC contracts (`Salvus.Shared`), SQLite persistence layer (`Salvus.Data`), and dark-first WPF MVVM desktop application (`Salvus.Desktop`) with DI.
- Integrated official `Salvus.ico` and `Salvus.png` assets directly from the repository root.
- Added GitHub Actions CI (`ci.yml`) and release packaging + SBOM (`release.yml`) workflows.
- Added GPL-3.0-or-later `LICENSE` and core project documentation.
