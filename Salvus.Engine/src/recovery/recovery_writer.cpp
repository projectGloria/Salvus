#include "recovery/recovery_components.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

namespace salvus::recovery {

namespace {

wchar_t ExtractDriveLetter(const std::wstring& path) noexcept {
    // Matches "C:\...", "C:", "\\.\C:", "\\?\C:\..."
    if (path.size() >= 2U && std::iswalpha(path[0]) != 0 && path[1] == L':') {
        return static_cast<wchar_t>(std::towupper(path[0]));
    }
    if (path.size() >= 6U &&
        (path.rfind(L"\\\\.\\", 0) == 0 || path.rfind(L"\\\\?\\", 0) == 0) &&
        std::iswalpha(path[4]) != 0 && path[5] == L':') {
        return static_cast<wchar_t>(std::towupper(path[4]));
    }
    return L'\0';
}

#ifdef _WIN32
bool TryGetPhysicalDiskNumberForDriveLetter(wchar_t driveLetter, std::uint32_t& outDiskIndex) {
    const std::wstring volPath = std::wstring(L"\\\\.\\") + driveLetter + L":";
    HANDLE h = ::CreateFileW(
        volPath.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::vector<std::uint8_t> buf(sizeof(VOLUME_DISK_EXTENTS) + 4 * sizeof(DISK_EXTENT), 0);
    DWORD returned = 0;
    const BOOL ok = ::DeviceIoControl(
        h,
        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
        nullptr,
        0,
        buf.data(),
        static_cast<DWORD>(buf.size()),
        &returned,
        nullptr);
    ::CloseHandle(h);
    if (!ok) {
        return false;
    }

    const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buf.data());
    if (extents->NumberOfDiskExtents == 0) {
        return false;
    }
    outDiskIndex = extents->Extents[0].DiskNumber;
    return true;
}
#endif

} // namespace

common::Result<void> RecoveryWriter::ValidateSeparateDestination(
    const std::wstring& sourceDeviceOrImagePath,
    const std::wstring& destinationFilePath) {
    if (sourceDeviceOrImagePath.empty() || destinationFilePath.empty()) {
        return common::Result<void>::Fail(
            common::StatusCode::InvalidArgument, "Source and destination paths must not be empty.");
    }

    const std::filesystem::path destPath(destinationFilePath);
    std::error_code ec;
    const std::filesystem::path absDest = std::filesystem::weakly_canonical(destPath, ec);
    const std::wstring destWide = ec ? destinationFilePath : absDest.wstring();

    // Check identical file path (e.g., attempting to overwrite a source disk image)
    if (sourceDeviceOrImagePath.rfind(L"\\\\.\\", 0) != 0 &&
        sourceDeviceOrImagePath.rfind(L"\\\\?\\", 0) != 0) {
        std::error_code srcEc;
        const std::filesystem::path absSrc = std::filesystem::weakly_canonical(sourceDeviceOrImagePath, srcEc);
        const std::wstring srcWide = srcEc ? sourceDeviceOrImagePath : absSrc.wstring();
        if (_wcsicmp(srcWide.c_str(), destWide.c_str()) == 0) {
            return common::Result<void>::Fail(
                common::StatusCode::InvalidArgument,
                "Recovery destination cannot overwrite the source disk image file.");
        }
        return common::Result<void>::Ok();
    }

    // Source is a Win32 device/volume path: enforce strictly separate volume/drive!
    const wchar_t destDrive = ExtractDriveLetter(destWide);
    const wchar_t srcDrive = ExtractDriveLetter(sourceDeviceOrImagePath);
    if (srcDrive != L'\0' && destDrive != L'\0' && srcDrive == destDrive) {
        return common::Result<void>::Fail(
            common::StatusCode::InvalidArgument,
            "Recovery destination must be on a separate drive from the source volume.");
    }

#ifdef _WIN32
    // If source is \\.\PhysicalDriveN, verify destination drive is not on PhysicalDriveN
    const std::wstring physPrefix = L"\\\\.\\PhysicalDrive";
    if (sourceDeviceOrImagePath.size() > physPrefix.size() &&
        _wcsnicmp(sourceDeviceOrImagePath.c_str(), physPrefix.c_str(), physPrefix.size()) == 0 &&
        destDrive != L'\0') {
        try {
            const auto srcDiskNum = static_cast<std::uint32_t>(
                std::stoul(sourceDeviceOrImagePath.substr(physPrefix.size())));
            std::uint32_t destDiskNum = 0;
            if (TryGetPhysicalDiskNumberForDriveLetter(destDrive, destDiskNum) &&
                destDiskNum == srcDiskNum) {
                return common::Result<void>::Fail(
                    common::StatusCode::InvalidArgument,
                    "Recovery destination resides on the same physical disk as the source device.");
            }
        } catch (...) {
            // Non-numeric suffix; continue safely
        }
    }
#endif

    return common::Result<void>::Ok();
}

common::Result<RecoveryWriteResult> RecoveryWriter::WriteRecoveredFile(
    const std::wstring& sourceDeviceOrImagePath,
    const std::wstring& destinationFilePath,
    bool isResident,
    std::span<const std::uint8_t> residentData,
    const std::vector<FileExtent>& extents,
    std::uint64_t fileSizeBytes) const {
    const auto destCheck = ValidateSeparateDestination(sourceDeviceOrImagePath, destinationFilePath);
    if (!destCheck.ok()) {
        return common::Result<RecoveryWriteResult>::Fail(
            destCheck.error().code, destCheck.error().message);
    }

    const std::filesystem::path destPath(destinationFilePath);
    if (destPath.has_parent_path()) {
        std::error_code dirEc;
        std::filesystem::create_directories(destPath.parent_path(), dirEc);
    }

    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return common::Result<RecoveryWriteResult>::Fail(
            common::StatusCode::IoError, "Failed to open destination file for writing.");
    }

