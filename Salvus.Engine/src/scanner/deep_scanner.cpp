#include "scanner/deep_scanner.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace salvus::scanner {

namespace {

std::uint32_t ReadLe32(std::span<const std::uint8_t> buf, std::size_t off) noexcept {
    return static_cast<std::uint32_t>(buf[off]) |
           (static_cast<std::uint32_t>(buf[off + 1U]) << 8U) |
           (static_cast<std::uint32_t>(buf[off + 2U]) << 16U) |
           (static_cast<std::uint32_t>(buf[off + 3U]) << 24U);
}

// Searches forward in bounded streaming chunks for `footer` starting at `startOffset`.
// Returns total size from `startOffset` through the end of `footer` (+ extraTrailingBytes), or 0 if not found.
std::uint64_t SearchFooterBounded(
    const disk::DiskReader& reader,
    std::uint64_t startOffset,
    std::uint64_t maxBytes,
    std::span<const std::uint8_t> footer,
    std::uint64_t extraTrailingBytes = 0ULL) {
    if (footer.empty() || maxBytes < footer.size()) {
        return 0ULL;
    }

    constexpr std::size_t kChunk = 256U * 1024U; // 256 KiB bounded search window
    const std::size_t overlap = footer.size() > 1U ? (footer.size() - 1U) : 0U;
    std::vector<std::uint8_t> buf(kChunk + overlap, 0U);

    std::uint64_t scanned = 0;
    std::size_t carried = 0;

    while (scanned < maxBytes) {
        const std::uint64_t remaining = maxBytes - scanned;
        const auto toRead = static_cast<std::size_t>((std::min)(remaining, static_cast<std::uint64_t>(kChunk)));
        const auto readRes = reader.ReadBytes(
            startOffset + scanned,
            std::span<std::uint8_t>(buf.data() + carried, toRead));
        if (!readRes.ok() || readRes.value() == 0U) {
            break;
        }

        const std::size_t totalValid = carried + readRes.value();
        if (totalValid >= footer.size()) {
            const auto* beginPtr = buf.data();
            const auto* endPtr = buf.data() + totalValid;
            const auto* found = std::search(beginPtr, endPtr, footer.begin(), footer.end());
            if (found != endPtr) {
                const std::size_t foundIndex = static_cast<std::size_t>(found - beginPtr);
                const std::uint64_t windowBaseOffset = scanned - carried;
                const std::uint64_t endOffsetRel = windowBaseOffset + foundIndex + footer.size() + extraTrailingBytes;
                return (std::min)(endOffsetRel, maxBytes);
            }
        }

        scanned += readRes.value();
        if (overlap > 0U && totalValid >= overlap) {
            std::memmove(buf.data(), buf.data() + (totalValid - overlap), overlap);
            carried = overlap;
        } else {
            carried = 0U;
        }
    }

    return 0ULL;
}

std::string MakeCarvedFileName(std::uint64_t id, const std::string& ext) {
    std::ostringstream oss;
    oss << "carved_" << std::setw(6) << std::setfill('0') << id << "." << ext;
    return oss.str();
}

} // namespace

DeepScanner::DeepScanner(SignatureDatabase database)
    : database_(std::move(database)) {}

