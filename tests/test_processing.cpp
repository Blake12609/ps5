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

TEST_CASE("anti-dead zone makes the game's dead zone smaller and adds none") {
    StickSettings s;
    s.antiDeadzone = 0.2f;  // the game ignores the first 20%
    CHECK(processStick(0.0f, 0.0f, s).x == 0.0f);
    const float step = 1.0f / 128.0f;  // one sensor step of the DualSense
    // Resting flicker of one step stays below half of it: no drift in the game.
    CHECK(processStick(step, 0.0f, s).x == doctest::Approx(0.1f));
    // Two steps of stick travel already reach the edge of the game's dead zone.
    CHECK(processStick(2.0f * step + 1e-4f, 0.0f, s).x >= 0.2f);
    CHECK(processStick(0.05f, 0.0f, s).x < 0.25f);
    float previous = 0.0f;
    for (int i = 1; i <= 1000; ++i) {
        const float r = static_cast<float>(i) / 1000.0f;
        const float out = processStick(r, 0.0f, s).x;
        CHECK(out > 0.0f);   // every movement registers
        CHECK(out >= r);     // never less than without it: it never adds dead zone
        CHECK(out >= previous);
        previous = out;
    }
    CHECK(previous == doctest::Approx(1.0f));
    // Diagonals too, and it keeps the direction.
    const Vec2 d = processStick(0.01f, 0.01f, s);
    CHECK(d.x == doctest::Approx(d.y));
    CHECK(std::hypot(d.x, d.y) > 0.1f);
}

