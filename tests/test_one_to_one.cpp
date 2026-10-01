#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>

#include "core/axis.hpp"
#include "core/config_json.hpp"
#include "core/device_path.hpp"
#include "core/dualsense.hpp"
#include "core/pipeline.hpp"

using namespace edgepad;

namespace {

InputState parseSticks(uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry, uint8_t l2 = 0, uint8_t r2 = 0) {
    std::array<uint8_t, 64> report{};
    report[0] = dualsense::kUsbInputReportId;
    report[1] = lx;
    report[2] = ly;
    report[3] = rx;
    report[4] = ry;
    report[5] = l2;
    report[6] = r2;
    report[8] = 0x08;  // d-pad neutral
    const auto parsed = dualsense::parseInputReport(report.data(), report.size());
    REQUIRE(parsed);
    return parsed->state;
}

}  // namespace

TEST_CASE("raw stick values convert exactly and back") {
    CHECK(stickFromRaw(0) == -1.0f);
    CHECK(stickFromRaw(128) == 0.0f);
    CHECK(stickFromRaw(255) == 1.0f);
    for (int raw = 0; raw < 256; ++raw) {
        const auto r = static_cast<uint8_t>(raw);
        CHECK(stickToRaw(stickFromRaw(r)) == r);
        CHECK(triggerToRaw(triggerFromRaw(r)) == r);
        if (raw > 0) CHECK(stickToThumb(stickFromRaw(r)) > stickToThumb(stickFromRaw(static_cast<uint8_t>(raw - 1))));
    }
    CHECK(stickToThumb(-1.0f) == -32768);
    CHECK(stickToThumb(0.0f) == 0);
    CHECK(stickToThumb(1.0f) == 32767);
    CHECK(stickToRaw(std::nanf("")) == 128);
    CHECK(stickToThumb(2.0f) == 32767);
    CHECK(triggerToRaw(-0.5f) == 0);
}

TEST_CASE("default settings pass every stick and trigger value through 1:1") {
    Config cfg = Config::defaults();
    cfg.normalize();
    REQUIRE(isOneToOne(cfg.active().leftStick));
    REQUIRE(isOneToOne(cfg.active().rightStick));
    Pipeline pipeline;
    // Every raw value on both axes, including the corners (which reach past a radius of 1).
    for (int x = 0; x < 256; x += 3) {
        for (int y = 0; y < 256; y += 5) {
            const auto rx = static_cast<uint8_t>(x);
            const auto ry = static_cast<uint8_t>(y);
            const InputState in = parseSticks(rx, ry, ry, rx, rx, ry);
            const OutputState out = pipeline.process(in, cfg, 0.004f);
            // DualShock 4 output: the same bytes the controller sent (DS4 Y is also 0 = up).
            CHECK(stickToRaw(out.lx) == rx);
            CHECK(stickToRaw(-out.ly) == ry);
            CHECK(stickToRaw(out.rx) == ry);
            CHECK(stickToRaw(-out.ry) == rx);
            CHECK(triggerToRaw(out.l2) == rx);
            CHECK(triggerToRaw(out.r2) == ry);
        }
    }
    // Xbox output reaches both ends exactly.
    const OutputState full = pipeline.process(parseSticks(0, 0, 255, 255), cfg, 0.004f);
    CHECK(stickToThumb(full.lx) == -32768);
    CHECK(stickToThumb(full.ly) == 32767);  // up
    CHECK(stickToThumb(full.rx) == 32767);
    CHECK(stickToThumb(full.ry) == -32768);  // down
}

TEST_CASE("diagonals past full throw are not pulled in") {
    StickSettings s;
    const Vec2 corner = processStick(0.85f, 0.8f, s);
    CHECK(corner.x == doctest::Approx(0.85f));
    CHECK(corner.y == doctest::Approx(0.8f));
    // With a dead zone the stick still reaches full output and keeps its corners.
    s.deadzone = 0.1f;
    const Vec2 edge = processStick(1.0f, 0.0f, s);
    CHECK(edge.x == doctest::Approx(1.0f));
    const Vec2 dz = processStick(0.85f, 0.8f, s);
    CHECK(dz.x == doctest::Approx(0.85f));
    CHECK(dz.y == doctest::Approx(0.8f));
}

