#include <doctest/doctest.h>

#include <cmath>

#include "core/config_json.hpp"
#include "core/pipeline.hpp"

using namespace edgepad;

namespace {

Config testConfig() {
    Config cfg = Config::defaults();
    cfg.normalize();
    return cfg;
}

Binding& bindingOf(BindingMap& map, Button b) { return map[static_cast<size_t>(index(b))]; }

InputState pressing(ButtonMask buttons) {
    InputState in;
    in.buttons = buttons;
    return in;
}

InputState clickingTouchpadAt(uint16_t x, uint16_t y, bool click = true) {
    InputState in;
    if (click) in.buttons = bit(Button::Touchpad);
    in.motion.touch[0] = TouchPoint{true, 1, x, y};
    return in;
}

InputState gyroRate(float pitchDps, float yawDps, float rollDps) {
    InputState in;
    in.motion.gyro = {static_cast<int16_t>(pitchDps * kGyroCountsPerDegPerSec),
                      static_cast<int16_t>(yawDps * kGyroCountsPerDegPerSec),
                      static_cast<int16_t>(rollDps * kGyroCountsPerDegPerSec)};
    return in;
}

}  // namespace

// ---------------------------------------------------------------------------
// Keyboard / mouse bindings
// ---------------------------------------------------------------------------
TEST_CASE("buttons can be bound to keyboard keys and mouse buttons") {
    Config cfg = testConfig();
    bindingOf(cfg.active().buttons, Button::PaddleLeft) = Binding::toKey(Key::Space);
    bindingOf(cfg.active().buttons, Button::PaddleRight) = Binding::toKey(Key::MouseLeft);
    Pipeline p;
    const OutputState out = p.process(pressing(bit(Button::PaddleLeft) | bit(Button::PaddleRight)), cfg, 0.004f);
    CHECK(out.keys.test(Key::Space));
    CHECK(out.keys.test(Key::MouseLeft));
    CHECK_FALSE(out.keys.test(Key::A));
    CHECK(out.buttons == 0);
    CHECK_FALSE(p.process(InputState{}, cfg, 0.004f).keys.any());
}

TEST_CASE("an Edge Fn button can be bound when it is not the Fn button") {
    Config cfg = testConfig();
    cfg.settings.fnMode = FnMode::LeftFn;
    bindingOf(cfg.active().buttons, Button::FnRight) = Binding::toKey(Key::M);
    Pipeline p;
    CHECK(p.process(pressing(bit(Button::FnRight)), cfg, 0.004f).keys.test(Key::M));
    // The left Fn still switches profiles.
    p.process(pressing(bit(Button::FnLeft) | bit(Button::Circle)), cfg, 0.004f);
    CHECK(cfg.settings.activeProfile == 1);
}

TEST_CASE("key ids round trip") {
    for (int i = 1; i < kKeyCount; ++i) {
        const Key k = static_cast<Key>(i);
        CAPTURE(i);
        CHECK(keyFromId(keyId(k)) == k);
    }
    CHECK_FALSE(keyFromId("nope"));
    CHECK(isMouseButton(Key::Mouse5));
    CHECK_FALSE(isMouseButton(Key::Z));
}

// ---------------------------------------------------------------------------
// Toggle and turbo
// ---------------------------------------------------------------------------
TEST_CASE("toggle: tap on, tap off") {
    Config cfg = testConfig();
    bindingOf(cfg.active().buttons, Button::L3).toggle = true;
    Pipeline p;
    CHECK(has(p.process(pressing(bit(Button::L3)), cfg, 0.004f).buttons, Button::L3));
    CHECK(has(p.process(pressing(0), cfg, 0.004f).buttons, Button::L3));  // stays on after release
    CHECK(has(p.process(pressing(0), cfg, 0.004f).buttons, Button::L3));
    CHECK_FALSE(has(p.process(pressing(bit(Button::L3)), cfg, 0.004f).buttons, Button::L3));  // tap again: off
    CHECK_FALSE(has(p.process(pressing(0), cfg, 0.004f).buttons, Button::L3));
}

