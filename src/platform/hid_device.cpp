#include "platform/hid_device.hpp"

#include <algorithm>
#include <cwchar>

#include <hidapi.h>

namespace edgepad {
namespace {

std::string narrow(const wchar_t* text) {
    std::string out;
    if (text == nullptr) return out;
    for (; *text != L'\0'; ++text) out.push_back(*text < 128 ? static_cast<char>(*text) : '?');
    return out;
}

std::string hidError(hid_device* dev) {
    const std::string msg = narrow(hid_error(dev));
    return msg.empty() ? std::string("unknown HID error") : msg;
}

}  // namespace

bool hidInit() { return hid_init() == 0; }
void hidShutdown() { hid_exit(); }

std::vector<HidDeviceInfo> enumerateControllers() {
    std::vector<HidDeviceInfo> result;
    hid_device_info* list = hid_enumerate(dualsense::kSonyVendorId, 0);
    for (hid_device_info* it = list; it != nullptr; it = it->next) {
        if (!dualsense::isSupportedProduct(it->product_id) || it->path == nullptr) continue;
        const std::string path = it->path;
        const bool duplicate = std::any_of(result.begin(), result.end(), [&](const HidDeviceInfo& d) { return d.path == path; });
        if (!duplicate) result.push_back({path, it->product_id});
    }
    hid_free_enumeration(list);
    return result;
}

DualSenseDevice::~DualSenseDevice() { close(); }

bool DualSenseDevice::open(const HidDeviceInfo& info, std::string& error) {
    close();
    dev_ = hid_open_path(info.path.c_str());
    if (dev_ == nullptr) {
        error = "could not open controller: " + hidError(nullptr);
        return false;
    }
    info_ = info;
    connection_ = dualsense::Connection::Unknown;
    sequence_ = 0;
    needsLightbarSetup_ = true;
    lastSent_.reset();
    error_.clear();

    // Reading the calibration feature report switches Bluetooth controllers from the
    // reduced "simple" report to the full report (with Edge buttons, battery, ...).
    unsigned char feature[64] = {dualsense::kCalibrationFeatureReportId};
    hid_get_feature_report(dev_, feature, sizeof(feature));
    return true;
}

void DualSenseDevice::close() {
    if (dev_ != nullptr) {
        hid_close(dev_);
        dev_ = nullptr;
    }
    connection_ = dualsense::Connection::Unknown;
    lastSent_.reset();
}

DualSenseDevice::ReadResult DualSenseDevice::read(InputState& state, int timeoutMs) {
    if (dev_ == nullptr) return ReadResult::Error;
    unsigned char buffer[128];
    const int n = hid_read_timeout(dev_, buffer, sizeof(buffer), timeoutMs);
    if (n < 0) {
        error_ = hidError(dev_);
        return ReadResult::Error;
    }
    if (n == 0) return ReadResult::Timeout;
    const auto parsed = dualsense::parseInputReport(buffer, static_cast<size_t>(n));
    if (!parsed) return ReadResult::Timeout;
    if (connection_ != parsed->connection) {
        connection_ = parsed->connection;
        needsLightbarSetup_ = true;
        lastSent_.reset();
    }
    state = parsed->state;
    return ReadResult::Data;
}

bool DualSenseDevice::sendEffects(const dualsense::Effects& effects) {
    if (dev_ == nullptr || connection_ == dualsense::Connection::Unknown) return false;
    if (lastSent_ && *lastSent_ == effects) return true;

    auto write = [&](bool setup) {
        const auto report = dualsense::buildOutputReport(effects, connection_, sequence_, setup);
        sequence_ = static_cast<uint8_t>((sequence_ + 1) & 0x0F);
        return hid_write(dev_, report.data(), report.size()) >= 0;
    };
    if (needsLightbarSetup_) {
        if (!write(true)) return false;
        needsLightbarSetup_ = false;
    }
    if (!write(false)) {
        error_ = hidError(dev_);
        return false;
    }
    lastSent_ = effects;
    return true;
}

}  // namespace edgepad
