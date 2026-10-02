#include "core/processing.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
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
    const float ad = clamp01(s.antiDeadzone);
    if (ad <= 0.0f) return clamp01(applyCurve(t, s.curve, s.curveIntensity, s.customCurve));
    // Anti-dead zone: straight to the edge of the game's dead zone, within two sensor steps.
    if (t <= 0.0f) return 0.0f;
    if (t < kAntiDeadzoneEase) return ad * t / kAntiDeadzoneEase;
    const float rest = (t - kAntiDeadzoneEase) / (1.0f - kAntiDeadzoneEase);
    return ad + (1.0f - ad) * clamp01(applyCurve(rest, s.curve, s.curveIntensity, s.customCurve));
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

bool isOneToOne(const StickSettings& s) {
    return s.deadzone <= 0.0f && s.outerDeadzone <= 0.0f && s.antiDeadzone <= 0.0f && s.curve == Curve::Default &&
           s.rcFilter == 0.0f && !s.invertX && !s.invertY;
}

bool isOneToOne(const TriggerSettings& t) {
    return t.mode == TriggerMode::Analog && t.deadzone <= 0.0f && t.maxRange >= 1.0f && t.antiDeadzone <= 0.0f &&
           !t.turbo;
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

    const float r = vectorLength(x, y);
    if (r <= dz || r <= 0.0f) return {0.0f, 0.0f};
    // Past full throw (the stick's corners reach slightly beyond a radius of 1) the direction and
    // length are kept as they are, so diagonals are not pulled in: with no dead zone and the
    // default curve the stick passes through exactly 1:1.
    const float rr = std::min(r, 1.0f);
    const float scale = shapeMagnitude(normalizeThrow(rr, s), s) / rr;
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

bool Turbo::update(bool active, int intervalMs, float dtSeconds, int randomMs) {
    if (!active) {
        reset();
        return false;
    }
    if (!running_) {  // first press goes out immediately
        running_ = true;
        on_ = true;
        timer_ = 0.0f;
        half_ = cycleHalf(intervalMs, randomMs);
        return true;
    }
    if (randomMs <= 0) half_ = cycleHalf(intervalMs, 0);  // follows the slider right away
    timer_ += std::clamp(dtSeconds, 0.0f, 0.1f);
    if (timer_ >= half_) {
        on_ = !on_;
        timer_ -= half_;
        if (on_ && randomMs > 0) half_ = cycleHalf(intervalMs, randomMs);  // a new press: a new length
        if (timer_ >= half_) timer_ = 0.0f;  // never flip twice in one report
    }
    return on_;
}

float Turbo::cycleHalf(int intervalMs, int randomMs) {
    float ms = static_cast<float>(std::clamp(intervalMs, 1, 1000));
    if (randomMs > 0) {
        // xorshift32: cheap, and plenty random for timing.
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        const float u = static_cast<float>(random_ >> 8) * (1.0f / 16777216.0f);  // 0..1
        ms = std::max(1.0f, ms + (u - 0.5f) * static_cast<float>(std::clamp(randomMs, 0, 1000)));
    }
    return ms / 2000.0f;
}

uint32_t Turbo::newSeed() {
    // Every turbo gets its own sequence, different on every run.
    static std::atomic<uint32_t> next{
        static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    uint32_t x = next.fetch_add(0x9E3779B9u, std::memory_order_relaxed);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x != 0 ? x : 1u;  // xorshift never leaves 0
}

float TriggerProcessor::apply(float value, const TriggerSettings& s, float dtSeconds) {
    float out = 0.0f;
    if (s.mode == TriggerMode::HairTrigger) {
        out = hair(value, s);
    } else {
        pressed_ = false;
        extreme_ = 0.0f;
        out = processTrigger(value, s);
    }
    if (!s.turbo) {
        turbo_.reset();
        return out;
    }
    return turbo_.update(out > 0.0f, s.turboIntervalMs, dtSeconds, s.turboRandomMs) ? 1.0f : 0.0f;
}

float TriggerProcessor::hair(float value, const TriggerSettings& s) {
    const float v = clamp01(value);
    const float activation = std::max(std::min(clamp01(s.deadzone), 0.9f), kHairMinActivation);
    const float resetDistance = std::clamp(s.hairResetDistance, kHairMinReset, 0.5f);

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
    turbo_.reset();
}

Vec2 recenterStick(Vec2 raw, const std::array<float, 2>& center) {
    auto axis = [](float v, float c) {
        c = std::clamp(c, -0.5f, 0.5f);
        const float span = v >= c ? 1.0f - c : 1.0f + c;
        return clamp11((v - c) / span);
    };
    return {axis(raw.x, center[0]), axis(raw.y, center[1])};
}

Vec2 GyroAim::update(const MotionState& motion, const GyroSettings& s, const std::array<float, 3>& bias, bool held,
                     bool pressedNow, float dtSeconds) {
    switch (s.activation) {
        case GyroActivation::Always: active_ = true; break;
        case GyroActivation::WhileHeld: active_ = held; break;
        case GyroActivation::Toggle:
            if (pressedNow) toggled_ = !toggled_;
            active_ = toggled_;
            break;
        case GyroActivation::Off:
        case GyroActivation::Count: active_ = false; break;
    }
    if (!active_) {
        smoothed_ = {};
        return {};
    }

    // Angular velocity in degrees per second. [0] pitch, [1] yaw, [2] roll.
    auto rate = [&](size_t i) { return (static_cast<float>(motion.gyro[i]) - bias[i]) / kGyroCountsPerDegPerSec; };
    const float turn = s.horizontalAxis == GyroAxis::Roll ? rate(2) : rate(1);
    Vec2 v{-turn, rate(0) * s.verticalRatio};  // turning the pad left aims left, tilting it up aims up
    if (s.invertX) v.x = -v.x;
    if (s.invertY) v.y = -v.y;

    // Soft tiered smoothing: slow (tremor sized) movements are averaged, fast ones pass straight through.
    const float dt = std::clamp(dtSeconds, 0.0005f, 0.1f);
    const float alpha = dt / (0.040f + dt);
    smoothed_.x += alpha * (v.x - smoothed_.x);
    smoothed_.y += alpha * (v.y - smoothed_.y);
    const float speed = vectorLength(v.x, v.y);
    const float threshold = std::clamp(s.smoothing, 0.0f, 1.0f) * 20.0f;
    const float direct = threshold <= 0.0f ? 1.0f : clamp01((speed - threshold * 0.5f) / (threshold * 0.5f));
    v.x = direct * v.x + (1.0f - direct) * smoothed_.x;
    v.y = direct * v.y + (1.0f - direct) * smoothed_.y;

    const float magnitude = vectorLength(v.x, v.y);
    const float deadzone = std::max(0.0f, s.deadzone);
    if (magnitude <= deadzone) return {};
    // Stick deflection grows with rotation speed; full deflection at 360 / sensitivity deg/s.
    const float deflection = clamp01((magnitude - deadzone) * std::max(0.01f, s.sensitivity) / 360.0f);
    const float ad = clamp01(s.antiDeadzone);
    const float out = ad + (1.0f - ad) * deflection;
    return {v.x / magnitude * out, v.y / magnitude * out};
}

void GyroAim::reset() {
    smoothed_ = {};
    toggled_ = false;
    active_ = false;
}

bool stickActive(float x, float y, const StickSettings& s) {
    return vectorLength(x, y) > std::max(clamp01(s.deadzone), kRcMinActiveDeflection);
}

Vec2 RcFilter::apply(Vec2 raw, float strength, float dtSeconds, bool active) {
    strength = clamp11(strength);
    if (!active || strength <= 0.0f) {
        // Follow the stick exactly while it rests (or with no stabilizer): letting go stops
        // instantly, and the next movement is filtered starting from where the stick really is.
        state_ = raw;
        return raw;
    }
    // First order RC low-pass: alpha = dt / (RC + dt). Time based, so the feel does not
    // change with the controller's polling rate.
    const float dt = std::clamp(dtSeconds, 0.0005f, 0.1f);
    const float rc = strength * kRcMaxTimeConstant;
    const float alpha = dt / (rc + dt);
    state_.x += alpha * (raw.x - state_.x);
    state_.y += alpha * (raw.y - state_.y);
    return state_;
}

Vec2 RcFilter::amplify(Vec2 out, float strength, float dtSeconds, bool active) {
    strength = clamp11(strength);
    const float length = vectorLength(out.x, out.y);
    if (!active || strength >= 0.0f || length <= 1e-4f) {
        lowPass_ = out;  // follow the stick: the next movement starts from where it really is
        return out;
    }
    const float amount = -strength;
    // The same RC low-pass as the stabilizer (time based: the same at 250 or 1000 Hz)...
    const float dt = std::clamp(dtSeconds, 0.0005f, 0.1f);
    const float alpha = dt / (amount * kRcMaxTimeConstant + dt);
    lowPass_.x += alpha * (out.x - lowPass_.x);
    lowPass_.y += alpha * (out.y - lowPass_.y);
    // ...flipped: what it would remove is added back, amplified.
    const float gain = 1.0f + (kRcMaxAmplify - 1.0f) * amount;
    const Vec2 boost{gain * (out.x - lowPass_.x), gain * (out.y - lowPass_.y)};
    // Only the sideways part, as a turn of the stick: its length - the aim speed - stays exact.
    const float sideways = (out.x * boost.y - out.y * boost.x) / length;
    const float angle = std::clamp(std::atan2(sideways, length), -kRcJitterMaxAngle, kRcJitterMaxAngle);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {out.x * c - out.y * s, out.x * s + out.y * c};
}

void RcFilter::reset() {
    state_ = {};
    lowPass_ = {};
}

}  // namespace edgepad
