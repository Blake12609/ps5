#include <doctest/doctest.h>

#include "core/pipeline.hpp"

using namespace edgepad;

namespace {

InputState pressing(ButtonMask buttons) {
    InputState in;
    in.buttons = buttons;
    return in;
}

Config testConfig() {
    Config cfg = Config::defaults();  // Default (Fn+Cross), FPS (Fn+Circle), Racing (Fn+Square)
    cfg.normalize();
    return cfg;
}

}  // namespace

TEST_CASE("buttons map to themselves by default") {
    Config cfg = testConfig();
    Pipeline p;
    const OutputState out = p.process(pressing(bit(Button::Cross) | bit(Button::DpadUp)), cfg, 0.004f);
    CHECK(out.buttons == (bit(Button::Cross) | bit(Button::DpadUp)));
}

TEST_CASE("back buttons use the profile mapping") {
    Config cfg = testConfig();
    Pipeline p;
    CHECK(p.process(pressing(bit(Button::PaddleLeft)), cfg, 0.004f).buttons == bit(Button::Circle));
    cfg.active().buttons[static_cast<size_t>(index(Button::PaddleRight))] = Button::R2;
    const OutputState out = p.process(pressing(bit(Button::PaddleRight)), cfg, 0.004f);
    CHECK(out.buttons == 0);
    CHECK(out.r2 == 1.0f);
}

TEST_CASE("a disabled button is not forwarded") {
    Config cfg = testConfig();
    cfg.active().buttons[static_cast<size_t>(index(Button::Cross))].reset();
    Pipeline p;
    CHECK(p.process(pressing(bit(Button::Cross)), cfg, 0.004f).buttons == 0);
}

TEST_CASE("Fn + face button switches profile and swallows the combo") {
    Config cfg = testConfig();
    Pipeline p;
    PipelineEvents ev;
    p.process(pressing(bit(Button::FnLeft)), cfg, 0.004f, &ev);
    CHECK_FALSE(ev.profileChanged);
    OutputState out = p.process(pressing(bit(Button::FnLeft) | bit(Button::Circle)), cfg, 0.004f, &ev);
    CHECK(ev.profileChanged);
    CHECK(cfg.settings.activeProfile == 1);
    CHECK(out.buttons == 0);
    // Releasing Fn while still holding Circle must not leak a Circle press to the game.
    out = p.process(pressing(bit(Button::Circle)), cfg, 0.004f);
    CHECK(out.buttons == 0);
    out = p.process(pressing(0), cfg, 0.004f);
    out = p.process(pressing(bit(Button::Circle)), cfg, 0.004f);
    CHECK(out.buttons == bit(Button::Circle));
}

TEST_CASE("buttons held before Fn keep working") {
    Config cfg = testConfig();
    Pipeline p;
    p.process(pressing(bit(Button::Cross)), cfg, 0.004f);
    const OutputState out = p.process(pressing(bit(Button::Cross) | bit(Button::Mute)), cfg, 0.004f);
    CHECK(out.buttons == bit(Button::Cross));
}

TEST_CASE("Mute works as Fn on a regular DualSense, unless disabled") {
    Config cfg = testConfig();
    Pipeline p;
    p.process(pressing(bit(Button::Mute) | bit(Button::Square)), cfg, 0.004f);
    CHECK(cfg.settings.activeProfile == 2);

    Config off = testConfig();
    off.settings.fnMode = FnMode::Disabled;
    Pipeline q;
    q.process(pressing(bit(Button::Mute) | bit(Button::Square)), off, 0.004f);
    CHECK(off.settings.activeProfile == 0);
}

TEST_CASE("Fn + Options toggles raw passthrough") {
    Config cfg = testConfig();
    Pipeline p;
    PipelineEvents ev;
    p.process(pressing(bit(Button::FnRight) | bit(Button::Options)), cfg, 0.004f, &ev);
    CHECK(ev.enabledChanged);
    CHECK_FALSE(cfg.settings.enabled);

    InputState in;
    in.lx = 0.02f;  // inside the dead zone, but passthrough forwards it untouched
    in.l2 = 0.4f;
    const OutputState out = p.process(in, cfg, 0.004f);
    CHECK(out.lx == doctest::Approx(0.02f));
    CHECK(out.l2 == doctest::Approx(0.4f));
}

