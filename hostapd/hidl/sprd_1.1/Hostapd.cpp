// FIXME: your file license if you have one
/*
 * hidl interface for wpa_hostapd daemon
 * Copyright (c) 2004-2018, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2018, Roshan Pius <rpius@google.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>

#include "Hostapd.h"
#include "hidl_return_util.h"

extern "C"
{
#include "utils/eloop.h"
}

// The HIDL implementation for hostapd creates a hostapd.conf dynamically for
// each interface. This file can then be used to hook onto the normal config
// file parsing logic in hostapd code.  Helps us to avoid duplication of code
// in the HIDL interface.
// TOOD(b/71872409): Add unit tests for this.
namespace {
constexpr char kConfFileNameFmt[] = "/data/vendor/wifi/hostapd/hostapd_%s.conf";

using android::base::RemoveFileIfExists;
using android::base::StringPrintf;
using android::base::WriteStringToFile;
using vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapd;
using vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallbackEx;


std::string WriteHostapdConfig(
    const std::string& interface_name, const std::string& config)
{
	const std::string file_path =
	    StringPrintf(kConfFileNameFmt, interface_name.c_str());
	if (WriteStringToFile(
		config, file_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
		getuid(), getgid())) {
		return file_path;
	}
	// Diagnose failure
	int error = errno;
	wpa_printf(
	    MSG_ERROR, "Cannot write hostapd config to %s, error: %s",
	    file_path.c_str(), strerror(error));
	struct stat st;
	int result = stat(file_path.c_str(), &st);
	if (result == 0) {
		wpa_printf(
		    MSG_ERROR, "hostapd config file uid: %d, gid: %d, mode: %d",
		    st.st_uid, st.st_gid, st.st_mode);
	} else {
		wpa_printf(
		    MSG_ERROR,
		    "Error calling stat() on hostapd config file: %s",
		    strerror(errno));
	}
	return "";
}

std::string CreateHostapdConfig(
    const IHostapd::IfaceParams& iface_params,
    const IHostapd::NetworkParams& nw_params)
{
	if (nw_params.ssid.size() >
	    static_cast<uint32_t>(
		IHostapd::ParamSizeLimits::SSID_MAX_LEN_IN_BYTES)) {
		wpa_printf(
		    MSG_ERROR, "Invalid SSID size: %zu", nw_params.ssid.size());
		return "";
	}
	if ((nw_params.encryptionType != IHostapd::EncryptionType::NONE) &&
	    (nw_params.pskPassphrase.size() <
		 static_cast<uint32_t>(
		     IHostapd::ParamSizeLimits::
			 WPA2_PSK_PASSPHRASE_MIN_LEN_IN_BYTES) ||
	     nw_params.pskPassphrase.size() >
		 static_cast<uint32_t>(
		     IHostapd::ParamSizeLimits::
			 WPA2_PSK_PASSPHRASE_MAX_LEN_IN_BYTES))) {
		wpa_printf(
		    MSG_ERROR, "Invalid psk passphrase size: %zu",
		    nw_params.pskPassphrase.size());
		return "";
	}

	// SSID string
	std::stringstream ss;
	ss << std::hex;
	ss << std::setfill('0');
	for (uint8_t b : nw_params.ssid) {
		ss << std::setw(2) << static_cast<unsigned int>(b);
	}
	const std::string ssid_as_string = ss.str();

	// Encryption config string
	std::string encryption_config_as_string;
	switch (nw_params.encryptionType) {
	case IHostapd::EncryptionType::NONE:
		// no security params
		break;
	case IHostapd::EncryptionType::WPA:
		encryption_config_as_string = StringPrintf(
		    "wpa=3\n"
		    "wpa_pairwise=TKIP CCMP\n"
		    "wpa_passphrase=%s",
		    nw_params.pskPassphrase.c_str());
		break;
	case IHostapd::EncryptionType::WPA2:
		encryption_config_as_string = StringPrintf(
		    "wpa=2\n"
		    "rsn_pairwise=CCMP\n"
		    "wpa_passphrase=%s",
		    nw_params.pskPassphrase.c_str());
		break;
	default:
		wpa_printf(MSG_ERROR, "Unknown encryption type");
		return "";
	}

	std::string channel_config_as_string;
	if (iface_params.V1_0.channelParams.enableAcs) {
		std::string chanlist_as_string;
		for (const auto &range :
		     iface_params.channelParams.acsChannelRanges) {
			if (range.start != range.end) {
				chanlist_as_string +=
					StringPrintf("%d-%d ", range.start, range.end);
			} else {
				chanlist_as_string += StringPrintf("%d ", range.start);
			}
		}
		channel_config_as_string = StringPrintf(
		    "channel=0\n"
		    "acs_exclude_dfs=%d\n"
		    "chanlist=%s",
		    iface_params.V1_0.channelParams.acsShouldExcludeDfs,
		    chanlist_as_string.c_str());
	} else {
		channel_config_as_string = StringPrintf(
		    "channel=%d", iface_params.V1_0.channelParams.channel);
	}

	// Hw Mode String
	std::string hw_mode_as_string;
	std::string ht_cap_vht_oper_chwidth_as_string;
	switch (iface_params.V1_0.channelParams.band) {
	case IHostapd::Band::BAND_2_4_GHZ:
		hw_mode_as_string = "hw_mode=g";
		break;
	case IHostapd::Band::BAND_5_GHZ:
		hw_mode_as_string = "hw_mode=a";
		if (iface_params.V1_0.channelParams.enableAcs) {
			ht_cap_vht_oper_chwidth_as_string =
			    "ht_capab=[HT40+]\n"
			    "vht_oper_chwidth=1";
		}
		break;
	case IHostapd::Band::BAND_ANY:
		hw_mode_as_string = "hw_mode=any";
		if (iface_params.V1_0.channelParams.enableAcs) {
			ht_cap_vht_oper_chwidth_as_string =
			    "ht_capab=[HT40+]\n"
			    "vht_oper_chwidth=1";
		}
		break;
	default:
		wpa_printf(MSG_ERROR, "Invalid band");
		return "";
	}

	return StringPrintf(
	    "interface=%s\n"
	    "driver=nl80211\n"
	    "ctrl_interface=/data/vendor/wifi/hostapd/ctrl\n"
	    // ssid2 signals to hostapd that the value is not a literal value
	    // for use as a SSID.  In this case, we're giving it a hex
	    // std::string and hostapd needs to expect that.
	    "ssid2=%s\n"
	    "%s\n"
	    "ieee80211n=%d\n"
	    "ieee80211ac=%d\n"
	    "%s\n"
	    "%s\n"
	    "ignore_broadcast_ssid=%d\n"
	    "wowlan_triggers=any\n"
	    "%s\n",
	    iface_params.V1_0.ifaceName.c_str(), ssid_as_string.c_str(),
	    channel_config_as_string.c_str(),
	    iface_params.V1_0.hwModeParams.enable80211N ? 1 : 0,
	    iface_params.V1_0.hwModeParams.enable80211AC ? 1 : 0,
	    hw_mode_as_string.c_str(), ht_cap_vht_oper_chwidth_as_string.c_str(),
	    nw_params.isHidden ? 1 : 0, encryption_config_as_string.c_str());
}

std::string CreateSprdHostapdConfig(
    const IHostapd::IfaceParams& iface_params,
    const IHostapd::NetworkParamsEx& nw_params)
{
	if (nw_params.networkParams.ssid.size() >
	    static_cast<uint32_t>(
		IHostapd::ParamSizeLimits::SSID_MAX_LEN_IN_BYTES)) {
		wpa_printf(
		    MSG_ERROR, "Invalid SSID size: %zu", nw_params.networkParams.ssid.size());
		return "";
	}
	if ((nw_params.networkParams.encryptionType != IHostapd::EncryptionType::NONE) &&
	    (nw_params.networkParams.pskPassphrase.size() <
		 static_cast<uint32_t>(
		     IHostapd::ParamSizeLimits::
			 WPA2_PSK_PASSPHRASE_MIN_LEN_IN_BYTES) ||
	     nw_params.networkParams.pskPassphrase.size() >
		 static_cast<uint32_t>(
		     IHostapd::ParamSizeLimits::
			 WPA2_PSK_PASSPHRASE_MAX_LEN_IN_BYTES))) {
		wpa_printf(
		    MSG_ERROR, "Invalid psk passphrase size: %zu",
		    nw_params.networkParams.pskPassphrase.size());
		return "";
	}

	// SSID string
	std::stringstream ss;
	ss << std::hex;
	ss << std::setfill('0');
	for (uint8_t b : nw_params.networkParams.ssid) {
		ss << std::setw(2) << static_cast<unsigned int>(b);
	}
	const std::string ssid_as_string = ss.str();

	// Encryption config string
	std::string encryption_config_as_string;
	switch (nw_params.networkParams.encryptionType) {
	case IHostapd::EncryptionType::NONE:
		// no security params
		break;
	case IHostapd::EncryptionType::WPA:
		encryption_config_as_string = StringPrintf(
		    "wpa=3\n"
		    "wpa_pairwise=TKIP CCMP\n"
		    "wpa_passphrase=%s",
		    nw_params.networkParams.pskPassphrase.c_str());
		break;
	case IHostapd::EncryptionType::WPA2:
		encryption_config_as_string = StringPrintf(
		    "wpa=2\n"
		    "rsn_pairwise=CCMP\n"
		    "wpa_passphrase=%s",
		    nw_params.networkParams.pskPassphrase.c_str());
		break;
	default:
		wpa_printf(MSG_ERROR, "Unknown encryption type");
		return "";
	}

	std::string channel_config_as_string;
	if (iface_params.V1_0.channelParams.enableAcs) {
		std::string chanlist_as_string;
		for (const auto &range :
		     iface_params.channelParams.acsChannelRanges) {
			if (range.start != range.end) {
				chanlist_as_string +=
					StringPrintf("%d-%d ", range.start, range.end);
			} else {
				chanlist_as_string += StringPrintf("%d ", range.start);
			}
		}
		channel_config_as_string = StringPrintf(
		    "channel=0\n"
		    "acs_exclude_dfs=%d\n"
		    "chanlist=%s",
		    iface_params.V1_0.channelParams.acsShouldExcludeDfs,
		    chanlist_as_string.c_str());
	} else {
		channel_config_as_string = StringPrintf(
		    "channel=%d", iface_params.V1_0.channelParams.channel);
	}

	// Hw Mode String
	std::string hw_mode_as_string;
	std::string ht_cap_vht_oper_chwidth_as_string;
	switch (iface_params.V1_0.channelParams.band) {
	case IHostapd::Band::BAND_2_4_GHZ:
		hw_mode_as_string = "hw_mode=g";
		break;
	case IHostapd::Band::BAND_5_GHZ:
		hw_mode_as_string = "hw_mode=a";
		if (iface_params.V1_0.channelParams.enableAcs) {
			ht_cap_vht_oper_chwidth_as_string =
			    "ht_capab=[HT40+]\n"
			    "vht_oper_chwidth=1";
		}
		break;
	case IHostapd::Band::BAND_ANY:
		hw_mode_as_string = "hw_mode=any";
		if (iface_params.V1_0.channelParams.enableAcs) {
			ht_cap_vht_oper_chwidth_as_string =
			    "ht_capab=[HT40+]\n"
			    "vht_oper_chwidth=1";
		}
		break;
	default:
		wpa_printf(MSG_ERROR, "Invalid band");
		return "";
	}


	return StringPrintf(
	    "interface=%s\n"
	    "driver=nl80211\n"
	    "ctrl_interface=/data/vendor/wifi/hostapd/ctrl\n"
	    // ssid2 signals to hostapd that the value is not a literal value
	    // for use as a SSID.  In this case, we're giving it a hex
	    // std::string and hostapd needs to expect that.
	    "ssid2=%s\n"
	    "%s\n"
	    "ieee80211n=%d\n"
	    "ieee80211ac=%d\n"
	    "%s\n"
	    "%s\n"
	    "ignore_broadcast_ssid=%d\n"
	    "wowlan_triggers=any\n"
	    "%s\n"
	    "max_num_sta=%d\n"
	    "macaddr_acl=%d\n"
	    "accept_mac_file=/data/vendor/wifi/hostapd/hostapd.accept\n"
	    "config_methods=virtual_push_button keypad\n"
	    "eap_server=1\n"
	    "wps_state=2\n"
	    "ap_setup_locked=1\n",
	    iface_params.V1_0.ifaceName.c_str(), ssid_as_string.c_str(),
	    channel_config_as_string.c_str(),
	    iface_params.V1_0.hwModeParams.enable80211N ? 1 : 0,
	    iface_params.V1_0.hwModeParams.enable80211AC ? 1 : 0,
	    hw_mode_as_string.c_str(), ht_cap_vht_oper_chwidth_as_string.c_str(),
	    nw_params.networkParams.isHidden ? 1 : 0, encryption_config_as_string.c_str(),
	    nw_params.softApMaxNumSta, nw_params.macAddrAcl);
}

// hostapd core functions accept "C" style function pointers, so use global
// functions to pass to the hostapd core function and store the corresponding
// std::function methods to be invoked.
//
// NOTE: Using the pattern from the vendor HAL (wifi_legacy_hal.cpp).
//
// Callback to be invoked once setup is complete
std::function<void(struct hostapd_data*)> on_setup_complete_internal_callback;
std::function<void(char* data)> sta_info_event_callback;
void onAsyncSetupCompleteCb(void* ctx)
{
	struct hostapd_data* iface_hapd = (struct hostapd_data*)ctx;
	if (on_setup_complete_internal_callback) {
		on_setup_complete_internal_callback(iface_hapd);
		// Invalidate this callback since we don't want this firing
		// again.
		on_setup_complete_internal_callback = nullptr;
	}
}

void onAsyncStaInfoCb(char* ctx)
{
	wpa_printf(MSG_ERROR, "onAsyncStaInfoCb ");
	char* data = ctx;
	if (sta_info_event_callback) {
		sta_info_event_callback(data);
		// Invalidate this callback since we don't want this firing
		// again.
		//sta_info_event_callback = nullptr;
	}
}
}  // namespace

namespace vendor {
namespace sprd {
namespace hardware {
namespace wifi {
namespace hostapd {
namespace V1_1 {
namespace implementation {
using hidl_return_util::call;
using namespace android::hardware::wifi::hostapd::V1_0;
using namespace vendor::sprd::hardware::wifi::hostapd::V1_1;

Hostapd::Hostapd(struct hapd_interfaces* interfaces) : interfaces_(interfaces)
{}

Return<void> Hostapd::addAccessPoint(
    const android::hardware::wifi::hostapd::V1_0::IHostapd::IfaceParams& iface_params,
    const NetworkParams& nw_params, addAccessPoint_cb _hidl_cb) {
	return call(
	    this, &Hostapd::addAccessPointInternal, _hidl_cb, iface_params,
	    nw_params);
}

Return<void> Hostapd::removeAccessPoint(
    const hidl_string& iface_name, removeAccessPoint_cb _hidl_cb) {
	return call(
	    this, &Hostapd::removeAccessPointInternal, _hidl_cb, iface_name);
}

Return<void> Hostapd::terminate() {
	wpa_printf(MSG_INFO, "Terminating...");
	eloop_terminate();
	return Void();
}

Return<void> Hostapd::addAccessPoint_1_1(
    const IfaceParams& iface_params, const NetworkParams& nw_params,
    addAccessPoint_1_1_cb _hidl_cb) {
	return call(
	    this, &Hostapd::addAccessPointInternal_1_1, _hidl_cb, iface_params,
	    nw_params);
}

Return<void> Hostapd::registerCallbackEx(
    const sp<vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallbackEx>& callback, registerCallback_cb _hidl_cb) {
    wpa_printf(MSG_INFO,"registerCallbackEx");
	return call(
	    this, &Hostapd::registerCallbackInternalEx, _hidl_cb, callback);
}

Return<void> Hostapd::registerCallback(
    const sp<::android::hardware::wifi::hostapd::V1_1::IHostapdCallback>& callback, registerCallback_cb _hidl_cb) {
	return call(
	    this, &Hostapd::registerCallbackInternal, _hidl_cb, callback);
}


Return<void> Hostapd::addAccessPointEx_1_1(
    const IfaceParams& iface_params, const NetworkParamsEx& nw_params,
    addAccessPointEx_1_1_cb _hidl_cb) {
	return call(
	    this, &Hostapd::addAccessPointInternalEx_1_1, _hidl_cb, iface_params,
	    nw_params);
}

Return<bool> Hostapd::doHostapdBooleanCommand(
    const IfaceParams& iface_params, const hidl_string& type) {
	char reply[4097];
	char cmd[1024];
	int reply_len = 0;
	//std::string s(&cmd[0],&cmd[strlen(cmd)]);
	memset(cmd,0,1024);
	strncpy(cmd,type.c_str(),type.size());
	struct hostapd_data* iface_hapd =
	    hostapd_get_iface(interfaces_, iface_params.V1_0.ifaceName.c_str());
	wpa_printf(MSG_INFO,"doHostapdBooleanCommand %s" , type.c_str());
	if (iface_hapd) {
		if (os_strncmp(cmd, "WPS_PIN ", 8) == 0) {
			if (hostapd_ctrl_iface_wps_pin(iface_hapd, cmd + 8))
			    reply_len = -1;
		} else if (os_strcmp(cmd, "WPS_CANCEL") == 0) {
			if (hostapd_wps_cancel(iface_hapd))
			   reply_len = -1;
		} else if (os_strcmp(cmd, "WPS_PBC") == 0) {
			if (hostapd_wps_button_pushed(iface_hapd, NULL))
			    reply_len = -1;
		} else if(os_strncmp(cmd, "DRIVER ",7) == 0) {
			if (!iface_hapd->driver->driver_cmd) {
			    return false;
			} else {
			    iface_hapd->driver->driver_cmd(iface_hapd->drv_priv, (u8*)(cmd+7), reply, sizeof(reply));
			}
			std::string ret(&reply[0],&reply[strlen(reply)]);
			wpa_printf(MSG_INFO,"doHostapdBooleanCommand %s,%zu" , ret.c_str(),sizeof(reply));
			return (strcmp(reply, "OK") == 0);
		}
		if(reply_len < 0) {
		   return false;
		}else {
		   return true;
		}
	}
	return false;
}

Return<uint32_t> Hostapd::doHostapdIntCommand(
    const IfaceParams& iface_params, const hidl_string& type) {
	char reply[4097];
	char cmd[1024];
	memset(cmd,0,1024);
	strncpy(cmd,type.c_str(),type.size());
	struct hostapd_data* iface_hapd =
	    hostapd_get_iface(interfaces_, iface_params.V1_0.ifaceName.c_str());
	wpa_printf(MSG_INFO,"doHostapdintCommand %s" , type.c_str());
	if (iface_hapd) {
		if(os_strncmp(cmd, "DRIVER ",7) == 0) {
			if (!iface_hapd->driver->driver_cmd) {
			    return 0;
			} else {
			    iface_hapd->driver->driver_cmd(iface_hapd->drv_priv, (u8*)(cmd+7), reply, sizeof(reply));
			}
		}
		wpa_printf(MSG_INFO,"doHostapdintCommand %s" , reply);
		return (atoi(reply));
	}
	return 0;
}

Return<void> Hostapd::doHostapdStringCommand(
    const IfaceParams& iface_params, const hidl_string& type, doHostapdStringCommand_cb _hidl_cb) {
	wpa_printf(MSG_INFO,"doHostapdstringCommand %s" , type.c_str());
    return hidl_return_util::validateAndCall(this,&Hostapd::doHostapdStringCommandInternal,_hidl_cb,iface_params,type);

}

HostapdStatus Hostapd::addAccessPointInternal(
    const android::hardware::wifi::hostapd::V1_0::IHostapd::IfaceParams& iface_params,
    const NetworkParams& nw_params)
{
	return {HostapdStatusCode::FAILURE_UNKNOWN, ""};
}

HostapdStatus Hostapd::addAccessPointInternal_1_1(
    const IfaceParams& iface_params, const NetworkParams& nw_params)
{
	if (hostapd_get_iface(interfaces_, iface_params.V1_0.ifaceName.c_str())) {
		wpa_printf(
		    MSG_ERROR, "Interface %s already present",
		    iface_params.V1_0.ifaceName.c_str());
		return {HostapdStatusCode::FAILURE_IFACE_EXISTS, ""};
	}
	const auto conf_params = CreateHostapdConfig(iface_params, nw_params);
	if (conf_params.empty()) {
		wpa_printf(MSG_ERROR, "Failed to create config params");
		return {HostapdStatusCode::FAILURE_ARGS_INVALID, ""};
	}
	const auto conf_file_path =
	    WriteHostapdConfig(iface_params.V1_0.ifaceName, conf_params);
	if (conf_file_path.empty()) {
		wpa_printf(MSG_ERROR, "Failed to write config file");
		return {HostapdStatusCode::FAILURE_UNKNOWN, ""};
	}
	std::string add_iface_param_str = StringPrintf(
	    "%s config=%s", iface_params.V1_0.ifaceName.c_str(),
	    conf_file_path.c_str());
	std::vector<char> add_iface_param_vec(
	    add_iface_param_str.begin(), add_iface_param_str.end() + 1);
	if (hostapd_add_iface(interfaces_, add_iface_param_vec.data()) < 0) {
		wpa_printf(
		    MSG_ERROR, "Adding interface %s failed",
		    add_iface_param_str.c_str());
		return {HostapdStatusCode::FAILURE_UNKNOWN, ""};
	}
	struct hostapd_data* iface_hapd =
	    hostapd_get_iface(interfaces_, iface_params.V1_0.ifaceName.c_str());
	WPA_ASSERT(iface_hapd != nullptr && iface_hapd->iface != nullptr);
	// Register the setup complete callbacks
	on_setup_complete_internal_callback =
	    [this](struct hostapd_data* iface_hapd) {
		    wpa_printf(
			MSG_DEBUG, "AP interface setup completed - state %s",
			hostapd_state_text(iface_hapd->iface->state));
		    if (iface_hapd->iface->state == HAPD_IFACE_DISABLED) {
			    // Invoke the failure callback on all registered
			    // clients.
			    for (const auto& callback : callbacks_) {
				    callback->onFailure(
					iface_hapd->conf->iface);
			    }
		    }
	    };
	iface_hapd->setup_complete_cb = onAsyncSetupCompleteCb;
	iface_hapd->setup_complete_cb_ctx = iface_hapd;
	if (hostapd_enable_iface(iface_hapd->iface) < 0) {
		wpa_printf(
		    MSG_ERROR, "Enabling interface %s failed",
		    iface_params.V1_0.ifaceName.c_str());
		return {HostapdStatusCode::FAILURE_UNKNOWN, ""};
	}
	return {HostapdStatusCode::SUCCESS, ""};
}

HostapdStatus Hostapd::addAccessPointInternalEx_1_1(
    const IfaceParams& iface_params, const NetworkParamsEx& nw_params)
{
	if (hostapd_get_iface(interfaces_, iface_params.V1_0.ifaceName.c_str())) {
		wpa_printf(
		    MSG_ERROR, "Interface %s already present",
		    iface_params.V1_0.ifaceName.c_str());
		return {HostapdStatusCode::FAILURE_IFACE_EXISTS, ""};
	}
	const auto conf_params = CreateSprdHostapdConfig(iface_params, nw_params);
	if (conf_params.empty()) {
		wpa_printf(MSG_ERROR, "Failed to create config params");
		return {HostapdStatusCode::FAILURE_ARGS_INVALID, ""};
	}
	const auto conf_file_path =
	    WriteHostapdConfig(iface_params.V1_0.ifaceName, conf_params);
	if (conf_file_path.empty()) {
		wpa_printf(MSG_ERROR, "Failed to write config file");
		return {HostapdStatusCode::FAILURE_UNKNOWN, ""};
	}
	std::string add_iface_param_str = StringPrintf(
	    "%s config=%s", iface_params.V1_0.ifaceName.c_str(),
	    conf_file_path.c_str());
	std::vector<char> add_iface_param_vec(
	    add_iface_param_str.begin(), add_iface_param_str.end() + 1);
	if (hostapd_add_iface(interfaces_, add_iface_param_vec.data()) < 0) {
		wpa_printf(
		    MSG_ERROR, "Adding interface %s failed",
		    add_iface_param_str.c_str());
		return {HostapdStatusCode::FAILURE_UNKNOWN, ""};
	}
	struct hostapd_data* iface_hapd =
	    hostapd_get_iface(interfaces_, iface_params.V1_0.ifaceName.c_str());
	WPA_ASSERT(iface_hapd != nullptr && iface_hapd->iface != nullptr);
	// Register the setup complete callbacks
	on_setup_complete_internal_callback =
	    [this](struct hostapd_data* iface_hapd) {
		    wpa_printf(
			MSG_DEBUG, "AP interface setup completed - state %s",
			hostapd_state_text(iface_hapd->iface->state));
		    if (iface_hapd->iface->state == HAPD_IFACE_DISABLED) {
			    // Invoke the failure callback on all registered
			    // clients.
			    for (const auto& callback : callbacks_) {
				    callback->onFailure(
					iface_hapd->conf->iface);
			    }
		    }
	    };
	sta_info_event_callback =
	    [this] (char * string_data) {
		    for (const auto& callback : callbacksex_) {
			    wpa_printf(MSG_ERROR, "sta_info_event_callback callbacksex_ = %s ", string_data);
			    callback->HostApEvents(
				string_data);
		    }
	    };
	iface_hapd->setup_complete_cb = onAsyncSetupCompleteCb;
	iface_hapd->setup_complete_cb_ctx = iface_hapd;
	iface_hapd->sta_info_event_cb = onAsyncStaInfoCb;
	iface_hapd->sta_info_event_cb_ctx = iface_hapd;
	if (hostapd_enable_iface(iface_hapd->iface) < 0) {
		wpa_printf(
		    MSG_ERROR, "Enabling interface %s failed",
		    iface_params.V1_0.ifaceName.c_str());
		return {HostapdStatusCode::FAILURE_UNKNOWN, ""};
	}
	return {HostapdStatusCode::SUCCESS, ""};
}


HostapdStatus Hostapd::removeAccessPointInternal(const std::string& iface_name)
{
	std::vector<char> remove_iface_param_vec(
	    iface_name.begin(), iface_name.end() + 1);
	if (hostapd_remove_iface(interfaces_, remove_iface_param_vec.data()) <
	    0) {
		wpa_printf(
		    MSG_ERROR, "Removing interface %s failed",
		    iface_name.c_str());
		return {HostapdStatusCode::FAILURE_UNKNOWN, ""};
	}
	return {HostapdStatusCode::SUCCESS, ""};
}

HostapdStatus Hostapd::registerCallbackInternal(
    const sp<android::hardware::wifi::hostapd::V1_1::IHostapdCallback>& callback)
{
	callbacks_.push_back(callback);
	return {HostapdStatusCode::SUCCESS, ""};
}

HostapdStatus Hostapd::registerCallbackInternalEx(
    const sp<vendor::sprd::hardware::wifi::hostapd::V1_1::IHostapdCallbackEx>& callback)
{
	callbacksex_.push_back(callback);
	return {HostapdStatusCode::SUCCESS, ""};
}

std::string Hostapd::doHostapdStringCommandInternal(const IfaceParams& iface_params,
    const hidl_string& type)
{
	char reply[4097];
	int reply_size= 0;
	char cmd[1024];
	memset(cmd,0,1024);
	strncpy(cmd,type.c_str(),type.size());
	struct hostapd_data* iface_hapd =
	    hostapd_get_iface(interfaces_, iface_params.V1_0.ifaceName.c_str());
	if (iface_hapd) {
		if (os_strncmp(cmd, "WPS_CHECK_PIN ", 14) == 0) {
		     hostapd_ctrl_iface_wps_check_pin(
		           iface_hapd, cmd + 14, reply, reply_size);
		}else if(os_strncmp(cmd, "DRIVER ",7) == 0) {
		    if (!iface_hapd->driver->driver_cmd)
		        return "";
		    else
		        iface_hapd->driver->driver_cmd(iface_hapd->drv_priv, (u8*)(cmd+7), reply, sizeof(reply));
		}
		wpa_printf(MSG_INFO,"doHostapdStringCommandInternal reply %s" , reply);
		return reply;
	}
	return NULL;
}

// Methods from ::android::hidl::base::V1_0::IBase follow.

//IHostapd* HIDL_FETCH_IHostapd(const char* /* name */) {
    //return new Hostapd();
//}
//
}  // namespace implementation
}  // namespace V1_1
}  // namespace hostapd
}  // namespace wifi
}  // namespace hardware
}  // namespace sprd
}  // namespace vendor
