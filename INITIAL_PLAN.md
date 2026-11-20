# SALVUS — Master Project Specification & Development Prompt

## 1. Project Identity

**Project Name:** Salvus  
**Product Type:** Professional Windows Data Recovery Software  
**Platform:** Windows 10 and Windows 11 (x64)  
**Distribution:** Open-source  
**License:** GNU GPL-3.0-or-later  
**Primary Audience:** Regular Windows users, IT professionals, technicians, and advanced users.

**Product tagline:** Recover what matters.

### Mission

Build Salvus, a professional-grade Windows data recovery application capable of recovering deleted, lost, formatted, and inaccessible files from storage devices.

Salvus must prioritize data integrity, recovery accuracy, security, performance, and usability.

This must be a genuine data recovery application with its own modular recovery engine, not merely a graphical interface wrapping existing command-line recovery utilities.

---

## 2. Core Development Principles

Follow these principles throughout development:

1. Data integrity takes priority over recovery speed.
2. Never modify source storage during normal scanning.
3. Never automatically format, repair, or initialize a storage device.
4. Never write recovered files to the source device.
5. Use modular, independently testable components.
6. Separate the desktop interface from the recovery engine.
7. Handle malformed and corrupted file-system metadata safely.
8. Never claim a file is recoverable without sufficient evidence.
9. Avoid unnecessary dependencies.
10. Maintain clean, documented, production-quality code.

Do not implement fake functionality, simulated recovery results, placeholder buttons presented as completed features, or hardcoded scan progress.

If a feature is not implemented, clearly mark it as unavailable or under development.

---

## 3. Technology Stack

Use the following architecture.

### Desktop Application

- Language: C#
- Framework: .NET 10
- UI: WPF
- Architecture: MVVM
- UI communication: Named Pipes
- Dependency Injection: Microsoft.Extensions.DependencyInjection

Responsibilities:

- Application interface
- Device selection
- Scan configuration
- Recovery results
- File previews
- Recovery destination selection
- Scan session management
- Settings and preferences

### Recovery Engine

- Language: C++20
- Build system: CMake
- Compiler: MSVC
- Testing: GoogleTest

Responsibilities:

- Physical disk access
- Sector reading
- Partition analysis
- File-system parsing
- Metadata recovery
- Signature-based scanning
- File reconstruction
- Disk imaging
- Recovery operations

The engine must operate as a separate process.

### Database

Use SQLite.

Store:

- Scan sessions
- Scan configurations
- Discovered file metadata
- Recovery history
- User preferences
- Recovery reports

Do not store recovered file contents in SQLite.

### Windows Integration

Use native Windows APIs for:

- Physical disk enumeration
- Volume enumeration
- Raw disk access
- Storage device information
- Device geometry
- File-system information
- Privilege management

---

## 4. Official Application Branding and Icon Assets

Salvus has two official icon files:

- `salvus.ico`
- `salvus.png`

**Both files will be located directly in the repository root.**

Do not move them into another directory or rename them.

### salvus.ico

Use this file as the official Windows application icon.

Requirements:

- Set it as the WPF application's icon.
- Embed it into the Windows executable where appropriate.
- Use it for the application window.
- Use it for the Windows taskbar.
- Use it for application shortcuts.
- Include it in the Windows installer.
- Ensure the icon displays correctly in Windows Explorer and other relevant Windows interfaces.

### salvus.png

Use this file for in-application branding.

Requirements:

- Display it on the Dashboard where appropriate.
- Use it on the About Salvus page.
- Use it in appropriate application branding locations.
- Preserve its original aspect ratio.
- Preserve transparency.
- Never stretch, distort, recolor, or modify the original image.

### Asset Integration Rules

1. Treat both files as official, mandatory project assets.
2. Reference them using paths relative to the repository root or appropriate project resource references.
3. Configure correct WPF resource and build settings.
4. Ensure both assets are included in release builds and packaging.
5. Do not generate replacement icons.
6. Do not substitute generic icons or placeholders.
7. Do not silently ignore missing assets.
8. If either file is missing, report the issue clearly.
9. Do not duplicate the original assets unnecessarily.
10. Maintain consistent branding throughout the application.

The application must use the official Salvus branding across its desktop interface, executable, taskbar, installer, and shortcuts.

---

## 5. Supported Storage Devices

Implement detection and identification for:

- Internal HDDs
- Internal SSDs
- External HDDs
- External SSDs
- USB flash drives
- SD cards
- microSD cards
- Virtual disk images

