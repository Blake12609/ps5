#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/axis.hpp"
#include "core/dualsense.hpp"
#include "core/pipeline.hpp"
#include "core/virtual_reports.hpp"

using namespace edgepad;
using namespace edgepad::virtual_reports;

namespace {

// A DualSense USB input report (report id + 63 byte body) with neutral sticks and d-pad.
std::array<uint8_t, 64> usbReport() {
    std::array<uint8_t, 64> r{};
    r[0] = dualsense::kUsbInputReportId;
    r[1] = r[2] = r[3] = r[4] = 128;
    r[8] = 0x08;  // hat: neutral
    return r;
}

InputState parse(const std::array<uint8_t, 64>& report) {
    const auto parsed = dualsense::parseInputReport(report.data(), report.size());
    REQUIRE(parsed);
    return parsed->state;
}

Config defaults() {
    Config cfg = Config::defaults();
    cfg.normalize();
    return cfg;
}

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void putLe16(std::vector<uint8_t>& d, size_t at, int v) {
    d[at] = static_cast<uint8_t>(v & 0xFF);
    d[at + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

// A plausible DualSense calibration report (feature 0x05, 41 bytes).
std::vector<uint8_t> dualSenseCalibration() {
    std::vector<uint8_t> d(41, 0);
    d[0] = dualsense::kCalibrationFeatureReportId;
    const int values[] = {-3,   5,     2,           // gyro bias pitch, yaw, roll
                          8860, -8850, 8790, -8805, 8920, -8912,  // gyro plus / minus per axis
                          540,  540,                // gyro speed plus / minus
                          8210, -8170, 8150, -8240, 8060, -8330};  // accel plus / minus per axis
    for (size_t i = 0; i < std::size(values); ++i) putLe16(d, 1 + 2 * i, values[i]);
    return d;
}

float physical(int16_t raw, const MotionCalibration& c, size_t axis) {
    return (static_cast<float>(raw) - c.bias[axis]) * c.scale[axis];
}

}  // namespace

TEST_CASE("DualShock 4 report: every stick and trigger byte reaches the game unchanged") {
    Config cfg = defaults();
    Pipeline pipeline;
    for (int a = 0; a < 256; ++a) {
        const int b = 255 - a;
        const int c = (a * 7) % 256;
        auto report = usbReport();
        report[1] = static_cast<uint8_t>(a);  // left X
        report[2] = static_cast<uint8_t>(b);  // left Y
        report[3] = static_cast<uint8_t>(c);  // right X
        report[4] = static_cast<uint8_t>(a);  // right Y
        report[5] = static_cast<uint8_t>(c);  // L2
        report[6] = static_cast<uint8_t>(b);  // R2
        const auto out = buildDs4Report(pipeline.process(parse(report), cfg, 0.004f), 0);
        CHECK(out[0] == a);
        CHECK(out[1] == b);
        CHECK(out[2] == c);
        CHECK(out[3] == a);
        CHECK(out[7] == c);
        CHECK(out[8] == b);
    }
}

TEST_CASE("DualShock 4 report: buttons, d-pad, PS / touchpad and the report counter") {
    Config cfg = defaults();
    Pipeline pipeline;
    struct Case {
        uint8_t b8, b9, b10;  // DualSense body bytes 7, 8, 9
        uint16_t buttons;     // expected DS4 bytes 4-5
        uint8_t special;      // expected DS4 byte 6 (counter 0)
    };
    const Case cases[] = {
        {0x18, 0, 0, ds4::kHatNone | ds4::kSquare, 0},
        {0x28, 0, 0, ds4::kHatNone | ds4::kCross, 0},
        {0x48, 0, 0, ds4::kHatNone | ds4::kCircle, 0},
        {0x88, 0, 0, ds4::kHatNone | ds4::kTriangle, 0},
        {0x08, 0x01, 0, ds4::kHatNone | ds4::kL1, 0},
        {0x08, 0x02, 0, ds4::kHatNone | ds4::kR1, 0},
        {0x08, 0x10, 0, ds4::kHatNone | ds4::kShare, 0},
        {0x08, 0x20, 0, ds4::kHatNone | ds4::kOptions, 0},
        {0x08, 0x40, 0, ds4::kHatNone | ds4::kL3, 0},
        {0x08, 0x80, 0, ds4::kHatNone | ds4::kR3, 0},
        {0x08, 0, 0x01, ds4::kHatNone, ds4::kPs},
        {0x08, 0, 0x02, ds4::kHatNone, ds4::kTouchpadClick},
    };
    for (const Case& c : cases) {
        auto report = usbReport();
        report[8] = c.b8;
        report[9] = c.b9;
        report[10] = c.b10;
        const auto out = buildDs4Report(pipeline.process(parse(report), cfg, 0.004f), 0);
        CHECK(le16(&out[4]) == c.buttons);
        CHECK(out[6] == c.special);
    }
    // Every d-pad direction keeps its hat value (0 = up, clockwise).
    for (uint8_t hat = 0; hat <= 8; ++hat) {
        auto report = usbReport();
        report[8] = hat;
        const auto out = buildDs4Report(pipeline.process(parse(report), cfg, 0.004f), 0);
        CHECK((out[4] & 0x0F) == hat);
    }
    // The counter sits in the upper six bits of byte 6, next to PS / touchpad.
    OutputState s;
    s.buttons = bit(Button::PS);
    CHECK(buildDs4Report(s, 0x2A)[6] == ((0x2A << 2) | ds4::kPs));
}

TEST_CASE("L2 / R2 digital bits: the controller's own while the trigger is 1:1") {
    Config cfg = defaults();
    Pipeline pipeline;
    auto report = usbReport();
    report[5] = 1;  // L2 barely touched, controller says "not pressed"
    report[6] = 200;
    report[9] = 0x08;  // R2 pressed
    auto out = buildDs4Report(pipeline.process(parse(report), cfg, 0.004f), 0);
    CHECK((le16(&out[4]) & ds4::kL2) == 0);
    CHECK((le16(&out[4]) & ds4::kR2) != 0);
    report[9] = 0x04;  // now the controller reports L2 (and not R2)
    out = buildDs4Report(pipeline.process(parse(report), cfg, 0.004f), 0);
    CHECK((le16(&out[4]) & ds4::kL2) != 0);
    CHECK((le16(&out[4]) & ds4::kR2) == 0);

    // A processed trigger (hair trigger) gets a bit that matches what the game gets.
    cfg.profiles[0].r2.mode = TriggerMode::HairTrigger;
    report[9] = 0x00;
    out = buildDs4Report(pipeline.process(parse(report), cfg, 0.004f), 0);
    CHECK(out[8] == 255);
    CHECK((le16(&out[4]) & ds4::kR2) != 0);
    report[6] = 0;
    report[9] = 0x08;  // the controller's bit lags behind; the game sees a release
    out = buildDs4Report(pipeline.process(parse(report), cfg, 0.004f), 0);
    CHECK(out[8] == 0);
    CHECK((le16(&out[4]) & ds4::kR2) == 0);
}

TEST_CASE("DualShock 4 report: timestamp, motion, battery and touchpad layout") {
    OutputState s;
    s.motion.timestamp = 16 * 0x1234 + 7;
    s.motion.gyro = {-2, 300, -32768};
    s.motion.accel = {8192, -1, 32767};
    s.motion.touch[0] = TouchPoint{true, 5, 1919, 1079};
    s.battery = 75;
    const auto r = buildDs4Report(s, 9);
    CHECK(le16(&r[9]) == 0x1234);  // 1/3 us DualSense ticks -> 16/3 us DualShock 4 ticks
    CHECK(static_cast<int16_t>(le16(&r[12])) == -2);
    CHECK(static_cast<int16_t>(le16(&r[14])) == 300);
    CHECK(static_cast<int16_t>(le16(&r[16])) == -32768);
    CHECK(static_cast<int16_t>(le16(&r[18])) == 8192);
    CHECK(static_cast<int16_t>(le16(&r[20])) == -1);
    CHECK(static_cast<int16_t>(le16(&r[22])) == 32767);
    CHECK(r[29] == (0x10 | 7));
    CHECK(r[32] == 1);
    CHECK(r[33] == 9);
    const auto t0 = dualsense::encodeDs4Touch(s.motion.touch[0]);
    const auto t1 = dualsense::encodeDs4Touch(s.motion.touch[1]);
    CHECK(std::memcmp(&r[34], t0.data(), 4) == 0);
    CHECK(std::memcmp(&r[38], t1.data(), 4) == 0);
    CHECK(r[38] == 0x80);  // second finger: not touching
    for (size_t slot : {43u, 47u, 52u, 56u}) CHECK(r[slot] == 0x80);

    // The sensor clock wraps around without a jump.
    OutputState a, b;
    a.motion.timestamp = 0xFFFFFFF0u;
    b.motion.timestamp = 0x00000010u;
    const uint16_t ta = le16(&buildDs4Report(a, 0)[9]);
    const uint16_t tb = le16(&buildDs4Report(b, 0)[9]);
    CHECK(static_cast<uint16_t>(tb - ta) == 2);
}

TEST_CASE("Xbox 360 report: buttons, sticks and triggers") {
    Config cfg = defaults();
    Pipeline pipeline;
    auto report = usbReport();
    report[1] = 0;
    report[2] = 0;  // left stick up-left
    report[3] = 255;
    report[4] = 255;  // right stick down-right
    report[5] = 255;
    report[6] = 1;
    report[8] = 0xF0 | 0x01;  // all face buttons, d-pad up-right
    report[9] = 0x01 | 0x02 | 0x10 | 0x20 | 0x40 | 0x80;
    report[10] = 0x01;
    const XusbReport x = buildXusbReport(pipeline.process(parse(report), cfg, 0.004f));
    CHECK(x.thumbLX == -32768);
    CHECK(x.thumbLY == 32767);
    CHECK(x.thumbRX == 32767);
    CHECK(x.thumbRY == -32768);
    CHECK(x.leftTrigger == 255);
    CHECK(x.rightTrigger == 1);
    CHECK(x.buttons == (xusb::kA | xusb::kB | xusb::kX | xusb::kY | xusb::kDpadUp | xusb::kDpadRight |
                        xusb::kLeftShoulder | xusb::kRightShoulder | xusb::kBack | xusb::kStart | xusb::kLeftThumb |
                        xusb::kRightThumb | xusb::kGuide));
    const XusbReport idle = buildXusbReport(pipeline.process(parse(usbReport()), cfg, 0.004f));
    CHECK(idle == XusbReport{});
}

TEST_CASE("virtual DualShock 4 calibration is read like SDL reads a wired DualShock 4") {
    const MotionCalibration& v = virtualDs4Calibration();
    CHECK(v.bias[0] == 1.0f);
    CHECK(v.bias[1] == 0.0f);
    CHECK(v.bias[2] == 0.0f);
    CHECK(v.scale[0] == doctest::Approx(1080.0f / (8838.0f + 8838.0f)));
    CHECK(v.scale[1] == doctest::Approx(1080.0f / (8882.0f + 8889.0f)));
    CHECK(v.scale[2] == doctest::Approx(1080.0f / (8893.0f + 8893.0f)));
    CHECK(v.bias[3] == -297.0f);
    CHECK(v.bias[4] == -42.0f);
    CHECK(v.bias[5] == -512.0f);
    CHECK(v.scale[3] == doctest::Approx(2.0f / 16209.0f));
    CHECK(v.scale[4] == doctest::Approx(2.0f / 16148.0f));
    CHECK(v.scale[5] == doctest::Approx(2.0f / 15988.0f));
}

TEST_CASE("motion reaches the game as the same degrees per second and g") {
    const auto bytes = dualSenseCalibration();
    const MotionCalibration ds = parseDualSenseCalibration(bytes.data(), bytes.size());
    CHECK(ds.bias[0] == -3.0f);
    CHECK(ds.scale[0] == doctest::Approx(1080.0f / (8860.0f + 8850.0f)));
    CHECK(ds.bias[3] == static_cast<float>(8210 - 16380 / 2));
    const MotionCalibration& v = virtualDs4Calibration();
    for (int raw = -30000; raw <= 30000; raw += 37) {
        MotionState m;
        for (size_t i = 0; i < 3; ++i) {
            m.gyro[i] = static_cast<int16_t>(raw);
            m.accel[i] = static_cast<int16_t>(raw / 3);
        }
        const MotionState out = remapMotion(m, ds, v);
        for (size_t i = 0; i < 3; ++i) {
            // Within half a count of the virtual controller's resolution: the closest value possible.
            CHECK(std::fabs(physical(out.gyro[i], v, i) - physical(m.gyro[i], ds, i)) <= 0.5f * v.scale[i] + 1e-3f);
            CHECK(std::fabs(physical(out.accel[i], v, i + 3) - physical(m.accel[i], ds, i + 3)) <=
                  0.5f * v.scale[i + 3] + 1e-6f);
        }
    }
    // At rest the game sees no rotation.
    MotionState rest;
    rest.gyro = {-3, 5, 2};
    const MotionState restOut = remapMotion(rest, ds, v);
    for (size_t i = 0; i < 3; ++i) CHECK(std::fabs(physical(restOut.gyro[i], v, i)) <= 0.5f * v.scale[i]);
    // Converting to the same calibration changes nothing.
    MotionState same;
    same.gyro = {123, -456, 789};
    same.accel = {8000, -8000, 1};
    CHECK(remapMotion(same, ds, ds) == same);
}

TEST_CASE("a missing or broken DualSense calibration falls back to the nominal scale") {
    const MotionCalibration nominal = nominalCalibration();
    CHECK(parseDualSenseCalibration(nullptr, 0).scale == nominal.scale);
    auto bytes = dualSenseCalibration();
    CHECK(parseDualSenseCalibration(bytes.data(), 20).scale == nominal.scale);  // short read
    putLe16(bytes, 7, 0);
    putLe16(bytes, 9, 0);  // pitch plus == minus: division by zero
    CHECK(parseDualSenseCalibration(bytes.data(), bytes.size()).scale == nominal.scale);
    bytes = dualSenseCalibration();
    putLe16(bytes, 1, 2000);  // absurd bias
    CHECK(parseDualSenseCalibration(bytes.data(), bytes.size()).bias == nominal.bias);
    bytes = dualSenseCalibration();
    bytes[0] = 0x09;  // a different report
    CHECK(parseDualSenseCalibration(bytes.data(), bytes.size()).scale == nominal.scale);
}
