#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>

#include "core/auto_profile.hpp"
#include "core/config_json.hpp"
#include "core/pipeline.hpp"
#include "core/tester.hpp"

using namespace edgepad;

namespace {

int brightness(const std::array<uint8_t, 3>& c) { return c[0] + c[1] + c[2]; }

}  // namespace

TEST_CASE("lightbar: static color, scaled by brightness") {
    Profile p;
    p.lightbar = {200, 100, 50};
    CHECK(lightbarColor(p, 0.0) == p.lightbar);
    CHECK(lightbarColor(p, 12.3) == p.lightbar);
    p.light.brightness = 0.5f;
    CHECK(lightbarColor(p, 1.0) == std::array<uint8_t, 3>{100, 50, 25});
}

TEST_CASE("lightbar: breathing fades in and out once per period, never fully dark") {
    Profile p;
    p.lightbar = {0, 0, 255};
    p.light.effect = LightEffect::Breathing;
    p.light.periodSeconds = 2.0f;
    CHECK(lightbarColor(p, 1.0) == p.lightbar);           // full at half the period
    CHECK(brightness(lightbarColor(p, 0.0)) > 0);         // dim, not off
    CHECK(brightness(lightbarColor(p, 0.0)) < 40);
    CHECK(lightbarColor(p, 0.5) == lightbarColor(p, 2.5));  // repeats
    CHECK(brightness(lightbarColor(p, 0.5)) < brightness(lightbarColor(p, 0.9)));
}

TEST_CASE("lightbar: rainbow goes all around the color wheel") {
    Profile p;
    p.light.effect = LightEffect::Rainbow;
    p.light.periodSeconds = 6.0f;
    CHECK(lightbarColor(p, 0.0) == std::array<uint8_t, 3>{255, 0, 0});
    CHECK(lightbarColor(p, 2.0) == std::array<uint8_t, 3>{0, 255, 0});
    CHECK(lightbarColor(p, 4.0) == std::array<uint8_t, 3>{0, 0, 255});
    std::set<std::array<uint8_t, 3>> seen;
    for (int i = 0; i < 60; ++i) seen.insert(lightbarColor(p, i * 0.1));
    CHECK(seen.size() > 40);  // smooth, many colors
}

TEST_CASE("lightbar: color cycle fades through the chosen colors in order") {
    Profile p;
    p.lightbar = {255, 0, 0};
    p.light.effect = LightEffect::Cycle;
    p.light.colorCount = 3;
    p.light.extraColors = {{{0, 255, 0}, {0, 0, 255}, {9, 9, 9}}};
    p.light.periodSeconds = 3.0f;
    CHECK(lightbarColor(p, 0.0) == std::array<uint8_t, 3>{255, 0, 0});
    CHECK(lightbarColor(p, 1.0) == std::array<uint8_t, 3>{0, 255, 0});
    CHECK(lightbarColor(p, 2.0) == std::array<uint8_t, 3>{0, 0, 255});
    const auto halfway = lightbarColor(p, 0.5);  // red -> green
    CHECK(halfway[0] > 0);
    CHECK(halfway[1] > 0);
    CHECK(halfway[2] == 0);
    for (int i = 0; i < 30; ++i) CHECK(lightbarColor(p, i * 0.1) != std::array<uint8_t, 3>{9, 9, 9});  // 4th unused
}

TEST_CASE("lightbar: battery level from red to green, profile color when unknown") {
    Profile p;
    p.light.effect = LightEffect::Battery;
    CHECK(lightbarColor(p, 0.0, 0) == std::array<uint8_t, 3>{255, 0, 0});
    CHECK(lightbarColor(p, 0.0, 50) == std::array<uint8_t, 3>{255, 200, 0});
    CHECK(lightbarColor(p, 0.0, 100) == std::array<uint8_t, 3>{0, 255, 40});
    CHECK(lightbarColor(p, 0.0, -1) == p.lightbar);
    // Breathes while charging.
    CHECK(lightbarColor(p, 0.0, 100, true) != lightbarColor(p, p.light.periodSeconds / 2.0, 100, true));
}

TEST_CASE("lightbar: the effect reaches the controller unless a game sets the lightbar") {
    Config cfg = Config::defaults();
    cfg.active().light.effect = LightEffect::Rainbow;
    cfg.active().light.periodSeconds = 6.0f;
    CHECK(effectsForConfig(cfg, 0, 0, std::nullopt, 2.0).lightbar == std::array<uint8_t, 3>{0, 255, 0});
    cfg.settings.gameLightbar = true;
    const std::array<uint8_t, 3> game{1, 2, 3};
    CHECK(effectsForConfig(cfg, 0, 0, game, 2.0).lightbar == game);
}

