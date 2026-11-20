#include "imaging/disk_image_manager.hpp"

namespace salvus::imaging {

common::Result<void> DiskImageManager::CreateRawImage(const DiskImageOptions& /*options*/) const {
    return common::Result<void>::Fail(
        common::StatusCode::NotImplemented,
        "Disk imaging is scheduled for Phase 6 (Professional Features).");
}

} // namespace salvus::imaging
