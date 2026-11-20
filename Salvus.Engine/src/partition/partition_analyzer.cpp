#include "partition/partition_analyzer.hpp"
#include "filesystem/exfat_parser.hpp"
#include "filesystem/fat32_parser.hpp"
#include "filesystem/filesystem_detector.hpp"
#include "filesystem/ntfs_parser.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace salvus::partition {

namespace {

std::uint16_t ReadLe16(std::span<const std::uint8_t> buf, std::size_t off) noexcept {
    return static_cast<std::uint16_t>(buf[off]) |
           (static_cast<std::uint16_t>(buf[off + 1U]) << 8U);
}

std::uint32_t ReadLe32(std::span<const std::uint8_t> buf, std::size_t off) noexcept {
    return static_cast<std::uint32_t>(buf[off]) |
           (static_cast<std::uint32_t>(buf[off + 1U]) << 8U) |
           (static_cast<std::uint32_t>(buf[off + 2U]) << 16U) |
           (static_cast<std::uint32_t>(buf[off + 3U]) << 24U);
}

std::uint64_t ReadLe64(std::span<const std::uint8_t> buf, std::size_t off) noexcept {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8U; ++i) {
        v |= (static_cast<std::uint64_t>(buf[off + i]) << (i * 8U));
    }
    return v;
}

std::uint32_t ComputeCrc32(std::span<const std::uint8_t> data) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t b : data) {
        crc ^= b;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = (crc & 1U) ? 0xEDB88320U : 0U;
            crc = (crc >> 1U) ^ mask;
        }
    }
    return ~crc;
}

std::string DecodeGptName(std::span<const std::uint8_t> nameBytes) {
    std::wstring wide;
    for (std::size_t i = 0; i + 1U < nameBytes.size(); i += 2U) {
        const std::uint16_t ch = ReadLe16(nameBytes, i);
        if (ch == 0U) {
            break;
        }
        wide.push_back(static_cast<wchar_t>(ch));
    }
    if (wide.empty()) {
        return {};
    }
#ifdef _WIN32
    const int req = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (req <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(req), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), req, nullptr, nullptr);
    return out;
#else
    std::string out;
    for (wchar_t c : wide) {
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return out;
#endif
}

std::string MbrTypeDescription(std::uint8_t type) {
    switch (type) {
        case 0x07: return "NTFS / exFAT";
        case 0x0B: return "FAT32 (CHS)";
        case 0x0C: return "FAT32 (LBA)";
        case 0x0E: return "FAT16 (LBA)";
        case 0x05:
        case 0x0F: return "Extended Partition";
        case 0x82: return "Linux Swap";
        case 0x83: return "Linux Native";
        case 0xEE: return "GPT Protective MBR";
        case 0xEF: return "EFI System Partition";
        default:   return "MBR Type 0x" + std::to_string(type);
    }
}

bool ValidateGptHeader(std::span<const std::uint8_t> headerSector, std::uint32_t& outHeaderSize) noexcept {
    if (headerSector.size() < 92U) {
        return false;
    }
    if (std::memcmp(headerSector.data(), "EFI PART", 8U) != 0) {
        return false;
    }
    const std::uint32_t headerSize = ReadLe32(headerSector, 12U);
    if (headerSize < 92U || headerSize > headerSector.size()) {
        return false;
    }
    const std::uint32_t expectedCrc = ReadLe32(headerSector, 16U);
    std::vector<std::uint8_t> copy(headerSector.begin(), headerSector.begin() + headerSize);
    copy[16U] = copy[17U] = copy[18U] = copy[19U] = 0U;
    if (ComputeCrc32(copy) != expectedCrc) {
        return false;
    }
    outHeaderSize = headerSize;
    return true;
}

bool IsAllZero(std::span<const std::uint8_t> bytes) noexcept {
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0U; });
}