TEST_CASE("lightbar and games survive saving") {
    Config cfg = Config::defaults();
    Profile& p = cfg.profiles[1];
    p.light.effect = LightEffect::Cycle;
    p.light.colorCount = 4;
    p.light.extraColors[2] = {7, 8, 9};
    p.light.periodSeconds = 7.5f;
    p.light.brightness = 0.4f;
    p.games = {"cod.exe", "r5apex.exe"};
    const Config loaded = configFromJson(configToJson(cfg));
    CHECK(loaded.profiles[1].light == p.light);
    CHECK(loaded.profiles[1].games == p.games);
}

TEST_CASE("share codes: every default profile round-trips") {
    for (const Profile& p : Config::defaults().profiles) {
        const std::string code = profileShareCode(p);
        CHECK(code.rfind("EP1-", 0) == 0);
        std::string error;
        const auto back = profileFromShareCode(code, &error);
        REQUIRE_MESSAGE(back, error);
        CHECK(*back == p);
    }
    // A new profile has nothing to share but its name: a very short code.
    CHECK(profileShareCode(Profile{}).size() < 40);
}

TEST_CASE("share codes: a heavily customized profile round-trips") {
    Profile p;
    p.name = "Warzone sweat";
    p.hotkey.reset();
    p.rightStick.antiDeadzone = 0.17f;
    p.rightStick.curve = Curve::Custom;
    p.rightStick.customCurve = {{0.2f, 0.1f}, {0.6f, 0.5f}};
    p.rightStick.rcFilter = -0.35f;
    p.l2.mode = TriggerMode::HairTrigger;
    p.r2.turbo = true;
    p.r2.turboIntervalMs = 33;
    p.buttons[static_cast<size_t>(index(Button::PaddleLeft))] = Binding::toKey(Key::Space);
    p.shiftButton = Button::PaddleRight;
    p.gyro.activation = GyroActivation::WhileHeld;
    p.light.effect = LightEffect::Breathing;
    p.games = {"cod.exe"};
    Config cfg;
    cfg.profiles = {p};
    cfg.normalize();  // the same clamping an import gets
    const Profile expected = cfg.profiles.front();
    const auto back = profileFromShareCode(profileShareCode(expected));
    REQUIRE(back);
    CHECK(*back == expected);
    CHECK_FALSE(back->hotkey);  // "no hotkey" survives (null is kept, not dropped)
}

