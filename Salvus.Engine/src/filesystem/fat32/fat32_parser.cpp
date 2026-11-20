#include "filesystem/fat32_parser.hpp"
#include "disk/disk_validation.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
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

std::string FatDateTimeToIso8601(std::uint16_t fatDate, std::uint16_t fatTime) {
    if (fatDate == 0U) {
        return {};
    }
    const int year = 1980 + ((fatDate >> 9U) & 0x7FU);
    const int month = (fatDate >> 5U) & 0x0FU;
    const int day = fatDate & 0x1FU;
    const int hour = (fatTime >> 11U) & 0x1FU;
    const int minute = (fatTime >> 5U) & 0x3FU;
    const int second = (fatTime & 0x1FU) * 2;
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return {};
    }
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << year << "-"
        << std::setw(2) << month << "-"
        << std::setw(2) << day << "T"
        << std::setw(2) << hour << ":"
        << std::setw(2) << minute << ":"
        << std::setw(2) << second << "Z";
    return oss.str();
}

std::uint64_t ClusterToOffset(const Fat32BootSector& bpb, std::uint32_t cluster) noexcept {
    return bpb.dataStartOffsetBytes +
           static_cast<std::uint64_t>(cluster - 2U) * static_cast<std::uint64_t>(bpb.clusterSizeBytes);
}

std::uint32_t ReadFat32Entry(const disk::DiskReader& reader, const Fat32BootSector& bpb, std::uint32_t cluster) {
    const std::uint64_t fatEntryOffset = bpb.fatStartOffsetBytes + static_cast<std::uint64_t>(cluster) * 4ULL;
    std::uint8_t raw[4]{};
    const auto res = reader.ReadBytes(fatEntryOffset, raw);
    if (!res.ok() || res.value() < 4U) {
        return 0x0FFFFFFFU;
    }
    return ReadLe32(raw, 0U) & 0x0FFFFFFFU;
}

std::vector<std::uint32_t> FollowClusterChain(
    const disk::DiskReader& reader,
    const Fat32BootSector& bpb,
    std::uint32_t startCluster,
    std::size_t maxClusters = 65536U) {
    std::vector<std::uint32_t> chain;
    if (startCluster < 2U || startCluster >= bpb.totalClusters + 2U) {
        return chain;
    }
    std::unordered_set<std::uint32_t> visited;
    std::uint32_t current = startCluster;

    // If the starting cluster is marked free (0) in the FAT, the chain was cleared on deletion
    if (ReadFat32Entry(reader, bpb, current) == 0U) {
        return chain;
    }

    while (current >= 2U && current < 0x0FFFFFF8U && current < bpb.totalClusters + 2U && chain.size() < maxClusters) {
        if (!visited.insert(current).second) {
            break; // Cycle protection
        }
        chain.push_back(current);
        const std::uint32_t next = ReadFat32Entry(reader, bpb, current);
        if (next == 0U || next >= 0x0FFFFFF8U) {
            break;
        }
        current = next;
    }
    return chain;
}

