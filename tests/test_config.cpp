#include <doctest/doctest.h>

#include "core/config_json.hpp"

using namespace edgepad;

TEST_CASE("config survives a JSON round trip") {
    Config cfg = Config::defaults();
    cfg.settings.output = OutputKind::DualShock4;
    cfg.settings.fnMode = FnMode::Touchpad;
    cfg.settings.gameLightbar = true;
    cfg.settings.activeProfile = 1;
    cfg.profiles[1].rightStick.rcFilter = -0.4f;
    cfg.profiles[1].rightStick.curve = Curve::Custom;
    cfg.profiles[1].rightStick.customCurve = {{0.2f, 0.3f}, {0.6f, 0.5f}};
    cfg.profiles[0].buttons[static_cast<size_t>(index(Button::PaddleLeft))] = Button::R2;
    cfg.profiles[0].buttons[static_cast<size_t>(index(Button::Create))].reset();
    cfg.profiles[2].hotkey.reset();
    cfg.normalize();

    std::string warning;
    const Config loaded = configFromJson(configToJson(cfg), &warning);
    CHECK(warning.empty());
    CHECK(loaded == cfg);
}

TEST_CASE("broken JSON falls back to defaults") {
    std::string warning;
    Config expected = Config::defaults();
    expected.normalize();
    CHECK(configFromJson("{ not json", &warning) == expected);
    CHECK_FALSE(warning.empty());
}

TEST_CASE("out of range and unknown values are repaired") {
    const std::string text = R"({
        "settings": {"output": "gamecube", "fn_mode": "mute", "active_profile": 42},
        "profiles": [{
            "name": "Mine",
            "hotkey": "l1",
            "lightbar": [300, -5, 128],
            "left_stick": {"deadzone": 5.0, "curve": "banana", "rc_filter": -3, "anti_deadzone": 0.25},
            "l2": {"max_range": 0.0, "mode": "hair", "resistance_strength": 99},
            "buttons": {"paddle_left": "r2", "cross": "none", "fn_left": "paddle_right", "l2": "cross"}
        }]
    })";
    const Config cfg = configFromJson(text);
    REQUIRE(cfg.profiles.size() == 1);
    const Profile& p = cfg.profiles[0];
    CHECK(cfg.settings.output == OutputKind::Xbox360);
    CHECK(cfg.settings.fnMode == FnMode::Mute);
    CHECK(cfg.settings.activeProfile == 0);
    CHECK(p.name == "Mine");
    CHECK_FALSE(p.hotkey);  // L1 is not a profile hotkey
    CHECK(p.lightbar[0] == 255);
    CHECK(p.lightbar[1] == 0);
    CHECK(p.leftStick.deadzone == doctest::Approx(0.9f));
    CHECK(p.leftStick.curve == Curve::Default);
    CHECK(p.leftStick.rcFilter == doctest::Approx(-1.0f));
    CHECK(p.leftStick.antiDeadzone == doctest::Approx(0.25f));
    CHECK(p.l2.mode == TriggerMode::HairTrigger);
    CHECK(p.l2.resistanceStrength == 8);
    CHECK(p.l2.maxRange >= p.l2.deadzone + 0.02f);
    CHECK(p.buttons[static_cast<size_t>(index(Button::PaddleLeft))] == Button::R2);
    CHECK_FALSE(p.buttons[static_cast<size_t>(index(Button::Cross))]);
    CHECK_FALSE(p.buttons[static_cast<size_t>(index(Button::FnLeft))]);
    // Buttons that were not mentioned keep their defaults.
    CHECK(p.buttons[static_cast<size_t>(index(Button::Triangle))] == Button::Triangle);
}

TEST_CASE("an empty profile list gets the built-in profiles") {
    const Config cfg = configFromJson(R"({"profiles": []})");
    CHECK(cfg.profiles.size() == Config::defaults().profiles.size());
}
