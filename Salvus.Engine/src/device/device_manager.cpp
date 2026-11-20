#include "device/device_manager.hpp"

namespace salvus::device {

common::Result<std::vector<StorageDeviceInfo>> DeviceManager::EnumeratePhysicalDisks() const {
    return common::Result<std::vector<StorageDeviceInfo>>::Fail(
        common::StatusCode::NotImplemented,
        "Physical disk enumeration is scheduled for Phase 2 (Storage Access).");
}

common::Result<std::vector<VolumeInfo>> DeviceManager::EnumerateVolumes() const {
    return common::Result<std::vector<VolumeInfo>>::Fail(
        common::StatusCode::NotImplemented,
        "Volume enumeration is scheduled for Phase 2 (Storage Access).");
}

} // namespace salvus::device