TEST_CASE("anti-dead zone with an inner dead zone starts where the dead zone ends") {
    StickSettings s;
    s.deadzone = 0.05f;  // chosen for a drifting stick
    s.antiDeadzone = 0.2f;
    CHECK(processStick(0.049f, 0.0f, s).x == 0.0f);
    CHECK(processStick(0.05f + 2.0f / 128.0f * 0.95f + 1e-3f, 0.0f, s).x >= 0.2f);
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

TEST_CASE("RC filter: stabilizer stops instantly when the stick is released") {
    RcFilter f;
    for (int i = 0; i < 50; ++i) f.apply({0.9f, 0.1f}, 1.0f, 0.004f, true);
    const Vec2 released = f.apply({0.01f, 0.0f}, 1.0f, 0.004f, false);
    CHECK(released.x == 0.01f);
    CHECK(released.y == 0.0f);
}

TEST_CASE("RC filter: smoothing does not depend on the polling rate") {
    RcFilter slow, fast;
    slow.apply({0.0f, 0.0f}, 0.6f, 0.004f, false);
    fast.apply({0.0f, 0.0f}, 0.6f, 0.001f, false);
    Vec2 a{}, b{};
    for (int i = 0; i < 10; ++i) a = slow.apply({1.0f, 0.0f}, 0.6f, 0.004f, true);  // 40 ms at 250 Hz
    for (int i = 0; i < 40; ++i) b = fast.apply({1.0f, 0.0f}, 0.6f, 0.001f, true);  // 40 ms at 1000 Hz
    CHECK(a.x == doctest::Approx(b.x).epsilon(0.05));
}

TEST_CASE("RC filter: negative values leave the raw stick alone (jitter comes after processing)") {
    RcFilter f;
    for (int i = 0; i < 20; ++i) {
        const Vec2 out = f.apply({0.4f + 0.01f * static_cast<float>(i), 0.1f}, -1.0f, 0.004f, true);
        CHECK(out.x == doctest::Approx(0.4f + 0.01f * static_cast<float>(i)));
        CHECK(out.y == doctest::Approx(0.1f));
    }
}

namespace {

// A thumb holding the stick at `aim` with a small tremor across it, at `hz` reports per second.
Vec2 tremor(Vec2 aim, float t, float size = 0.01f, float freq = 9.0f) {
    const float len = std::hypot(aim.x, aim.y);
    const float w = size * std::sin(6.2832f * freq * t);
    return {aim.x - w * aim.y / len, aim.y + w * aim.x / len};
}

float angleOf(Vec2 v) { return std::atan2(v.y, v.x); }

}  // namespace

TEST_CASE("RC amplify never changes the aim speed (stick length)") {
    for (float angle = 0.0f; angle < 6.28f; angle += 0.4f) {
        for (float length : {0.06f, 0.2f, 0.55f, 1.0f}) {
            RcFilter f;
            const Vec2 aim{length * std::cos(angle), length * std::sin(angle)};
            for (int i = 0; i < 250; ++i) {
                const Vec2 in = tremor(aim, static_cast<float>(i) * 0.004f, 0.02f);
                const Vec2 out = f.amplify(in, -1.0f, 0.004f, true);
                CHECK(std::hypot(out.x, out.y) == doctest::Approx(std::hypot(in.x, in.y)).epsilon(1e-4));
            }
        }
    }
}

TEST_CASE("RC amplify is the stabilizer's RC filter flipped around") {
    RcFilter f;
    f.amplify({0.5f, 0.0f}, -0.5f, 0.004f, false);  // resting at (0.5, 0): the filter follows
    const Vec2 in{0.5f, 0.05f};                      // the thumb moves up a little
    const Vec2 out = f.amplify(in, -0.5f, 0.004f, true);
    // y += alpha * (x - y) with alpha = dt / (RC + dt), RC = 0.5 * 40 ms...
    const float alpha = 0.004f / (0.5f * kRcMaxTimeConstant + 0.004f);
    const Vec2 lowPass{0.5f, alpha * 0.05f};
    // ...and what it would remove, amplified (gain 4.5 at -50%), applied sideways only.
    const float gain = 1.0f + (kRcMaxAmplify - 1.0f) * 0.5f;
    const float len = std::hypot(in.x, in.y);
    const float sideways = (in.x * gain * (in.y - lowPass.y) - in.y * gain * (in.x - lowPass.x)) / len;
    CHECK(angleOf(out) == doctest::Approx(angleOf(in) + std::atan2(sideways, len)).epsilon(1e-4));
    CHECK(angleOf(out) > angleOf(in));  // it pushes further the way the thumb moved
}

TEST_CASE("RC amplify: a stick held perfectly still gets no jitter") {
    RcFilter f;
    f.amplify({0.3f, 0.4f}, -1.0f, 0.004f, false);
    for (int i = 0; i < 100; ++i) {
        const Vec2 out = f.amplify({0.3f, 0.4f}, -1.0f, 0.004f, true);
        CHECK(out.x == doctest::Approx(0.3f));
        CHECK(out.y == doctest::Approx(0.4f));
    }
}

TEST_CASE("RC amplify: a movement's kick decays like an RC filter") {
    RcFilter f;
    f.amplify({0.6f, 0.0f}, -1.0f, 0.004f, false);
    const Vec2 moved{0.6f, 0.03f};
    float previous = angleOf(f.amplify(moved, -1.0f, 0.004f, true)) - angleOf(moved);
    const float alpha = 0.004f / (kRcMaxTimeConstant + 0.004f);
    CHECK(previous > 0.0f);
    for (int i = 0; i < 20; ++i) {  // the thumb holds still: the kick fades with the RC time constant
        const float extra = angleOf(f.amplify(moved, -1.0f, 0.004f, true)) - angleOf(moved);
        CHECK(std::tan(extra) == doctest::Approx(std::tan(previous) * (1.0f - alpha)).epsilon(0.01));
        previous = extra;
    }
}

TEST_CASE("RC amplify: thumb tremor becomes jitter to both sides, stronger at stronger settings") {
    auto peakJitter = [](float strength, float dt) {
        RcFilter f;
        const Vec2 aim{0.5f, 0.2f};
        f.amplify(aim, strength, dt, false);
        float up = 0.0f, down = 0.0f;
        for (float t = 0.0f; t < 1.0f; t += dt) {
            const Vec2 in = tremor(aim, t);
            const float extra = angleOf(f.amplify(in, strength, dt, true)) - angleOf(in);
            up = std::max(up, extra);
            down = std::min(down, extra);
        }
        CHECK(up > 0.0f);
        CHECK(down < 0.0f);
        return up - down;
    };
    const float strong = peakJitter(-1.0f, 0.004f);
    CHECK(strong > 3.0f * peakJitter(-0.3f, 0.004f));
    CHECK(strong < 2.0f * kRcJitterMaxAngle + 1e-4f);
    // The same thumb movement gives the same jitter at 250 Hz and 1000 Hz (time based RC).
    CHECK(peakJitter(-1.0f, 0.001f) == doctest::Approx(strong).epsilon(0.15));
}

TEST_CASE("RC amplify: pushing the stick straight out adds no jitter") {
    RcFilter f;
    f.amplify({0.1f, 0.1f}, -1.0f, 0.004f, false);
    for (int i = 1; i <= 50; ++i) {
        const float r = 0.1f + 0.012f * static_cast<float>(i);
        const Vec2 out = f.amplify({r, r}, -1.0f, 0.004f, true);
        CHECK(out.x == doctest::Approx(r));
        CHECK(out.y == doctest::Approx(r));
    }
}

TEST_CASE("RC filter: no amplify with the stabilizer or with the filter off") {
    RcFilter f;
    for (float strength : {0.0f, 0.5f}) {
        f.amplify({0.3f, 0.2f}, strength, 0.004f, true);
        const Vec2 out = f.amplify({0.32f, 0.25f}, strength, 0.004f, true);
        CHECK(out.x == 0.32f);
        CHECK(out.y == 0.25f);
    }
}

TEST_CASE("RC filter: nothing while the stick rests") {
    RcFilter f;
    for (int i = 0; i < 8; ++i) {
        const Vec2 out = f.amplify({0.01f * static_cast<float>(i % 2), 0.01f}, -1.0f, 0.004f, false);
        CHECK(out.x == 0.01f * static_cast<float>(i % 2));
        CHECK(out.y == 0.01f);
    }
}
