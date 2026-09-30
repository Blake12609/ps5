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
