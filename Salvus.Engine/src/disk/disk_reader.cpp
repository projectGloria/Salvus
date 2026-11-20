#include "disk/disk_reader.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

namespace salvus::disk {

namespace {

#ifdef _WIN32

bool IsWin32DevicePath(const std::wstring& path) noexcept {
    return path.rfind(L"\\\\.\\", 0) == 0 || path.rfind(L"\\\\?\\", 0) == 0;
}

common::Result<std::size_t> ReadAtOffsetWin32(
    HANDLE handle,
    std::uint64_t byteOffset,
    std::span<std::uint8_t> destination) {
    std::size_t totalRead = 0;
    constexpr std::size_t kMaxChunk = 16U * 1024U * 1024U; // 16 MiB bounded chunk

    while (totalRead < destination.size()) {
        const std::uint64_t currentOffset = byteOffset + static_cast<std::uint64_t>(totalRead);
        const std::size_t chunkSize = (std::min)(destination.size() - totalRead, kMaxChunk);

        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFFULL);
        ov.OffsetHigh = static_cast<DWORD>((currentOffset >> 32U) & 0xFFFFFFFFULL);

        DWORD bytesRead = 0;
        const BOOL ok = ::ReadFile(
            handle,
            destination.data() + totalRead,
            static_cast<DWORD>(chunkSize),
            &bytesRead,
            &ov);

        if (!ok) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_HANDLE_EOF) {
                break;
            }
            return common::Result<std::size_t>::Fail(
                err == ERROR_ACCESS_DENIED ? common::StatusCode::AccessDenied : common::StatusCode::IoError,
                "ReadFile failed at offset " + std::to_string(currentOffset) +
                    " with Win32 error " + std::to_string(err));
        }

        if (bytesRead == 0) {
            break;
        }
        totalRead += bytesRead;
    }

    return common::Result<std::size_t>::Ok(totalRead);
}

#endif // _WIN32

} // namespace

DiskReader::~DiskReader() {
    Close();
}

common::Result<void> DiskReader::OpenReadOnly(
    const std::wstring& deviceOrImagePath,
    std::uint32_t defaultImageSectorSize) {
#ifndef _WIN32
    (void)deviceOrImagePath;
    (void)defaultImageSectorSize;
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented, "DiskReader is only supported on Windows.");
#else
    if (deviceOrImagePath.empty()) {
        return common::Result<void>::Fail(
            common::StatusCode::InvalidArgument, "Target device or image path must not be empty.");
    }
    if (!IsValidSectorSize(defaultImageSectorSize)) {
        return common::Result<void>::Fail(
            common::StatusCode::InvalidArgument, "Default sector size must be a valid power of two (512..65536).");
    }

    Close();

    // Strictly read-only: GENERIC_READ only, never GENERIC_WRITE, OPEN_EXISTING only.
    HANDLE h = ::CreateFileW(
        deviceOrImagePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = ::GetLastError();
        return common::Result<void>::Fail(
            err == ERROR_ACCESS_DENIED ? common::StatusCode::AccessDenied : common::StatusCode::IoError,
            "CreateFileW (read-only) failed with Win32 error " + std::to_string(err));
    }

    const bool rawDevice = IsWin32DevicePath(deviceOrImagePath);
    DiskGeometry geom{};

    if (rawDevice) {
        DISK_GEOMETRY_EX diskGeomEx{};
        DWORD bytesReturned = 0;
        if (::DeviceIoControl(
                h,
                IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                nullptr,
                0,
                &diskGeomEx,
                sizeof(diskGeomEx),
                &bytesReturned,
                nullptr)) {
            geom.totalBytes = static_cast<std::uint64_t>(diskGeomEx.DiskSize.QuadPart);
            geom.bytesPerSector = diskGeomEx.Geometry.BytesPerSector;
        } else {
            GET_LENGTH_INFORMATION lenInfo{};
            if (::DeviceIoControl(
                    h,
                    IOCTL_DISK_GET_LENGTH_INFO,
                    nullptr,
                    0,
                    &lenInfo,
                    sizeof(lenInfo),
                    &bytesReturned,
                    nullptr)) {
                geom.totalBytes = static_cast<std::uint64_t>(lenInfo.Length.QuadPart);
                geom.bytesPerSector = defaultImageSectorSize;
            } else {
                const DWORD err = ::GetLastError();
                ::CloseHandle(h);
                return common::Result<void>::Fail(
                    common::StatusCode::IoError,
                    "Failed to query device geometry with Win32 error " + std::to_string(err));
            }
        }
    } else {
        LARGE_INTEGER fileSize{};
        if (!::GetFileSizeEx(h, &fileSize) || fileSize.QuadPart <= 0) {
            const DWORD err = ::GetLastError();
            ::CloseHandle(h);
            return common::Result<void>::Fail(
                common::StatusCode::InvalidArgument,
                "Disk image file is empty or unreadable (Win32 error " + std::to_string(err) + ").");
        }
        geom.totalBytes = static_cast<std::uint64_t>(fileSize.QuadPart);
        geom.bytesPerSector = defaultImageSectorSize;
    }

    if (!IsValidSectorSize(geom.bytesPerSector) || geom.totalBytes == 0U) {
        ::CloseHandle(h);
        return common::Result<void>::Fail(
            common::StatusCode::IoError, "Reported device geometry is invalid.");
    }

    handle_ = h;
    isRawDevice_ = rawDevice;
    geometry_ = geom;
    return common::Result<void>::Ok();
