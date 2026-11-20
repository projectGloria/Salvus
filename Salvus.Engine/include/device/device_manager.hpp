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
    std::string devicePath;
    std::string deviceName;
    std::string model;
    std::string manufacturer;
    std::string connectionType;
    std::uint64_t capacityBytes = 0;
    std::uint32_t sectorSizeBytes = 512;
    StorageDeviceType deviceType = StorageDeviceType::Unknown;
    bool isPhysicalDisk = true;
    bool isRemovable = false;
};

struct VolumeInfo {
    std::string volumePath;
    std::string driveLetter;
    std::string label;
    std::string fileSystem;
    std::uint64_t totalBytes = 0;
    std::uint64_t availableBytes = 0;
    std::uint32_t physicalDiskIndex = 0;
};

class DeviceManager {
public:
    DeviceManager() = default;
    ~DeviceManager() = default;

    // Phase 2: Native Windows physical disk and volume enumeration.
    [[nodiscard]] common::Result<std::vector<StorageDeviceInfo>> EnumeratePhysicalDisks() const;
    [[nodiscard]] common::Result<std::vector<VolumeInfo>> EnumerateVolumes() const;
};

} // namespace salvus::device