void ParseGptEntries(
    const disk::DiskReader& reader,
    std::span<const std::uint8_t> headerSector,
    bool isBackup,
    std::vector<PartitionEntry>& outPartitions) {
    const std::uint32_t sectorSize = reader.geometry().bytesPerSector;
    const std::uint64_t totalBytes = reader.geometry().totalBytes;

    const std::uint64_t entryArrayLba = ReadLe64(headerSector, 72U);
    const std::uint32_t numEntries = ReadLe32(headerSector, 80U);
    const std::uint32_t entrySize = ReadLe32(headerSector, 84U);

    if (entrySize < 128U || entrySize > 512U || numEntries == 0U || numEntries > 1024U) {
        return;
    }

    const std::uint64_t arrayOffset = entryArrayLba * sectorSize;
    const std::uint64_t arrayBytes = static_cast<std::uint64_t>(numEntries) * entrySize;
    if (arrayOffset >= totalBytes || arrayBytes > totalBytes - arrayOffset) {
        return;
    }

    std::vector<std::uint8_t> tableBuf(static_cast<std::size_t>(arrayBytes), 0U);
    const auto readRes = reader.ReadBytes(arrayOffset, tableBuf);
    if (!readRes.ok() || readRes.value() < entrySize) {
        return;
    }

    filesystem::FileSystemDetector fsDetector;
    std::vector<std::uint8_t> bootSector(512U, 0U);

    for (std::uint32_t i = 0; i < numEntries; ++i) {
        const auto ent = std::span<const std::uint8_t>(tableBuf.data() + i * entrySize, entrySize);
        if (IsAllZero(ent.subspan(0U, 16U))) {
            continue; // Unused GPT entry
        }

        const std::uint64_t firstLba = ReadLe64(ent, 32U);
        const std::uint64_t lastLba = ReadLe64(ent, 40U);
        if (lastLba < firstLba) {
            continue;
        }

        const std::uint64_t startBytes = firstLba * sectorSize;
        const std::uint64_t lengthBytes = (lastLba - firstLba + 1ULL) * sectorSize;
        if (startBytes >= totalBytes || lengthBytes > totalBytes - startBytes) {
            continue;
        }

        PartitionEntry p{};
        p.index = i + 1U;
        p.startOffsetBytes = startBytes;
        p.lengthBytes = lengthBytes;
        p.name = DecodeGptName(ent.subspan(56U, 72U));
        p.typeDescription = isBackup ? "GPT Partition (Backup Header)" : "GPT Partition";
        p.isLostCandidate = false;
        p.isFromBackupGpt = isBackup;

        if (reader.ReadBytes(startBytes, bootSector).ok()) {
            const auto fsRes = fsDetector.Detect(bootSector);
            p.detectedFileSystem = fsRes.ok() ? filesystem::FileSystemTypeToString(fsRes.value()) : "Unknown";
        }

        outPartitions.push_back(std::move(p));
    }
}

