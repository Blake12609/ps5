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

TEST_CASE("hair trigger fires instantly and works as a rapid trigger") {
    TriggerSettings t;
    t.mode = TriggerMode::HairTrigger;
    t.hairResetDistance = 0.05f;
    TriggerProcessor p;
    CHECK(p.apply(0.0f, t) == 0.0f);
    CHECK(p.apply(0.01f, t) == 0.0f);   // resting noise never fires
    CHECK(p.apply(0.03f, t) == 1.0f);   // the slightest pull fires a full press
    CHECK(p.apply(0.8f, t) == 1.0f);
    CHECK(p.apply(0.77f, t) == 1.0f);   // small wobble while holding keeps it pressed
    CHECK(p.apply(0.74f, t) == 0.0f);   // coming back up 6% releases, trigger still 74% down
    CHECK(p.apply(0.6f, t) == 0.0f);
    CHECK(p.apply(0.64f, t) == 0.0f);
    CHECK(p.apply(0.66f, t) == 1.0f);   // pulling down 6% fires again without letting go
    CHECK(p.apply(0.0f, t) == 0.0f);
    CHECK(p.apply(0.04f, t) == 1.0f);   // after a full release the next pull fires at once
}

TEST_CASE("hair trigger resets on a 1% lift at any depth") {
    TriggerSettings t;
    t.mode = TriggerMode::HairTrigger;
    CHECK(t.hairResetDistance == doctest::Approx(0.01f));  // default
    auto counts = [](int c) { return static_cast<float>(c) / 255.0f; };  // the sensor reports 0..255
    for (int depth : {10, 40, 128, 200, 255}) {
        CAPTURE(depth);
        TriggerProcessor p;
        CHECK(p.apply(counts(depth), t) == 1.0f);
        CHECK(p.apply(counts(depth - 3), t) == 0.0f);  // eased off ~1%: released straight away
        CHECK(p.apply(counts(depth - 1), t) == 0.0f);
        CHECK(p.apply(counts(depth), t) == 1.0f);      // pulled ~1% again: fires again
    }
}

TEST_CASE("hair trigger ignores sensor noise while held steady") {
    TriggerSettings t;
    t.mode = TriggerMode::HairTrigger;
    TriggerProcessor p;
    CHECK(p.apply(120.0f / 255.0f, t) == 1.0f);
    for (int i = 0; i < 50; ++i) {
        const int wobble = (i % 3) - 1;  // +-1 step of noise
        CHECK(p.apply(static_cast<float>(120 + wobble) / 255.0f, t) == 1.0f);
    }
}

TEST_CASE("hair trigger activation point") {
    TriggerSettings t;
    t.mode = TriggerMode::HairTrigger;
    t.deadzone = 0.3f;
    TriggerProcessor p;
    CHECK(p.apply(0.25f, t) == 0.0f);
    CHECK(p.apply(0.31f, t) == 1.0f);
    CHECK(p.apply(0.2f, t) == 0.0f);  // above the activation point it is back at "rest"
}

TEST_CASE("trigger processor in analog mode matches processTrigger") {
    TriggerSettings t;
    t.deadzone = 0.1f;
    t.maxRange = 0.6f;
    TriggerProcessor p;
    for (float v : {0.0f, 0.05f, 0.3f, 0.6f, 1.0f}) CHECK(p.apply(v, t) == processTrigger(v, t));
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
    const Vec2 out = f.apply({0.3f, -0.2f}, 0.0f, 0.004f, true);
    CHECK(out.x == 0.3f);
    CHECK(out.y == -0.2f);
}

