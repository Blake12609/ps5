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
    cfg.profiles[0].buttons[static_cast<size_t>(index(Button::PaddleLeft))] = Binding::toButton(Button::R2);
    cfg.profiles[0].buttons[static_cast<size_t>(index(Button::Create))] = Binding::disabled();
    cfg.profiles[1].r2.hairResetDistance = 0.08f;
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
    CHECK(p.buttons[static_cast<size_t>(index(Button::PaddleLeft))] == Binding::toButton(Button::R2));
    CHECK(p.buttons[static_cast<size_t>(index(Button::Cross))].kind == Binding::Kind::Disabled);
    CHECK(p.buttons[static_cast<size_t>(index(Button::FnLeft))].kind == Binding::Kind::Disabled);
    // Buttons that were not mentioned keep their defaults.
    CHECK(p.buttons[static_cast<size_t>(index(Button::Triangle))] == Binding::toButton(Button::Triangle));
}

TEST_CASE("an empty profile list gets the built-in profiles") {
    const Config cfg = configFromJson(R"({"profiles": []})");
    CHECK(cfg.profiles.size() == Config::defaults().profiles.size());
}

TEST_CASE("old files move the hair trigger reset from the old 4% default to 1%") {
    const std::string v1 = R"({"version": 1, "profiles": [{"l2": {"mode": "hair", "hair_reset_distance": 0.04},
                                                          "r2": {"mode": "hair", "hair_reset_distance": 0.1}}]})";
    const Config migrated = configFromJson(v1);
    CHECK(migrated.profiles[0].l2.hairResetDistance == doctest::Approx(0.01f));
    CHECK(migrated.profiles[0].r2.hairResetDistance == doctest::Approx(0.1f));  // a custom value stays

    const std::string v2 = R"({"version": 2, "profiles": [{"l2": {"hair_reset_distance": 0.04}}]})";
    CHECK(configFromJson(v2).profiles[0].l2.hairResetDistance == doctest::Approx(0.04f));  // chosen on purpose
}
