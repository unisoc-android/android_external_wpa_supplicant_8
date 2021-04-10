// FIXME: your file license if you have one

#pragma once

#include <vendor/sprd/hardware/wifi/hostapd/1.1/IHostapdCallback.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

namespace vendor {
namespace sprd {
namespace hardware {
namespace wifi {
namespace hostapd {
namespace V1_1 {
namespace implementation {

using vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallback;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct HostapdCallback : public IHostapdCallback {
    // Methods from ::android::hardware::wifi::hostapd::V1_1::IHostapdCallback follow.
    Return<void> onFailure(const hidl_string& ifaceName) override;

    // Methods from ::vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallback follow.
    Return<void> HostApEvents(const hidl_string& dataString) override;

    // Methods from ::android::hidl::base::V1_0::IBase follow.

};

// FIXME: most likely delete, this is only for passthrough implementations
// extern "C" IHostapdCallback* HIDL_FETCH_IHostapdCallback(const char* name);

}  // namespace implementation
}  // namespace V1_1
}  // namespace hostapd
}  // namespace wifi
}  // namespace hardware
}  // namespace sprd
}  // namespace vendor
