#include "common/ipc_protocol.hpp"
#include "common/version.hpp"
#include "device/device_manager.hpp"

#include <gtest/gtest.h>

namespace {

using namespace salvus::device;
using namespace salvus::common;

TEST(DeviceManagerTests, EnumeratesRealVolumesOnWindows) {
    DeviceManager mgr;
    const auto vols = mgr.EnumerateVolumes();
    ASSERT_TRUE(vols.ok()) << vols.error().message;
    EXPECT_FALSE(vols.value().empty());

    // At least one volume should have a non-empty volume path
    bool foundMounted = false;
    for (const auto& v : vols.value()) {
        EXPECT_FALSE(v.volumePath.empty());
        if (!v.driveLetter.empty()) {
            foundMounted = true;
        }
    }
    EXPECT_TRUE(foundMounted);
}

TEST(DeviceManagerTests, EnumeratesPhysicalDisksWithoutCrashing) {
    DeviceManager mgr;
    const auto disks = mgr.EnumeratePhysicalDisks();
    ASSERT_TRUE(disks.ok()) << disks.error().message;
    EXPECT_FALSE(disks.value().empty());

    for (const auto& d : disks.value()) {
        EXPECT_TRUE(d.isPhysicalDisk);
        EXPECT_FALSE(d.devicePath.empty());
    }
}

TEST(DeviceManagerTests, DispatchesListDevicesAndListVolumesOverIpc) {
    CommandDispatcher dispatcher;

    const auto devResp = dispatcher.Dispatch(IpcRequest{"d-1", kIpcProtocolVersion, "list_devices", "{}"});
    EXPECT_TRUE(devResp.response.ok);
    EXPECT_NE(devResp.response.payloadJson.find("\"devices\":["), std::string::npos);

    const auto volResp = dispatcher.Dispatch(IpcRequest{"v-1", kIpcProtocolVersion, "list_volumes", "{}"});
    EXPECT_TRUE(volResp.response.ok);
    EXPECT_NE(volResp.response.payloadJson.find("\"volumes\":["), std::string::npos);
}

} // namespace