Display:

- Device name
- Model
- Manufacturer, when available
- Capacity
- Available capacity, when applicable
- Partition layout
- File system
- Connection type, when available
- Physical disk identifier
- Device health information, when available

Clearly distinguish physical disks from logical volumes.

Never confuse a partition with an entire physical storage device.

---

## 6. File-System Support

Implement recovery support in stages.

### NTFS — Primary Priority

Implement:

- NTFS boot sector parsing
- Master File Table (MFT) parsing
- MFT record validation
- Deleted file detection
- Directory structure reconstruction
- Filename recovery
- File timestamps
- Data run parsing
- Non-resident data reconstruction
- Fragmented file reconstruction
- Alternate Data Stream identification
- Metadata consistency validation

### FAT32

Implement:

- Boot sector parsing
- FAT table analysis
- Directory entry analysis
- Deleted file detection
- Cluster chain reconstruction
- Long filename recovery

### exFAT

Implement:

- Volume boot record parsing
- Allocation bitmap analysis
- Directory entry parsing
- File extent reconstruction
- Deleted entry analysis

### GPT and MBR

Implement:

- GPT header analysis
- GPT partition entry analysis
- Backup GPT detection
- MBR partition table parsing
- Lost partition identification
- Partition boundary validation

### ReFS

Treat ReFS as experimental.

Do not claim complete ReFS recovery without extensive implementation and validation.

### RAW Volumes

Allow raw signature-based scanning when the file system cannot be identified.

---

## 7. Recovery Methods

Salvus must provide three primary recovery methods.

### A. Quick Scan

Purpose: Recover recently deleted files using available file-system metadata.

Features:

- Fast metadata analysis
- Deleted file detection
- Original filenames
- Original folder paths where available
- File timestamps where available
- Recovery confidence classification

### B. Deep Scan

Purpose: Find files when file-system metadata is missing, damaged, or insufficient.

Implement signature-based file carving.

Initial supported signatures:

**Images**
- JPG
- JPEG
- PNG
- GIF
- BMP
- TIFF
- WebP
- ICO

**Video**
- MP4
- MOV
- AVI
- MKV
- WMV
- MPEG

**Audio**
- MP3
- WAV
- FLAC
- AAC
- OGG

**Documents**
- PDF
- DOC
- DOCX
- XLS
- XLSX
- PPT
- PPTX
- TXT
- RTF

**Archives**
- ZIP
- RAR
- 7Z
- GZIP
- TAR

**Other**
- EXE
- DLL
- ISO

Implement extensible signature definitions.

Support:

- Header identification
- Footer identification when applicable
- File size estimation
- File boundary detection
- Fragmentation limitations
- Validation of recovered structures

Do not assume every signature match represents a complete recoverable file.

### C. Partition Recovery

Implement:

- Partition table analysis
- Lost partition detection
- File-system boot sector analysis
- Backup metadata detection
- Partition boundary estimation
- Recovery candidate reporting

Partition recovery must initially be non-destructive.

Never automatically restore or rewrite partition tables.

---

## 8. Recovery Result Classification

Every discovered file must receive a recovery status.

### High Confidence
Sufficient original metadata and file extents are available.

### Medium Confidence
Some metadata is missing or reconstruction is required.

### Low Confidence
File identification relies primarily on signature-based carving.

### Damaged
The file is incomplete or contains structural inconsistencies.

### Unrecoverable
Required data is unavailable or overwritten.

Display confidence explanations to users.

Never equate a detected filename with successful file recovery.

---

## 9. Recovery Engine Architecture

Create independent modules.

```text
Salvus.Engine/
├── include/
│   ├── device/
│   ├── disk/
│   ├── filesystem/
│   ├── partition/
│   ├── scanner/
│   ├── recovery/
│   ├── imaging/
│   └── common/
│
├── src/
│   ├── device/
│   ├── disk/
│   ├── filesystem/
│   │   ├── ntfs/
│   │   ├── fat32/
│   │   ├── exfat/
│   │   └── refs/
│   ├── partition/
│   ├── scanner/
│   ├── recovery/
│   ├── imaging/
│   └── common/
│
├── tests/
└── CMakeLists.txt
```

Required components:

- DeviceManager
- DiskReader
- DiskImageManager
- FileSystemDetector
- NTFSParser
- FAT32Parser
- ExFATParser
- PartitionAnalyzer
- QuickScanner
- DeepScanner
- SignatureDatabase
- FileReconstructor
- RecoveryWriter
- IntegrityChecker
- ScanManager
- RecoveryReportGenerator

