#include "device/device_manager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>
#endif

namespace salvus::device {

namespace {

std::string TrimString(std::string_view input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }
    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }
    return std::string(input.substr(start, end - start));
}

#ifdef _WIN32

struct ScopedHandle {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE h) : handle(h) {}
    ~ScopedHandle() {
        if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
            ::CloseHandle(handle);
        }
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
};

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int required = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), required, nullptr, nullptr);
    return utf8;
}

std::string BusTypeToString(STORAGE_BUS_TYPE busType) {
    switch (busType) {
        case BusTypeScsi:               return "SCSI";
        case BusTypeAtapi:              return "ATAPI";
        case BusTypeAta:                return "ATA";
        case BusType1394:               return "IEEE 1394";
        case BusTypeSsa:                return "SSA";
        case BusTypeFibre:              return "Fibre Channel";
        case BusTypeUsb:                return "USB";
        case BusTypeRAID:               return "RAID";
        case BusTypeiScsi:              return "iSCSI";
        case BusTypeSas:                return "SAS";
        case BusTypeSata:               return "SATA";
        case BusTypeSd:                 return "SD";
        case BusTypeMmc:                return "MMC";
        case BusTypeVirtual:            return "Virtual";
        case BusTypeFileBackedVirtual:  return "Virtual Disk";
        case BusTypeSpaces:             return "Storage Spaces";
        case BusTypeNvme:               return "NVMe";
        case BusTypeSCM:                return "SCM";
        case BusTypeUfs:                return "UFS";
        default:                        return "Unknown";
    }
}

StorageDeviceType ClassifyDeviceType(STORAGE_BUS_TYPE busType, bool removable, bool isSsd) {
    if (busType == BusTypeVirtual || busType == BusTypeFileBackedVirtual) {
        return StorageDeviceType::VirtualDiskImage;
    }
    if (busType == BusTypeSd || busType == BusTypeMmc) {
        return StorageDeviceType::SdCard;
    }
    if (busType == BusTypeUsb) {
        if (removable) {
            return StorageDeviceType::UsbFlashDrive;
        }
        return isSsd ? StorageDeviceType::ExternalSsd : StorageDeviceType::ExternalHdd;
    }
    if (busType == BusTypeNvme) {
        return StorageDeviceType::InternalSsd;
    }
    return isSsd ? StorageDeviceType::InternalSsd : StorageDeviceType::InternalHdd;
}

std::string ExtractDescriptorString(const std::vector<std::uint8_t>& buffer, DWORD offset) {
    if (offset == 0 || offset >= buffer.size()) {
        return {};
    }
    const char* ptr = reinterpret_cast<const char*>(buffer.data() + offset);
    std::size_t maxLen = buffer.size() - offset;
    std::size_t len = 0;
    while (len < maxLen && ptr[len] != '\0') {
        ++len;
    }
    return TrimString(std::string_view(ptr, len));
}

