#include "filesystem/exfat_parser.hpp"

#include <cstring>
#include <limits>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace salvus::filesystem {

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

std::string Utf16ToUtf8(const std::u16string& u16) {
    if (u16.empty()) {
        return {};
    }
    std::wstring wide(u16.begin(), u16.end());
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

std::uint64_t ClusterToOffset(const ExFatBootSector& vbr, std::uint32_t cluster) noexcept {
    return vbr.clusterHeapOffsetBytes +
           static_cast<std::uint64_t>(cluster - 2U) * static_cast<std::uint64_t>(vbr.clusterSizeBytes);
}

std::uint32_t ReadExFatEntry(const disk::DiskReader& reader, const ExFatBootSector& vbr, std::uint32_t cluster) {
    const std::uint64_t off = vbr.fatStartOffsetBytes + static_cast<std::uint64_t>(cluster) * 4ULL;
    std::uint8_t raw[4]{};
    const auto res = reader.ReadBytes(off, raw);
    if (!res.ok() || res.value() < 4U) {
        return 0xFFFFFFFFU;
    }
    return ReadLe32(raw, 0U);
}

std::vector<std::uint32_t> FollowExFatChain(
    const disk::DiskReader& reader,
    const ExFatBootSector& vbr,
    std::uint32_t startCluster,
    std::size_t maxClusters = 65536U) {
    std::vector<std::uint32_t> chain;
    if (startCluster < 2U || startCluster >= vbr.clusterCount + 2U) {
        return chain;
    }
    std::unordered_set<std::uint32_t> visited;
    std::uint32_t current = startCluster;
    while (current >= 2U && current < 0xFFFFFFF8U && current < vbr.clusterCount + 2U && chain.size() < maxClusters) {
        if (!visited.insert(current).second) {
            break;
        }
        chain.push_back(current);
        const std::uint32_t next = ReadExFatEntry(reader, vbr, current);
        if (next == 0U || next >= 0xFFFFFFF8U) {
            break;
        }
        current = next;
    }
    return chain;
}

void ScanExFatDirectory(
    const disk::DiskReader& reader,
    const ExFatBootSector& vbr,
    std::uint32_t dirCluster,
    const std::string& parentPath,
    bool includeActiveFiles,
    std::unordered_set<std::uint32_t>& visitedDirs,
    std::uint64_t& nextRecordId,
    ExFatScanSummary& summary) {
    if (dirCluster < 2U || !visitedDirs.insert(dirCluster).second || visitedDirs.size() > 4096U) {
        return;
    }

    summary.directoriesScanned++;
    auto clusters = FollowExFatChain(reader, vbr, dirCluster, 256U);
    if (clusters.empty()) {
        clusters.push_back(dirCluster);
    }

    std::vector<std::uint8_t> clusterBuf(vbr.clusterSizeBytes, 0U);
    std::vector<std::uint8_t> allEntries;
    for (const std::uint32_t c : clusters) {
        const auto res = reader.ReadBytes(ClusterToOffset(vbr, c), clusterBuf);
        if (!res.ok() || res.value() < 32U) {
            break;
        }
        allEntries.insert(allEntries.end(), clusterBuf.begin(), clusterBuf.begin() + res.value());
    }

    const std::size_t count = allEntries.size() / 32U;
    std::size_t idx = 0;

    while (idx < count) {
        const auto entry = std::span<const std::uint8_t>(allEntries.data() + idx * 32U, 32U);
        const std::uint8_t entryType = entry[0U];
        if (entryType == 0x00U) {
            break;
        }

        // File Directory Entry: 0x85 (in-use) or 0x05 (deleted)
        if (entryType != 0x85U && entryType != 0x05U) {
            ++idx;
            continue;
        }

        const bool isDeleted = (entryType == 0x05U);
        const std::uint8_t secondaryCount = entry[1U];
        const std::uint16_t fileAttrs = ReadLe16(entry, 4U);
        const bool isDirectory = (fileAttrs & 0x0010U) != 0U;

        if (secondaryCount < 2U || idx + secondaryCount >= count) {
            ++idx;
            continue;
        }

        // Next entry must be Stream Extension (0xC0 active or 0x40 deleted)
        const auto streamEntry = std::span<const std::uint8_t>(allEntries.data() + (idx + 1U) * 32U, 32U);
        const std::uint8_t streamType = streamEntry[0U];
        if (streamType != 0xC0U && streamType != 0x40U) {
            ++idx;
            continue;
        }

        const std::uint8_t secondaryFlags = streamEntry[1U];
        const bool noFatChain = (secondaryFlags & 0x02U) != 0U;
        const std::uint8_t nameLenChars = streamEntry[3U];
        const std::uint32_t firstCluster = ReadLe32(streamEntry, 20U);
        const std::uint64_t dataLength = ReadLe64(streamEntry, 24U);

        std::u16string nameU16;
        for (std::size_t s = 2U; s <= secondaryCount && (idx + s) < count; ++s) {
            const auto nameEntry = std::span<const std::uint8_t>(allEntries.data() + (idx + s) * 32U, 32U);
            if (nameEntry[0U] != 0xC1U && nameEntry[0U] != 0x41U) {
                break;
            }
            for (std::size_t c = 0; c < 15U && nameU16.size() < nameLenChars; ++c) {
                const std::uint16_t ch = ReadLe16(nameEntry, 2U + c * 2U);
                if (ch == 0U) {
                    break;
                }
                nameU16.push_back(static_cast<char16_t>(ch));
            }
        }

        idx += static_cast<std::size_t>(secondaryCount) + 1U;

        const std::string fileName = Utf16ToUtf8(nameU16);
        if (fileName.empty()) {
            continue;
        }
        const std::string fullPath = parentPath + "\\" + fileName;

        if (isDirectory) {
            if (!isDeleted && firstCluster >= 2U) {
                ScanExFatDirectory(
                    reader, vbr, firstCluster, fullPath, includeActiveFiles, visitedDirs, nextRecordId, summary);
            }
            continue;
        }

        if (!isDeleted && !includeActiveFiles) {
            continue;
        }

        NtfsFileEntry file{};
        file.recordNumber = nextRecordId++;
        file.fileName = fileName;
        file.fullPath = fullPath;
        file.isDeleted = isDeleted;
        file.isDirectory = false;
        file.isResident = false;
        file.fileSizeBytes = dataLength;

        if (dataLength == 0ULL) {
            file.confidence = RecoveryConfidence::High;
            file.confidenceReason = "Zero-byte exFAT file with valid directory entry set.";
        } else if (firstCluster >= 2U && firstCluster < vbr.clusterCount + 2U) {
            const std::uint64_t startOff = ClusterToOffset(vbr, firstCluster);
            if (noFatChain) {
                if (startOff <= vbr.totalVolumeBytes && dataLength <= vbr.totalVolumeBytes - startOff) {
                    file.dataExtents.push_back(recovery::FileExtent{startOff, dataLength, false});
                    file.confidence = RecoveryConfidence::High;
                    file.confidenceReason = "exFAT NoFatChain contiguous extent is valid and within volume bounds.";
                } else {
                    file.confidence = RecoveryConfidence::Damaged;
                    file.confidenceReason = "exFAT contiguous extent exceeds volume capacity.";
                }
            } else {
                const auto chain = FollowExFatChain(reader, vbr, firstCluster);
                const std::uint64_t reqClusters =
                    (dataLength + vbr.clusterSizeBytes - 1ULL) / vbr.clusterSizeBytes;
                if (chain.size() >= reqClusters) {
                    for (const std::uint32_t cl : chain) {
                        file.dataExtents.push_back(recovery::FileExtent{
                            ClusterToOffset(vbr, cl), vbr.clusterSizeBytes, false});
                    }
                    file.confidence = RecoveryConfidence::High;
                    file.confidenceReason = "Intact exFAT FAT chain covering full file size.";
                } else if (startOff <= vbr.totalVolumeBytes && dataLength <= vbr.totalVolumeBytes - startOff) {
                    file.dataExtents.push_back(recovery::FileExtent{startOff, dataLength, false});
                    file.confidence = RecoveryConfidence::Medium;
                    file.confidenceReason = "exFAT FAT chain cleared on deletion; contiguous extent from first cluster assumed.";
                } else {
                    file.confidence = RecoveryConfidence::Damaged;
                    file.confidenceReason = "exFAT file extent exceeds volume capacity.";
                }
            }
        } else {
            file.confidence = RecoveryConfidence::Unrecoverable;
            file.confidenceReason = "exFAT stream extension has no valid first cluster.";
        }

        summary.entries.push_back(std::move(file));
    }
}

} // namespace

