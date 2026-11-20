# Changelog

All notable changes to Salvus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] — Release Ready

### Added — Phase 7: Release Preparation
- Added corruption and malformed-metadata resilience test suite (`corruption_resilience_tests.cpp`) covering zero/0xFF boot sectors, NTFS BPB overflows, zero-length and `UINT32_MAX` MFT attribute traps, corrupted GPT CRC32 rejection, and malformed IPC JSON inputs.
- Added Inno Setup 6 Windows x64 installer definition (`installer/Salvus.iss`) referencing `Salvus.ico` and `Salvus.png` directly from the repository root and integrated it into `.github/workflows/release.yml`.
- Completed security and data-integrity audit documentation (`docs/SECURITY_AUDIT.md`).
- Finalized `1.0.0` versioning across `Salvus.Engine`, `Salvus.Desktop`, `Salvus.Shared`, `Salvus.Data`, and Windows version resources.

### Added — Phase 6: Professional Features
- Implemented native Windows CNG (`bcrypt.dll`) streaming SHA-256 verification (`IntegrityChecker::ComputeSha256`) with zero third-party crypto dependencies.
- Implemented `DiskImageManager::CreateRawImage` with strictly read-only source access, separate-destination enforcement, bounded bad-sector retries (max 5), zero-filled LBA preservation, persistent bad-sector map output (`.badsectors.txt`), and automatic SHA-256 checksum computation.
- Implemented structured JSON `RecoveryReportGenerator` and automatic SHA-256 hashing of every recovered file in `recover_file`.
- Added `create_disk_image` and `verify_sha256` Named Pipe IPC commands and SQLite `recovery_history` persistence (`InsertRecoveryHistoryAsync`).
- Wired `Salvus.Desktop` Disk Imaging and Recovery History views to live engine imaging, SHA-256 verification, and SQLite audit history.
- Added GoogleTest suites (`imaging_integrity_tests.cpp`) verifying NIST SHA-256 vector, exact raw imaging, and overwrite rejection, plus end-to-end live Named Pipe imaging & SHA-256 verification in `EngineIpcIntegrationTests.cs`.

### Added — Phase 5: Additional File Systems & Partition Recovery
- Implemented `FAT32Parser` (BPB validation, FAT cluster chain traversal, 8.3 & Long File Name reconstruction, `0xE5` deleted entry detection, contiguous deleted cluster recovery).
- Implemented `ExFATParser` (VBR validation, File/StreamExtension/FileName entry set parsing, `NoFatChain` & FAT chain extent reconstruction, deleted entry analysis).
- Implemented non-destructive `PartitionAnalyzer` (MBR partition table parsing, GPT primary & backup header parsing with CRC32 verification, and lost partition detection via unallocated boot-sector scanning).
- Added unified `quick_scan` (auto-detecting NTFS, FAT32, and exFAT) and `analyze_partitions` Named Pipe IPC commands.
- Enabled **Partition Recovery (Non-Destructive)** and unified Quick Scan in `Salvus.Desktop`.
- Added GoogleTest suites for FAT32 LFN recovery, exFAT deleted entry recovery, and Backup GPT CRC32 fallback + lost NTFS partition detection.

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