bool QueryStorageDeviceInfo(std::uint32_t diskIndex, StorageDeviceInfo& outInfo) {
    const std::wstring widePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskIndex);

    // Try read-only open first (succeeds when elevated); fall back to 0 access (metadata query only).
    bool hasRawRead = false;
    ScopedHandle h(::CreateFileW(
        widePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr));

    if (h.handle != INVALID_HANDLE_VALUE) {
        hasRawRead = true;
    } else {
        h.handle = ::CreateFileW(
            widePath.c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (h.handle == INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    outInfo.physicalDiskIndex = diskIndex;
    outInfo.devicePath = "\\\\.\\PhysicalDrive" + std::to_string(diskIndex);
    outInfo.deviceName = "PhysicalDrive" + std::to_string(diskIndex);
    outInfo.isPhysicalDisk = true;
    outInfo.rawAccessAvailable = hasRawRead;
    outInfo.partitionLayout = "Unknown";
    outInfo.healthStatus = hasRawRead ? "Accessible (Read-Only)" : "Metadata Only (Elevation Required for Raw Read)";

    STORAGE_BUS_TYPE busType = BusTypeUnknown;

    // 1. StorageDeviceProperty (vendor, product/model, bus type, removable media)
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    std::vector<std::uint8_t> propBuf(4096, 0);
    DWORD returned = 0;
    if (::DeviceIoControl(
            h.handle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(query),
            propBuf.data(),
            static_cast<DWORD>(propBuf.size()),
            &returned,
            nullptr) &&
        returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(propBuf.data());
        busType = desc->BusType;
        outInfo.connectionType = BusTypeToString(busType);
        outInfo.isRemovable = (desc->RemovableMedia != FALSE);
        outInfo.manufacturer = ExtractDescriptorString(propBuf, desc->VendorIdOffset);
        outInfo.model = ExtractDescriptorString(propBuf, desc->ProductIdOffset);
        if (!outInfo.model.empty()) {
            outInfo.deviceName = outInfo.manufacturer.empty()
                                     ? outInfo.model
                                     : (outInfo.manufacturer + " " + outInfo.model);
        }
    } else {
        outInfo.connectionType = "Unknown";
    }

    // 2. Seek penalty (SSD vs HDD heuristic)
    STORAGE_PROPERTY_QUERY seekQuery{};
    seekQuery.PropertyId = StorageDeviceSeekPenaltyProperty;
    seekQuery.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seekDesc{};
    if (::DeviceIoControl(
            h.handle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &seekQuery,
            sizeof(seekQuery),
            &seekDesc,
            sizeof(seekDesc),
            &returned,
            nullptr)) {
        outInfo.isSsd = (seekDesc.IncursSeekPenalty == FALSE);
    } else if (busType == BusTypeNvme) {
        outInfo.isSsd = true;
    }

    outInfo.deviceType = ClassifyDeviceType(busType, outInfo.isRemovable, outInfo.isSsd);

    // 3. Disk geometry (capacity & sector size)
    DISK_GEOMETRY_EX geomEx{};
    if (::DeviceIoControl(
            h.handle,
            IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
            nullptr,
            0,
            &geomEx,
            sizeof(geomEx),
            &returned,
            nullptr)) {
        outInfo.capacityBytes = static_cast<std::uint64_t>(geomEx.DiskSize.QuadPart);
        outInfo.sectorSizeBytes = geomEx.Geometry.BytesPerSector;
    }

    // 4. Partition layout style (GPT / MBR / RAW)
    std::vector<std::uint8_t> layoutBuf(sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 128 * sizeof(PARTITION_INFORMATION_EX));
    if (::DeviceIoControl(
            h.handle,
            IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
            nullptr,
            0,
            layoutBuf.data(),
            static_cast<DWORD>(layoutBuf.size()),
            &returned,
            nullptr) &&
        returned >= sizeof(DRIVE_LAYOUT_INFORMATION_EX)) {
        const auto* layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(layoutBuf.data());
        switch (layout->PartitionStyle) {
            case PARTITION_STYLE_GPT: outInfo.partitionLayout = "GPT"; break;
            case PARTITION_STYLE_MBR: outInfo.partitionLayout = "MBR"; break;
            case PARTITION_STYLE_RAW: outInfo.partitionLayout = "RAW"; break;
            default:                  outInfo.partitionLayout = "Unknown"; break;
        }
    }

    return true;
}

#endif // _WIN32

} // namespace

std::string StorageDeviceTypeToString(StorageDeviceType type) {
    switch (type) {
        case StorageDeviceType::InternalHdd:      return "Internal HDD";
        case StorageDeviceType::InternalSsd:      return "Internal SSD";
        case StorageDeviceType::ExternalHdd:      return "External HDD";
        case StorageDeviceType::ExternalSsd:      return "External SSD";
        case StorageDeviceType::UsbFlashDrive:    return "USB Flash Drive";
        case StorageDeviceType::SdCard:           return "SD Card";
        case StorageDeviceType::MicroSdCard:      return "microSD Card";
        case StorageDeviceType::VirtualDiskImage: return "Virtual Disk";
        default:                                  return "Unknown";
    }
}

bool IsProcessElevated() noexcept {
#ifndef _WIN32
    return false;
#else
    HANDLE rawToken = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        return false;
    }
    ScopedHandle token(rawToken);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (!::GetTokenInformation(token.handle, TokenElevation, &elevation, sizeof(elevation), &size)) {
        return false;
    }
    return elevation.TokenIsElevated != 0;
#endif
}

