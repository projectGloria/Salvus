#include "imaging/disk_image_manager.hpp"
#include "disk/disk_reader.hpp"
#include "recovery/recovery_components.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace salvus::imaging {

common::Result<DiskImageResult> DiskImageManager::CreateRawImage(const DiskImageOptions& options) const {
    const auto destCheck = recovery::RecoveryWriter::ValidateSeparateDestination(
        options.sourceDevicePath, options.destinationImagePath);
    if (!destCheck.ok()) {
        return common::Result<DiskImageResult>::Fail(destCheck.error().code, destCheck.error().message);
    }

    disk::DiskReader reader;
    const auto openRes = reader.OpenReadOnly(options.sourceDevicePath);
    if (!openRes.ok()) {
        return common::Result<DiskImageResult>::Fail(openRes.error().code, openRes.error().message);
    }

    const std::filesystem::path destPath(options.destinationImagePath);
    if (destPath.has_parent_path()) {
        std::error_code dirEc;
        std::filesystem::create_directories(destPath.parent_path(), dirEc);
    }

    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return common::Result<DiskImageResult>::Fail(
            common::StatusCode::IoError, "Failed to create destination raw disk image file.");
    }

    const std::uint32_t sectorSize = reader.geometry().bytesPerSector;
    const std::uint64_t totalBytes = reader.geometry().totalBytes;
    const std::uint32_t clampedBuf = std::clamp(options.bufferSizeBytes, 65536U, 16U * 1024U * 1024U);
    const std::uint32_t alignedChunk = (clampedBuf / sectorSize) * sectorSize;
    const std::uint32_t maxRetries = (std::min)(options.maxSectorRetries, 5U); // Strictly bounded retries

    std::vector<std::uint8_t> buffer(alignedChunk > 0U ? alignedChunk : sectorSize, 0U);
    std::vector<std::uint8_t> sectorBuf(sectorSize, 0U);
    std::vector<std::uint64_t> badSectorOffsets;

    std::uint64_t offset = 0;
    while (offset < totalBytes) {
        const std::uint64_t remaining = totalBytes - offset;
        const auto toRead = static_cast<std::size_t>((std::min)(
            remaining, static_cast<std::uint64_t>(buffer.size())));

        const auto chunkRes = reader.ReadBytes(offset, std::span<std::uint8_t>(buffer.data(), toRead));
        if (chunkRes.ok() && chunkRes.value() == toRead) {
            out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(toRead));
            if (!out.good()) {
                return common::Result<DiskImageResult>::Fail(
                    common::StatusCode::IoError, "I/O error writing destination disk image.");
            }
            offset += toRead;
            continue;
        }

        // Fallback to per-sector reads with bounded retries to isolate bad sectors without aggressive loops
        std::uint64_t subOff = 0;
        while (subOff < toRead) {
            const auto subLen = static_cast<std::size_t>((std::min)(
                static_cast<std::uint64_t>(sectorSize),
                static_cast<std::uint64_t>(toRead) - subOff));

            bool sectorOk = false;
            for (std::uint32_t attempt = 0; attempt <= maxRetries; ++attempt) {
                const auto sRes = reader.ReadBytes(
                    offset + subOff, std::span<std::uint8_t>(sectorBuf.data(), subLen));
                if (sRes.ok() && sRes.value() == subLen) {
                    sectorOk = true;
                    break;
                }
            }

            if (!sectorOk) {
                std::fill(sectorBuf.begin(), sectorBuf.begin() + subLen, std::uint8_t{0});
                badSectorOffsets.push_back(offset + subOff);
            }

            out.write(reinterpret_cast<const char*>(sectorBuf.data()), static_cast<std::streamsize>(subLen));
            if (!out.good()) {
                return common::Result<DiskImageResult>::Fail(
                    common::StatusCode::IoError, "I/O error writing destination disk image sector.");
            }
            subOff += subLen;
        }

        offset += toRead;
    }

    out.flush();
    out.close();

    DiskImageResult result{};
    result.totalBytesCopied = offset;
    result.badSectorCount = badSectorOffsets.size();

    if (!badSectorOffsets.empty()) {
        const std::wstring mapPath = options.destinationImagePath + L".badsectors.txt";
        std::ofstream mapOut(mapPath, std::ios::trunc);
        if (mapOut.is_open()) {
            mapOut << "# Salvus Persistent Bad-Sector Map (byte offsets)\n";
            for (const std::uint64_t badOff : badSectorOffsets) {
                mapOut << badOff << "\n";
            }
            result.badSectorMapPath = mapPath;
        }
    }

    if (options.computeSha256) {
        recovery::IntegrityChecker checker;
        const auto shaRes = checker.ComputeSha256(options.destinationImagePath);
        if (!shaRes.ok()) {
            return common::Result<DiskImageResult>::Fail(shaRes.error().code, shaRes.error().message);
        }
        result.sha256Hex = shaRes.value();
    }

    return common::Result<DiskImageResult>::Ok(std::move(result));
}

} // namespace salvus::imaging