TEST_CASE("turbo repeats presses at the chosen interval") {
    Config cfg = testConfig();
    Binding& b = bindingOf(cfg.active().buttons, Button::R1);
    b.turbo = true;
    b.turboIntervalMs = 40;  // a new press every 40 ms -> 25 presses per second
    Pipeline p;
    int presses = 0;
    bool previous = false;
    for (int i = 0; i < 1000; ++i) {  // one second at 1000 Hz
        const bool on = has(p.process(pressing(bit(Button::R1)), cfg, 0.001f).buttons, Button::R1);
        if (on && !previous) ++presses;
        previous = on;
    }
    CHECK(presses == doctest::Approx(25).epsilon(0.08));
    CHECK_FALSE(has(p.process(pressing(0), cfg, 0.001f).buttons, Button::R1));
}

TEST_CASE("turbo is limited to one change per controller report") {
    Turbo t;
    CHECK(t.update(true, 1, 0.004f));  // first press immediately
    CHECK_FALSE(t.update(true, 1, 0.004f));
    CHECK(t.update(true, 1, 0.004f));
    CHECK_FALSE(t.update(true, 1, 0.004f));
    CHECK_FALSE(t.update(false, 1, 0.004f));
}

TEST_CASE("turbo on a trigger pulses full presses while it is held") {
    TriggerSettings s;
    s.turbo = true;
    s.turboIntervalMs = 20;
    TriggerProcessor tp;
    int presses = 0;
    float previous = 0.0f;
    for (int i = 0; i < 250; ++i) {  // one second at 250 Hz, trigger held at 60%
        const float out = tp.apply(0.6f, s, 0.004f);
        CHECK((out == 0.0f || out == 1.0f));
        if (out > 0.0f && previous == 0.0f) ++presses;
        previous = out;
    }
    CHECK(presses == doctest::Approx(50).epsilon(0.1));
    CHECK(tp.apply(0.0f, s, 0.004f) == 0.0f);
}

// ---------------------------------------------------------------------------
// Shift layer
// ---------------------------------------------------------------------------
TEST_CASE("shift layer changes what buttons do while the shift button is held") {
    Config cfg = testConfig();
    Profile& prof = cfg.active();
    prof.shiftButton = Button::PaddleLeft;
    bindingOf(prof.shiftButtons, Button::Cross) = Binding::toButton(Button::Triangle);
    Pipeline p;
    CHECK(p.process(pressing(bit(Button::Cross)), cfg, 0.004f).buttons == bit(Button::Cross));
    p.process(pressing(0), cfg, 0.004f);
    p.process(pressing(bit(Button::PaddleLeft)), cfg, 0.004f);
    const OutputState shifted = p.process(pressing(bit(Button::PaddleLeft) | bit(Button::Cross)), cfg, 0.004f);
    CHECK(shifted.buttons == bit(Button::Triangle));  // the shift button itself sends nothing
    // "Same as normal" buttons keep their normal binding on the shift layer.
    CHECK(p.process(pressing(bit(Button::PaddleLeft) | bit(Button::Cross) | bit(Button::Square)), cfg, 0.004f).buttons ==
          (bit(Button::Triangle) | bit(Button::Square)));
}

TEST_CASE("a button keeps the layer it was pressed in until released") {
    Config cfg = testConfig();
    Profile& prof = cfg.active();
    prof.shiftButton = Button::PaddleLeft;
    bindingOf(prof.shiftButtons, Button::Cross) = Binding::toKey(Key::E);
    Pipeline p;
    p.process(pressing(bit(Button::PaddleLeft)), cfg, 0.004f);
    CHECK(p.process(pressing(bit(Button::PaddleLeft) | bit(Button::Cross)), cfg, 0.004f).keys.test(Key::E));
    // Shift released while Cross is still held: still the shift binding.
    const OutputState held = p.process(pressing(bit(Button::Cross)), cfg, 0.004f);
    CHECK(held.keys.test(Key::E));
    CHECK(held.buttons == 0);
    p.process(pressing(0), cfg, 0.004f);
    CHECK(p.process(pressing(bit(Button::Cross)), cfg, 0.004f).buttons == bit(Button::Cross));
}

