#include "core/processing.hpp"

#include <algorithm>
#include <cmath>

namespace edgepad {

float clamp01(float v) {
    if (!(v > 0.0f)) return 0.0f;  // also catches NaN
    return v > 1.0f ? 1.0f : v;
}

float clamp11(float v) {
    if (std::isnan(v)) return 0.0f;
    return std::clamp(v, -1.0f, 1.0f);
}

namespace {

// Piecewise linear through (0,0), the custom points (sorted by x) and (1,1).
// Runs for every controller report, so it must not allocate.
float interpolateCustom(const std::vector<CurvePoint>& custom, float t) {
    float px = 0.0f;
    float py = 0.0f;
    for (const auto& p : custom) {
        if (!(p.x > px) || p.x >= 1.0f) continue;  // skip out-of-order or out-of-range points
        const float y = clamp01(p.y);
        if (t <= p.x) return py + (y - py) * (t - px) / (p.x - px);
        px = p.x;
        py = y;
    }
    return py + (1.0f - py) * (t - px) / (1.0f - px);
}

// Distance travelled between the inner and outer dead zones, normalised to 0..1.
float normalizeThrow(float r, const StickSettings& s) {
    const float lo = std::min(clamp01(s.deadzone), 0.9f);
    const float hi = std::max(lo + 0.01f, 1.0f - clamp01(s.outerDeadzone));
    return clamp01((r - lo) / (hi - lo));
}

float shapeMagnitude(float t, const StickSettings& s) {
    const float c = clamp01(applyCurve(t, s.curve, s.curveIntensity, s.customCurve));
    if (c <= 0.0f) return 0.0f;
    const float ad = clamp01(s.antiDeadzone);
    return ad + (1.0f - ad) * c;
}

}  // namespace

float applyCurve(float t, Curve curve, float intensity, const std::vector<CurvePoint>& custom) {
    t = clamp01(t);
    const float s = clamp01(intensity);
    switch (curve) {
        case Curve::Quick: {  // fast response right away
            const float k = 1.0f + 2.0f * s;
            return 1.0f - std::pow(1.0f - t, k);
        }
        case Curve::Precise: {  // slow start for fine aim
            const float k = 1.0f + 2.0f * s;
            return std::pow(t, k);
        }
        case Curve::Steady: {  // flat, predictable mid range
            const float k = 1.0f + 2.0f * s;
            const float d = 2.0f * t - 1.0f;
            return 0.5f + 0.5f * std::copysign(std::pow(std::fabs(d), k), d);
        }
        case Curve::Digital: {  // reaches full output after a short throw
            const float reach = 0.6f - 0.5f * s;
            return std::min(1.0f, t / reach);
        }
        case Curve::Dynamic: {  // slow for small movements, fast for big ones
            const float k = 1.0f + 3.0f * s;
            const float a = std::pow(t, k);
            const float b = std::pow(1.0f - t, k);
            return (a + b) > 0.0f ? a / (a + b) : 0.0f;
        }
        case Curve::Custom:
            return interpolateCustom(custom, t);
        case Curve::Default:
        case Curve::Count:
            break;
    }
    return t;
}

Vec2 processStick(float x, float y, const StickSettings& s) {
    if (s.invertX) x = -x;
    if (s.invertY) y = -y;
    x = clamp11(x);
    y = clamp11(y);
    const float dz = clamp01(s.deadzone);

    if (s.shape == DeadzoneShape::Axial) {
        auto axis = [&](float v) {
            const float a = std::fabs(v);
            if (a <= dz) return 0.0f;
            return std::copysign(shapeMagnitude(normalizeThrow(a, s), s), v);
        };
        return {axis(x), axis(y)};
    }

    const float r = std::hypot(x, y);
    if (r <= dz || r <= 0.0f) return {0.0f, 0.0f};
    const float magnitude = shapeMagnitude(normalizeThrow(std::min(r, 1.0f), s), s);
    const float scale = magnitude / r;
    return {clamp11(x * scale), clamp11(y * scale)};
}

float processTrigger(float value, const TriggerSettings& s) {
    const float v = clamp01(value);
    const float lo = std::min(clamp01(s.deadzone), 0.95f);
    if (s.mode == TriggerMode::HairTrigger) return v > std::max(lo, kHairMinActivation) ? 1.0f : 0.0f;  // stateless view
    if (v <= lo) return 0.0f;
    const float hi = std::max(lo + 0.02f, clamp01(s.maxRange));
    const float t = clamp01((v - lo) / (hi - lo));
    const float ad = clamp01(s.antiDeadzone);
    return ad + (1.0f - ad) * t;
}

float TriggerProcessor::apply(float value, const TriggerSettings& s) {
    if (s.mode != TriggerMode::HairTrigger) {
        reset();
        return processTrigger(value, s);
    }
    const float v = clamp01(value);
    const float activation = std::max(std::min(clamp01(s.deadzone), 0.9f), kHairMinActivation);
    const float resetDistance = std::clamp(s.hairResetDistance, 0.01f, 0.5f);

    if (v <= activation) {  // back at the top: always released, next pull fires immediately
        pressed_ = false;
        extreme_ = v;
        return 0.0f;
    }
    if (pressed_) {
        extreme_ = std::max(extreme_, v);
        if (v <= extreme_ - resetDistance) {  // started coming back up: release right away
            pressed_ = false;
            extreme_ = v;
        }
    } else {
        extreme_ = std::min(extreme_, v);
        if (extreme_ <= activation || v >= extreme_ + resetDistance) {  // pulled again: fire
            pressed_ = true;
            extreme_ = v;
        }
    }
    return pressed_ ? 1.0f : 0.0f;
}

void TriggerProcessor::reset() {
    pressed_ = false;
    extreme_ = 0.0f;
}

bool stickActive(float x, float y, const StickSettings& s) {
    return std::hypot(x, y) > std::max(clamp01(s.deadzone), kRcMinActiveDeflection);
}

Vec2 RcFilter::smooth(Vec2 raw, float strength, float dtSeconds, bool active) {
    strength = clamp11(strength);
    if (!active || strength <= 0.0f) {
        // Follow the stick exactly while it rests: letting go stops instantly, and the next
        // movement is smoothed starting from where the stick really is.
        state_ = raw;
        primed_ = true;
        return raw;
    }
    if (!primed_) {
        state_ = raw;
        primed_ = true;
        return raw;
    }
    // First order RC low-pass: alpha = dt / (RC + dt). Time based, so the
    // feel does not change with the controller's polling rate.
    const float dt = std::clamp(dtSeconds, 0.0005f, 0.1f);
    const float rc = strength * kRcMaxTimeConstant;
    const float alpha = dt / (rc + dt);
    state_.x += alpha * (raw.x - state_.x);
    state_.y += alpha * (raw.y - state_.y);
    return state_;
}

Vec2 RcFilter::jitter(Vec2 out, float strength, float dtSeconds, bool active) {
    strength = clamp11(strength);
    const float magnitude = std::hypot(out.x, out.y);
    if (!active || strength >= 0.0f || magnitude <= 1e-4f) {
        jitterClock_ = 0.0f;
        jitterSide_ = 1.0f;
        return out;
    }
    // Change side on a fixed clock (not per report) so it is identical at 250 or 1000 Hz.
    jitterClock_ += std::clamp(dtSeconds, 0.0f, 0.1f);
    while (jitterClock_ >= kRcJitterFlipSeconds) {
        jitterClock_ -= kRcJitterFlipSeconds;
        jitterSide_ = -jitterSide_;
    }
    // Offset at right angles to the stick direction: the aim wobbles side to side around the
    // target while the length of the stick vector (aim speed) never drops.
    const float amplitude = -strength * kRcMaxJitter * jitterSide_;
    Vec2 j{out.x - (out.y / magnitude) * amplitude, out.y + (out.x / magnitude) * amplitude};
    const float length = std::hypot(j.x, j.y);
    if (length > 1.0f) {
        j.x /= length;
        j.y /= length;
    }
    return j;
}

void RcFilter::reset() {
    state_ = {};
    primed_ = false;
    jitterClock_ = 0.0f;
    jitterSide_ = 1.0f;
}

}  // namespace edgepad
