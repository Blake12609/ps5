#include <doctest/doctest.h>

#include <cmath>

#include "core/config_json.hpp"
#include "core/pipeline.hpp"
#include "core/virtual_dualsense.hpp"

using namespace edgepad;

namespace {

InputState gyroTurn(float yawDps) {
    InputState in;
    in.motion.gyro = {0, static_cast<int16_t>(yawDps * kGyroCountsPerDegPerSec), 0};
    return in;
}

GyroSettings plainGyro() {
    GyroSettings g;
    g.activation = GyroActivation::Always;
    g.sensitivity = 2.0f;  // full deflection at 180 deg/s
    g.deadzone = 0.0f;
    g.antiDeadzone = 0.0f;
    g.smoothing = 0.0f;
    return g;
}

}  // namespace

// ---------------------------------------------------------------------------
// Gyro: second activation button, precision
// ---------------------------------------------------------------------------
TEST_CASE("gyro: either of two buttons turns it on (aiming or firing)") {
    Config cfg = Config::defaults();
    GyroSettings& g = cfg.active().gyro;
    g.activation = GyroActivation::WhileHeld;
    g.button = Button::L2;
    g.button2 = Button::R2;
    g.smoothing = 0.0f;
    Pipeline p;
    InputState in = gyroTurn(90.0f);
    CHECK(p.process(in, cfg, 0.004f).rx == 0.0f);
    in.buttons = bit(Button::R2);
    CHECK(p.process(in, cfg, 0.004f).rx < 0.0f);
    in.buttons = bit(Button::L2);
    CHECK(p.process(in, cfg, 0.004f).rx < 0.0f);
    in.buttons = bit(Button::Cross);
    CHECK(p.process(in, cfg, 0.004f).rx == 0.0f);
}

TEST_CASE("gyro: precision slows small movements only, fast turns keep full sensitivity") {
    const std::array<float, 3> noBias{};
    GyroSettings plain = plainGyro();
    GyroSettings precise = plainGyro();
    precise.precision = 0.5f;
    precise.precisionSpeed = 40.0f;
    GyroAim a, b;
    // 10 deg/s: a quarter of the way to full speed -> sensitivity x (1 - 0.5 * 0.75) = 0.625.
    const float slowPlain = a.update(gyroTurn(-10.0f).motion, plain, noBias, false, false, 0.004f).x;
    const float slowPrecise = b.update(gyroTurn(-10.0f).motion, precise, noBias, false, false, 0.004f).x;
    CHECK(slowPrecise == doctest::Approx(slowPlain * 0.625f).epsilon(0.01));
    // At and above 40 deg/s nothing changes.
    CHECK(b.update(gyroTurn(-60.0f).motion, precise, noBias, false, false, 0.004f).x ==
          doctest::Approx(a.update(gyroTurn(-60.0f).motion, plain, noBias, false, false, 0.004f).x));
    // Still grows with speed: a faster movement never aims slower.
    float last = 0.0f;
    for (float dps = 1.0f; dps < 100.0f; dps += 1.0f) {
        const float x = b.update(gyroTurn(-dps).motion, precise, noBias, false, false, 0.004f).x;
        CHECK(x >= last);
        last = x;
    }
}

TEST_CASE("gyro: second button and precision are saved, a duplicate second button is dropped") {
    Config cfg = Config::defaults();
    GyroSettings& g = cfg.profiles[1].gyro;
    g.button = Button::L2;
    g.button2 = Button::R2;
    g.precision = 0.35f;
    g.precisionSpeed = 55.0f;
    cfg.normalize();
    Config loaded = configFromJson(configToJson(cfg));
    CHECK(loaded.profiles[1].gyro == g);
    loaded.profiles[1].gyro.button2.reset();
    CHECK_FALSE(configFromJson(configToJson(loaded)).profiles[1].gyro.button2);

    cfg.profiles[1].gyro.button2 = Button::L2;  // same as the first: nothing to add
    cfg.profiles[1].gyro.precision = 3.0f;
    cfg.normalize();
    CHECK_FALSE(cfg.profiles[1].gyro.button2);
    CHECK(cfg.profiles[1].gyro.precision == doctest::Approx(0.9f));
}

