#include "filesystem/ntfs_parser.hpp"
#include "disk/disk_validation.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace salvus::filesystem {

namespace {

constexpr std::uint32_t kAttrStandardInfo = 0x10U;
constexpr std::uint32_t kAttrFileName = 0x30U;
constexpr std::uint32_t kAttrData = 0x80U;
constexpr std::uint32_t kAttrEndMarker = 0xFFFFFFFFU;

constexpr std::uint16_t kMftFlagInUse = 0x0001U;
constexpr std::uint16_t kMftFlagDirectory = 0x0002U;

std::uint16_t ReadLe16(std::span<const std::uint8_t> buf, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(buf[offset]) |
           (static_cast<std::uint16_t>(buf[offset + 1U]) << 8U);
}

std::uint32_t ReadLe32(std::span<const std::uint8_t> buf, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(buf[offset]) |
           (static_cast<std::uint32_t>(buf[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(buf[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(buf[offset + 3U]) << 24U);
}

std::uint64_t ReadLe64(std::span<const std::uint8_t> buf, std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8U; ++i) {
        value |= (static_cast<std::uint64_t>(buf[offset + i]) << (i * 8U));
    }
    return value;
}

std::string FileTimeToIso8601Utc(std::uint64_t fileTime) {
    if (fileTime == 0ULL) {
        return {};
    }
    // Windows FILETIME: 100-ns intervals since 1601-01-01; Unix epoch offset is 11644473600 seconds.
    constexpr std::uint64_t kTicksPerSecond = 10000000ULL;
    constexpr std::uint64_t kEpochDiffSeconds = 11644473600ULL;
    const std::uint64_t totalSeconds = fileTime / kTicksPerSecond;
    if (totalSeconds < kEpochDiffSeconds) {
        return {};
    }
    const std::uint64_t unixSeconds = totalSeconds - kEpochDiffSeconds;
    if (unixSeconds > 4102444800ULL) { // Clamp beyond year 2100 for malformed timestamps
        return {};
    }

    const auto timeVal = static_cast<std::time_t>(unixSeconds);
    std::tm utcTm{};
#ifdef _WIN32
    if (gmtime_s(&utcTm, &timeVal) != 0) {
        return {};
    }
#else
    if (gmtime_r(&timeVal, &utcTm) == nullptr) {
        return {};
    }
#endif
    std::ostringstream oss;
    oss << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string Utf16LeToUtf8(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 2U) {
        return {};
    }
    const std::size_t charCount = bytes.size() / 2U;
    std::wstring wide(charCount, L'\0');
    for (std::size_t i = 0; i < charCount; ++i) {
        wide[i] = static_cast<wchar_t>(ReadLe16(bytes, i * 2U));
    }
#ifdef _WIN32
    const int required = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(required), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), required, nullptr, nullptr);
    return out;
#else
    std::string out;
    out.reserve(charCount);
    for (wchar_t ch : wide) {
        out.push_back(ch < 128 ? static_cast<char>(ch) : '?');
    }
    return out;
#endif
}

struct DataRunParseOutput {
    std::vector<recovery::FileExtent> extents;
    bool hasOutOfBoundsOrCorruptRun = false;
};

// Parses NTFS non-resident mapping pairs (data runs) with signed relative LCNs and strict bounds checks.
DataRunParseOutput ParseDataRuns(
    std::span<const std::uint8_t> runBytes,
    const NtfsBootSector& bootSector) {
    DataRunParseOutput output{};
    std::size_t pos = 0;
    std::int64_t currentLcn = 0;
    constexpr std::size_t kMaxRuns = 65536U;

    while (pos < runBytes.size() && output.extents.size() < kMaxRuns) {
        const std::uint8_t header = runBytes[pos++];
        if (header == 0x00U) {
            break; // End of data runs
        }

        const std::uint8_t lengthSize = header & 0x0FU;
        const std::uint8_t offsetSize = (header >> 4U) & 0x0FU;

        if (lengthSize == 0U || lengthSize > 8U || offsetSize > 8U) {
            output.hasOutOfBoundsOrCorruptRun = true;
            break;
        }
        if (pos + lengthSize + offsetSize > runBytes.size()) {
            output.hasOutOfBoundsOrCorruptRun = true;
            break;
        }

        std::uint64_t runClusters = 0;
        for (std::uint8_t i = 0; i < lengthSize; ++i) {
            runClusters |= (static_cast<std::uint64_t>(runBytes[pos++]) << (i * 8U));
        }

        std::int64_t deltaLcn = 0;
        if (offsetSize > 0U) {
            std::uint64_t rawDelta = 0;
            for (std::uint8_t i = 0; i < offsetSize; ++i) {
                rawDelta |= (static_cast<std::uint64_t>(runBytes[pos++]) << (i * 8U));
            }
            // Sign-extend if MSB of the last byte is set
            const std::uint64_t signBit = 1ULL << (offsetSize * 8U - 1U);
            if ((rawDelta & signBit) != 0ULL && offsetSize < 8U) {
                rawDelta |= (~0ULL << (offsetSize * 8U));
            }
            deltaLcn = static_cast<std::int64_t>(rawDelta);
        }

        if (runClusters == 0ULL) {
            output.hasOutOfBoundsOrCorruptRun = true;
            break;
        }

        // Overflow check for run length in bytes
        if (runClusters > (std::numeric_limits<std::uint64_t>::max)() / bootSector.clusterSizeBytes) {
            output.hasOutOfBoundsOrCorruptRun = true;
            break;
        }
        const std::uint64_t runLengthBytes = runClusters * bootSector.clusterSizeBytes;

        if (offsetSize == 0U) {
            // Sparse run
            output.extents.push_back(recovery::FileExtent{0ULL, runLengthBytes, true});
            continue;
        }

        // Signed LCN accumulation with underflow/overflow checks
        if ((deltaLcn > 0 && currentLcn > (std::numeric_limits<std::int64_t>::max)() - deltaLcn) ||
            (deltaLcn < 0 && currentLcn < (std::numeric_limits<std::int64_t>::min)() - deltaLcn)) {
            output.hasOutOfBoundsOrCorruptRun = true;
            break;
        }
        currentLcn += deltaLcn;
        if (currentLcn < 0) {
            output.hasOutOfBoundsOrCorruptRun = true;
            break;
        }

        const auto absLcn = static_cast<std::uint64_t>(currentLcn);
        if (absLcn > (std::numeric_limits<std::uint64_t>::max)() / bootSector.clusterSizeBytes) {
            output.hasOutOfBoundsOrCorruptRun = true;
            break;
        }
        const std::uint64_t diskOffset = absLcn * bootSector.clusterSizeBytes;

        if (diskOffset > bootSector.totalVolumeBytes ||
            runLengthBytes > bootSector.totalVolumeBytes - diskOffset) {
            output.hasOutOfBoundsOrCorruptRun = true;
            break;
        }

        output.extents.push_back(recovery::FileExtent{diskOffset, runLengthBytes, false});
    }

    return output;
}

bool ApplyUsaFixup(std::vector<std::uint8_t>& record, std::uint16_t bytesPerSector) noexcept {
    if (record.size() < 48U || bytesPerSector < 512U) {
        return false;
    }

    const std::uint16_t usaOffset = ReadLe16(record, 4U);
    const std::uint16_t usaCount = ReadLe16(record, 6U);
    if (usaCount < 2U) {
        return false;
    }
    const std::size_t usaBytes = static_cast<std::size_t>(usaCount) * 2U;
    if (static_cast<std::size_t>(usaOffset) + usaBytes > record.size()) {
        return false;
    }

    const std::uint16_t expectedUsn = ReadLe16(record, usaOffset);
    const std::size_t sectorCount = static_cast<std::size_t>(usaCount) - 1U;
    if (sectorCount * static_cast<std::size_t>(bytesPerSector) > record.size()) {
        return false;
    }

    for (std::size_t s = 0; s < sectorCount; ++s) {
        const std::size_t sectorEndOffset = (s + 1U) * static_cast<std::size_t>(bytesPerSector) - 2U;
        const std::uint16_t actualUsn = ReadLe16(record, sectorEndOffset);
        if (actualUsn != expectedUsn) {
            return false;
        }
        const std::size_t fixupEntryOffset = static_cast<std::size_t>(usaOffset) + (s + 1U) * 2U;
        record[sectorEndOffset] = record[fixupEntryOffset];
        record[sectorEndOffset + 1U] = record[fixupEntryOffset + 1U];
    }

    return true;
}

} // namespace

std::string RecoveryConfidenceToString(RecoveryConfidence confidence) {
    switch (confidence) {
        case RecoveryConfidence::High:          return "High";
        case RecoveryConfidence::Medium:        return "Medium";
        case RecoveryConfidence::Low:           return "Low";
        case RecoveryConfidence::Damaged:       return "Damaged";
        case RecoveryConfidence::Unrecoverable: return "Unrecoverable";
    }
    return "Unrecoverable";
}

common::Result<NtfsBootSector> NTFSParser::ParseBootSector(std::span<const std::uint8_t> bootSector) const {
    if (bootSector.size() < 512U) {
        return common::Result<NtfsBootSector>::Fail(
            common::StatusCode::InvalidArgument, "NTFS boot sector must be at least 512 bytes.");
    }

    if (std::memcmp(bootSector.data() + 3U, "NTFS    ", 8U) != 0) {
        return common::Result<NtfsBootSector>::Fail(
            common::StatusCode::InvalidArgument, "Volume OEM ID is not 'NTFS    '.");
    }

    if (bootSector[510] != 0x55U || bootSector[511] != 0xAAU) {
        return common::Result<NtfsBootSector>::Fail(
            common::StatusCode::InvalidArgument, "Missing 0x55AA boot sector signature.");
    }

    NtfsBootSector bpb{};
    bpb.bytesPerSector = ReadLe16(bootSector, 11U);
    bpb.sectorsPerCluster = bootSector[13U];
    bpb.totalSectors = ReadLe64(bootSector, 40U);
    bpb.mftStartCluster = ReadLe64(bootSector, 48U);
    bpb.mftMirrorStartCluster = ReadLe64(bootSector, 56U);
    bpb.volumeSerialNumber = ReadLe64(bootSector, 72U);

    if (!disk::IsValidSectorSize(bpb.bytesPerSector) || bpb.bytesPerSector > 4096U) {
        return common::Result<NtfsBootSector>::Fail(
            common::StatusCode::InvalidArgument, "Invalid NTFS bytesPerSector value.");
    }
    if (bpb.sectorsPerCluster == 0U || !disk::IsPowerOfTwo(bpb.sectorsPerCluster)) {
        return common::Result<NtfsBootSector>::Fail(
            common::StatusCode::InvalidArgument, "Invalid NTFS sectorsPerCluster value.");
    }

    bpb.clusterSizeBytes = static_cast<std::uint32_t>(bpb.bytesPerSector) *
                           static_cast<std::uint32_t>(bpb.sectorsPerCluster);

    if (bpb.totalSectors == 0ULL ||
        bpb.totalSectors > (std::numeric_limits<std::uint64_t>::max)() / bpb.bytesPerSector) {
        return common::Result<NtfsBootSector>::Fail(
            common::StatusCode::OutOfBounds, "NTFS totalSectors is zero or overflows volume size.");
    }
    bpb.totalVolumeBytes = bpb.totalSectors * bpb.bytesPerSector;

    const auto clustersPerMftRecord = static_cast<std::int8_t>(bootSector[64U]);
    if (clustersPerMftRecord > 0) {
        bpb.mftRecordSizeBytes = static_cast<std::uint32_t>(clustersPerMftRecord) * bpb.clusterSizeBytes;
    } else if (clustersPerMftRecord < 0) {
        const int shift = -static_cast<int>(clustersPerMftRecord);
        if (shift < 9 || shift > 16) {
            return common::Result<NtfsBootSector>::Fail(
                common::StatusCode::InvalidArgument, "Invalid negative NTFS MFT record size exponent.");
        }
        bpb.mftRecordSizeBytes = 1U << static_cast<unsigned int>(shift);
    } else {
        return common::Result<NtfsBootSector>::Fail(
            common::StatusCode::InvalidArgument, "NTFS clustersPerMftRecord cannot be zero.");
    }

    if (bpb.mftStartCluster > bpb.totalVolumeBytes / bpb.clusterSizeBytes) {
        return common::Result<NtfsBootSector>::Fail(
            common::StatusCode::OutOfBounds, "NTFS $MFT start cluster exceeds volume capacity.");
    }

    return common::Result<NtfsBootSector>::Ok(bpb);
}

common::Result<NtfsFileEntry> NTFSParser::ParseMftRecord(
    std::uint64_t recordNumber,
    std::span<const std::uint8_t> rawRecord,
    const NtfsBootSector& bootSector) const {
    if (rawRecord.size() < 48U || rawRecord.size() < bootSector.mftRecordSizeBytes) {
        return common::Result<NtfsFileEntry>::Fail(
            common::StatusCode::InvalidArgument, "MFT record buffer is smaller than configured MFT record size.");
    }

    // Check "FILE" magic
    if (std::memcmp(rawRecord.data(), "FILE", 4U) != 0) {
        return common::Result<NtfsFileEntry>::Fail(
            common::StatusCode::InvalidArgument, "MFT record does not start with 'FILE' signature.");
    }

    std::vector<std::uint8_t> record(rawRecord.begin(), rawRecord.begin() + bootSector.mftRecordSizeBytes);
    const bool fixupValid = ApplyUsaFixup(record, bootSector.bytesPerSector);

    const std::uint16_t firstAttrOffset = ReadLe16(record, 20U);
    const std::uint16_t flags = ReadLe16(record, 22U);
    const std::uint32_t usedSize = ReadLe32(record, 24U);

    if (firstAttrOffset < 42U || firstAttrOffset >= record.size()) {
        return common::Result<NtfsFileEntry>::Fail(
            common::StatusCode::InvalidArgument, "Invalid first attribute offset in MFT record.");
    }

    const std::size_t parseLimit = (std::min)(
        static_cast<std::size_t>(usedSize > firstAttrOffset ? usedSize : record.size()),
        record.size());

    NtfsFileEntry entry{};
    entry.recordNumber = recordNumber;
    entry.isDeleted = (flags & kMftFlagInUse) == 0U;
    entry.isDirectory = (flags & kMftFlagDirectory) != 0U;

    bool hasWin32Name = false;
    bool hasDefaultDataAttr = false;
    bool hasCorruptDataRun = false;

    std::size_t attrOffset = firstAttrOffset;
    constexpr int kMaxAttributes = 256;
    int attrCount = 0;

    while (attrOffset + 8U <= parseLimit && attrCount < kMaxAttributes) {
        ++attrCount;
        const std::uint32_t attrType = ReadLe32(record, attrOffset);
        if (attrType == kAttrEndMarker) {
            break;
        }

        const std::uint32_t attrLength = ReadLe32(record, attrOffset + 4U);
        if (attrLength < 16U || attrOffset + attrLength > parseLimit) {
            hasCorruptDataRun = true;
            break;
        }

        const bool isNonResident = (record[attrOffset + 8U] != 0U);
        const std::uint8_t nameLengthChars = record[attrOffset + 9U];
        const std::uint16_t nameOffset = ReadLe16(record, attrOffset + 10U);

        std::string attrName;
        if (nameLengthChars > 0U) {
            const std::size_t nameBytes = static_cast<std::size_t>(nameLengthChars) * 2U;
            if (static_cast<std::size_t>(nameOffset) + nameBytes <= attrLength) {
                attrName = Utf16LeToUtf8(std::span<const std::uint8_t>(
                    record.data() + attrOffset + nameOffset, nameBytes));
            }
        }

        if (attrType == kAttrStandardInfo && !isNonResident && attrLength >= 24U) {
            const std::uint32_t valLen = ReadLe32(record, attrOffset + 16U);
            const std::uint16_t valOff = ReadLe16(record, attrOffset + 20U);
            if (valLen >= 32U && static_cast<std::size_t>(valOff) + valLen <= attrLength) {
                const std::size_t base = attrOffset + valOff;
                entry.createdUtc = FileTimeToIso8601Utc(ReadLe64(record, base));
                entry.modifiedUtc = FileTimeToIso8601Utc(ReadLe64(record, base + 8U));
            }
        } else if (attrType == kAttrFileName && !isNonResident && attrLength >= 24U) {
            const std::uint32_t valLen = ReadLe32(record, attrOffset + 16U);
            const std::uint16_t valOff = ReadLe16(record, attrOffset + 20U);
            if (valLen >= 66U && static_cast<std::size_t>(valOff) + valLen <= attrLength) {
                const std::size_t base = attrOffset + valOff;
                const std::uint64_t parentRef = ReadLe64(record, base) & 0x0000FFFFFFFFFFFFULL;
                const std::uint8_t fnLenChars = record[base + 64U];
                const std::uint8_t fnNamespace = record[base + 65U];
                const std::size_t fnBytes = static_cast<std::size_t>(fnLenChars) * 2U;

                if (66U + fnBytes <= valLen) {
                    const std::string parsedName = Utf16LeToUtf8(std::span<const std::uint8_t>(
                        record.data() + base + 66U, fnBytes));

                    // Prefer Win32 (1) or Win32&DOS (3) over DOS-only (2)
                    const bool isPreferred = (fnNamespace == 1U || fnNamespace == 3U || fnNamespace == 0U);
                    if (entry.fileName.empty() || (!hasWin32Name && isPreferred)) {
                        entry.fileName = parsedName;
                        entry.parentRecordNumber = parentRef;
                        hasWin32Name = isPreferred;
                        if (entry.createdUtc.empty()) {
                            entry.createdUtc = FileTimeToIso8601Utc(ReadLe64(record, base + 8U));
                        }
                        if (entry.modifiedUtc.empty()) {
                            entry.modifiedUtc = FileTimeToIso8601Utc(ReadLe64(record, base + 16U));
                        }
                    }
                }
            }
        } else if (attrType == kAttrData) {
            if (!attrName.empty()) {
                entry.alternateDataStreams.push_back(attrName);
            } else {
                hasDefaultDataAttr = true;
                if (!isNonResident && attrLength >= 24U) {
                    const std::uint32_t valLen = ReadLe32(record, attrOffset + 16U);
                    const std::uint16_t valOff = ReadLe16(record, attrOffset + 20U);
                    if (static_cast<std::size_t>(valOff) + valLen <= attrLength) {
                        entry.isResident = true;
                        entry.fileSizeBytes = valLen;
                        const std::uint8_t* dataPtr = record.data() + attrOffset + valOff;
                        entry.residentData.assign(dataPtr, dataPtr + valLen);
                    } else {
                        hasCorruptDataRun = true;
                    }
                } else if (isNonResident && attrLength >= 64U) {
                    entry.isResident = false;
                    const std::uint16_t runOffset = ReadLe16(record, attrOffset + 32U);
                    const std::uint64_t realSize = ReadLe64(record, attrOffset + 48U);
                    entry.fileSizeBytes = realSize;

                    if (runOffset >= 64U && runOffset < attrLength) {
                        const auto runSpan = std::span<const std::uint8_t>(
                            record.data() + attrOffset + runOffset,
                            attrLength - runOffset);
                        auto parsedRuns = ParseDataRuns(runSpan, bootSector);
                        entry.dataExtents = std::move(parsedRuns.extents);
                        if (parsedRuns.hasOutOfBoundsOrCorruptRun) {
                            hasCorruptDataRun = true;
                        }
                    } else {
                        hasCorruptDataRun = true;
                    }
                }
            }
        }

        attrOffset += attrLength;
    }

    // Classify recovery confidence transparently based on structural evidence (Section 8)
    if (!fixupValid) {
        entry.confidence = RecoveryConfidence::Damaged;
        entry.confidenceReason = "MFT record Update Sequence Array (USA) fixup mismatch; metadata may be partially overwritten.";
    } else if (hasCorruptDataRun) {
        entry.confidence = RecoveryConfidence::Damaged;
        entry.confidenceReason = "One or more NTFS data runs are malformed or point outside volume bounds.";
    } else if (entry.isDirectory) {
        entry.confidence = RecoveryConfidence::High;
        entry.confidenceReason = "Valid directory MFT metadata record.";
    } else if (!hasDefaultDataAttr) {
        entry.confidence = RecoveryConfidence::Unrecoverable;
        entry.confidenceReason = "No default $DATA attribute present in MFT record.";
    } else if (entry.isResident) {
        entry.confidence = RecoveryConfidence::High;
        entry.confidenceReason = "File content is stored resident directly inside a validated MFT record.";
    } else if (!entry.dataExtents.empty()) {
        std::uint64_t coveredBytes = 0;
        for (const auto& ext : entry.dataExtents) {
            coveredBytes += ext.lengthBytes;
        }
        if (coveredBytes >= entry.fileSizeBytes) {
            entry.confidence = RecoveryConfidence::High;
            entry.confidenceReason = "Valid MFT metadata with " + std::to_string(entry.dataExtents.size()) +
                                     " in-bounds data extent(s) covering full file size.";
        } else {
            entry.confidence = RecoveryConfidence::Damaged;
            entry.confidenceReason = "Allocated data extents cover fewer bytes than declared file size.";
        }
    } else if (entry.fileSizeBytes == 0ULL) {
        entry.confidence = RecoveryConfidence::High;
        entry.confidenceReason = "Zero-byte file with valid MFT metadata.";
    } else {
        entry.confidence = RecoveryConfidence::Unrecoverable;
        entry.confidenceReason = "Non-resident file has no recoverable data runs.";
    }

    return common::Result<NtfsFileEntry>::Ok(std::move(entry));
}

common::Result<NtfsScanSummary> NTFSParser::ScanVolume(
    const disk::DiskReader& reader,
    const NtfsScanOptions& options) const {
    if (!reader.is_open()) {
        return common::Result<NtfsScanSummary>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader must be opened read-only before scanning NTFS.");
    }

    std::vector<std::uint8_t> bootBuf(512U, 0U);
    const auto bootRead = reader.ReadBytes(0ULL, bootBuf);
    if (!bootRead.ok() || bootRead.value() < 512U) {
        return common::Result<NtfsScanSummary>::Fail(
            common::StatusCode::IoError, "Failed to read NTFS boot sector.");
    }

    const auto bpbRes = ParseBootSector(bootBuf);
    if (!bpbRes.ok()) {
        return common::Result<NtfsScanSummary>::Fail(bpbRes.error().code, bpbRes.error().message);
    }

    const NtfsBootSector& bpb = bpbRes.value();
    const std::uint64_t mftStartOffset = bpb.mftStartCluster * bpb.clusterSizeBytes;

    // Read MFT record 0 ($MFT itself) to discover the $MFT data extents (handles fragmented $MFT!)
    std::vector<std::uint8_t> mft0Buf(bpb.mftRecordSizeBytes, 0U);
    const auto mft0Read = reader.ReadBytes(mftStartOffset, mft0Buf);
    if (!mft0Read.ok() || mft0Read.value() < bpb.mftRecordSizeBytes) {
        return common::Result<NtfsScanSummary>::Fail(
            common::StatusCode::IoError, "Failed to read $MFT record 0.");
    }

    const auto mft0Res = ParseMftRecord(0ULL, mft0Buf, bpb);
    std::vector<recovery::FileExtent> mftExtents;
    std::uint64_t mftTotalBytes = 0;

    if (mft0Res.ok() && !mft0Res.value().isResident && !mft0Res.value().dataExtents.empty()) {
        mftExtents = mft0Res.value().dataExtents;
        mftTotalBytes = mft0Res.value().fileSizeBytes;
    } else {
        // Fallback: scan contiguous region from $MFT start up to remaining volume size (bounded)
        const std::uint64_t remaining = bpb.totalVolumeBytes > mftStartOffset
                                            ? (bpb.totalVolumeBytes - mftStartOffset)
                                            : 0ULL;
        const std::uint64_t maxBytes = (std::min)(
            remaining,
            options.maxMftRecords * static_cast<std::uint64_t>(bpb.mftRecordSizeBytes));
        mftExtents.push_back(recovery::FileExtent{mftStartOffset, maxBytes, false});
        mftTotalBytes = maxBytes;
    }

    NtfsScanSummary summary{};
    summary.bootSector = bpb;

    struct DirNode {
        std::uint64_t parentRecord = 0;
        std::string name;
    };
    std::unordered_map<std::uint64_t, DirNode> dirMap;
    std::vector<NtfsFileEntry> candidateFiles;

    std::uint64_t recordNumber = 0;
    std::uint64_t bytesProcessed = 0;
    constexpr std::size_t kBatchRecords = 64U;
    std::vector<std::uint8_t> batchBuf(kBatchRecords * bpb.mftRecordSizeBytes, 0U);

    for (const auto& ext : mftExtents) {
        if (ext.isSparse || recordNumber >= options.maxMftRecords || bytesProcessed >= mftTotalBytes) {
            continue;
        }

        std::uint64_t extOffset = 0;
        while (extOffset + bpb.mftRecordSizeBytes <= ext.lengthBytes &&
               recordNumber < options.maxMftRecords &&
               bytesProcessed < mftTotalBytes) {
            const std::uint64_t remainingInExt = ext.lengthBytes - extOffset;
            const std::uint64_t remainingInMft = mftTotalBytes - bytesProcessed;
            const std::uint64_t maxRecordsLeft = options.maxMftRecords - recordNumber;

            std::uint64_t chunkBytes = (std::min)(
                static_cast<std::uint64_t>(batchBuf.size()),
                (std::min)(remainingInExt, remainingInMft));
            chunkBytes = (std::min)(chunkBytes, maxRecordsLeft * bpb.mftRecordSizeBytes);
            chunkBytes -= (chunkBytes % bpb.mftRecordSizeBytes);
            if (chunkBytes == 0ULL) {
                break;
            }

            const auto readRes = reader.ReadBytes(
                ext.diskOffsetBytes + extOffset,
                std::span<std::uint8_t>(batchBuf.data(), static_cast<std::size_t>(chunkBytes)));
            if (!readRes.ok() || readRes.value() < bpb.mftRecordSizeBytes) {
                break;
            }

            const std::size_t recordsInChunk = readRes.value() / bpb.mftRecordSizeBytes;
            for (std::size_t i = 0; i < recordsInChunk; ++i) {
                const auto recSpan = std::span<const std::uint8_t>(
                    batchBuf.data() + i * bpb.mftRecordSizeBytes,
                    bpb.mftRecordSizeBytes);
                const auto parsed = ParseMftRecord(recordNumber, recSpan, bpb);
                if (parsed.ok()) {
                    const auto& e = parsed.value();
                    if (e.isDirectory && !e.fileName.empty()) {
                        dirMap[e.recordNumber] = DirNode{e.parentRecordNumber, e.fileName};
                    }
                    // Skip NTFS system metafiles (records 0..15) and entries without filenames
                    const bool isSystemMetafile = (e.recordNumber < 16ULL);
                    if (!isSystemMetafile && !e.isDirectory && !e.fileName.empty()) {
                        if (e.isDeleted || options.includeActiveFiles) {
                            candidateFiles.push_back(e);
                        }
                    }
                }
                ++recordNumber;
                bytesProcessed += bpb.mftRecordSizeBytes;
            }

            extOffset += static_cast<std::uint64_t>(recordsInChunk) * bpb.mftRecordSizeBytes;
        }
    }

    // Reconstruct directory paths safely with cycle protection (max depth 64)
    auto reconstructPath = [&dirMap](std::uint64_t parentRec, const std::string& fileName, bool& orphaned) -> std::string {
        std::vector<std::string> parts;
        parts.push_back(fileName);
        std::unordered_set<std::uint64_t> visited;
        std::uint64_t cur = parentRec;
        orphaned = false;

        for (int depth = 0; depth < 64; ++depth) {
            if (cur == 5ULL) { // MFT Record 5 is NTFS root directory "."
                break;
            }
            if (!visited.insert(cur).second) {
                orphaned = true;
                break;
            }
            const auto it = dirMap.find(cur);
            if (it == dirMap.end()) {
                orphaned = true;
                break;
            }
            if (it->second.name != ".") {
                parts.push_back(it->second.name);
            }
            if (it->second.parentRecord == cur) {
                break;
            }
            cur = it->second.parentRecord;
        }

        std::ostringstream oss;
        if (orphaned) {
            oss << "\\[Orphaned]";
        }
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            oss << "\\" << *it;
        }
        return oss.str();
    };

    for (auto& file : candidateFiles) {
        bool orphaned = false;
        file.fullPath = reconstructPath(file.parentRecordNumber, file.fileName, orphaned);
        if (orphaned && file.confidence == RecoveryConfidence::High) {
            file.confidence = RecoveryConfidence::Medium;
            file.confidenceReason += " Parent directory metadata is missing (orphaned path).";
        }
    }

    summary.mftRecordsScanned = recordNumber;
    summary.entries = std::move(candidateFiles);
    return common::Result<NtfsScanSummary>::Ok(std::move(summary));
}

} // namespace salvus::filesystem