Each component must have a clearly defined responsibility.

---

## 10. Desktop Application Interface

Create a professional, modern Windows desktop application.

### Design Direction

- Dark-first interface
- Minimal visual clutter
- Professional appearance
- Consistent spacing
- Clear typography
- Smooth, restrained animations
- High information density where appropriate
- Accessible contrast
- Resizable application window

Avoid excessive gradients, decorative effects, unnecessary cards, and oversized elements.

The interface should feel like professional desktop software rather than a generic web dashboard.

### Navigation

Create the following navigation sections:

1. Dashboard
2. Drives
3. Recovery
4. Scan Results
5. Disk Imaging
6. Recovery History
7. Settings
8. About Salvus

### Dashboard

Display:

- Connected storage devices
- Device capacity
- File-system information
- Recent scan sessions
- Recent recovery operations
- Application status

### Drives

Display all detected physical disks and volumes.

Provide clear distinctions between:

- Physical disks
- Partitions
- Removable devices
- Virtual disk images

### Recovery

Provide:

- Selected storage device
- Quick Scan
- Deep Scan
- Partition Recovery
- Scan configuration
- Start and cancel controls

### Scan Results

Implement:

- File tree
- File-type filtering
- Filename search
- File-size filtering
- Recovery confidence
- File preview
- File metadata
- Selection controls

### File Preview

Support safe previews for:

- Images
- Text files
- PDF documents where safely supported
- Basic media metadata

Never execute recovered files.

### Recovery History

Display:

- Previous scans
- Recovery sessions
- Recovered file counts
- Operation timestamps
- Destination paths
- Recovery reports

---

## 11. Application Workflow

Implement this workflow:

1. User launches Salvus.
2. Application enumerates available devices.
3. User selects a storage device or volume.
4. Salvus identifies the device and file system.
5. User selects a recovery method.
6. Salvus opens the source in read-only mode.
7. Scanning begins.
8. Results are indexed and classified.
9. User searches and previews files.
10. User selects files for recovery.
11. User selects a separate destination.
12. Salvus reconstructs and writes the files.
13. Salvus verifies recovered output where possible.
14. Recovery report is generated.

Scanning and recovery must support cancellation.

Preserve scan results when the user closes the application normally.

---

## 12. Security and Data Integrity

These are mandatory requirements.

- Read-only source access.
- Separate recovery destination.
- Strict disk offset validation.
- Sector boundary validation.
- Safe handling of corrupted file-system structures.
- Memory-safe buffer management.
- Resource limits for malformed metadata.
- Controlled administrative elevation.
- No automatic execution of recovered files.
- No automatic disk formatting.
- No automatic partition modification.
- No automatic file-system repair.
- No unsolicited telemetry.
- No uploading user data.

Use a hardware write blocker for forensic workflows requiring stronger write protection.

Support SHA-256 hashing for disk images and recovery reports.

---

## 13. Disk Imaging

Implement an advanced disk imaging module.

Features:

- Create raw disk images
- Open existing disk images
- Sector-by-sector copying
- Progress tracking
- Image verification
- SHA-256 checksums
- Interrupted operation handling
- Error reporting

Prioritize image-based recovery for unstable storage devices.

For failing drives, minimize repeated reads and avoid aggressive retry loops. Consider a configurable imaging strategy with bounded retries and a persistent bad-sector map.

Do not promise successful imaging of physically failing devices.

---

## 14. Performance Requirements

Design for large storage devices.

Requirements:

- Asynchronous scanning
- Bounded memory usage
- Efficient sequential reads
- Configurable read buffer sizes
- Cancellation support
- Background processing
- Thread-safe result handling
- Scan progress reporting
- Persistent scan results

Avoid loading entire storage devices into RAM.

Progress indicators must reflect actual completed work, not arbitrary timers.

---

## 15. Testing Strategy

Use automated tests and disposable test storage.

### Unit Testing

Test:

- NTFS parser
- FAT32 parser
- exFAT parser
- GPT parser
- MBR parser
- Signature detection
- File reconstruction
- Disk offset validation
- IPC communication

### Integration Testing

Test:

- Physical disk enumeration
- Volume enumeration
- Read-only disk access
- Scan cancellation
- Scan persistence
- Recovery destination validation
- File integrity verification

### Recovery Scenarios

Create controlled test environments for:

