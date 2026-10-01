#include "platform/hid_device.hpp"

#include <algorithm>
#include <cwchar>

#include <hidapi.h>

#include "core/virtual_dualsense.hpp"
#include "platform/hid_transport.hpp"

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

class HidapiTransport final : public HidTransport {
public:
    explicit HidapiTransport(hid_device* dev) : dev_(dev) {}
    ~HidapiTransport() override { hid_close(dev_); }
    HidapiTransport(const HidapiTransport&) = delete;
    HidapiTransport& operator=(const HidapiTransport&) = delete;

    int read(uint8_t* buffer, size_t size, int timeoutMs) override {
        return hid_read_timeout(dev_, buffer, size, timeoutMs);
    }
    bool write(const uint8_t* data, size_t size) override { return hid_write(dev_, data, size) >= 0; }
    int getFeature(uint8_t* buffer, size_t size) override { return hid_get_feature_report(dev_, buffer, size); }
    std::string error() const override { return hidError(dev_); }

private:
    hid_device* dev_;
};

}  // namespace

std::unique_ptr<HidTransport> openHidapiTransport(const std::string& path, std::string& error) {
    hid_device* dev = hid_open_path(path.c_str());
    if (dev == nullptr) {
        error = "could not open controller: " + hidError(nullptr);
        return nullptr;
    }
    return std::make_unique<HidapiTransport>(dev);
}

bool hidInit() { return hid_init() == 0; }
void hidShutdown() { hid_exit(); }

std::vector<HidDeviceInfo> enumerateControllers() {
    std::vector<HidDeviceInfo> result;
    hid_device_info* list = hid_enumerate(dualsense::kSonyVendorId, 0);
    for (hid_device_info* it = list; it != nullptr; it = it->next) {
        if (!dualsense::isSupportedProduct(it->product_id) || it->path == nullptr) continue;
        // Never EdgePad's own virtual DualSense.
        if (it->serial_number != nullptr && narrow(it->serial_number) == virtual_dualsense::kSerialNumber) continue;
        const std::string path = it->path;
        const bool duplicate = std::any_of(result.begin(), result.end(), [&](const HidDeviceInfo& d) { return d.path == path; });
        if (!duplicate) result.push_back({path, it->product_id});
    }
    hid_free_enumeration(list);
    return result;
}

DualSenseDevice::DualSenseDevice() = default;
DualSenseDevice::~DualSenseDevice() { close(); }

bool DualSenseDevice::open(const HidDeviceInfo& info, std::string& error, bool exclusive, bool* inUse) {
    close();
    if (inUse != nullptr) *inUse = false;
#ifdef _WIN32
    if (exclusive) {
        bool busy = false;
        dev_ = openExclusiveTransport(info.path, error, busy);
        if (inUse != nullptr) *inUse = busy;
    } else {
        dev_ = openHidapiTransport(info.path, error);
    }
#else
    exclusive = false;  // Linux hides the controller with an input grab instead (see controller_hide)
    dev_ = openHidapiTransport(info.path, error);
#endif
    if (!dev_) return false;
    exclusive_ = exclusive;
    info_ = info;
    connection_ = dualsense::Connection::Unknown;
    sequence_ = 0;
    needsLightbarSetup_ = true;
    lastSent_.reset();
    error_.clear();

    // Reading the calibration feature report switches Bluetooth controllers from the reduced
    // "simple" report to the full report (with Edge buttons, battery, ...). It also holds the
    // motion sensor calibration games use, needed to pass motion on 1:1.
    // The rest is passed on by a virtual DualSense, which answers with the real controller's data.
    features_.clear();
    for (uint8_t id : {dualsense::kCalibrationFeatureReportId, uint8_t{0x09}, uint8_t{0x20}, uint8_t{0x22}}) {
        unsigned char feature[64] = {id};
        const int n = dev_->getFeature(feature, sizeof(feature));
        if (n > 1 && feature[0] == id) features_[id].assign(feature, feature + n);
    }
    const auto calibration = features_.find(dualsense::kCalibrationFeatureReportId);
    motionCalibration_ = calibration == features_.end()
                             ? virtual_reports::nominalCalibration()
                             : virtual_reports::parseDualSenseCalibration(calibration->second.data(),
                                                                          calibration->second.size());
    return true;
}

void DualSenseDevice::close() {
    dev_.reset();
    exclusive_ = false;
    connection_ = dualsense::Connection::Unknown;
    lastSent_.reset();
}

DualSenseDevice::ReadResult DualSenseDevice::read(InputState& state, int timeoutMs) {
    if (!dev_) return ReadResult::Error;
    unsigned char buffer[128];
    const int n = dev_->read(buffer, sizeof(buffer), timeoutMs);
    if (n < 0) {
        error_ = dev_->error();
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

bool DualSenseDevice::sendOutput(const uint8_t* common) {
    if (!dev_ || connection_ == dualsense::Connection::Unknown) return false;
    if (needsLightbarSetup_) {  // the lightbar only listens after this (Bluetooth power-on animation)
        const auto setup = dualsense::buildOutputReport(dualsense::Effects{}, connection_, sequence_, true);
        sequence_ = static_cast<uint8_t>((sequence_ + 1) & 0x0F);
        if (!dev_->write(setup.data(), setup.size())) return false;
        needsLightbarSetup_ = false;
    }
    const auto report = dualsense::wrapOutputReport(common, connection_, sequence_);
    sequence_ = static_cast<uint8_t>((sequence_ + 1) & 0x0F);
    if (!dev_->write(report.data(), report.size())) {
        error_ = dev_->error();
        return false;
    }
    return true;
}

bool DualSenseDevice::sendEffects(const dualsense::Effects& effects) {
    if (!dev_ || connection_ == dualsense::Connection::Unknown) return false;
    if (lastSent_ && *lastSent_ == effects) return true;

    auto write = [&](bool setup) {
        const auto report = dualsense::buildOutputReport(effects, connection_, sequence_, setup);
        sequence_ = static_cast<uint8_t>((sequence_ + 1) & 0x0F);
        return dev_->write(report.data(), report.size());
    };
    if (needsLightbarSetup_) {
        if (!write(true)) return false;
        needsLightbarSetup_ = false;
    }
    if (!write(false)) {
        error_ = dev_->error();
        return false;
    }
    lastSent_ = effects;
    return true;
}

}  // namespace edgepad