    std::uint64_t totalWritten = 0;

    if (isResident) {
        const std::size_t toWrite = static_cast<std::size_t>(
            (std::min)(static_cast<std::uint64_t>(residentData.size()), fileSizeBytes));
        if (toWrite > 0U) {
            out.write(reinterpret_cast<const char*>(residentData.data()), static_cast<std::streamsize>(toWrite));
            if (!out.good()) {
                return common::Result<RecoveryWriteResult>::Fail(
                    common::StatusCode::IoError, "Failed writing resident file data to destination.");
            }
            totalWritten = toWrite;
        }
    } else if (fileSizeBytes > 0ULL) {
        disk::DiskReader reader;
        const auto openRes = reader.OpenReadOnly(sourceDeviceOrImagePath);
        if (!openRes.ok()) {
            return common::Result<RecoveryWriteResult>::Fail(
                openRes.error().code, openRes.error().message);
        }

        constexpr std::size_t kChunkBytes = 1024U * 1024U; // Bounded 1 MiB streaming buffer
        std::vector<std::uint8_t> buffer(kChunkBytes, 0U);
        std::vector<std::uint8_t> zeroBuffer(kChunkBytes, 0U);

        for (const auto& ext : extents) {
            if (totalWritten >= fileSizeBytes) {
                break;
            }
            std::uint64_t extRemaining = (std::min)(ext.lengthBytes, fileSizeBytes - totalWritten);
            std::uint64_t extOffset = 0;

            while (extRemaining > 0ULL) {
                const auto chunk = static_cast<std::size_t>((std::min)(
                    extRemaining, static_cast<std::uint64_t>(kChunkBytes)));

                if (ext.isSparse) {
                    out.write(reinterpret_cast<const char*>(zeroBuffer.data()), static_cast<std::streamsize>(chunk));
                } else {
                    const auto readRes = reader.ReadBytes(
                        ext.diskOffsetBytes + extOffset,
                        std::span<std::uint8_t>(buffer.data(), chunk));
                    if (!readRes.ok()) {
                        return common::Result<RecoveryWriteResult>::Fail(
                            readRes.error().code, readRes.error().message);
                    }
                    if (readRes.value() == 0U) {
                        return common::Result<RecoveryWriteResult>::Fail(
                            common::StatusCode::IoError, "Unexpected EOF reading source extent.");
                    }
                    out.write(reinterpret_cast<const char*>(buffer.data()),
                              static_cast<std::streamsize>(readRes.value()));
                    extOffset += readRes.value();
                    extRemaining -= readRes.value();
                    totalWritten += readRes.value();
                    if (!out.good()) {
                        return common::Result<RecoveryWriteResult>::Fail(
                            common::StatusCode::IoError, "I/O error writing recovered file.");
                    }
                    continue;
                }

                if (!out.good()) {
                    return common::Result<RecoveryWriteResult>::Fail(
                        common::StatusCode::IoError, "I/O error writing sparse extent.");
                }
                extOffset += chunk;
                extRemaining -= chunk;
                totalWritten += chunk;
            }
        }
    }

    out.flush();
    if (!out.good()) {
        return common::Result<RecoveryWriteResult>::Fail(
            common::StatusCode::IoError, "Failed to flush recovered file to destination.");
    }

    return common::Result<RecoveryWriteResult>::Ok(RecoveryWriteResult{totalWritten, destinationFilePath});
}

} // namespace salvus::recovery
