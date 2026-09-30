#include <doctest/doctest.h>

#include <cmath>

#include "core/processing.hpp"

using namespace edgepad;

TEST_CASE("inner dead zone swallows small movements") {
    StickSettings s;
    s.deadzone = 0.05f;
    const Vec2 out = processStick(0.03f, 0.02f, s);
    CHECK(out.x == 0.0f);
    CHECK(out.y == 0.0f);
}

TEST_CASE("anti-dead zone jumps straight past the game's dead zone") {
    StickSettings s;
    s.deadzone = 0.05f;
    s.antiDeadzone = 0.2f;
    const Vec2 out = processStick(0.052f, 0.0f, s);
    CHECK(out.x >= 0.2f);
    CHECK(out.x < 0.21f);
    CHECK(out.y == doctest::Approx(0.0f));
}

TEST_CASE("outer dead zone reaches full output early") {
    StickSettings s;
    s.outerDeadzone = 0.05f;
    CHECK(processStick(1.0f, 0.0f, s).x == doctest::Approx(1.0f));
    CHECK(processStick(0.96f, 0.0f, s).x == doctest::Approx(1.0f));
    CHECK(processStick(-0.96f, 0.0f, s).x == doctest::Approx(-1.0f));
}

TEST_CASE("radial processing keeps the stick direction") {
    StickSettings s;
    s.antiDeadzone = 0.1f;
    s.curve = Curve::Precise;
    const Vec2 out = processStick(0.4f, 0.4f, s);
    CHECK(out.x == doctest::Approx(out.y));
    CHECK(out.x > 0.0f);
}

TEST_CASE("axial dead zone works per axis") {
    StickSettings s;
    s.shape = DeadzoneShape::Axial;
    s.deadzone = 0.1f;
    const Vec2 out = processStick(0.6f, 0.05f, s);
    CHECK(out.x > 0.0f);
    CHECK(out.y == 0.0f);
}

TEST_CASE("invert flips an axis") {
    StickSettings s;
    s.invertY = true;
    CHECK(processStick(0.0f, 0.8f, s).y < 0.0f);
}

TEST_CASE("every curve starts at 0, ends at 1 and never goes backwards") {
    const std::vector<CurvePoint> custom{{0.3f, 0.1f}, {0.7f, 0.9f}};
    for (int c = 0; c < static_cast<int>(Curve::Count); ++c) {
        for (float intensity : {0.0f, 0.5f, 1.0f}) {
            CAPTURE(c);
            CAPTURE(intensity);
            const auto curve = static_cast<Curve>(c);
            CHECK(applyCurve(0.0f, curve, intensity, custom) == doctest::Approx(0.0f));
            CHECK(applyCurve(1.0f, curve, intensity, custom) == doctest::Approx(1.0f));
            float previous = 0.0f;
            for (int i = 0; i <= 100; ++i) {
                const float y = applyCurve(static_cast<float>(i) / 100.0f, curve, intensity, custom);
                CHECK(y >= previous - 1e-6f);
                previous = y;
            }
        }
    }
}

TEST_CASE("curve presets bend the expected way") {
    const std::vector<CurvePoint> none;
    CHECK(applyCurve(0.25f, Curve::Quick, 0.5f, none) > 0.25f);
    CHECK(applyCurve(0.25f, Curve::Precise, 0.5f, none) < 0.25f);
    CHECK(applyCurve(0.2f, Curve::Digital, 1.0f, none) == doctest::Approx(1.0f));
    CHECK(applyCurve(0.5f, Curve::Default, 0.5f, none) == doctest::Approx(0.5f));
}

TEST_CASE("custom curve interpolates between points") {
    const std::vector<CurvePoint> pts{{0.5f, 0.2f}};
    CHECK(applyCurve(0.5f, Curve::Custom, 0.0f, pts) == doctest::Approx(0.2f));
    CHECK(applyCurve(0.25f, Curve::Custom, 0.0f, pts) == doctest::Approx(0.1f));
    CHECK(applyCurve(0.75f, Curve::Custom, 0.0f, pts) == doctest::Approx(0.6f));
}

TEST_CASE("trigger dead zone, trigger stop and anti-dead zone") {
    TriggerSettings t;
    t.deadzone = 0.1f;
    t.maxRange = 0.5f;
    CHECK(processTrigger(0.05f, t) == 0.0f);
    CHECK(processTrigger(0.3f, t) == doctest::Approx(0.5f));
    CHECK(processTrigger(0.5f, t) == doctest::Approx(1.0f));
    CHECK(processTrigger(0.9f, t) == doctest::Approx(1.0f));
    t.antiDeadzone = 0.3f;
    CHECK(processTrigger(0.1001f, t) == doctest::Approx(0.3f).epsilon(0.01));
}