TEST_CASE("idle gyro aiming leaves the right stick alone") {
    Config cfg = Config::defaults();
    cfg.profiles[0].gyro.activation = GyroActivation::Always;
    cfg.normalize();
    Pipeline pipeline;
    const OutputState out = pipeline.process(parseSticks(128, 128, 250, 8), cfg, 0.004f);
    CHECK(stickToRaw(out.rx) == 250);
    CHECK(stickToRaw(-out.ry) == 8);
}

TEST_CASE("the FPS profile's anti-dead zone comes without a dead zone") {
    const Config cfg = Config::defaults();
    const Profile& fps = cfg.profiles[1];
    CHECK(fps.rightStick.antiDeadzone > 0.0f);
    CHECK(fps.rightStick.deadzone == 0.0f);
    CHECK(fps.rightStick.outerDeadzone == 0.0f);
    CHECK_FALSE(isOneToOne(fps.rightStick));
}

TEST_CASE("old files drop the 5% dead zone an anti-dead zone used to need") {
    const std::string v4 = R"({"version": 4, "profiles": [
        {"right_stick": {"deadzone": 0.05, "outer_deadzone": 0.02, "anti_deadzone": 0.12}},
        {"right_stick": {"deadzone": 0.08, "outer_deadzone": 0.02, "anti_deadzone": 0.12}}]})";
    const Config cfg = configFromJson(v4);
    CHECK(cfg.profiles[0].rightStick.deadzone == 0.0f);
    CHECK(cfg.profiles[0].rightStick.outerDeadzone == 0.0f);
    CHECK(cfg.profiles[0].rightStick.antiDeadzone == doctest::Approx(0.12f));
    CHECK(cfg.profiles[1].rightStick.deadzone == doctest::Approx(0.08f));  // chosen on purpose
}

TEST_CASE("old files move sticks still on the old 5% default to 1:1") {
    const std::string v2 = R"({"version": 2, "profiles": [
        {"left_stick": {"deadzone": 0.05, "outer_deadzone": 0.02},
         "right_stick": {"deadzone": 0.05, "outer_deadzone": 0.02, "anti_deadzone": 0.12}},
        {"left_stick": {"deadzone": 0.08, "outer_deadzone": 0.02}}]})";
    const Config cfg = configFromJson(v2);
    CHECK(cfg.profiles[0].leftStick.deadzone == 0.0f);
    CHECK(cfg.profiles[0].leftStick.outerDeadzone == 0.0f);
    CHECK(cfg.profiles[0].rightStick.deadzone == 0.0f);  // the anti-dead zone needs none either
    CHECK(cfg.profiles[1].leftStick.deadzone == doctest::Approx(0.08f));   // chosen on purpose

    const std::string v3 = R"({"version": 3, "profiles": [{"left_stick": {"deadzone": 0.05, "outer_deadzone": 0.02}}]})";
    CHECK(configFromJson(v3).profiles[0].leftStick.deadzone == doctest::Approx(0.05f));
}

TEST_CASE("hide controller setting is saved") {
    Config cfg = Config::defaults();
    CHECK_FALSE(cfg.settings.hideController);
    cfg.settings.hideController = true;
    CHECK(configFromJson(configToJson(cfg)).settings.hideController);
}

TEST_CASE("device instance id from a HID device path") {
    CHECK(instanceIdFromHidPath(R"(\\?\hid#vid_054c&pid_0ce6&mi_03#7&2a5b1c4&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})") ==
          R"(HID\VID_054C&PID_0CE6&MI_03\7&2A5B1C4&0&0000)");
    CHECK(instanceIdFromHidPath(
              R"(\\?\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&0df2#9&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})") ==
          R"(HID\{00001124-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF2\9&1A2B3C4D&0&0000)");
    CHECK(instanceIdFromHidPath("/dev/hidraw3").empty());
    CHECK(instanceIdFromHidPath("").empty());
}