// ---------------------------------------------------------------------------
// Touchpad zones
// ---------------------------------------------------------------------------
TEST_CASE("two touchpad zones: left and right clicks are separate buttons") {
    Config cfg = testConfig();
    Profile& prof = cfg.active();
    prof.touchpadZones = TouchpadZones::Two;
    bindingOf(prof.buttons, Button::TouchLeft) = Binding::toButton(Button::Create);
    bindingOf(prof.buttons, Button::TouchRight) = Binding::toKey(Key::Tab);
    Pipeline p;
    CHECK(p.process(clickingTouchpadAt(300, 500), cfg, 0.004f).buttons == bit(Button::Create));
    p.process(InputState{}, cfg, 0.004f);
    const OutputState right = p.process(clickingTouchpadAt(1500, 500), cfg, 0.004f);
    CHECK(right.buttons == 0);  // the plain touchpad click is replaced by the zone
    CHECK(right.keys.test(Key::Tab));
}

TEST_CASE("four touchpad zones and the zone stays put while the finger slides") {
    Config cfg = testConfig();
    Profile& prof = cfg.active();
    prof.touchpadZones = TouchpadZones::Four;
    bindingOf(prof.buttons, Button::TouchTopLeft) = Binding::toButton(Button::DpadUp);
    bindingOf(prof.buttons, Button::TouchBottomRight) = Binding::toButton(Button::DpadDown);
    Pipeline p;
    CHECK(p.process(clickingTouchpadAt(100, 100), cfg, 0.004f).buttons == bit(Button::DpadUp));
    CHECK(p.process(clickingTouchpadAt(1800, 1000), cfg, 0.004f).buttons == bit(Button::DpadUp));  // latched
    p.process(InputState{}, cfg, 0.004f);
    CHECK(p.process(clickingTouchpadAt(1800, 1000), cfg, 0.004f).buttons == bit(Button::DpadDown));
    CHECK(touchpadZoneAt(TouchPoint{true, 0, 1000, 100}, TouchpadZones::Four) == Button::TouchTopRight);
    CHECK(touchpadZoneAt(TouchPoint{true, 0, 10, 1000}, TouchpadZones::Four) == Button::TouchBottomLeft);
}

TEST_CASE("touch zones fire on touch without clicking") {
    Config cfg = testConfig();
    Profile& prof = cfg.active();
    prof.touchpadZones = TouchpadZones::Two;
    prof.zoneTrigger = ZoneTrigger::Touch;
    bindingOf(prof.buttons, Button::TouchLeft) = Binding::toButton(Button::L1);
    Pipeline p;
    CHECK(p.process(clickingTouchpadAt(100, 100, false), cfg, 0.004f).buttons == bit(Button::L1));
    CHECK(p.process(InputState{}, cfg, 0.004f).buttons == 0);
}

// ---------------------------------------------------------------------------
// Stick drift calibration
// ---------------------------------------------------------------------------
TEST_CASE("stick drift correction re-centres a drifting stick") {
    const std::array<float, 2> center{0.08f, -0.05f};
    const Vec2 rest = recenterStick({0.08f, -0.05f}, center);
    CHECK(rest.x == doctest::Approx(0.0f));
    CHECK(rest.y == doctest::Approx(0.0f));
    CHECK(recenterStick({1.0f, 1.0f}, center).x == doctest::Approx(1.0f));  // full throw still reachable
    CHECK(recenterStick({-1.0f, -1.0f}, center).y == doctest::Approx(-1.0f));

    Config cfg = testConfig();
    cfg.settings.leftStickCenter = {0.08f, -0.05f};
    cfg.active().leftStick.deadzone = 0.02f;
    Pipeline p;
    InputState drifting;
    drifting.lx = 0.08f;
    drifting.ly = -0.05f;
    const OutputState out = p.process(drifting, cfg, 0.004f);
    CHECK(out.lx == 0.0f);
    CHECK(out.ly == 0.0f);
}

// ---------------------------------------------------------------------------
// Gyro aiming
// ---------------------------------------------------------------------------
TEST_CASE("gyro turns the right stick only while the activation button is held") {
    Config cfg = testConfig();
    GyroSettings& g = cfg.active().gyro;
    g.activation = GyroActivation::WhileHeld;
    g.button = Button::L2;
    g.smoothing = 0.0f;
    Pipeline p;
    InputState turningLeft = gyroRate(0.0f, 90.0f, 0.0f);
    CHECK(p.process(turningLeft, cfg, 0.004f).rx == 0.0f);
    turningLeft.buttons = bit(Button::L2);
    const OutputState out = p.process(turningLeft, cfg, 0.004f);
    CHECK(out.rx < 0.0f);  // turning the pad left aims left
    CHECK(out.ry == doctest::Approx(0.0f));
}