TEST_CASE("hair trigger is fully on or off") {
    TriggerSettings t;
    t.mode = TriggerMode::HairTrigger;
    CHECK(processTrigger(0.01f, t) == 0.0f);
    CHECK(processTrigger(0.1f, t) == 1.0f);
}

TEST_CASE("RC filter: the stick counts as moving once it leaves its dead zone") {
    StickSettings s;
    s.deadzone = 0.08f;
    CHECK_FALSE(stickActive(0.0f, 0.0f, s));
    CHECK_FALSE(stickActive(0.05f, 0.05f, s));
    CHECK(stickActive(0.2f, 0.0f, s));
    s.deadzone = 0.0f;  // even without a dead zone, resting noise does not count as movement
    CHECK_FALSE(stickActive(0.015f, -0.01f, s));
    CHECK(stickActive(0.05f, 0.0f, s));
}

TEST_CASE("RC filter: zero strength passes the signal through") {
    RcFilter f;
    const Vec2 out = f.smooth({0.3f, -0.2f}, 0.0f, 0.004f, true);
    CHECK(out.x == 0.3f);
    CHECK(out.y == -0.2f);
    const Vec2 same = f.jitter({0.3f, -0.2f}, 0.0f, true);
    CHECK(same.x == 0.3f);
    CHECK(same.y == -0.2f);
}

TEST_CASE("RC filter: stabilizer is a first order low-pass while the stick moves") {
    RcFilter f;
    f.smooth({0.0f, 0.0f}, 0.5f, 0.004f, false);
    const Vec2 first = f.smooth({1.0f, 0.0f}, 0.5f, 0.004f, true);
    const float rc = 0.5f * kRcMaxTimeConstant;
    CHECK(first.x == doctest::Approx(0.004f / (rc + 0.004f)));
    Vec2 out{};
    for (int i = 0; i < 400; ++i) out = f.smooth({1.0f, 0.0f}, 0.5f, 0.004f, true);
    CHECK(out.x == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("RC filter: stabilizer stops instantly when the stick is released") {
    RcFilter f;
    for (int i = 0; i < 50; ++i) f.smooth({0.9f, 0.1f}, 1.0f, 0.004f, true);
    const Vec2 released = f.smooth({0.01f, 0.0f}, 1.0f, 0.004f, false);
    CHECK(released.x == 0.01f);
    CHECK(released.y == 0.0f);
}

TEST_CASE("RC filter: smoothing does not depend on the polling rate") {
    RcFilter slow, fast;
    slow.smooth({0.0f, 0.0f}, 0.6f, 0.004f, false);
    fast.smooth({0.0f, 0.0f}, 0.6f, 0.001f, false);
    Vec2 a{}, b{};
    for (int i = 0; i < 10; ++i) a = slow.smooth({1.0f, 0.0f}, 0.6f, 0.004f, true);  // 40 ms at 250 Hz
    for (int i = 0; i < 40; ++i) b = fast.smooth({1.0f, 0.0f}, 0.6f, 0.001f, true);  // 40 ms at 1000 Hz
    CHECK(a.x == doctest::Approx(b.x).epsilon(0.05));
}

TEST_CASE("RC filter: jitter is a zero-mean wobble while the stick moves") {
    RcFilter f;
    float sumX = 0.0f, sumY = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const Vec2 out = f.jitter({0.5f, 0.25f}, -1.0f, true);
        CHECK(std::fabs(out.x - 0.5f) == doctest::Approx(kRcMaxJitter));
        CHECK(std::fabs(out.y - 0.25f) == doctest::Approx(kRcMaxJitter));
        sumX += out.x;
        sumY += out.y;
    }
    CHECK(sumX / 4.0f == doctest::Approx(0.5f));
    CHECK(sumY / 4.0f == doctest::Approx(0.25f));
}

TEST_CASE("RC filter: no jitter while the stick rests") {
    RcFilter f;
    for (int i = 0; i < 8; ++i) {
        const Vec2 out = f.jitter({0.0f, 0.0f}, -1.0f, false);
        CHECK(out.x == 0.0f);
        CHECK(out.y == 0.0f);
    }
}
