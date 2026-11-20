#include "recovery/recovery_components.hpp"

#include <algorithm>
#include <cstring>

namespace salvus::recovery {

common::Result<std::vector<std::uint8_t>> FileReconstructor::ReconstructToBuffer(
    const disk::DiskReader& reader,
    const std::vector<FileExtent>& extents,
    std::uint64_t fileSizeBytes,
    std::uint64_t maxBufferBytes) const {
    if (!reader.is_open()) {
        return common::Result<std::vector<std::uint8_t>>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader must be open to reconstruct file.");
    }
    if (fileSizeBytes > maxBufferBytes) {
        return common::Result<std::vector<std::uint8_t>>::Fail(
            common::StatusCode::OutOfBounds, "File size exceeds bounded in-memory reconstruction limit.");
    }

    std::vector<std::uint8_t> output(static_cast<std::size_t>(fileSizeBytes), 0U);
    if (fileSizeBytes == 0ULL) {
        return common::Result<std::vector<std::uint8_t>>::Ok(std::move(output));
    }

    std::uint64_t written = 0;
    for (const auto& ext : extents) {
        if (written >= fileSizeBytes) {
            break;
        }
        const std::uint64_t toCopy = (std::min)(ext.lengthBytes, fileSizeBytes - written);
        if (ext.isSparse) {
            // Output is already zero-initialized
            written += toCopy;
            continue;
        }

        const auto readRes = reader.ReadBytes(
            ext.diskOffsetBytes,
            std::span<std::uint8_t>(output.data() + written, static_cast<std::size_t>(toCopy)));
        if (!readRes.ok()) {
            return common::Result<std::vector<std::uint8_t>>::Fail(
                readRes.error().code, readRes.error().message);
        }
        if (readRes.value() < toCopy) {
            return common::Result<std::vector<std::uint8_t>>::Fail(
                common::StatusCode::IoError, "Unexpected short read while reconstructing file extent.");
        }
        written += toCopy;
    }

    return common::Result<std::vector<std::uint8_t>>::Ok(std::move(output));
}

} // namespace salvus::recovery