bool OverlapsKnownPartition(std::uint64_t offset, const std::vector<PartitionEntry>& known) noexcept {
    for (const auto& p : known) {
        if (offset >= p.startOffsetBytes && offset < p.startOffsetBytes + p.lengthBytes) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string PartitionSchemeToString(PartitionScheme scheme) {
    switch (scheme) {
        case PartitionScheme::Mbr: return "MBR";
        case PartitionScheme::Gpt: return "GPT";
        default:                   return "Unknown / RAW";
    }
}

common::Result<PartitionAnalysisResult> PartitionAnalyzer::Analyze(
    const disk::DiskReader& reader,
    bool scanForLostPartitions) const {
    if (!reader.is_open()) {
        return common::Result<PartitionAnalysisResult>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader must be opened read-only before partition analysis.");
    }

    const std::uint32_t sectorSize = reader.geometry().bytesPerSector;
    const std::uint64_t totalBytes = reader.geometry().totalBytes;
    if (totalBytes < static_cast<std::uint64_t>(sectorSize) * 2ULL) {
        return common::Result<PartitionAnalysisResult>::Fail(
            common::StatusCode::InvalidArgument, "Target image/disk is too small to contain a partition table.");
    }

    PartitionAnalysisResult result{};
    std::vector<std::uint8_t> sector0(sectorSize, 0U);
    const auto s0Res = reader.ReadBytes(0ULL, sector0);
    if (!s0Res.ok() || s0Res.value() < 512U) {
        return common::Result<PartitionAnalysisResult>::Fail(
            common::StatusCode::IoError, "Failed to read sector 0 (MBR).");
    }

    // 1. Check Primary GPT at LBA 1
    std::vector<std::uint8_t> lba1(sectorSize, 0U);
    std::uint32_t hdrSize = 0;
    if (reader.ReadBytes(sectorSize, lba1).ok() && ValidateGptHeader(lba1, hdrSize)) {
        result.primaryGptValid = true;
        result.scheme = PartitionScheme::Gpt;
        ParseGptEntries(reader, lba1, false, result.partitions);
    }

    // 2. Check Backup GPT at final LBA
    const std::uint64_t lastLbaOffset = totalBytes - sectorSize;
    std::vector<std::uint8_t> lastLba(sectorSize, 0U);
    if (reader.ReadBytes(lastLbaOffset, lastLba).ok() && ValidateGptHeader(lastLba, hdrSize)) {
        result.backupGptValid = true;
        if (!result.primaryGptValid) {
            result.scheme = PartitionScheme::Gpt;
            ParseGptEntries(reader, lastLba, true, result.partitions);
        }
    }

    // 3. Parse MBR if not GPT
    if (result.scheme == PartitionScheme::Unknown && sector0[510] == 0x55U && sector0[511] == 0xAAU) {
        bool hasProtectiveMbr = false;
        filesystem::FileSystemDetector fsDetector;
        std::vector<std::uint8_t> bootSector(512U, 0U);

        for (std::uint32_t i = 0; i < 4U; ++i) {
            const std::size_t off = 446U + i * 16U;
            const std::uint8_t partType = sector0[off + 4U];
            const std::uint32_t startLba = ReadLe32(sector0, off + 8U);
            const std::uint32_t sectorCount = ReadLe32(sector0, off + 12U);

            if (partType == 0x00U || sectorCount == 0U) {
                continue;
            }
            if (partType == 0xEEU) {
                hasProtectiveMbr = true;
                continue;
            }

            const std::uint64_t startBytes = static_cast<std::uint64_t>(startLba) * sectorSize;
            const std::uint64_t lengthBytes = static_cast<std::uint64_t>(sectorCount) * sectorSize;
            if (startBytes >= totalBytes || lengthBytes > totalBytes - startBytes) {
                continue;
            }

            PartitionEntry p{};
            p.index = i + 1U;
            p.startOffsetBytes = startBytes;
            p.lengthBytes = lengthBytes;
            p.name = "MBR Partition " + std::to_string(i + 1U);
            p.typeDescription = MbrTypeDescription(partType);
            p.isLostCandidate = false;

            if (reader.ReadBytes(startBytes, bootSector).ok()) {
                const auto fsRes = fsDetector.Detect(bootSector);
                p.detectedFileSystem = fsRes.ok() ? filesystem::FileSystemTypeToString(fsRes.value()) : "Unknown";
            }

            result.partitions.push_back(std::move(p));
        }

        if (!result.partitions.empty()) {
            result.scheme = PartitionScheme::Mbr;
        } else if (hasProtectiveMbr) {
            result.scheme = PartitionScheme::Gpt;
        }
    }

    // 4. Non-destructive Lost Partition Scan (checks 1 MiB boundaries + 64 KiB steps on small images)
    if (scanForLostPartitions) {
        const std::uint64_t stepBytes = (totalBytes <= 16ULL * 1024ULL * 1024ULL)
                                            ? (64ULL * 1024ULL)
                                            : (1024ULL * 1024ULL);
        const std::uint64_t maxScanBytes = (std::min)(totalBytes, 64ULL * 1024ULL * 1024ULL * 1024ULL);

        filesystem::FileSystemDetector fsDetector;
        filesystem::NTFSParser ntfsParser;
        filesystem::FAT32Parser fat32Parser;
        filesystem::ExFATParser exfatParser;
        std::vector<std::uint8_t> candidateSector(512U, 0U);
        std::uint32_t lostIdx = 1000U;

        for (std::uint64_t off = stepBytes; off + 512ULL <= maxScanBytes; off += stepBytes) {
            if (OverlapsKnownPartition(off, result.partitions)) {
                continue;
            }
            if (!reader.ReadBytes(off, candidateSector).ok()) {
                continue;
            }
            const auto fsRes = fsDetector.Detect(candidateSector);
            if (!fsRes.ok() || fsRes.value() == filesystem::FileSystemType::Raw ||
                fsRes.value() == filesystem::FileSystemType::Unknown) {
                continue;
            }

            std::uint64_t estLength = 0;
            bool validBpb = false;

            if (fsRes.value() == filesystem::FileSystemType::Ntfs) {
                const auto bpb = ntfsParser.ParseBootSector(candidateSector);
                if (bpb.ok()) {
                    estLength = bpb.value().totalVolumeBytes;
                    validBpb = true;
                }
            } else if (fsRes.value() == filesystem::FileSystemType::Fat32) {
                const auto bpb = fat32Parser.ParseBootSector(candidateSector);
                if (bpb.ok()) {
                    estLength = bpb.value().totalVolumeBytes;
                    validBpb = true;
                }
            } else if (fsRes.value() == filesystem::FileSystemType::ExFat) {
                const auto vbr = exfatParser.ParseBootSector(candidateSector);
                if (vbr.ok()) {
                    estLength = vbr.value().totalVolumeBytes;
                    validBpb = true;
                }
            }

            if (validBpb && estLength > 0ULL && estLength <= totalBytes - off) {
                PartitionEntry lost{};
                lost.index = lostIdx++;
                lost.startOffsetBytes = off;
                lost.lengthBytes = estLength;
                lost.name = "Lost " + filesystem::FileSystemTypeToString(fsRes.value()) + " Partition Candidate";
                lost.typeDescription = "Unallocated Boot Sector Match (Non-Destructive)";
                lost.detectedFileSystem = filesystem::FileSystemTypeToString(fsRes.value());
                lost.isLostCandidate = true;
                result.partitions.push_back(std::move(lost));
            }
        }
    }

    return common::Result<PartitionAnalysisResult>::Ok(std::move(result));
}

} // namespace salvus::partition
