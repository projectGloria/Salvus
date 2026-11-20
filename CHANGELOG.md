# Changelog

All notable changes to Salvus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added — Phase 4: Deep Scanning
- Implemented extensible `SignatureDatabase` with 27+ built-in file signatures across Images (JPG, PNG, GIF, BMP, TIFF, WebP, ICO), Video (MP4, AVI, MKV, WMV, MPEG), Audio (MP3, WAV, FLAC, OGG), Documents (PDF, OLE2/DOC, RTF), Archives (ZIP/OOXML, RAR, 7Z, GZIP, TAR), and Binaries (PE/EXE with PE header validation).
- Implemented bounded streaming `DeepScanner` (4 MiB read buffer, 256 KiB footer search window) supporting footer matching, embedded BMP/RIFF size parsing, ZIP EOCD boundary detection, and honest `Low` / `Damaged` confidence classification with fragmentation caveats.
- Added `deep_scan` and `get_file_preview` Named Pipe IPC commands in `Salvus.Engine` and `EngineIpcClient` with non-executing safe previews for images (in-memory Base64 → WPF `BitmapImage`), sanitized UTF-8/ASCII text, and PDF header metadata.
- Enabled Deep Scan in `Salvus.Desktop` Recovery view and added the Safe Non-Executing Preview pane in Scan Results.
- Added `deep_scanner_tests.cpp` (GoogleTest) and live Named Pipe `deep_scan` + `get_file_preview` + `recover_file` integration assertions in `EngineIpcIntegrationTests.cs`.

### Added — Phase 3: NTFS Recovery
- Implemented `FileSystemDetector` for NTFS, FAT32, exFAT, experimental ReFS, and RAW boot sector identification.
- Implemented `NTFSParser` with boot sector validation, MFT record USA fixup verification, `$STANDARD_INFORMATION`, `$FILE_NAME` (UTF-16LE + Win32 namespace preference), `$DATA` (resident, non-resident, fragmented signed delta-LCN runs, sparse runs, and Alternate Data Streams), deleted file detection, cycle-safe directory path reconstruction, and transparent confidence classification.
- Implemented `FileReconstructor` and `RecoveryWriter` with bounded streaming I/O and strict separate-destination validation (blocks overwriting source image or writing to the same source volume/physical disk).
- Added `quick_scan_ntfs` and `recover_file` Named Pipe IPC commands in `Salvus.Engine` and `EngineIpcClient`.
- Wired `Salvus.Desktop` Recovery and Scan Results views for live NTFS Quick Scan, SQLite session persistence, filename/confidence filtering, metadata inspection, and separate-destination file recovery.
- Added synthetic NTFS GoogleTest suite covering resident recovery, 2-run fragmented negative-delta-LCN recovery, ADS detection, corrupted USA handling, and separate-destination rejection.

### Added — Phase 2: Storage Access
- Implemented native Windows physical disk enumeration (`DeviceManager::EnumeratePhysicalDisks`) via SetupAPI (`GUID_DEVINTERFACE_DISK`) and `IOCTL_STORAGE_QUERY_PROPERTY` / `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX` / `IOCTL_DISK_GET_DRIVE_LAYOUT_EX`.
- Implemented native Windows logical volume enumeration (`DeviceManager::EnumerateVolumes`) mapped to parent physical disk indices via `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`.
- Implemented strictly read-only `DiskReader` (`GENERIC_READ`, `OPEN_EXISTING`) supporting aligned sector reads and transparently sector-aligned arbitrary byte-range reads across raw devices and virtual disk images.
- Added `list_devices` and `list_volumes` Named Pipe IPC commands and process elevation detection (`isElevated`).
- Wired `Salvus.Desktop` Dashboard and Drives views to display real enumerated physical disks and logical volumes with clear distinction between physical drives and partitions.
- Added GoogleTest suites for `DiskReader` (synthetic raw disk image) and `DeviceManager`, plus end-to-end .NET live IPC integration assertions for device and volume enumeration.

### Added — Phase 1: Foundation
- Established monorepo structure (`Salvus.Engine`, `Salvus.Shared`, `Salvus.Data`, `Salvus.Desktop`, `Salvus.Tests`).
- Configured C++20 recovery engine (`Salvus.Engine`) with CMake, MSVC, Windows resource icon embedding, and GoogleTest.
- Implemented length-prefixed JSON over Windows Named Pipes (`PIPE_REJECT_REMOTE_CLIENTS`, user-restricted DACL) with `ping`, `get_engine_info`, `get_capabilities`, and `shutdown` commands.
- Implemented disk offset and sector boundary validation utilities in `Salvus.Engine`.
- Created .NET 10 solution (`Salvus.sln`) with shared IPC contracts (`Salvus.Shared`), SQLite persistence layer (`Salvus.Data`), and dark-first WPF MVVM desktop application (`Salvus.Desktop`) with DI.
- Integrated official `Salvus.ico` and `Salvus.png` assets directly from the repository root.
- Added GitHub Actions CI (`ci.yml`) and release packaging + SBOM (`release.yml`) workflows.
- Added GPL-3.0-or-later `LICENSE` and core project documentation.
