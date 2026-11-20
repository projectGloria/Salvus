# Salvus Security & Data-Integrity Audit

This document audits Salvus v1.0.0 against every mandatory security and data-integrity control defined in `INITIAL_PLAN.md` Section 12.

## 1. Mandatory Control Verification Matrix

| Control | Implementation & Enforcement | Verified By |
| --- | --- | --- |
| **Read-only source access** | `DiskReader::OpenReadOnly` (`Salvus.Engine/src/disk/disk_reader.cpp`) opens sources exclusively with `GENERIC_READ` and `OPEN_EXISTING`. No code path opens a source disk/volume with `GENERIC_WRITE`. | `DiskReaderImageTest`, code audit |
| **Separate recovery destination** | `RecoveryWriter::ValidateSeparateDestination` (`Salvus.Engine/src/recovery/recovery_writer.cpp`) blocks overwriting source image files, writing to the same drive letter as a source volume, or writing to any drive backed by the same `PhysicalDriveN` (`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`). Enforced by both `RecoveryWriter` and `DiskImageManager`. | `SyntheticNtfsTest.RecoveryWriterRejectsSameSourceDestination`, `DiskImageManagerTests` |
| **Strict disk offset & sector boundary validation** | `ValidateReadRange`, `IsValidSectorSize`, `AlignDownToSector`, `AlignUpToSector` (`Salvus.Engine/src/disk/disk_validation.cpp`) enforce power-of-two sector sizes, sector alignment, capacity bounds, and `uint64_t` overflow protection. | `DiskValidationTests` (5 tests) |
| **Safe handling of corrupted metadata** | Bounded attribute loops (max 256 attributes/record), USA fixup verification on local copies, cycle-protected directory path reconstruction (max depth 64), cycle-protected FAT chains, CRC32 GPT header verification, and strict IPC frame limits (16 MiB). | `CorruptionResilienceTests` (5 tests), `SyntheticNtfsTest`, `PartitionAnalyzerTests` |
| **Memory-safe bounded buffers** | Streaming 1 MiB recovery writer chunks, 4 MiB deep-scan read buffer, 256 KiB footer search window, 512 KiB image preview cap, 64 KiB text preview cap. Entire storage devices are never loaded into RAM. | Code audit, `DeepScannerRawImageTest` |
| **Secured process-isolated IPC** | `NamedPipeServer` (`Salvus.Engine/src/common/ipc_server.cpp`) uses `PIPE_REJECT_REMOTE_CLIENTS`, `FILE_FLAG_FIRST_PIPE_INSTANCE`, per-session pipe names, and an explicit SDDL granting access only to `SYSTEM` and the current user SID. | `IpcFramingTests`, `IpcProtocolTests`, `EngineIpcIntegrationTests` |
| **No automatic execution of recovered files** | Previews read bounded slices in-memory (WPF `BitmapImage` from `MemoryStream` or sanitized ASCII/UTF-8 text). Recovered files are never launched via `ShellExecute` or `Process.Start`. | Code audit |
| **No destructive disk operations** | Salvus contains zero formatting, partition-table rewriting, or file-system repair APIs. `PartitionAnalyzer` is strictly non-destructive. | Code audit |
| **No telemetry or data exfiltration** | Salvus opens zero outbound network sockets, includes no telemetry SDKs, and stores metadata solely in local SQLite (`%LocalAppData%\Salvus\salvus.db`). | Code audit, `docs/DEPENDENCIES.md` |
| **Cryptographic verification (SHA-256)** | Native Windows CNG (`bcrypt.dll`) streaming SHA-256 hashes every recovered file and raw disk image. | `IntegrityCheckerTests.MatchesKnownNistSha256Vector`, `EngineIpcIntegrationTests` |

## 2. Forensic & Release Signing Guidance

- **Hardware write blockers:** For forensic workflows requiring physical write protection beyond OS-level `GENERIC_READ` enforcement, attach target media through a certified hardware write blocker and create a raw disk image with SHA-256 verification before analysis.
- **Release binary signing:** Production releases should be Authenticode-signed (`Salvus.Desktop.exe`, `Salvus.Engine.exe`, and `Salvus-Setup-1.0.0-win-x64.exe`) using an EV/OV code-signing certificate via CI secrets (`SIGNING_CERT_PFX_BASE64` / `SIGNING_CERT_PASSWORD`) and published alongside `SHA256SUMS.txt` and `salvus-win-x64.spdx.json`.