TEST_CASE("gyro sensitivity, dead zone and anti-dead zone") {
    GyroSettings g;
    g.activation = GyroActivation::Always;
    g.sensitivity = 2.0f;  // full deflection at 180 deg/s
    g.deadzone = 2.0f;
    g.antiDeadzone = 0.1f;
    g.smoothing = 0.0f;
    const std::array<float, 3> noBias{};
    GyroAim aim;
    CHECK(aim.update(gyroRate(1.0f, 1.0f, 0.0f).motion, g, noBias, false, false, 0.004f).x == 0.0f);  // inside dead zone
    const Vec2 slow = aim.update(gyroRate(0.0f, -3.0f, 0.0f).motion, g, noBias, false, false, 0.004f);
    CHECK(slow.x == doctest::Approx(0.1f + 0.9f * (1.0f * 2.0f / 360.0f)).epsilon(0.02));  // floor + a little
    const Vec2 fast = aim.update(gyroRate(0.0f, -400.0f, 0.0f).motion, g, noBias, false, false, 0.004f);
    CHECK(fast.x == doctest::Approx(1.0f));
    const Vec2 up = aim.update(gyroRate(50.0f, 0.0f, 0.0f).motion, g, noBias, false, false, 0.004f);
    CHECK(up.y > 0.0f);  // tilting the pad up aims up
    g.invertY = true;
    CHECK(aim.update(gyroRate(50.0f, 0.0f, 0.0f).motion, g, noBias, false, false, 0.004f).y < 0.0f);
}

TEST_CASE("gyro calibration removes the resting drift") {
    GyroSettings g;
    g.activation = GyroActivation::Always;
    g.deadzone = 0.5f;
    g.smoothing = 0.0f;
    GyroAim aim;
    const InputState resting = gyroRate(3.0f, -4.0f, 2.0f);  // a drifting sensor, controller on the desk
    CHECK(aim.update(resting.motion, g, {}, false, false, 0.004f).x != 0.0f);
    const std::array<float, 3> bias{static_cast<float>(resting.motion.gyro[0]), static_cast<float>(resting.motion.gyro[1]),
                                    static_cast<float>(resting.motion.gyro[2])};
    const Vec2 calibrated = aim.update(resting.motion, g, bias, false, false, 0.004f);
    CHECK(calibrated.x == 0.0f);
    CHECK(calibrated.y == 0.0f);
}

TEST_CASE("gyro toggle activation and roll axis") {
    GyroSettings g;
    g.activation = GyroActivation::Toggle;
    g.horizontalAxis = GyroAxis::Roll;
    g.smoothing = 0.0f;
    GyroAim aim;
    const MotionState roll = gyroRate(0.0f, 0.0f, 60.0f).motion;
    CHECK(aim.update(roll, g, {}, false, false, 0.004f).x == 0.0f);
    CHECK(aim.update(roll, g, {}, true, true, 0.004f).x != 0.0f);   // toggled on
    CHECK(aim.update(roll, g, {}, false, false, 0.004f).x != 0.0f);  // stays on
    CHECK(aim.update(roll, g, {}, true, true, 0.004f).x == 0.0f);   // toggled off
}

