// FIXME: your file license if you have one

#include "HostapdCallback.h"

namespace vendor {
namespace sprd {
namespace hardware {
namespace wifi {
namespace hostapd {
namespace V1_1 {
namespace implementation {

// Methods from ::android::hardware::wifi::hostapd::V1_1::IHostapdCallback follow.
Return<void> HostapdCallback::onFailure(const hidl_string& ifaceName) {
    // TODO implement
    return Void();
}


// Methods from ::vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallback follow.
Return<void> HostapdCallback::HostApEvents(const hidl_string& dataString) {
    // TODO implement
    return Void();
}


// Methods from ::android::hidl::base::V1_0::IBase follow.

//IHostapdCallback* HIDL_FETCH_IHostapdCallback(const char* /* name */) {
    //return new HostapdCallback();
//}
//
}  // namespace implementation
}  // namespace V1_1
}  // namespace hostapd
}  // namespace wifi
}  // namespace hardware
}  // namespace sprd
}  // namespace vendor