// ---------------------------------------------------------------------------
// Hold for a second action
// ---------------------------------------------------------------------------
namespace {

struct Run {
    int crossFrom = -1, crossTo = -1;  // ms where Cross was sent (first / last)
    int spaceFrom = -1, spaceTo = -1;  // ms where the hold action (Space) was sent
    int crossReports = 0;
};

// Presses Cross from 0 to `releaseMs`, runs until `endMs`, 1 ms reports.
Run pressCross(Pipeline& pipe, Config& cfg, int releaseMs, int endMs, int startMs = 0) {
    Run r;
    for (int ms = startMs; ms < endMs; ++ms) {
        InputState in;
        if (ms < releaseMs) in.buttons = bit(Button::Cross);
        const OutputState out = pipe.process(in, cfg, 0.001f);
        if (has(out.buttons, Button::Cross)) {
            if (r.crossFrom < 0) r.crossFrom = ms;
            r.crossTo = ms;
            ++r.crossReports;
        }
        if (out.keys.test(Key::Space)) {
            if (r.spaceFrom < 0) r.spaceFrom = ms;
            r.spaceTo = ms;
        }
    }
    return r;
}

Config holdConfig() {
    Config cfg = Config::defaults();
    cfg.active().holdButtons[static_cast<size_t>(index(Button::Cross))] = Binding::toKey(Key::Space);
    cfg.active().holdMs = 300;
    return cfg;
}

}  // namespace

TEST_CASE("hold action: a short press sends the normal binding as a quick tap when let go") {
    Config cfg = holdConfig();
    Pipeline pipe;
    const Run r = pressCross(pipe, cfg, 100, 400);
    CHECK(r.crossFrom == 100);  // nothing while it could still become a hold
    CHECK(r.crossTo - r.crossFrom + 1 == doctest::Approx(61).epsilon(0.05));  // ~60 ms tap
    CHECK(r.spaceFrom == -1);
}

TEST_CASE("hold action: holding past the hold time sends the hold binding until let go") {
    Config cfg = holdConfig();
    Pipeline pipe;
    const Run r = pressCross(pipe, cfg, 700, 900);
    CHECK(r.crossFrom == -1);  // the normal binding never fires
    CHECK(r.spaceFrom == doctest::Approx(299).epsilon(0.01));
    CHECK(r.spaceTo == 699);
}

TEST_CASE("hold action: buttons without one are untouched, toggles flip on a short press only") {
    Config cfg = holdConfig();
    Pipeline plain;
    InputState in;
    in.buttons = bit(Button::Circle);  // no hold action: instant, no delay
    CHECK(has(plain.process(in, cfg, 0.001f).buttons, Button::Circle));

    cfg.active().buttons[static_cast<size_t>(index(Button::Cross))].toggle = true;
    Pipeline pipe;
    pressCross(pipe, cfg, 50, 200);  // short: toggles on
    InputState idle;
    CHECK(has(pipe.process(idle, cfg, 0.001f).buttons, Button::Cross));
    const Run longPress = pressCross(pipe, cfg, 500, 600);  // long: hold action, the toggle stays on
    CHECK(longPress.spaceFrom >= 0);
    CHECK(has(pipe.process(idle, cfg, 0.001f).buttons, Button::Cross));
    pressCross(pipe, cfg, 50, 200);  // short again: off
    CHECK_FALSE(has(pipe.process(idle, cfg, 0.001f).buttons, Button::Cross));
}

TEST_CASE("hold action: the shift layer's own binding is used without a hold delay") {
    Config cfg = holdConfig();
    Profile& p = cfg.active();
    p.shiftButton = Button::PaddleLeft;
    p.shiftButtons[static_cast<size_t>(index(Button::Cross))] = Binding::toKey(Key::E);
    Pipeline pipe;
    InputState in;
    in.buttons = bit(Button::PaddleLeft) | bit(Button::Cross);
    const OutputState out = pipe.process(in, cfg, 0.001f);
    CHECK(out.keys.test(Key::E));  // instantly
    CHECK_FALSE(out.keys.test(Key::Space));
}