TEST_CASE("RC filter: stabilizer is a first order low-pass while the stick moves") {
    RcFilter f;
    f.apply({0.0f, 0.0f}, 0.5f, 0.004f, false);
    const Vec2 first = f.apply({1.0f, 0.0f}, 0.5f, 0.004f, true);
    const float rc = 0.5f * kRcMaxTimeConstant;
    CHECK(first.x == doctest::Approx(0.004f / (rc + 0.004f)));
    Vec2 out{};
    for (int i = 0; i < 400; ++i) out = f.apply({1.0f, 0.0f}, 0.5f, 0.004f, true);
    CHECK(out.x == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("RC filter: stops instantly when the stick is released") {
    for (float strength : {1.0f, -1.0f}) {
        RcFilter f;
        for (int i = 0; i < 50; ++i) f.apply({0.9f, 0.1f}, strength, 0.004f, true);
        const Vec2 released = f.apply({0.01f, 0.0f}, strength, 0.004f, false);
        CHECK(released.x == 0.01f);
        CHECK(released.y == 0.0f);
    }
}

TEST_CASE("RC filter: smoothing does not depend on the polling rate") {
    for (float strength : {0.6f, -0.6f}) {
        RcFilter slow, fast;
        slow.apply({0.0f, 0.0f}, strength, 0.004f, false);
        fast.apply({0.0f, 0.0f}, strength, 0.001f, false);
        Vec2 a{}, b{};
        for (int i = 0; i < 10; ++i) a = slow.apply({0.5f, 0.0f}, strength, 0.004f, true);  // 40 ms at 250 Hz
        for (int i = 0; i < 40; ++i) b = fast.apply({0.5f, 0.0f}, strength, 0.001f, true);  // 40 ms at 1000 Hz
        CHECK(a.x == doctest::Approx(b.x).epsilon(0.05));
    }
}

TEST_CASE("RC filter: jitter is the exact mirror of the stabilizer") {
    // Any stick movement: stabilizer + jitter = 2 x the real stick, report by report.
    RcFilter stabilizer, jitter;
    stabilizer.apply({0.2f, 0.1f}, 0.7f, 0.004f, false);
    jitter.apply({0.2f, 0.1f}, -0.7f, 0.004f, false);
    for (int i = 0; i < 200; ++i) {
        const float t = static_cast<float>(i) * 0.004f;
        const Vec2 stick{0.4f + 0.3f * std::sin(t * 9.0f), 0.2f * std::cos(t * 5.0f)};
        const Vec2 lag = stabilizer.apply(stick, 0.7f, 0.004f, true);
        const Vec2 lead = jitter.apply(stick, -0.7f, 0.004f, true);
        CHECK((lag.x + lead.x) / 2.0f == doctest::Approx(stick.x));
        CHECK((lag.y + lead.y) / 2.0f == doctest::Approx(stick.y));
    }
}

TEST_CASE("RC filter: jitter runs ahead of the thumb and amplifies micro-movements") {
    RcFilter f;
    f.apply({0.5f, 0.0f}, -1.0f, 0.004f, false);
    // Pushing further: the output leads the stick.
    float stick = 0.5f;
    Vec2 out{};
    for (int i = 0; i < 10; ++i) {
        stick += 0.01f;
        out = f.apply({stick, 0.0f}, -1.0f, 0.004f, true);
    }
    CHECK(out.x > stick);
    // Tiny +-1 step sensor noise while aiming comes out bigger than it went in.
    RcFilter n;
    n.apply({0.5f, 0.0f}, -1.0f, 0.004f, false);
    float minOut = 1.0f, maxOut = -1.0f;
    for (int i = 0; i < 100; ++i) {
        const float noisy = 0.5f + ((i % 2) ? 1.0f : -1.0f) / 127.0f;
        const float x = n.apply({noisy, 0.0f}, -1.0f, 0.004f, true).x;
        minOut = std::min(minOut, x);
        maxOut = std::max(maxOut, x);
    }
    CHECK(maxOut - minOut > 1.8f * (2.0f / 127.0f));
}

TEST_CASE("RC filter: holding perfectly still adds no jitter") {
    RcFilter f;
    f.apply({0.6f, 0.2f}, -1.0f, 0.004f, false);
    for (int i = 0; i < 50; ++i) {
        const Vec2 out = f.apply({0.6f, 0.2f}, -1.0f, 0.004f, true);
        CHECK(out.x == doctest::Approx(0.6f));
        CHECK(out.y == doctest::Approx(0.2f));
    }
}

TEST_CASE("RC filter: no jitter while the stick rests") {
    RcFilter f;
    for (int i = 0; i < 8; ++i) {
        const float wobble = (i % 2) ? 0.02f : -0.02f;
        const Vec2 out = f.apply({wobble, 0.0f}, -1.0f, 0.004f, false);
        CHECK(out.x == wobble);
    }
}