common::Result<DeepScanSummary> DeepScanner::Scan(
    const disk::DiskReader& reader,
    const DeepScanOptions& options) const {
    if (!reader.is_open()) {
        return common::Result<DeepScanSummary>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader must be opened read-only before Deep Scan.");
    }
    if (options.stepBytes == 0U) {
        return common::Result<DeepScanSummary>::Fail(
            common::StatusCode::InvalidArgument, "DeepScan stepBytes must be greater than zero.");
    }

    const std::uint64_t totalDiskBytes = reader.geometry().totalBytes;
    const std::uint64_t scanLimit = (options.maxScanBytes > 0ULL)
                                        ? (std::min)(options.maxScanBytes, totalDiskBytes)
                                        : totalDiskBytes;

    DeepScanSummary summary{};
    constexpr std::size_t kBufferBytes = 4U * 1024U * 1024U; // Bounded 4 MiB streaming buffer
    std::vector<std::uint8_t> buffer(kBufferBytes, 0U);

    std::uint64_t currentOffset = 0;
    std::uint64_t nextCarvedId = 1;

    while (currentOffset < scanLimit && summary.files.size() < options.maxFiles) {
        const auto chunkToRead = static_cast<std::size_t>((std::min)(
            scanLimit - currentOffset,
            static_cast<std::uint64_t>(kBufferBytes)));

        const auto readRes = reader.ReadBytes(
            currentOffset,
            std::span<std::uint8_t>(buffer.data(), chunkToRead));
        if (!readRes.ok() || readRes.value() == 0U) {
            break;
        }

        const std::size_t validBytes = readRes.value();
        std::size_t pos = 0;

        while (pos < validBytes && summary.files.size() < options.maxFiles) {
            const std::size_t windowLen = (std::min)(validBytes - pos, std::size_t{512U});
            const auto window = std::span<const std::uint8_t>(buffer.data() + pos, windowLen);

            if (const FileSignature* sig = database_.MatchHeader(window)) {
                const std::uint64_t matchDiskOffset = currentOffset + pos;
                const std::uint64_t remainingOnDisk = totalDiskBytes - matchDiskOffset;
                const std::uint64_t boundedMax = (std::min)(sig->maxExpectedSizeBytes, remainingOnDisk);

                std::uint64_t estimatedSize = 0;
                bool verified = false;
                bool validCandidate = true;

                switch (sig->sizeStrategy) {
                    case SizeStrategy::BmpHeaderSize: {
                        if (window.size() >= 18U) {
                            const std::uint32_t bmpSize = ReadLe32(window, 2U);
                            const std::uint32_t pixelOffset = ReadLe32(window, 10U);
                            if (bmpSize >= sig->minSizeBytes && bmpSize <= boundedMax && pixelOffset < bmpSize) {
                                estimatedSize = bmpSize;
                                verified = true;
                            } else {
                                validCandidate = false;
                            }
                        } else {
                            validCandidate = false;
                        }
                        break;
                    }
                    case SizeStrategy::RiffHeaderSize: {
                        if (window.size() >= 12U) {
                            const std::uint64_t riffSize = static_cast<std::uint64_t>(ReadLe32(window, 4U)) + 8ULL;
                            if (riffSize >= sig->minSizeBytes && riffSize <= boundedMax) {
                                estimatedSize = riffSize;
                                verified = true;
                            } else {
                                validCandidate = false;
                            }
                        } else {
                            validCandidate = false;
                        }
                        break;
                    }
                    case SizeStrategy::FooterSearch: {
                        const std::uint64_t foundSize = SearchFooterBounded(
                            reader, matchDiskOffset, boundedMax, sig->footerBytes, 0ULL);
                        if (foundSize >= sig->minSizeBytes) {
                            estimatedSize = foundSize;
                            verified = true;
                        } else {
                            estimatedSize = (std::min)(boundedMax, std::uint64_t{65536ULL});
                            verified = false;
                        }
                        break;
                    }
                    case SizeStrategy::ZipEocdSearch: {
                        // ZIP EOCD header is 22 bytes starting with PK\x05\x06
                        const std::uint64_t foundSize = SearchFooterBounded(
                            reader, matchDiskOffset, boundedMax, sig->footerBytes, 18ULL);
                        if (foundSize >= sig->minSizeBytes) {
                            estimatedSize = foundSize;
                            verified = true;
                        } else {
                            estimatedSize = (std::min)(boundedMax, std::uint64_t{65536ULL});
                            verified = false;
                        }
                        break;
                    }
                    case SizeStrategy::FixedMaxCap: {
                        // Validate PE header pointer for MZ executables to avoid false positives
                        if (sig->id == "pe_exe") {
                            if (window.size() < 64U) {
                                validCandidate = false;
                            } else {
                                const std::uint32_t peOffset = ReadLe32(window, 60U);
                                if (peOffset < 64U || peOffset + 4U > window.size() ||
                                    std::memcmp(window.data() + peOffset, "PE\0\0", 4U) != 0) {
                                    validCandidate = false;
                                }
                            }
                        }
                        estimatedSize = (std::min)(boundedMax, std::uint64_t{1024ULL * 1024ULL});
                        verified = false;
                        break;
                    }
                }

                if (validCandidate && estimatedSize >= sig->minSizeBytes) {
                    CarvedFileEntry entry{};
                    entry.carvedId = nextCarvedId++;
                    entry.extension = sig->extension;
                    entry.category = sig->category;
                    entry.fileName = MakeCarvedFileName(entry.carvedId, sig->extension);
                    entry.diskOffsetBytes = matchDiskOffset;
                    entry.estimatedSizeBytes = estimatedSize;
                    entry.footerOrSizeVerified = verified;

                    if (verified) {
                        entry.confidence = filesystem::RecoveryConfidence::Low;
                        entry.confidenceReason =
                            "Signature-carved (" + sig->extension +
                            ") with verified structural boundary; assumes contiguous storage (fragmentation cannot be detected without FS metadata).";
                    } else {
                        entry.confidence = filesystem::RecoveryConfidence::Damaged;
                        entry.confidenceReason =
                            "Signature header matched (" + sig->extension +
                            "), but footer/end-of-file boundary was not verified within bounds; file may be truncated or fragmented.";
                    }

                    summary.files.push_back(std::move(entry));
                }
            }

            pos += options.stepBytes;
        }

        currentOffset += validBytes;
    }

    summary.bytesScanned = currentOffset;
    return common::Result<DeepScanSummary>::Ok(std::move(summary));
}

} // namespace salvus::scanner