common::Result<ExFatBootSector> ExFATParser::ParseBootSector(std::span<const std::uint8_t> bootSector) const {
    if (bootSector.size() < 512U) {
        return common::Result<ExFatBootSector>::Fail(
            common::StatusCode::InvalidArgument, "exFAT VBR must be at least 512 bytes.");
    }
    if (std::memcmp(bootSector.data() + 3U, "EXFAT   ", 8U) != 0 ||
        bootSector[510] != 0x55U || bootSector[511] != 0xAAU) {
        return common::Result<ExFatBootSector>::Fail(
            common::StatusCode::InvalidArgument, "Invalid exFAT Volume Boot Record signature.");
    }

    const std::uint8_t bpsShift = bootSector[108U];
    const std::uint8_t spcShift = bootSector[109U];
    if (bpsShift < 9U || bpsShift > 12U || spcShift > 25U || (bpsShift + spcShift) > 25U) {
        return common::Result<ExFatBootSector>::Fail(
            common::StatusCode::InvalidArgument, "Invalid exFAT sector/cluster shift parameters.");
    }

    ExFatBootSector vbr{};
    vbr.bytesPerSector = 1U << bpsShift;
    vbr.sectorsPerCluster = 1U << spcShift;
    vbr.clusterSizeBytes = vbr.bytesPerSector * vbr.sectorsPerCluster;
    vbr.volumeLengthSectors = ReadLe64(bootSector, 72U);
    vbr.fatOffsetSectors = ReadLe32(bootSector, 80U);
    vbr.fatLengthSectors = ReadLe32(bootSector, 84U);
    vbr.clusterHeapOffsetSectors = ReadLe32(bootSector, 88U);
    vbr.clusterCount = ReadLe32(bootSector, 92U);
    vbr.rootDirectoryCluster = ReadLe32(bootSector, 96U);

    if (vbr.volumeLengthSectors == 0ULL || vbr.clusterCount == 0U || vbr.rootDirectoryCluster < 2U) {
        return common::Result<ExFatBootSector>::Fail(
            common::StatusCode::InvalidArgument, "Invalid exFAT geometry parameters.");
    }

    vbr.totalVolumeBytes = vbr.volumeLengthSectors * vbr.bytesPerSector;
    vbr.fatStartOffsetBytes = static_cast<std::uint64_t>(vbr.fatOffsetSectors) * vbr.bytesPerSector;
    vbr.clusterHeapOffsetBytes = static_cast<std::uint64_t>(vbr.clusterHeapOffsetSectors) * vbr.bytesPerSector;
    if (vbr.clusterHeapOffsetBytes >= vbr.totalVolumeBytes) {
        return common::Result<ExFatBootSector>::Fail(
            common::StatusCode::OutOfBounds, "exFAT cluster heap starts beyond volume bounds.");
    }

    return common::Result<ExFatBootSector>::Ok(vbr);
}