#endif
}

void DiskReader::Close() noexcept {
#ifdef _WIN32
    if (handle_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
#endif
    isRawDevice_ = false;
    geometry_ = {};
}

common::Result<std::size_t> DiskReader::ReadSectors(
    std::uint64_t byteOffset,
    std::span<std::uint8_t> destination) const {
#ifndef _WIN32
    (void)byteOffset;
    (void)destination;
    return common::Result<std::size_t>::Fail(
        common::StatusCode::NotImplemented, "DiskReader is only supported on Windows.");
#else
    if (!is_open()) {
        return common::Result<std::size_t>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader is not open.");
    }

    const auto validation = ValidateReadRange(
        geometry_,
        ReadRange{byteOffset, static_cast<std::uint64_t>(destination.size())},
        true);
    if (!validation.ok()) {
        return common::Result<std::size_t>::Fail(validation.error().code, validation.error().message);
    }

    return ReadAtOffsetWin32(static_cast<HANDLE>(handle_), byteOffset, destination);
#endif
}

common::Result<std::size_t> DiskReader::ReadBytes(
    std::uint64_t byteOffset,
    std::span<std::uint8_t> destination) const {
#ifndef _WIN32
    (void)byteOffset;
    (void)destination;
    return common::Result<std::size_t>::Fail(
        common::StatusCode::NotImplemented, "DiskReader is only supported on Windows.");
#else
    if (!is_open()) {
        return common::Result<std::size_t>::Fail(
            common::StatusCode::InvalidArgument, "DiskReader is not open.");
    }

    const auto validation = ValidateReadRange(
        geometry_,
        ReadRange{byteOffset, static_cast<std::uint64_t>(destination.size())},
        false);
    if (!validation.ok()) {
        return common::Result<std::size_t>::Fail(validation.error().code, validation.error().message);
    }

    if (!isRawDevice_ ||
        (IsSectorAligned(byteOffset, geometry_.bytesPerSector) &&
         IsSectorAligned(static_cast<std::uint64_t>(destination.size()), geometry_.bytesPerSector))) {
        return ReadAtOffsetWin32(static_cast<HANDLE>(handle_), byteOffset, destination);
    }

    // Align down/up to sector boundaries for raw device unaligned reads
    const auto alignedStart = AlignDownToSector(byteOffset, geometry_.bytesPerSector);
    if (!alignedStart.ok()) {
        return common::Result<std::size_t>::Fail(alignedStart.error().code, alignedStart.error().message);
    }

    const std::uint64_t endOffset = byteOffset + static_cast<std::uint64_t>(destination.size());
    const auto alignedEnd = AlignUpToSector(endOffset, geometry_.bytesPerSector);
    if (!alignedEnd.ok()) {
        return common::Result<std::size_t>::Fail(alignedEnd.error().code, alignedEnd.error().message);
    }

    const std::uint64_t readLength = alignedEnd.value() - alignedStart.value();
    constexpr std::uint64_t kMaxUnalignedBuffer = 32ULL * 1024ULL * 1024ULL;
    if (readLength > kMaxUnalignedBuffer) {
        return common::Result<std::size_t>::Fail(
            common::StatusCode::OutOfBounds, "Unaligned read exceeds bounded temporary buffer limit.");
    }

    std::vector<std::uint8_t> alignedBuffer(static_cast<std::size_t>(readLength));
    const auto readRes = ReadAtOffsetWin32(
        static_cast<HANDLE>(handle_),
        alignedStart.value(),
        alignedBuffer);
    if (!readRes.ok()) {
        return readRes;
    }

    const std::size_t leadingSkip = static_cast<std::size_t>(byteOffset - alignedStart.value());
    if (readRes.value() <= leadingSkip) {
        return common::Result<std::size_t>::Ok(0U);
    }

    const std::size_t available = readRes.value() - leadingSkip;
    const std::size_t toCopy = (std::min)(available, destination.size());
    std::memcpy(destination.data(), alignedBuffer.data() + leadingSkip, toCopy);
    return common::Result<std::size_t>::Ok(toCopy);
#endif
}

} // namespace salvus::disk
