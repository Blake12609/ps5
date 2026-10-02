#include "core/virtual_dualsense.hpp"

#include <algorithm>
#include <map>

#include "core/axis.hpp"

namespace edgepad::virtual_dualsense {
namespace {

// Copied from a real DualSense (CFI-ZCT1W) over USB.
const std::vector<uint8_t> kReportDescriptor = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02, 0x06,
    0x00, 0xFF, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07,
    0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00, 0x05,
    0x09, 0x19, 0x01, 0x29, 0x0F, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0F, 0x81, 0x02, 0x06,
    0x00, 0xFF, 0x09, 0x21, 0x95, 0x0D, 0x81, 0x02, 0x06, 0x00, 0xFF, 0x09, 0x22, 0x15, 0x00, 0x26,
    0xFF, 0x00, 0x75, 0x08, 0x95, 0x34, 0x81, 0x02, 0x85, 0x02, 0x09, 0x23, 0x95, 0x2F, 0x91, 0x02,
    0x85, 0x05, 0x09, 0x33, 0x95, 0x28, 0xB1, 0x02, 0x85, 0x08, 0x09, 0x34, 0x95, 0x2F, 0xB1, 0x02,
    0x85, 0x09, 0x09, 0x24, 0x95, 0x13, 0xB1, 0x02, 0x85, 0x0A, 0x09, 0x25, 0x95, 0x1A, 0xB1, 0x02,
    0x85, 0x20, 0x09, 0x26, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0x21, 0x09, 0x27, 0x95, 0x04, 0xB1, 0x02,
    0x85, 0x22, 0x09, 0x40, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0x80, 0x09, 0x28, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x81, 0x09, 0x29, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0x82, 0x09, 0x2A, 0x95, 0x09, 0xB1, 0x02,
    0x85, 0x83, 0x09, 0x2B, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0x84, 0x09, 0x2C, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x85, 0x09, 0x2D, 0x95, 0x02, 0xB1, 0x02, 0x85, 0xA0, 0x09, 0x2E, 0x95, 0x01, 0xB1, 0x02,
    0x85, 0xE0, 0x09, 0x2F, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0xF0, 0x09, 0x30, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0xF1, 0x09, 0x31, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0xF2, 0x09, 0x32, 0x95, 0x0F, 0xB1, 0x02,
    0x85, 0xF4, 0x09, 0x35, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0xF5, 0x09, 0x36, 0x95, 0x03, 0xB1, 0x02,
    0xC0,
};

// Report sizes in bytes (report id included) for one report type, read from the report descriptor.
std::map<uint8_t, size_t> reportSizes(uint8_t mainItem) {
    std::map<uint8_t, size_t> bits;
    uint32_t reportSize = 0, reportCount = 0;
    uint8_t reportId = 0;
    const auto& d = kReportDescriptor;
    for (size_t i = 0; i < d.size();) {
        const uint8_t prefix = d[i];
        size_t len = prefix & 0x03;
        if (len == 3) len = 4;
        uint32_t value = 0;
        for (size_t k = 0; k < len && i + 1 + k < d.size(); ++k) value |= uint32_t{d[i + 1 + k]} << (8 * k);
        switch (prefix & 0xFC) {
            case 0x74: reportSize = value; break;   // Report Size
            case 0x94: reportCount = value; break;  // Report Count
            case 0x84: reportId = static_cast<uint8_t>(value); break;
            default:
                if ((prefix & 0xFC) == mainItem) bits[reportId] += size_t{reportSize} * reportCount;
        }
        i += 1 + len;
    }
    std::map<uint8_t, size_t> bytes;
    for (const auto& [id, n] : bits) bytes[id] = 1 + (n + 7) / 8;
    return bytes;
}

void put16(uint8_t* p, int v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

std::vector<uint8_t> usbString(const std::string& text) {
    std::vector<uint8_t> d{static_cast<uint8_t>(2 + 2 * text.size()), 0x03};
    for (char c : text) {
        d.push_back(static_cast<uint8_t>(c));
        d.push_back(0);
    }
    return d;
}

void encodeTouch(const TouchPoint& t, uint8_t* p) {
    p[0] = static_cast<uint8_t>((t.active ? 0x00 : 0x80) | (t.id & 0x7F));
    p[1] = static_cast<uint8_t>(t.x & 0xFF);
    p[2] = static_cast<uint8_t>(((t.x >> 8) & 0x0F) | ((t.y & 0x0F) << 4));
    p[3] = static_cast<uint8_t>((t.y >> 4) & 0xFF);
}

// Output report flags (offsets in the 47 common bytes).
constexpr size_t kFlags0 = 0, kFlags1 = 1, kMotorRight = 2, kMotorLeft = 3, kFlags2 = 38;
constexpr uint8_t kFlag0Rumble = 0x01 | 0x02;    // compatible vibration, haptics select
constexpr uint8_t kFlag0Triggers = 0x04 | 0x08;  // right, left trigger effect
constexpr uint8_t kFlag1Lights = 0x04 | 0x10;    // lightbar, player LEDs
constexpr uint8_t kFlag2LightbarSetup = 0x02;
constexpr uint8_t kFlag2Rumble = 0x04;  // compatible vibration 2

}  // namespace

const std::vector<uint8_t>& reportDescriptor() { return kReportDescriptor; }

std::vector<uint8_t> deviceDescriptor() {
    return {
        18, 0x01,                     // device descriptor
        0x00, 0x02,                   // USB 2.0
        0x00, 0x00, 0x00,             // class per interface
        64,                           // ep0 max packet size
        kVendorId & 0xFF, kVendorId >> 8, kProductId & 0xFF, kProductId >> 8,
        kRelease & 0xFF, kRelease >> 8,
        1, 2, 3,                      // manufacturer, product, serial number strings
        1,                            // one configuration
    };
}

std::vector<uint8_t> hidDescriptor() {
    const size_t n = kReportDescriptor.size();
    return {9, 0x21, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(n & 0xFF), static_cast<uint8_t>(n >> 8)};
}

std::vector<uint8_t> configurationDescriptor() {
    std::vector<uint8_t> d = {
        9, 0x02, 0, 0,  // configuration, total length filled in below
        1,              // one interface
        1, 0,           // configuration value 1, no string
        0xC0, 0xFA,     // self powered, 500 mA (like the real controller)
        9, 0x04, 0, 0, 2, 0x03, 0x00, 0x00, 0,  // interface 0: HID, two endpoints
    };
    const auto hid = hidDescriptor();
    d.insert(d.end(), hid.begin(), hid.end());
    // Interrupt endpoints, 64 bytes, every microframe: reports go out as soon as EdgePad has them.
    const std::vector<uint8_t> endpoints = {7, 0x05, kInterruptInEndpoint, 0x03, 64, 0, 1,
                                            7, 0x05, kInterruptOutEndpoint, 0x03, 64, 0, 1};
    d.insert(d.end(), endpoints.begin(), endpoints.end());
    d[2] = static_cast<uint8_t>(d.size() & 0xFF);
    d[3] = static_cast<uint8_t>(d.size() >> 8);
    return d;
}

std::vector<uint8_t> stringDescriptor(uint8_t index) {
    switch (index) {
        case 0: return {4, 0x03, 0x09, 0x04};  // US English
        case 1: return usbString("Sony Interactive Entertainment");
        case 2: return usbString("DualSense Wireless Controller");
        case 3: return usbString(kSerialNumber);
        default: return {};
    }
}

size_t featureReportSize(uint8_t reportId) {
    static const std::map<uint8_t, size_t> sizes = reportSizes(0xB0);
    const auto it = sizes.find(reportId);
    return it == sizes.end() ? 0 : it->second;
}

std::vector<uint8_t> defaultFeatureReport(uint8_t reportId) {
    const size_t size = featureReportSize(reportId);
    std::vector<uint8_t> r(size, 0);
    if (size == 0) return r;
    r[0] = reportId;
    switch (reportId) {
        case 0x05: {  // calibration: 1/16 deg/s and 1/8192 g per count, no bias
            const int gyro[] = {8640, -8640, 8640, -8640, 8640, -8640, 540, 540};
            for (size_t i = 0; i < std::size(gyro); ++i) put16(&r[7 + 2 * i], gyro[i]);
            const int accel[] = {8192, -8192, 8192, -8192, 8192, -8192};
            for (size_t i = 0; i < std::size(accel); ++i) put16(&r[23 + 2 * i], accel[i]);
            return r;
        }
        case 0x09: {  // pairing info: the controller's MAC address, little endian
            const uint8_t mac[] = {0x01, 0x00, 0x00, 0x6E, 0xED, 0x02};  // 02:ED:6E:00:00:01, locally administered
            std::copy(std::begin(mac), std::end(mac), r.begin() + 1);
            return r;
        }
        case 0x20:  // firmware info: all zero is accepted
            return r;
        default:
            return {};
    }
}

std::array<uint8_t, kInputReportSize> buildInputReport(const OutputState& s) {
    std::array<uint8_t, kInputReportSize> r{};
    r[0] = dualsense::kUsbInputReportId;
    uint8_t* b = r.data() + 1;
    if (s.raw.valid) {
        std::copy(s.raw.body.begin(), s.raw.body.end(), b);
    } else {
        // No report from a real controller (demo): sensors, touchpad and battery from the state.
        for (size_t i = 0; i < 3; ++i) {
            put16(b + 15 + 2 * i, s.motion.gyro[i]);
            put16(b + 21 + 2 * i, s.motion.accel[i]);
        }
        for (int k = 0; k < 4; ++k) b[27 + k] = static_cast<uint8_t>((s.motion.timestamp >> (8 * k)) & 0xFF);
        encodeTouch(s.motion.touch[0], b + 32);
        encodeTouch(s.motion.touch[1], b + 36);
        const int level = s.battery < 0 ? 10 : std::clamp(s.battery / 10, 0, 10);
        b[52] = static_cast<uint8_t>(level | (s.charging ? 0x10 : 0x00));
    }

    b[0] = stickToRaw(s.lx);
    b[1] = stickToRaw(-s.ly);  // 0 = up
    b[2] = stickToRaw(s.rx);
    b[3] = stickToRaw(-s.ry);
    b[4] = triggerToRaw(s.l2);
    b[5] = triggerToRaw(s.r2);

    auto has_ = [&](Button button) { return has(s.buttons, button); };
    const bool up = has_(Button::DpadUp), down = has_(Button::DpadDown);
    const bool left = has_(Button::DpadLeft), right = has_(Button::DpadRight);
    uint8_t hat = 8;
    if (up && right) hat = 1;
    else if (down && right) hat = 3;
    else if (down && left) hat = 5;
    else if (up && left) hat = 7;
    else if (up) hat = 0;
    else if (right) hat = 2;
    else if (down) hat = 4;
    else if (left) hat = 6;
    b[7] = static_cast<uint8_t>(hat | (has_(Button::Square) ? 0x10 : 0) | (has_(Button::Cross) ? 0x20 : 0) |
                                (has_(Button::Circle) ? 0x40 : 0) | (has_(Button::Triangle) ? 0x80 : 0));
    b[8] = static_cast<uint8_t>((has_(Button::L1) ? 0x01 : 0) | (has_(Button::R1) ? 0x02 : 0) |
                                (has_(Button::L2) ? 0x04 : 0) | (has_(Button::R2) ? 0x08 : 0) |
                                (has_(Button::Create) ? 0x10 : 0) | (has_(Button::Options) ? 0x20 : 0) |
                                (has_(Button::L3) ? 0x40 : 0) | (has_(Button::R3) ? 0x80 : 0));
    // Byte 9: PS, touchpad click, mute. Bit 3 is passed on as is; bits 4-7 are the Edge's Fn and
    // back buttons, which a regular DualSense does not have.
    b[9] = static_cast<uint8_t>((b[9] & 0x08) | (has_(Button::PS) ? 0x01 : 0) | (has_(Button::Touchpad) ? 0x02 : 0) |
                                (has_(Button::Mute) ? 0x04 : 0));
    return r;
}

std::optional<std::array<uint8_t, dualsense::kOutputCommonSize>> filterGameOutput(const uint8_t* report, size_t size,
                                                                                  const OutputFilter& filter) {
    if (report == nullptr || size < 1 + dualsense::kOutputCommonSize || report[0] != dualsense::kUsbOutputReportId) {
        return std::nullopt;
    }
    std::array<uint8_t, dualsense::kOutputCommonSize> c{};
    std::copy(report + 1, report + 1 + c.size(), c.begin());
    if (!filter.rumble) {
        c[kFlags0] &= static_cast<uint8_t>(~kFlag0Rumble);
        c[kFlags2] &= static_cast<uint8_t>(~kFlag2Rumble);
        c[kMotorRight] = 0;
        c[kMotorLeft] = 0;
    } else if (filter.rumbleStrength < 1.0f) {
        const float s = std::max(0.0f, filter.rumbleStrength);
        c[kMotorRight] = static_cast<uint8_t>(static_cast<float>(c[kMotorRight]) * s + 0.5f);
        c[kMotorLeft] = static_cast<uint8_t>(static_cast<float>(c[kMotorLeft]) * s + 0.5f);
    }
    if (!filter.triggers) c[kFlags0] &= static_cast<uint8_t>(~kFlag0Triggers);
    if (!filter.lights) {
        c[kFlags1] &= static_cast<uint8_t>(~kFlag1Lights);
        c[kFlags2] &= static_cast<uint8_t>(~kFlag2LightbarSetup);
    }
    if (c[kFlags0] == 0 && c[kFlags1] == 0 && c[kFlags2] == 0) return std::nullopt;
    return c;
}

uint8_t partsChanged(const std::array<uint8_t, dualsense::kOutputCommonSize>& c) {
    uint8_t parts = 0;
    if ((c[kFlags0] & kFlag0Rumble) || (c[kFlags2] & kFlag2Rumble)) parts |= dualsense::kPartRumble;
    if (c[kFlags0] & kFlag0Triggers) parts |= dualsense::kPartTriggers;
    if ((c[kFlags1] & kFlag1Lights) || (c[kFlags2] & kFlag2LightbarSetup)) parts |= dualsense::kPartLights;
    return parts;
}

}  // namespace edgepad::virtual_dualsense