- Deleted files
- Recycle Bin emptied
- Quick-formatted volumes
- Damaged file systems
- Missing partition entries
- Fragmented files
- Large files
- Corrupted metadata
- Interrupted scans

Use virtual disks and disposable test images.

Never test destructive recovery scenarios on important personal storage.

---

## 16. Open-Source Requirements

Use GPL-3.0-or-later.

The repository must include:

- README.md
- LICENSE
- CONTRIBUTING.md
- SECURITY.md
- CODE_OF_CONDUCT.md
- CHANGELOG.md
- Architecture documentation
- Build instructions
- Testing instructions
- Dependency inventory

Use GitHub Actions for:

- C++ builds
- .NET builds
- Automated tests
- Static analysis
- Release packaging

Produce reproducible release artifacts where practical.

Include a Software Bill of Materials (SBOM).

---

## 17. Development Roadmap

Develop Salvus incrementally.

### Phase 1 — Foundation

- Create repository structure.
- Configure CMake.
- Configure .NET solution.
- Establish IPC protocol.
- Create initial WPF interface.
- Configure automated builds.
- Integrate salvus.ico and salvus.png from the repository root.
- Verify that both icons appear correctly in the application and packaged builds.

### Phase 2 — Storage Access

- Physical drive detection.
- Volume detection.
- Device information.
- Read-only disk access.
- Sector reading.

### Phase 3 — NTFS Recovery

- NTFS boot sector parsing.
- MFT parsing.
- Deleted file detection.
- File extent reconstruction.
- Recovery writer.

### Phase 4 — Deep Scanning

- Signature database.
- File carving.
- File identification.
- Recovery classification.
- Preview support.

### Phase 5 — Additional File Systems

- FAT32
- exFAT
- GPT
- MBR
- Lost partition detection

### Phase 6 — Professional Features

- Disk imaging.
- Scan persistence.
- Recovery reports.
- Integrity verification.
- Advanced filtering.

### Phase 7 — Release Preparation

- Security audit.
- Performance optimization.
- Corruption testing.
- Installer creation.
- Documentation.
- Signed release binaries.

---

## 18. Instructions to the AI Development Agent

You are the lead software architect and senior development engineer responsible for implementing Salvus.

Your responsibilities:

1. Follow this specification as the source of truth.
2. Inspect the existing repository before making changes.
3. Never overwrite existing working implementations without understanding them.
4. Establish the architecture before implementing complex functionality.
5. Work in small, independently testable milestones.
6. Build and test after every meaningful implementation.
7. Fix compiler errors and failing tests before continuing.
8. Document important architectural decisions.
9. Do not introduce unnecessary dependencies.
10. Never silently replace real functionality with mock implementations.
11. Clearly report limitations and incomplete features.
12. Prioritize correctness over implementation speed.
13. Preserve the official Salvus icon assets in the repository root.
14. Never generate, rename, relocate, or replace the official icon files.
15. Ensure the application, executable, installer and shortcuts consistently use the supplied branding.

### Working Method

For every milestone:

1. Inspect the current project state.
2. Identify the required implementation.
3. Propose the implementation plan.
4. Implement the changes.
5. Build the project.
6. Run relevant tests.
7. Review security and data-integrity implications.
8. Document the result.
9. Report completed work and remaining tasks.

Do not attempt to implement the entire application in one operation.

Do not move to the next milestone while critical build failures or data-integrity issues remain unresolved.

### Initial Task

Start with Phase 1: Foundation.

First inspect the development environment and existing repository.

Verify that these two files exist in the repository root:

- salvus.ico
- salvus.png

Then:

- Establish the complete monorepo structure.
- Configure the C++20 engine using CMake and MSVC.
- Create the .NET 10 WPF application.
- Establish the named-pipe communication protocol.
- Configure shared data contracts.
- Integrate the official application icons.
- Configure application and executable icon resources.
- Configure initial automated build workflows.
- Add project documentation and GPL-3.0-or-later licensing.

Create a functional application shell, not a fake recovery implementation.

At the end, provide:

- Implemented files and components.
- Architecture decisions.
- Branding integration details.
- Build results.
- Test results.
- Known limitations.
- Next development milestone.

---

# Final Objective

Build Salvus into a reliable, genuinely capable, professional-grade open-source Windows data recovery application.

Its defining characteristics must be:

- Reliable recovery.
- Strong data integrity.
- Professional engineering.
- Transparent recovery results.
- Secure disk operations.
- Excellent Windows integration.
- Consistent Salvus branding.
- Maintainable open-source architecture.