common::Result<ExFatScanSummary> ExFATParser::ScanVolume(
    const disk::DiskReader& reader,
    bool includeActiveFiles) const {
    if (!reader.is_open()) {
        return common::Result<ExFatScanSummary>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader must be opened read-only.");
    }

    std::vector<std::uint8_t> bootBuf(512U, 0U);
    const auto readRes = reader.ReadBytes(0ULL, bootBuf);
    if (!readRes.ok() || readRes.value() < 512U) {
        return common::Result<ExFatScanSummary>::Fail(
            common::StatusCode::IoError, "Failed to read exFAT VBR.");
    }

    const auto vbrRes = ParseBootSector(bootBuf);
    if (!vbrRes.ok()) {
        return common::Result<ExFatScanSummary>::Fail(vbrRes.error().code, vbrRes.error().message);
    }

    ExFatScanSummary summary{};
    summary.bootSector = vbrRes.value();

    std::unordered_set<std::uint32_t> visitedDirs;
    std::uint64_t nextRecordId = 1;
    ScanExFatDirectory(
        reader, summary.bootSector, summary.bootSector.rootDirectoryCluster, "", includeActiveFiles, visitedDirs, nextRecordId, summary);

    return common::Result<ExFatScanSummary>::Ok(std::move(summary));
}

} // namespace salvus::filesystem