common::Result<std::vector<StorageDeviceInfo>> DeviceManager::EnumeratePhysicalDisks() const {
#ifndef _WIN32
    return common::Result<std::vector<StorageDeviceInfo>>::Fail(
        common::StatusCode::NotImplemented, "Physical disk enumeration is only supported on Windows.");
#else
    std::map<std::uint32_t, StorageDeviceInfo> disksByIndex;

    // Discover disk device interfaces via SetupAPI (enumerates all attached physical disks cleanly).
    HDEVINFO devInfo = ::SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_DISK,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devInfo != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA ifaceData{};
        ifaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD index = 0;
             ::SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_DEVINTERFACE_DISK, index, &ifaceData);
             ++index) {
            DWORD requiredSize = 0;
            ::SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &requiredSize, nullptr);
            if (requiredSize == 0) {
                continue;
            }

            std::vector<std::uint8_t> detailBuf(requiredSize, 0);
            auto* detailData = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            if (!::SetupDiGetDeviceInterfaceDetailW(
                    devInfo, &ifaceData, detailData, requiredSize, nullptr, nullptr)) {
                continue;
            }

            ScopedHandle ifaceHandle(::CreateFileW(
                detailData->DevicePath,
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr));
            if (ifaceHandle.handle == INVALID_HANDLE_VALUE) {
                continue;
            }

            STORAGE_DEVICE_NUMBER devNum{};
            DWORD returned = 0;
            if (::DeviceIoControl(
                    ifaceHandle.handle,
                    IOCTL_STORAGE_GET_DEVICE_NUMBER,
                    nullptr,
                    0,
                    &devNum,
                    sizeof(devNum),
                    &returned,
                    nullptr) &&
                devNum.DeviceType == FILE_DEVICE_DISK) {
                const auto diskIdx = static_cast<std::uint32_t>(devNum.DeviceNumber);
                if (disksByIndex.find(diskIdx) == disksByIndex.end()) {
                    StorageDeviceInfo info{};
                    if (QueryStorageDeviceInfo(diskIdx, info)) {
                        disksByIndex.emplace(diskIdx, std::move(info));
                    }
                }
            }
        }

        ::SetupDiDestroyDeviceInfoList(devInfo);
    }

    // Fallback probe for PhysicalDrive0..15 if SetupAPI returned nothing.
    if (disksByIndex.empty()) {
        for (std::uint32_t i = 0; i < 16U; ++i) {
            StorageDeviceInfo info{};
            if (QueryStorageDeviceInfo(i, info)) {
                disksByIndex.emplace(i, std::move(info));
            }
        }
    }

    std::vector<StorageDeviceInfo> results;
    results.reserve(disksByIndex.size());
    for (auto& [idx, info] : disksByIndex) {
        (void)idx;
        results.push_back(std::move(info));
    }
    return common::Result<std::vector<StorageDeviceInfo>>::Ok(std::move(results));
#endif
}

