#include "core/virtual_reports.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/axis.hpp"
#include "core/dualsense.hpp"

namespace edgepad::virtual_reports {
namespace {

int16_t le16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }

void put16(uint8_t* p, int v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

// ViGEmBus's answer to a GET_REPORT for feature report 0x02 (Ds4Pdo.cpp), i.e. what games read as
// the virtual DualShock 4's calibration.
constexpr uint8_t kVirtualDs4CalibrationReport[] = {
    0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87, 0x22, 0x7B, 0xDD, 0xB2, 0x22, 0x47, 0xDD, 0xBD, 0x22, 0x43, 0xDD,
    0x1C, 0x02, 0x1C, 0x02, 0x7F, 0x1E, 0x2E, 0xDF, 0x60, 0x1F, 0x4C, 0xE0, 0x3A, 0x1D, 0xC6, 0xDE, 0x08, 0x00,
};

// Accelerometer calibration, shared by both controllers: bias = plus - range / 2, 2 g over the range.
void accelAxis(MotionCalibration& c, int axis, int16_t plus, int16_t minus) {
    const auto range = static_cast<int16_t>(plus - minus);
    c.bias[axis] = static_cast<float>(plus - range / 2);
    c.scale[axis] = range != 0 ? 2.0f / static_cast<float>(range) : 0.0f;
}

// SDL rejects a calibration (and uses the nominal scale) if any axis looks broken.
bool plausible(const MotionCalibration& c) {
    const MotionCalibration nominal = nominalCalibration();
    for (size_t i = 0; i < 6; ++i) {
        if (std::fabs(c.bias[i]) > 1024.0f) return false;
        const float ratio = c.scale[i] / nominal.scale[i];
        if (!(std::fabs(1.0f - ratio) <= 0.5f)) return false;  // also catches NaN / infinity
    }
    return true;
}

MotionCalibration parseVirtualDs4Calibration() {
    const uint8_t* d = kVirtualDs4CalibrationReport;
    MotionCalibration c;
    const int16_t bias[3] = {le16(d + 1), le16(d + 3), le16(d + 5)};
    // A wired DualShock 4 lists plus / minus per axis: pitch+, pitch-, yaw+, yaw-, roll+, roll-.
    const int16_t plus[3] = {le16(d + 7), le16(d + 11), le16(d + 15)};
    const int16_t minus[3] = {le16(d + 9), le16(d + 13), le16(d + 17)};
    const float speed = static_cast<float>(le16(d + 19) + le16(d + 21));
    for (int i = 0; i < 3; ++i) {
        const float range = static_cast<float>(std::abs(plus[i] - bias[i]) + std::abs(minus[i] - bias[i]));
        c.bias[i] = bias[i];
        c.scale[i] = range != 0.0f ? speed / range : 0.0f;
    }
    accelAxis(c, 3, le16(d + 23), le16(d + 25));
    accelAxis(c, 4, le16(d + 27), le16(d + 29));
    accelAxis(c, 5, le16(d + 31), le16(d + 33));
    return plausible(c) ? c : nominalCalibration();
}

}  // namespace

MotionCalibration nominalCalibration() {
    MotionCalibration c;
    for (size_t i = 0; i < 3; ++i) c.scale[i] = 1.0f / 16.0f;
    for (size_t i = 3; i < 6; ++i) c.scale[i] = 1.0f / 8192.0f;
    return c;
}

MotionCalibration parseDualSenseCalibration(const uint8_t* report, size_t length) {
    if (report == nullptr || length < 35 || report[0] != dualsense::kCalibrationFeatureReportId) {
        return nominalCalibration();
    }
    const uint8_t* d = report;
    MotionCalibration c;
    const int16_t bias[3] = {le16(d + 1), le16(d + 3), le16(d + 5)};
    const int16_t plus[3] = {le16(d + 7), le16(d + 11), le16(d + 15)};
    const int16_t minus[3] = {le16(d + 9), le16(d + 13), le16(d + 17)};
    const float speed = static_cast<float>(le16(d + 19) + le16(d + 21));
    for (int i = 0; i < 3; ++i) {
        const int range = plus[i] - minus[i];
        c.bias[i] = bias[i];
        c.scale[i] = range != 0 ? speed / static_cast<float>(range) : 0.0f;
    }
    accelAxis(c, 3, le16(d + 23), le16(d + 25));
    accelAxis(c, 4, le16(d + 27), le16(d + 29));
    accelAxis(c, 5, le16(d + 31), le16(d + 33));
    return plausible(c) ? c : nominalCalibration();
}

const MotionCalibration& virtualDs4Calibration() {
    static const MotionCalibration calibration = parseVirtualDs4Calibration();
    return calibration;
}

MotionState remapMotion(const MotionState& motion, const MotionCalibration& from, const MotionCalibration& to) {
    MotionState out = motion;
    auto convert = [&](int16_t raw, size_t i) {
        if (!(to.scale[i] != 0.0f)) return raw;
        const float physical = (static_cast<float>(raw) - from.bias[i]) * from.scale[i];
        const float value = physical / to.scale[i] + to.bias[i];
        return static_cast<int16_t>(std::clamp<long>(std::lround(value), -32768, 32767));
    };
    for (size_t i = 0; i < 3; ++i) {
        out.gyro[i] = convert(motion.gyro[i], i);
        out.accel[i] = convert(motion.accel[i], i + 3);
    }
    return out;
}

std::array<uint8_t, kDs4ReportSize> buildDs4Report(const OutputState& s, uint8_t counter) {
    std::array<uint8_t, kDs4ReportSize> r{};
    r[0] = stickToRaw(s.lx);
    r[1] = stickToRaw(-s.ly);  // DualShock 4: 0 = up
    r[2] = stickToRaw(s.rx);
    r[3] = stickToRaw(-s.ry);

    const bool up = has(s.buttons, Button::DpadUp), down = has(s.buttons, Button::DpadDown);
    const bool left = has(s.buttons, Button::DpadLeft), right = has(s.buttons, Button::DpadRight);
    uint16_t buttons = ds4::kHatNone;
    if (up && right) buttons = 1;
    else if (down && right) buttons = 3;
    else if (down && left) buttons = 5;
    else if (up && left) buttons = 7;
    else if (up) buttons = 0;
    else if (right) buttons = 2;
    else if (down) buttons = 4;
    else if (left) buttons = 6;
    auto set = [&](Button b, uint16_t flag) {
        if (has(s.buttons, b)) buttons = static_cast<uint16_t>(buttons | flag);
    };
    set(Button::Square, ds4::kSquare);
    set(Button::Cross, ds4::kCross);
    set(Button::Circle, ds4::kCircle);
    set(Button::Triangle, ds4::kTriangle);
    set(Button::L1, ds4::kL1);
    set(Button::R1, ds4::kR1);
    set(Button::L2, ds4::kL2);  // the pipeline keeps these in step with the analog triggers
    set(Button::R2, ds4::kR2);
    set(Button::Create, ds4::kShare);
    set(Button::Options, ds4::kOptions);
    set(Button::L3, ds4::kL3);
    set(Button::R3, ds4::kR3);
    put16(&r[4], buttons);

    uint8_t special = static_cast<uint8_t>(counter << 2);
    if (has(s.buttons, Button::PS)) special |= ds4::kPs;
    if (has(s.buttons, Button::Touchpad)) special |= ds4::kTouchpadClick;
    r[6] = special;
    r[7] = triggerToRaw(s.l2);
    r[8] = triggerToRaw(s.r2);

    // DualSense sensor clock ticks every 1/3 us, the DualShock 4 one every 16/3 us.
    put16(&r[9], static_cast<int>(s.motion.timestamp / 16));
    for (size_t i = 0; i < 3; ++i) {
        put16(&r[12 + 2 * i], s.motion.gyro[i]);
        put16(&r[18 + 2 * i], s.motion.accel[i]);
    }
    // Battery 0..10 in the low nibble, 0x10 = cable connected (the virtual pad is "wired").
    const int level = s.battery < 0 ? 10 : std::clamp(s.battery / 10, 0, 10);
    r[29] = static_cast<uint8_t>(0x10 | level);

    // Touchpad: one packet with both fingers; the unused history slots say "not touching".
    r[32] = 1;
    r[33] = counter;
    for (size_t f = 0; f < 2; ++f) {
        const auto t = dualsense::encodeDs4Touch(s.motion.touch[f]);
        std::copy(t.begin(), t.end(), r.begin() + 34 + 4 * static_cast<std::ptrdiff_t>(f));
    }
    for (size_t slot : {43u, 47u, 52u, 56u}) r[slot] = 0x80;
    return r;
}

XusbReport buildXusbReport(const OutputState& s) {
    XusbReport r;
    auto set = [&](Button b, uint16_t flag) {
        if (has(s.buttons, b)) r.buttons = static_cast<uint16_t>(r.buttons | flag);
    };
    set(Button::Cross, xusb::kA);
    set(Button::Circle, xusb::kB);
    set(Button::Square, xusb::kX);
    set(Button::Triangle, xusb::kY);
    set(Button::L1, xusb::kLeftShoulder);
    set(Button::R1, xusb::kRightShoulder);
    set(Button::L3, xusb::kLeftThumb);
    set(Button::R3, xusb::kRightThumb);
    set(Button::Create, xusb::kBack);
    set(Button::Touchpad, xusb::kBack);
    set(Button::Options, xusb::kStart);
    set(Button::PS, xusb::kGuide);
    set(Button::DpadUp, xusb::kDpadUp);
    set(Button::DpadDown, xusb::kDpadDown);
    set(Button::DpadLeft, xusb::kDpadLeft);
    set(Button::DpadRight, xusb::kDpadRight);
    r.leftTrigger = triggerToRaw(s.l2);
    r.rightTrigger = triggerToRaw(s.r2);
    r.thumbLX = stickToThumb(s.lx);
    r.thumbLY = stickToThumb(s.ly);
    r.thumbRX = stickToThumb(s.rx);
    r.thumbRY = stickToThumb(s.ry);
    return r;
}

}  // namespace edgepad::virtual_reports