TEST_CASE("hold action: settings are saved; hold time and options are clamped") {
    Config cfg = holdConfig();
    Binding& hold = cfg.profiles[0].holdButtons[static_cast<size_t>(index(Button::Square))];
    hold = Binding::toButton(Button::Triangle);
    hold.turbo = true;  // not used for hold actions
    cfg.profiles[0].holdMs = 5;
    cfg.normalize();
    CHECK_FALSE(hold.turbo);
    CHECK(cfg.profiles[0].holdMs == kHoldMinMs);
    const Config loaded = configFromJson(configToJson(cfg));
    CHECK(loaded.profiles[0].holdButtons == cfg.profiles[0].holdButtons);
    CHECK(loaded.profiles[0].holdMs == cfg.profiles[0].holdMs);
    // Unused hold actions are not written out.
    const std::string json = configToJson(Config::defaults());
    CHECK(json.find("\"hold_buttons\": {}") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Touchpad as a mouse
// ---------------------------------------------------------------------------
namespace {

InputState touching(uint8_t id, uint16_t x, uint16_t y) {
    InputState in;
    in.motion.touch[0] = TouchPoint{true, id, x, y};
    return in;
}

Config mouseConfig(float speed = 1.0f) {
    Config cfg = Config::defaults();
    cfg.active().touchpadMouse = true;
    cfg.active().touchpadMouseSpeed = speed;
    return cfg;
}

}  // namespace

TEST_CASE("touchpad mouse: one finger moves the pointer, slow movements are not lost") {
    Config cfg = mouseConfig();
    Pipeline pipe;
    int x = 0, y = 0;
    for (int step = 0; step <= 100; ++step) {  // 1 touchpad step per report: 0.6 px each
        const OutputState out = pipe.process(touching(1, static_cast<uint16_t>(500 + step), 400), cfg, 0.004f);
        x += out.mouseX;
        y += out.mouseY;
        CHECK(out.wheel == 0);
    }
    CHECK(x == doctest::Approx(60).epsilon(0.02));
    CHECK(y == 0);

    Config fast = mouseConfig(2.0f);
    Pipeline pipe2;
    pipe2.process(touching(1, 500, 400), fast, 0.004f);
    CHECK(pipe2.process(touching(1, 500, 450), fast, 0.004f).mouseY == 60);  // 50 steps x 0.6 x 2
}

TEST_CASE("touchpad mouse: a new finger never makes the pointer jump") {
    Config cfg = mouseConfig();
    Pipeline pipe;
    pipe.process(touching(1, 1500, 900), cfg, 0.004f);
    pipe.process(InputState{}, cfg, 0.004f);  // lifted
    const OutputState out = pipe.process(touching(2, 100, 100), cfg, 0.004f);
    CHECK(out.mouseX == 0);
    CHECK(out.mouseY == 0);
}

TEST_CASE("touchpad mouse: two fingers scroll, the page follows the fingers") {
    Config cfg = mouseConfig();
    Pipeline pipe;
    int wheel = 0, moved = 0;
    for (int step = 0; step <= 120; step += 4) {
        InputState in = touching(1, 600, static_cast<uint16_t>(300 + step));
        in.motion.touch[1] = TouchPoint{true, 2, 900, static_cast<uint16_t>(300 + step)};
        const OutputState out = pipe.process(in, cfg, 0.004f);
        wheel += out.wheel;
        moved += std::abs(out.mouseX) + std::abs(out.mouseY);
    }
    CHECK(wheel == 2);  // fingers down 120 steps: two notches up
    CHECK(moved == 0);
}

TEST_CASE("touchpad mouse: clicking the pad clicks, two fingers right click, no touchpad button") {
    Config cfg = mouseConfig();
    Pipeline pipe;
    InputState click = touching(1, 900, 500);
    click.buttons = bit(Button::Touchpad);
    OutputState out = pipe.process(click, cfg, 0.004f);
    CHECK(out.keys.test(Key::MouseLeft));
    CHECK_FALSE(has(out.buttons, Button::Touchpad));
    click.motion.touch[1] = TouchPoint{true, 2, 1200, 500};  // a second finger mid-click: still left
    CHECK(pipe.process(click, cfg, 0.004f).keys.test(Key::MouseLeft));
    pipe.process(InputState{}, cfg, 0.004f);  // let go
    out = pipe.process(click, cfg, 0.004f);  // now clicking with two fingers down
    CHECK(out.keys.test(Key::MouseRight));
    CHECK_FALSE(out.keys.test(Key::MouseLeft));

    Config off = Config::defaults();
    Pipeline plain;
    InputState c2 = touching(1, 900, 500);
    c2.buttons = bit(Button::Touchpad);
    plain.process(c2, off, 0.004f);
    c2.motion.touch[0].x = 1200;
    out = plain.process(c2, off, 0.004f);
    CHECK(out.mouseX == 0);
    CHECK(has(out.buttons, Button::Touchpad));  // off: the touchpad is a controller button as before

    const Config loaded = configFromJson(configToJson(mouseConfig(2.5f)));
    CHECK(loaded.active().touchpadMouse);
    CHECK(loaded.active().touchpadMouseSpeed == doctest::Approx(2.5f));
}

// ---------------------------------------------------------------------------
// Rumble strength, low battery alert, lightbar off
// ---------------------------------------------------------------------------
TEST_CASE("rumble strength scales game rumble on every virtual controller") {
    Config cfg = Config::defaults();
    cfg.settings.rumbleStrength = 0.5f;
    const dualsense::Effects fx = effectsForConfig(cfg, 200, 100);
    CHECK(fx.rumbleLeft == 100);
    CHECK(fx.rumbleRight == 50);
    cfg.settings.rumbleStrength = 0.0f;  // clamped: never below 10%
    cfg.normalize();
    CHECK(cfg.settings.rumbleStrength == doctest::Approx(0.1f));

    // Virtual DualSense: the game's own rumble motors in its output report.
    std::array<uint8_t, 48> game{};
    game[0] = 0x02;
    game[1] = 0x03;  // flags: compatible vibration
    game[3] = 240;   // right motor
    game[4] = 120;   // left motor
    virtual_dualsense::OutputFilter filter;
    filter.rumbleStrength = 0.25f;
    const auto c = virtual_dualsense::filterGameOutput(game.data(), game.size(), filter);
    REQUIRE(c);
    CHECK((*c)[2] == 60);
    CHECK((*c)[3] == 30);
    filter.rumbleStrength = 1.0f;
    CHECK((*virtual_dualsense::filterGameOutput(game.data(), game.size(), filter))[2] == 240);  // untouched at 100%
}

TEST_CASE("low battery alert: the lightbar pulses red at 10% or less, not while charging") {
    Config cfg = Config::defaults();
    cfg.active().lightbar = {0, 0, 255};
    CHECK(effectsForConfig(cfg, 0, 0, std::nullopt, 0.0, 50, false).lightbar == std::array<uint8_t, 3>{0, 0, 255});
    const auto bright = effectsForConfig(cfg, 0, 0, std::nullopt, 0.0, 10, false).lightbar;
    const auto dim = effectsForConfig(cfg, 0, 0, std::nullopt, 0.5, 10, false).lightbar;
    CHECK(bright == std::array<uint8_t, 3>{255, 0, 0});
    CHECK(dim[0] < 60);  // pulsing
    CHECK(dim[0] > 0);
    CHECK(dim[2] == 0);
    CHECK(effectsForConfig(cfg, 0, 0, std::nullopt, 0.0, 10, true).lightbar == std::array<uint8_t, 3>{0, 0, 255});
    CHECK(effectsForConfig(cfg, 0, 0, std::nullopt, 0.0, -1, false).lightbar == std::array<uint8_t, 3>{0, 0, 255});
    cfg.settings.lowBatteryAlert = false;
    CHECK(effectsForConfig(cfg, 0, 0, std::nullopt, 0.0, 5, false).lightbar == std::array<uint8_t, 3>{0, 0, 255});
    const Config loaded = configFromJson(configToJson(cfg));
    CHECK_FALSE(loaded.settings.lowBatteryAlert);
}

TEST_CASE("lightbar brightness 0 turns it off") {
    Config cfg = Config::defaults();
    cfg.active().light.brightness = 0.0f;
    cfg.normalize();
    CHECK(cfg.active().light.brightness == 0.0f);
    CHECK(lightbarColor(cfg.active(), 1.0) == std::array<uint8_t, 3>{0, 0, 0});
}
