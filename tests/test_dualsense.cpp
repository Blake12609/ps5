#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <string>

#include "core/dualsense.hpp"

using namespace edgepad;
using namespace edgepad::dualsense;

namespace {

// Common input report body shared by USB and Bluetooth full reports.
std::array<uint8_t, 63> sampleBody() {
    std::array<uint8_t, 63> body{};
    body[0] = 0;       // left X: full left
    body[1] = 128;     // left Y: centred
    body[2] = 255;     // right X: full right
    body[3] = 0;       // right Y: full up
    body[4] = 255;     // L2
    body[5] = 0;       // R2
    body[7] = 0x22;    // hat = right, cross
    body[8] = 0x21;    // L1, Options
    body[9] = 0x65;    // PS, Mute, right Fn, left paddle
    body[52] = 0x17;   // charging, level 7
    return body;
}

void checkSample(const ParsedReport& r) {
    const InputState& s = r.state;
    CHECK(s.lx == doctest::Approx(-1.0f));
    CHECK(s.ly == doctest::Approx(0.0f));
    CHECK(s.rx == doctest::Approx(1.0f));
    CHECK(s.ry == doctest::Approx(1.0f));
    CHECK(s.l2 == doctest::Approx(1.0f));
    CHECK(s.r2 == doctest::Approx(0.0f));
    const ButtonMask expected = bit(Button::DpadRight) | bit(Button::Cross) | bit(Button::L1) | bit(Button::Options) |
                                bit(Button::PS) | bit(Button::Mute) | bit(Button::FnRight) | bit(Button::PaddleLeft);
    CHECK(s.buttons == expected);
    CHECK(s.battery == 75);
    CHECK(s.charging);
}

uint32_t readLe32(const uint8_t* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

}  // namespace

TEST_CASE("USB input report") {
    std::array<uint8_t, 64> report{};
    report[0] = kUsbInputReportId;
    const auto body = sampleBody();
    std::memcpy(report.data() + 1, body.data(), 63);
    const auto parsed = parseInputReport(report.data(), report.size());
    REQUIRE(parsed);
    CHECK(parsed->connection == Connection::Usb);
    checkSample(*parsed);
}

TEST_CASE("Bluetooth full input report") {
    std::array<uint8_t, 78> report{};
    report[0] = kBtInputReportId;
    report[1] = 0x01;
    const auto body = sampleBody();
    std::memcpy(report.data() + 2, body.data(), 63);
    const auto parsed = parseInputReport(report.data(), report.size());
    REQUIRE(parsed);
    CHECK(parsed->connection == Connection::Bluetooth);
    checkSample(*parsed);
}

TEST_CASE("Bluetooth simple input report") {
    const std::array<uint8_t, 10> report{0x01, 128, 128, 128, 128, 0x77, 0x02, 0x00, 0, 255};
    const auto parsed = parseInputReport(report.data(), report.size());
    REQUIRE(parsed);
    CHECK(parsed->connection == Connection::Bluetooth);
    CHECK(parsed->state.buttons == (bit(Button::DpadUp) | bit(Button::DpadLeft) | bit(Button::Circle) |
                                    bit(Button::Square) | bit(Button::Cross) | bit(Button::R1)));
    CHECK(parsed->state.r2 == doctest::Approx(1.0f));
}

TEST_CASE("Bluetooth simple report padded to 78 bytes (Windows) is not mistaken for USB") {
    std::array<uint8_t, 78> report{};
    report[0] = 0x01;
    report[1] = report[2] = report[3] = report[4] = 128;
    report[5] = 0x24;  // hat = down, cross
    report[9] = 200;   // R2
    const auto parsed = parseInputReport(report.data(), report.size());
    REQUIRE(parsed);
    CHECK(parsed->connection == Connection::Bluetooth);
    CHECK(parsed->state.buttons == (bit(Button::DpadDown) | bit(Button::Cross)));
    CHECK(parsed->state.r2 == doctest::Approx(200.0f / 255.0f));
}

TEST_CASE("unknown or empty reports are ignored") {
    const std::array<uint8_t, 4> junk{0x42, 1, 2, 3};
    CHECK_FALSE(parseInputReport(junk.data(), junk.size()));
    CHECK_FALSE(parseInputReport(nullptr, 0));
}

TEST_CASE("CRC-32 check value") {
    const char* text = "123456789";
    CHECK(crc32(reinterpret_cast<const uint8_t*>(text), 9) == 0xCBF43926u);
    // Chaining gives the same result as one pass.
    const uint32_t part = crc32(reinterpret_cast<const uint8_t*>(text), 4);
    CHECK(crc32(reinterpret_cast<const uint8_t*>(text) + 4, 5, part) == 0xCBF43926u);
}

TEST_CASE("USB output report layout") {
    Effects fx;
    fx.lightbar = {10, 20, 30};
    fx.playerLeds = 0x15;
    fx.rumbleLeft = 200;
    fx.rumbleRight = 100;
    fx.rightTrigger = triggerEffectWall(0.5f, 8);
    const auto r = buildOutputReport(fx, Connection::Usb, 0, false);
    REQUIRE(r.size() == kUsbOutputReportSize);
    CHECK(r[0] == kUsbOutputReportId);
    CHECK(r[1] == 0x0F);  // vibration, haptics, both trigger effects
    CHECK(r[2] == 0x14);  // lightbar + player LEDs
    CHECK(r[3] == 100);
    CHECK(r[4] == 200);
    CHECK(r[1 + 10] == 0x21);
    CHECK(r[1 + 21] == 0x05);
    CHECK(r[1 + 43] == 0x15);
    CHECK(r[1 + 44] == 10);
    CHECK(r[1 + 45] == 20);
    CHECK(r[1 + 46] == 30);
}

TEST_CASE("Bluetooth output report has sequence, tag and CRC") {
    Effects fx;
    fx.lightbar = {1, 2, 3};
    const auto r = buildOutputReport(fx, Connection::Bluetooth, 5, false);
    REQUIRE(r.size() == kBtOutputReportSize);
    CHECK(r[0] == kBtOutputReportId);
    CHECK(r[1] == 0x50);
    CHECK(r[2] == 0x10);
    CHECK(r[3 + 44] == 1);
    CHECK(r[3 + 46] == 3);
    const uint8_t seed = 0xA2;
    const uint32_t expected = crc32(r.data(), 74, crc32(&seed, 1));
    CHECK(readLe32(r.data() + 74) == expected);
}

TEST_CASE("lightbar setup report only releases the lightbar") {
    const auto r = buildOutputReport(Effects{}, Connection::Usb, 0, true);
    CHECK(r[1] == 0);
    CHECK(r[1 + 38] == 0x02);
    CHECK(r[1 + 41] == 0x02);
}

TEST_CASE("adaptive trigger wall effect bytes") {
    const TriggerEffect e = triggerEffectWall(0.5f, 8);
    CHECK(e[0] == 0x21);
    CHECK(e[1] == 0xE0);  // zones 5-9 active
    CHECK(e[2] == 0x03);
    CHECK(e[3] == 0x00);  // force 7 in zones 5-9
    CHECK(e[4] == 0x80);
    CHECK(e[5] == 0xFF);
    CHECK(e[6] == 0x3F);
    CHECK(triggerEffectOff()[0] == 0x05);
    TriggerSettings off;
    CHECK(triggerEffectFor(off) == triggerEffectOff());
}
