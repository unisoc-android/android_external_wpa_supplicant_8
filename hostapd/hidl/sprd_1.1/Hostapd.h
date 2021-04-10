/*
 * hidl interface for wpa_hostapd daemon
 * Copyright (c) 2004-2018, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2018, Roshan Pius <rpius@google.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
#pragma once

#ifndef HOSTAPD_HIDL_SUPPLICANT_H
#define HOSTAPD_HIDL_SUPPLICANT_H
#define VENDOR_SPRD_HARDWARE_WIFI_HOSTAPD_V1_0_HOSTAPD_H

#include <string>

#include <android-base/macros.h>

#include <vendor/sprd/hardware/wifi/hostapd/1.1/IHostapd.h>
#include <vendor/sprd/hardware/wifi/hostapd/1.1/IHostapdCallbackEx.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

extern "C"
{
#include "utils/common.h"
#include "utils/includes.h"
#include "utils/wpa_debug.h"
#include "ap/hostapd.h"
#include "ctrl_iface.h"
#include "ap/wps_hostapd.h"

}

namespace vendor {
namespace sprd {
namespace hardware {
namespace wifi {
namespace hostapd {
namespace V1_1 {
namespace implementation {
using namespace android::hardware::wifi::hostapd::V1_0;
using ::android::hardware::wifi::hostapd::V1_0::HostapdStatus;
using ::android::hardware::wifi::hostapd::V1_0::HostapdStatusCode;
using vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapd;
using vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallbackEx;

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct Hostapd : public V1_1::IHostapd
{
public:
	Hostapd(hapd_interfaces* interfaces);
	~Hostapd() override = default;

    Return<void> addAccessPoint(
	    const android::hardware::wifi::hostapd::V1_0::IHostapd::IfaceParams& ifaceParams,
	    const NetworkParams& nwParams, addAccessPoint_cb _hidl_cb) override;
    Return<void> removeAccessPoint(
	    const hidl_string& ifaceName, removeAccessPoint_cb _hidl_cb) override;
    Return<void> terminate() override;

    Return<void> addAccessPoint_1_1(
	    const IfaceParams& ifaceParams,
	    const NetworkParams& nwParams, addAccessPoint_1_1_cb _hidl_cb) override;

    Return<void> registerCallback(
			const sp<android::hardware::wifi::hostapd::V1_1::IHostapdCallback>& callback, registerCallback_cb _hidl_cb);

    Return<void> registerCallbackEx(
	    const sp<vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallbackEx>& callback, registerCallback_cb _hidl_cb);

    Return<void> addAccessPointEx_1_1(
	    const IfaceParams& ifaceParams,
	    const NetworkParamsEx& nwParams, addAccessPointEx_1_1_cb _hidl_cb) override;
    Return<bool> doHostapdBooleanCommand(
	    const IfaceParams& ifaceParams, const hidl_string& type) override;
    Return<uint32_t> doHostapdIntCommand(
	    const IfaceParams& ifaceParams, const hidl_string& type) override;
    Return<void> doHostapdStringCommand(
	    const IfaceParams& ifaceParams,
	    const hidl_string& type, doHostapdStringCommand_cb _hidl_cb) override;

private:
	// Corresponding worker functions for the HIDL methods.
	HostapdStatus addAccessPointInternal(
		const android::hardware::wifi::hostapd::V1_0::IHostapd::IfaceParams& iface_params,
		const NetworkParams& nw_params);
	HostapdStatus addAccessPointInternal_1_1(
		const IfaceParams& IfaceParams, const NetworkParams& nw_params);
	HostapdStatus addAccessPointInternalEx_1_1(
		const IfaceParams& IfaceParams, const NetworkParamsEx& nw_params);
	HostapdStatus removeAccessPointInternal(const std::string& iface_name);
	HostapdStatus registerCallbackInternal(
		const sp<android::hardware::wifi::hostapd::V1_1::IHostapdCallback>& callback);
	HostapdStatus registerCallbackInternalEx(
		const sp<vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallbackEx>& callback);
	std::string doHostapdStringCommandInternal(const IfaceParams& iface_params,
				const hidl_string& type);

	// Raw pointer to the global structure maintained by the core.
	struct hapd_interfaces* interfaces_;
	// Callbacks registered.
	std::vector<sp<android::hardware::wifi::hostapd::V1_1::IHostapdCallback>> callbacks_;
	std::vector<sp<vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallbackEx>> callbacksex_;

	DISALLOW_COPY_AND_ASSIGN(Hostapd);

};

// FIXME: most likely delete, this is only for passthrough implementations
// extern "C" IHostapd* HIDL_FETCH_IHostapd(const char* name);

}  // namespace implementation
}  // namespace V1_1
}  // namespace hostapd
}  // namespace wifi
}  // namespace hardware
}  // namespace sprd
}  // namespace vendor
#endif
