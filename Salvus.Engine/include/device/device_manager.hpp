#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace salvus::device {

enum class StorageDeviceType {
    Unknown = 0,
    InternalHdd,
    InternalSsd,
    ExternalHdd,
    ExternalSsd,
    UsbFlashDrive,
    SdCard,
    MicroSdCard,
    VirtualDiskImage
};

struct StorageDeviceInfo {
    std::uint32_t physicalDiskIndex = 0;
    std::string devicePath;
    std::string deviceName;
    std::string model;
    std::string manufacturer;
    std::string connectionType;
    std::string partitionLayout;
    std::string healthStatus;
    std::uint64_t capacityBytes = 0;
    std::uint32_t sectorSizeBytes = 512;
    StorageDeviceType deviceType = StorageDeviceType::Unknown;
    bool isPhysicalDisk = true;
    bool isRemovable = false;
    bool isSsd = false;
    bool rawAccessAvailable = false;
};

struct VolumeInfo {
    std::string volumePath;
    std::string driveLetter;
    std::string label;
    std::string fileSystem;
    std::uint64_t totalBytes = 0;
    std::uint64_t availableBytes = 0;
    std::int32_t physicalDiskIndex = -1;
    std::uint64_t partitionOffsetBytes = 0;
    std::uint64_t partitionLengthBytes = 0;
    bool isRemovable = false;
};

[[nodiscard]] std::string StorageDeviceTypeToString(StorageDeviceType type);
[[nodiscard]] bool IsProcessElevated() noexcept;

class DeviceManager {
public:
    DeviceManager() = default;
    ~DeviceManager() = default;

    // Native Windows physical disk and volume enumeration (strictly read-only metadata queries).
    [[nodiscard]] common::Result<std::vector<StorageDeviceInfo>> EnumeratePhysicalDisks() const;
    [[nodiscard]] common::Result<std::vector<VolumeInfo>> EnumerateVolumes() const;
};

} // namespace salvus::device