TEST_CASE("Fn buttons and paddles are never sent to the game as themselves") {
    Config cfg = testConfig();
    cfg.settings.fnMode = FnMode::Disabled;
    cfg.active().buttons[static_cast<size_t>(index(Button::FnLeft))].reset();
    Pipeline p;
    CHECK(p.process(pressing(bit(Button::FnLeft)), cfg, 0.004f).buttons == 0);
}

TEST_CASE("swap sticks") {
    Config cfg = testConfig();
    cfg.active().swapSticks = true;
    Pipeline p;
    InputState in;
    in.lx = 1.0f;
    const OutputState out = p.process(in, cfg, 0.004f);
    CHECK(out.lx == 0.0f);
    CHECK(out.rx == doctest::Approx(1.0f));
}

TEST_CASE("RC jitter only wobbles the stick while it is moved") {
    Config cfg = testConfig();
    StickSettings& right = cfg.active().rightStick;
    right.deadzone = 0.0f;       // worst case: no dead zone
    right.antiDeadzone = 0.2f;   // and an anti-dead zone that would amplify any jitter
    right.rcFilter = -1.0f;
    Pipeline p;
    for (int i = 0; i < 8; ++i) {  // hands off the stick: perfectly still output
        const OutputState out = p.process(InputState{}, cfg, 0.004f);
        CHECK(out.rx == 0.0f);
        CHECK(out.ry == 0.0f);
    }
    InputState moved;
    moved.rx = 0.6f;
    const float steady = processStick(0.6f, 0.0f, right).x;
    const float a = p.process(moved, cfg, 0.004f).rx;
    const float b = p.process(moved, cfg, 0.004f).rx;
    CHECK(a != b);  // wobbles while pushed
    CHECK((a + b) / 2.0f == doctest::Approx(steady));
}

TEST_CASE("RC stabilizer smooths movement but lets go instantly") {
    Config cfg = testConfig();
    cfg.active().leftStick.rcFilter = 1.0f;
    Pipeline p;
    p.process(InputState{}, cfg, 0.004f);
    InputState pushed;
    pushed.lx = 1.0f;
    CHECK(p.process(pushed, cfg, 0.004f).lx < 0.5f);  // smoothed on the way out
    for (int i = 0; i < 200; ++i) p.process(pushed, cfg, 0.004f);
    CHECK(p.process(InputState{}, cfg, 0.004f).lx == 0.0f);  // released: no smoothing tail
}

TEST_CASE("gyro, accelerometer and touchpad pass through to the virtual controller") {
    Config cfg = testConfig();
    InputState in;
    in.motion.gyro = {10, -20, 30};
    in.motion.accel = {100, 200, -8192};
    in.motion.timestamp = 12345;
    in.motion.touch[0] = TouchPoint{true, 2, 640, 480};
    in.battery = 60;
    Pipeline p;
    OutputState out = p.process(in, cfg, 0.004f);
    CHECK(out.motion == in.motion);
    CHECK(out.battery == 60);
    cfg.settings.enabled = false;  // passthrough keeps it too
    out = p.process(in, cfg, 0.004f);
    CHECK(out.motion == in.motion);
}

TEST_CASE("games can colour the lightbar when allowed") {
    Config cfg = testConfig();
    const std::array<uint8_t, 3> red{255, 0, 0};
    CHECK(effectsForConfig(cfg, 0, 0, red).lightbar == cfg.active().lightbar);  // off by default
    cfg.settings.gameLightbar = true;
    CHECK(effectsForConfig(cfg, 0, 0, red).lightbar == red);
    CHECK(effectsForConfig(cfg, 0, 0, std::nullopt).lightbar == cfg.active().lightbar);  // game never set one
}

TEST_CASE("effects follow the active profile") {
    Config cfg = testConfig();
    cfg.settings.activeProfile = 2;
    const auto fx = effectsForConfig(cfg, 120, 60);
    CHECK(fx.lightbar == cfg.profiles[2].lightbar);
    CHECK(fx.playerLeds == playerLedsForProfile(2));
    CHECK(fx.leftTrigger[0] == 0x21);  // Racing profile has an L2 wall
    CHECK(fx.rumbleLeft == 120);
    CHECK(fx.rumbleRight == 60);
    cfg.settings.rumble = false;
    CHECK(effectsForConfig(cfg, 120, 60).rumbleLeft == 0);
}