TEST_CASE("gyro smoothing calms slow movements but keeps fast ones instant") {
    GyroSettings g;
    g.activation = GyroActivation::Always;
    g.deadzone = 0.0f;
    g.antiDeadzone = 0.0f;
    g.smoothing = 1.0f;  // slow movements below ~10 deg/s get averaged
    GyroAim smooth;
    const Vec2 tremor = smooth.update(gyroRate(0.0f, -4.0f, 0.0f).motion, g, {}, false, false, 0.004f);
    GyroSettings raw = g;
    raw.smoothing = 0.0f;
    GyroAim direct;
    const Vec2 tremorRaw = direct.update(gyroRate(0.0f, -4.0f, 0.0f).motion, raw, {}, false, false, 0.004f);
    CHECK(std::fabs(tremor.x) < std::fabs(tremorRaw.x) * 0.5f);
    const Vec2 flick = smooth.update(gyroRate(0.0f, -200.0f, 0.0f).motion, g, {}, false, false, 0.004f);
    const Vec2 flickRaw = direct.update(gyroRate(0.0f, -200.0f, 0.0f).motion, raw, {}, false, false, 0.004f);
    CHECK(flick.x == doctest::Approx(flickRaw.x));
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
TEST_CASE("new features survive a JSON round trip") {
    Config cfg = testConfig();
    Profile& p = cfg.active();
    bindingOf(p.buttons, Button::PaddleLeft) = Binding::toKey(Key::MouseRight);
    Binding turbo = Binding::toButton(Button::R2);
    turbo.turbo = true;
    turbo.turboIntervalMs = 7;
    bindingOf(p.buttons, Button::PaddleRight) = turbo;
    Binding toggle = Binding::toKey(Key::Shift);
    toggle.toggle = true;
    bindingOf(p.buttons, Button::L3) = toggle;
    p.shiftButton = Button::FnRight;
    bindingOf(p.shiftButtons, Button::Cross) = Binding::toKey(Key::F5);
    bindingOf(p.shiftButtons, Button::Circle) = Binding::disabled();
    p.touchpadZones = TouchpadZones::Four;
    p.zoneTrigger = ZoneTrigger::Touch;
    p.gyro.activation = GyroActivation::Toggle;
    p.gyro.button = Button::PaddleLeft;
    p.gyro.sensitivity = 4.5f;
    p.gyro.horizontalAxis = GyroAxis::Roll;
    p.gyro.invertX = true;
    p.r2.turbo = true;
    p.r2.turboIntervalMs = 33;
    cfg.settings.fnMode = FnMode::RightFn;
    cfg.settings.leftStickCenter = {0.03f, -0.02f};
    cfg.settings.gyroBias = {12.0f, -3.5f, 0.25f};
    cfg.normalize();

    std::string warning;
    const Config loaded = configFromJson(configToJson(cfg), &warning);
    CHECK(warning.empty());
    CHECK(loaded == cfg);
}

TEST_CASE("older config files still load") {
    const std::string old = R"({"profiles": [{"name": "Old", "buttons": {"paddle_left": "r2", "cross": "none"}}]})";
    const Config cfg = configFromJson(old);
    const Profile& p = cfg.profiles[0];
    CHECK(p.buttons[static_cast<size_t>(index(Button::PaddleLeft))] == Binding::toButton(Button::R2));
    CHECK(p.buttons[static_cast<size_t>(index(Button::Cross))].kind == Binding::Kind::Disabled);
    CHECK(p.shiftButtons[static_cast<size_t>(index(Button::Cross))].kind == Binding::Kind::Inherit);
    CHECK(p.gyro.activation == GyroActivation::Off);
    CHECK(p.touchpadZones == TouchpadZones::Off);
}

TEST_CASE("bindings are cleaned up on load") {
    const std::string text = R"({"profiles": [{
        "buttons": {"cross": {"to": "key:space", "turbo": true, "turbo_ms": 5000},
                    "circle": "inherit", "square": "key:nonsense", "triangle": {"to": "fn_left"}},
        "shift_button": "r2"
    }]})";
    const Config cfg = configFromJson(text);
    const Profile& p = cfg.profiles[0];
    const Binding& cross = p.buttons[static_cast<size_t>(index(Button::Cross))];
    CHECK(cross.kind == Binding::Kind::Key);
    CHECK(cross.key == Key::Space);
    CHECK(cross.turboIntervalMs == kTurboMaxMs);
    CHECK(p.buttons[static_cast<size_t>(index(Button::Circle))].kind == Binding::Kind::Disabled);  // no inherit on the normal layer
    CHECK(p.buttons[static_cast<size_t>(index(Button::Square))].kind == Binding::Kind::Disabled);
    CHECK(p.buttons[static_cast<size_t>(index(Button::Triangle))].kind == Binding::Kind::Disabled);
    CHECK_FALSE(p.shiftButton);  // R2 is analog, it cannot be the shift button
}

TEST_CASE("binding labels") {
    CHECK(bindingLabel(Binding::toButton(Button::Cross)) == "Cross");
    CHECK(bindingLabel(Binding::toKey(Key::Space)) == "Key: Space");
    CHECK(bindingLabel(Binding::toKey(Key::MouseLeft)) == "Mouse left");
    CHECK(bindingLabel(Binding::disabled()) == "Disabled");
    CHECK(bindingLabel(Binding::inherit()) == "Same as normal");
}