std::u16string ExtractLfnChunk(std::span<const std::uint8_t> entry) {
    static constexpr std::size_t kOffsets[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    std::u16string part;
    for (std::size_t off : kOffsets) {
        const std::uint16_t ch = ReadLe16(entry, off);
        if (ch == 0x0000U || ch == 0xFFFFU) {
            break;
        }
        part.push_back(static_cast<char16_t>(ch));
    }
    return part;
}

std::string ParseShort83Name(std::span<const std::uint8_t> entry, bool isDeleted) {
    std::string base;
    for (std::size_t i = 0; i < 8U; ++i) {
        const char ch = (i == 0U && isDeleted) ? '_' : static_cast<char>(entry[i]);
        base.push_back(ch);
    }
    while (!base.empty() && base.back() == ' ') {
        base.pop_back();
    }

    std::string ext;
    for (std::size_t i = 8U; i < 11U; ++i) {
        ext.push_back(static_cast<char>(entry[i]));
    }
    while (!ext.empty() && ext.back() == ' ') {
        ext.pop_back();
    }

    return ext.empty() ? base : (base + "." + ext);
}

void ScanDirectoryCluster(
    const disk::DiskReader& reader,
    const Fat32BootSector& bpb,
    std::uint32_t dirCluster,
    const std::string& parentPath,
    bool includeActiveFiles,
    std::unordered_set<std::uint32_t>& visitedDirs,
    std::uint64_t& nextRecordId,
    Fat32ScanSummary& summary) {
    if (dirCluster < 2U || !visitedDirs.insert(dirCluster).second || visitedDirs.size() > 4096U) {
        return;
    }

    summary.directoriesScanned++;
    const auto dirClusters = FollowClusterChain(reader, bpb, dirCluster, 256U);
    if (dirClusters.empty()) {
        return;
    }

    std::vector<std::uint8_t> clusterBuf(bpb.clusterSizeBytes, 0U);
    std::vector<std::u16string> lfnParts;

    for (const std::uint32_t c : dirClusters) {
        const auto readRes = reader.ReadBytes(ClusterToOffset(bpb, c), clusterBuf);
        if (!readRes.ok() || readRes.value() < 32U) {
            break;
        }

        const std::size_t entryCount = readRes.value() / 32U;
        for (std::size_t i = 0; i < entryCount; ++i) {
            const auto entry = std::span<const std::uint8_t>(clusterBuf.data() + i * 32U, 32U);
            const std::uint8_t firstByte = entry[0U];
            if (firstByte == 0x00U) {
                return; // End of directory
            }

            const std::uint8_t attr = entry[11U];
            if (attr == 0x0FU) {
                // Long File Name entry (stored in reverse order before the 8.3 entry)
                lfnParts.push_back(ExtractLfnChunk(entry));
                continue;
            }

            // Skip volume labels
            if ((attr & 0x08U) != 0U) {
                lfnParts.clear();
                continue;
            }

            const bool isDeleted = (firstByte == 0xE5U);
            const bool isDirectory = (attr & 0x10U) != 0U;

            std::string fileName;
            if (!lfnParts.empty()) {
                std::u16string fullLfn;
                for (auto it = lfnParts.rbegin(); it != lfnParts.rend(); ++it) {
                    fullLfn += *it;
                }
                fileName = Utf16ToUtf8(fullLfn);
                lfnParts.clear();
            }
            if (fileName.empty()) {
                fileName = ParseShort83Name(entry, isDeleted);
            }

            if (fileName == "." || fileName == ".." || fileName.empty()) {
                continue;
            }

            const std::uint32_t startCluster =
                (static_cast<std::uint32_t>(ReadLe16(entry, 20U)) << 16U) |
                static_cast<std::uint32_t>(ReadLe16(entry, 26U));
            const std::uint32_t fileSize = ReadLe32(entry, 28U);
            const std::string fullPath = parentPath + "\\" + fileName;

            if (isDirectory) {
                if (!isDeleted && startCluster >= 2U) {
                    ScanDirectoryCluster(
                        reader, bpb, startCluster, fullPath, includeActiveFiles, visitedDirs, nextRecordId, summary);
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
            file.fileSizeBytes = fileSize;
            file.createdUtc = FatDateTimeToIso8601(ReadLe16(entry, 16U), ReadLe16(entry, 14U));
            file.modifiedUtc = FatDateTimeToIso8601(ReadLe16(entry, 24U), ReadLe16(entry, 22U));

            if (fileSize == 0U) {
                file.confidence = RecoveryConfidence::High;
                file.confidenceReason = "Zero-byte FAT32 file with valid directory metadata.";
            } else if (startCluster >= 2U && startCluster < bpb.totalClusters + 2U) {
                const auto chain = FollowClusterChain(reader, bpb, startCluster);
                const std::uint64_t requiredClusters =
                    (static_cast<std::uint64_t>(fileSize) + bpb.clusterSizeBytes - 1ULL) / bpb.clusterSizeBytes;

                if (chain.size() >= requiredClusters) {
                    for (const std::uint32_t cl : chain) {
                        file.dataExtents.push_back(recovery::FileExtent{
                            ClusterToOffset(bpb, cl), bpb.clusterSizeBytes, false});
                    }
                    file.confidence = RecoveryConfidence::High;
                    file.confidenceReason = "Intact FAT32 cluster chain covering full declared file size.";
                } else {
                    // Deleted FAT32 entries clear their FAT chain; reconstruct contiguous clusters from startCluster
                    const std::uint64_t startOff = ClusterToOffset(bpb, startCluster);
                    const std::uint64_t contigLen = requiredClusters * bpb.clusterSizeBytes;
                    if (startOff <= bpb.totalVolumeBytes && contigLen <= bpb.totalVolumeBytes - startOff) {
                        file.dataExtents.push_back(recovery::FileExtent{startOff, contigLen, false});
                        file.confidence = RecoveryConfidence::Medium;
                        file.confidenceReason =
                            "FAT32 cluster chain was cleared on deletion; contiguous allocation from starting cluster assumed.";
                    } else {
                        file.confidence = RecoveryConfidence::Damaged;
                        file.confidenceReason = "Starting cluster and declared file size exceed volume bounds.";
                    }
                }
            } else {
                file.confidence = RecoveryConfidence::Unrecoverable;
                file.confidenceReason = "FAT32 directory entry has no valid starting cluster.";
            }

            summary.entries.push_back(std::move(file));
        }
    }
}

} // namespace

common::Result<Fat32BootSector> FAT32Parser::ParseBootSector(std::span<const std::uint8_t> bootSector) const {
    if (bootSector.size() < 512U) {
        return common::Result<Fat32BootSector>::Fail(
            common::StatusCode::InvalidArgument, "FAT32 boot sector must be at least 512 bytes.");
    }
    if (bootSector[510] != 0x55U || bootSector[511] != 0xAAU) {
        return common::Result<Fat32BootSector>::Fail(
            common::StatusCode::InvalidArgument, "Missing 0x55AA boot signature.");
    }

    Fat32BootSector bpb{};
    bpb.bytesPerSector = ReadLe16(bootSector, 11U);
    bpb.sectorsPerCluster = bootSector[13U];
    bpb.reservedSectors = ReadLe16(bootSector, 14U);
    bpb.numFats = bootSector[16U];
    const std::uint16_t totalSectors16 = ReadLe16(bootSector, 19U);
    const std::uint32_t totalSectors32 = ReadLe32(bootSector, 32U);
    bpb.totalSectors = totalSectors16 != 0U ? totalSectors16 : totalSectors32;
    bpb.sectorsPerFat = ReadLe32(bootSector, 36U);
    bpb.rootCluster = ReadLe32(bootSector, 44U);

    if (!disk::IsValidSectorSize(bpb.bytesPerSector) || bpb.bytesPerSector > 4096U) {
        return common::Result<Fat32BootSector>::Fail(
            common::StatusCode::InvalidArgument, "Invalid FAT32 bytesPerSector.");
    }
    if (bpb.sectorsPerCluster == 0U || !disk::IsPowerOfTwo(bpb.sectorsPerCluster)) {
        return common::Result<Fat32BootSector>::Fail(
            common::StatusCode::InvalidArgument, "Invalid FAT32 sectorsPerCluster.");
    }
    if (bpb.numFats == 0U || bpb.sectorsPerFat == 0U || bpb.rootCluster < 2U) {
        return common::Result<Fat32BootSector>::Fail(
            common::StatusCode::InvalidArgument, "Invalid FAT32 layout parameters.");
    }

    bpb.clusterSizeBytes = static_cast<std::uint32_t>(bpb.bytesPerSector) *
                           static_cast<std::uint32_t>(bpb.sectorsPerCluster);
    bpb.totalVolumeBytes = static_cast<std::uint64_t>(bpb.totalSectors) * bpb.bytesPerSector;
    bpb.fatStartOffsetBytes = static_cast<std::uint64_t>(bpb.reservedSectors) * bpb.bytesPerSector;

    const std::uint64_t dataStartSector =
        static_cast<std::uint64_t>(bpb.reservedSectors) +
        static_cast<std::uint64_t>(bpb.numFats) * bpb.sectorsPerFat;
    if (dataStartSector >= bpb.totalSectors) {
        return common::Result<Fat32BootSector>::Fail(
            common::StatusCode::OutOfBounds, "FAT32 data region starts beyond volume capacity.");
    }

    bpb.dataStartOffsetBytes = dataStartSector * bpb.bytesPerSector;
    bpb.totalClusters = static_cast<std::uint32_t>((bpb.totalSectors - dataStartSector) / bpb.sectorsPerCluster);
    return common::Result<Fat32BootSector>::Ok(bpb);
}

common::Result<Fat32ScanSummary> FAT32Parser::ScanVolume(
    const disk::DiskReader& reader,
    bool includeActiveFiles) const {
    if (!reader.is_open()) {
        return common::Result<Fat32ScanSummary>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader must be opened read-only.");
    }

    std::vector<std::uint8_t> bootBuf(512U, 0U);
    const auto readRes = reader.ReadBytes(0ULL, bootBuf);
    if (!readRes.ok() || readRes.value() < 512U) {
        return common::Result<Fat32ScanSummary>::Fail(
            common::StatusCode::IoError, "Failed to read FAT32 boot sector.");
    }

    const auto bpbRes = ParseBootSector(bootBuf);
    if (!bpbRes.ok()) {
        return common::Result<Fat32ScanSummary>::Fail(bpbRes.error().code, bpbRes.error().message);
    }

    Fat32ScanSummary summary{};
    summary.bootSector = bpbRes.value();

    std::unordered_set<std::uint32_t> visitedDirs;
    std::uint64_t nextRecordId = 1;
    ScanDirectoryCluster(
        reader, summary.bootSector, summary.bootSector.rootCluster, "", includeActiveFiles, visitedDirs, nextRecordId, summary);

    return common::Result<Fat32ScanSummary>::Ok(std::move(summary));
}

} // namespace salvus::filesystem