TEST_CASE("share codes: damaged, cut off or foreign codes are rejected") {
    Profile p;
    p.name = "Racing";
    p.leftStick.deadzone = 0.03f;
    const std::string code = profileShareCode(p);
    std::string error;
    // Spaces, line breaks and a lower case prefix are fine.
    CHECK(profileFromShareCode("  ep1-" + code.substr(4, 10) + "\n " + code.substr(14) + "\n", &error));
    std::string damaged = code;
    damaged[damaged.size() / 2] = damaged[damaged.size() / 2] == 'A' ? 'B' : 'A';
    CHECK_FALSE(profileFromShareCode(damaged, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(profileFromShareCode(code.substr(0, code.size() - 3), &error));
    CHECK_FALSE(profileFromShareCode("hello", &error));
    CHECK_FALSE(profileFromShareCode("EP1-!!!!", &error));
    CHECK_FALSE(profileFromShareCode("", &error));
}

TEST_CASE("tester: a perfect circle is fully covered with no error") {
    tester::StickTest t;
    CHECK(t.coverage() == 0.0f);
    for (int i = 0; i < 720; ++i) {
        const float a = static_cast<float>(i) * 6.2831853f / 720.0f;
        t.add(std::cos(a), std::sin(a));
    }
    CHECK(t.coverage() == doctest::Approx(1.0f));
    CHECK(t.averageError() == doctest::Approx(0.0f).epsilon(0.001));
    CHECK_FALSE(t.restSeen());
}

TEST_CASE("tester: a square gate shows up as circularity error, half a circle as half coverage") {
    tester::StickTest square;
    for (int i = 0; i < 720; ++i) {
        const float a = static_cast<float>(i) * 6.2831853f / 720.0f;
        const float x = std::cos(a), y = std::sin(a);
        const float scale = 1.0f / std::max(std::fabs(x), std::fabs(y));  // out to the square's edge
        square.add(x * scale, y * scale);
    }
    CHECK(square.averageError() > 0.1f);  // a square averages ~15% outside the circle

    tester::StickTest half;
    for (int i = 0; i <= 360; ++i) {
        const float a = static_cast<float>(i) * 3.14159265f / 360.0f;
        half.add(0.95f * std::cos(a), 0.95f * std::sin(a));
    }
    CHECK(half.coverage() == doctest::Approx(0.5f).epsilon(0.03));
    CHECK(half.averageError() == doctest::Approx(0.05f).epsilon(0.01));
    // Small movements near the center never count as reaching the edge.
    tester::StickTest small;
    small.add(0.3f, 0.3f);
    CHECK(small.coverage() == 0.0f);
}

TEST_CASE("tester: resting noise is the range seen near the center") {
    tester::StickTest t;
    t.add(0.01f, -0.02f);
    t.add(-0.03f, 0.01f);
    t.add(0.02f, 0.0f);
    t.add(0.9f, 0.0f);  // a real movement is not noise
    REQUIRE(t.restSeen());
    CHECK(t.restRangeX() == doctest::Approx(0.05f));
    CHECK(t.restRangeY() == doctest::Approx(0.03f));
    t.reset();
    CHECK_FALSE(t.restSeen());
    CHECK(t.coverage() == 0.0f);
}

TEST_CASE("tester: trigger travel") {
    tester::TriggerTest t;
    CHECK_FALSE(t.seen());
    CHECK_FALSE(t.fullRange());
    t.add(0.0f);
    t.add(0.6f);
    CHECK(t.seen());
    CHECK_FALSE(t.fullRange());
    t.add(0.999f);  // shows as 255: that is all the way
    CHECK(t.fullRange());
    t.add(1.0f);
    CHECK(t.fullRange());
    CHECK(t.min() == 0.0f);
    CHECK(t.max() == 1.0f);
}

TEST_CASE("tester: report timing summary and history") {
    tester::ReportStats s;
    CHECK(s.summary().samples == 0);
    for (int i = 0; i < 10; ++i) s.add(i % 2 == 0 ? 3.0f : 5.0f, 20.0f + i);
    const auto sum = s.summary();
    CHECK(sum.samples == 10);
    CHECK(sum.averageMs == doctest::Approx(4.0f));
    CHECK(sum.rateHz == doctest::Approx(250.0f));
    CHECK(sum.minMs == 3.0f);
    CHECK(sum.maxMs == 5.0f);
    CHECK(sum.jitterMs == doctest::Approx(1.0f));
    CHECK(sum.processingAverageUs == doctest::Approx(24.5f));
    CHECK(sum.processingMaxUs == 29.0f);

    // Oldest first, the valid entries at the end; old entries roll off.
    tester::ReportStats r;
    for (int i = 1; i <= 300; ++i) r.add(static_cast<float>(i), 0.0f);
    CHECK(r.count() == tester::ReportStats::kHistory);
    const auto h = r.history();
    CHECK(h.front() == 45.0f);
    CHECK(h.back() == 300.0f);
    CHECK(r.summary().minMs == 45.0f);

    tester::ReportStats few;
    few.add(1.0f, 0.0f);
    few.add(2.0f, 0.0f);
    const auto fh = few.history();
    CHECK(fh[tester::ReportStats::kHistory - 2] == 1.0f);
    CHECK(fh[tester::ReportStats::kHistory - 1] == 2.0f);
}

namespace {

// Config with profiles 0..2; profile 1 lists cod.exe, profile 2 lists apex.exe.
Config gamesConfig() {
    Config cfg = Config::defaults();
    while (cfg.profiles.size() < 3) cfg.profiles.push_back(Profile{});
    cfg.profiles[1].games = {"cod.exe"};
    cfg.profiles[2].games = {"apex.exe"};
    cfg.settings.activeProfile = 0;
    return cfg;
}

// Runs one check, applying a switch the way the engine does.
std::optional<int> check(AutoProfile& a, Config& cfg, std::string_view app) {
    const auto target = a.update(cfg, app);
    if (target) cfg.settings.activeProfile = *target;
    return target;
}

// The same program in front for long enough to count.
std::optional<int> focus(AutoProfile& a, Config& cfg, std::string_view app) {
    std::optional<int> result;
    for (int i = 0; i < AutoProfile::kSteadyChecks; ++i) {
        if (auto t = check(a, cfg, app)) result = t;
    }
    return result;
}

}  // namespace

TEST_CASE("auto profile: game in front switches to its profile, leaving it switches back") {
    Config cfg = gamesConfig();
    AutoProfile a;
    CHECK_FALSE(focus(a, cfg, "explorer.exe"));
    CHECK(check(a, cfg, "cod.exe") == std::nullopt);  // a moment is not enough
    CHECK(check(a, cfg, "cod.exe") == 1);
    CHECK(a.game() == "cod.exe");
    CHECK_FALSE(focus(a, cfg, "cod.exe"));  // stays, no repeats
    CHECK(focus(a, cfg, "discord.exe") == 0);
    CHECK(a.game().empty());
    CHECK(cfg.settings.activeProfile == 0);
}

TEST_CASE("auto profile: brief windows and EdgePad itself change nothing") {
    Config cfg = gamesConfig();
    AutoProfile a;
    focus(a, cfg, "cod.exe");
    REQUIRE(cfg.settings.activeProfile == 1);
    CHECK_FALSE(check(a, cfg, "popup.exe"));  // flashes up once
    CHECK_FALSE(check(a, cfg, "cod.exe"));
    CHECK_FALSE(focus(a, cfg, ""));  // EdgePad's window / unknown
    CHECK_FALSE(focus(a, cfg, "cod.exe"));
    CHECK(cfg.settings.activeProfile == 1);
}

TEST_CASE("auto profile: from one game straight to another, then back to the profile from before") {
    Config cfg = gamesConfig();
    cfg.settings.activeProfile = 0;
    AutoProfile a;
    CHECK(focus(a, cfg, "cod.exe") == 1);
    CHECK(focus(a, cfg, "apex.exe") == 2);
    CHECK(a.game() == "apex.exe");
    CHECK(focus(a, cfg, "firefox") == 0);
}

TEST_CASE("auto profile: a profile picked by hand wins") {
    Config cfg = gamesConfig();
    AutoProfile a;
    focus(a, cfg, "cod.exe");
    REQUIRE(cfg.settings.activeProfile == 1);
    cfg.settings.activeProfile = 2;  // Fn + button in game
    CHECK_FALSE(focus(a, cfg, "cod.exe"));  // not switched over again
    CHECK(a.game().empty());
    CHECK_FALSE(focus(a, cfg, "explorer.exe"));  // and no switch back
    CHECK(cfg.settings.activeProfile == 2);
    CHECK(focus(a, cfg, "cod.exe") == 1);  // coming back to the game later switches again
}

TEST_CASE("auto profile: adding the game in front to a profile switches right away") {
    Config cfg = gamesConfig();
    AutoProfile a;
    CHECK_FALSE(focus(a, cfg, "game.exe"));
    cfg.profiles[2].games.push_back("game.exe");
    CHECK(check(a, cfg, "game.exe") == 2);
}

TEST_CASE("auto profile: already on the game's profile - nothing to go back to") {
    Config cfg = gamesConfig();
    cfg.settings.activeProfile = 1;
    AutoProfile a;
    CHECK_FALSE(focus(a, cfg, "cod.exe"));
    CHECK_FALSE(focus(a, cfg, "explorer.exe"));
    CHECK(cfg.settings.activeProfile == 1);
}

TEST_CASE("auto profile: switched off, nothing happens and the state is forgotten") {
    Config cfg = gamesConfig();
    AutoProfile a;
    focus(a, cfg, "cod.exe");
    REQUIRE(cfg.settings.activeProfile == 1);
    cfg.settings.autoProfiles = false;
    CHECK_FALSE(focus(a, cfg, "explorer.exe"));
    CHECK(a.game().empty());
    CHECK(cfg.settings.activeProfile == 1);
}

TEST_CASE("auto profile: game names are file names, any case") {
    CHECK(normalizeGameName("C:\\Program Files\\Call of Duty\\COD.exe") == "cod.exe");
    CHECK(normalizeGameName("/home/me/.steam/Game.x86_64") == "game.x86_64");
    CHECK(normalizeGameName("  r5apex.EXE ") == "r5apex.exe");
    std::vector<std::string> games{"A.exe", "a.exe", "", "b.exe"};
    normalizeGames(games);
    CHECK(games == std::vector<std::string>{"a.exe", "b.exe"});
    const Config cfg = gamesConfig();
    CHECK(profileForGame(cfg, "D:\\Games\\COD.EXE") == 1);
    CHECK_FALSE(profileForGame(cfg, "notepad.exe"));
}