common::Result<std::vector<VolumeInfo>> DeviceManager::EnumerateVolumes() const {
#ifndef _WIN32
    return common::Result<std::vector<VolumeInfo>>::Fail(
        common::StatusCode::NotImplemented, "Volume enumeration is only supported on Windows.");
#else
    std::vector<VolumeInfo> volumes;

    std::array<wchar_t, MAX_PATH> volumeNameBuf{};
    HANDLE findHandle = ::FindFirstVolumeW(volumeNameBuf.data(), static_cast<DWORD>(volumeNameBuf.size()));
    if (findHandle == INVALID_HANDLE_VALUE) {
        return common::Result<std::vector<VolumeInfo>>::Fail(
            common::StatusCode::IoError,
            "FindFirstVolumeW failed with Win32 error " + std::to_string(::GetLastError()));
    }

    do {
        const std::wstring volumeGuidWithSlash(volumeNameBuf.data());
        std::wstring volumeGuidNoSlash = volumeGuidWithSlash;
        if (!volumeGuidNoSlash.empty() && volumeGuidNoSlash.back() == L'\\') {
            volumeGuidNoSlash.pop_back();
        }

        const UINT driveType = ::GetDriveTypeW(volumeGuidWithSlash.c_str());
        if (driveType == DRIVE_CDROM || driveType == DRIVE_REMOTE || driveType == DRIVE_NO_ROOT_DIR) {
            continue;
        }

        VolumeInfo vol{};
        vol.volumePath = WideToUtf8(volumeGuidNoSlash);
        vol.isRemovable = (driveType == DRIVE_REMOVABLE);

        // Mounted drive letter(s) / mount paths
        DWORD pathChars = 0;
        ::GetVolumePathNamesForVolumeNameW(volumeGuidWithSlash.c_str(), nullptr, 0, &pathChars);
        if (pathChars > 1) {
            std::vector<wchar_t> mountPaths(pathChars, L'\0');
            if (::GetVolumePathNamesForVolumeNameW(
                    volumeGuidWithSlash.c_str(), mountPaths.data(), pathChars, &pathChars)) {
                vol.driveLetter = WideToUtf8(mountPaths.data());
            }
        }

        // Volume label and file system name
        std::array<wchar_t, MAX_PATH + 1> labelBuf{};
        std::array<wchar_t, MAX_PATH + 1> fsBuf{};
        if (::GetVolumeInformationW(
                volumeGuidWithSlash.c_str(),
                labelBuf.data(),
                static_cast<DWORD>(labelBuf.size()),
                nullptr,
                nullptr,
                nullptr,
                fsBuf.data(),
                static_cast<DWORD>(fsBuf.size()))) {
            vol.label = WideToUtf8(labelBuf.data());
            vol.fileSystem = WideToUtf8(fsBuf.data());
        } else {
            vol.fileSystem = "RAW / Unmounted";
        }

        // Capacity and free space
        ULARGE_INTEGER freeBytesAvail{};
        ULARGE_INTEGER totalBytes{};
        ULARGE_INTEGER totalFreeBytes{};
        if (::GetDiskFreeSpaceExW(
                volumeGuidWithSlash.c_str(), &freeBytesAvail, &totalBytes, &totalFreeBytes)) {
            vol.totalBytes = totalBytes.QuadPart;
            vol.availableBytes = freeBytesAvail.QuadPart;
        }

        // Map volume to its parent PhysicalDrive index and byte offset
        ScopedHandle volHandle(::CreateFileW(
            volumeGuidNoSlash.c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr));
        if (volHandle.handle != INVALID_HANDLE_VALUE) {
            std::vector<std::uint8_t> extentBuf(sizeof(VOLUME_DISK_EXTENTS) + 8 * sizeof(DISK_EXTENT), 0);
            DWORD returned = 0;
            if (::DeviceIoControl(
                    volHandle.handle,
                    IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                    nullptr,
                    0,
                    extentBuf.data(),
                    static_cast<DWORD>(extentBuf.size()),
                    &returned,
                    nullptr)) {
                const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(extentBuf.data());
                if (extents->NumberOfDiskExtents > 0) {
                    vol.physicalDiskIndex = static_cast<std::int32_t>(extents->Extents[0].DiskNumber);
                    vol.partitionOffsetBytes = static_cast<std::uint64_t>(extents->Extents[0].StartingOffset.QuadPart);
                    vol.partitionLengthBytes = static_cast<std::uint64_t>(extents->Extents[0].ExtentLength.QuadPart);
                    if (vol.totalBytes == 0U) {
                        vol.totalBytes = vol.partitionLengthBytes;
                    }
                }
            }
        }

        volumes.push_back(std::move(vol));
    } while (::FindNextVolumeW(findHandle, volumeNameBuf.data(), static_cast<DWORD>(volumeNameBuf.size())));

    ::FindVolumeClose(findHandle);

    std::sort(volumes.begin(), volumes.end(), [](const VolumeInfo& a, const VolumeInfo& b) {
        if (a.driveLetter.empty() != b.driveLetter.empty()) {
            return !a.driveLetter.empty();
        }
        return a.driveLetter < b.driveLetter;
    });

    return common::Result<std::vector<VolumeInfo>>::Ok(std::move(volumes));
#endif
}

} // namespace salvus::device
