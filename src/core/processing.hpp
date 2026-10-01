#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/types.hpp"

namespace edgepad {

// Response curves modelled after the DualSense Edge stick presets.
enum class Curve : uint8_t { Default, Quick, Precise, Steady, Digital, Dynamic, Custom, Count };
enum class DeadzoneShape : uint8_t { Radial, Axial, Count };
enum class TriggerMode : uint8_t { Analog, HairTrigger, Count };
enum class TriggerResistance : uint8_t { Off, Wall, Count };

struct CurvePoint {
    float x = 0.0f;
    float y = 0.0f;
    bool operator==(const CurvePoint&) const = default;
};

struct StickSettings {
    // Defaults are exactly 1:1: the stick reaches the game as the controller sends it, and the
    // game's own dead zone handles resting noise (like playing on the controller directly).
    float deadzone = 0.0f;       // inner dead zone, fraction of full throw
    float outerDeadzone = 0.0f;  // outer edge treated as full deflection
    float antiDeadzone = 0.0f;    // minimum output once past the dead zone (cancels in-game dead zones)
    DeadzoneShape shape = DeadzoneShape::Radial;
    Curve curve = Curve::Default;
    float curveIntensity = 0.5f;  // 0..1, how strongly the curve bends
    std::vector<CurvePoint> customCurve{{0.25f, 0.15f}, {0.5f, 0.4f}, {0.75f, 0.7f}};
    // GameSir style RC filter, -1..1, only active while the stick is moved.
    // Positive: RC low-pass "stabilizer". Negative: "jitter" that keeps the aim speed unchanged.
    float rcFilter = 0.0f;
    bool invertX = false;
    bool invertY = false;

    bool operator==(const StickSettings&) const = default;
};

// True when the settings leave the stick exactly as the controller sends it (1:1).
bool isOneToOne(const StickSettings& s);

// Hair trigger reset distance: default 1% (about 3 steps of the trigger sensor, so holding the
// trigger steady never releases it by accident), minimum 0.5%.
inline constexpr float kHairDefaultReset = 0.01f;
inline constexpr float kHairMinReset = 0.005f;

struct TriggerSettings {
    float deadzone = 0.0f;  // start of the effective range (hair trigger: activation point)
    float maxRange = 1.0f;  // end of the effective range (software trigger stop)
    float antiDeadzone = 0.0f;
    TriggerMode mode = TriggerMode::Analog;
    TriggerResistance resistance = TriggerResistance::Off;  // adaptive trigger wall
    float resistancePosition = 0.5f;                        // 0..1 of the trigger travel
    int resistanceStrength = 6;                             // 1..8
    // Hair trigger: how far the trigger has to come back up to release, and go down again to
    // fire, anywhere in its travel (rapid trigger). Smaller = faster follow-up shots.
    float hairResetDistance = kHairDefaultReset;
    bool turbo = false;        // rapid fire: pulse full presses while the trigger is pressed
    int turboIntervalMs = 50;  // time between turbo presses, 1..100 ms

    bool operator==(const TriggerSettings&) const = default;
};

// Gyro aiming: turning / tilting the controller moves the right stick.
enum class GyroActivation : uint8_t { Off, Always, WhileHeld, Toggle, Count };
enum class GyroAxis : uint8_t { Yaw, Roll, Count };  // what turns the aim left / right

struct GyroSettings {
    GyroActivation activation = GyroActivation::Off;
    Button button = Button::L2;       // hold / toggle button (e.g. aim down sights)
    float sensitivity = 3.0f;         // full stick deflection at 360/sensitivity degrees per second
    float verticalRatio = 1.0f;       // vertical speed relative to horizontal
    GyroAxis horizontalAxis = GyroAxis::Yaw;
    float deadzone = 1.5f;            // degrees per second ignored (hand tremor, sensor noise)
    float smoothing = 0.25f;          // 0..1, smooths slow movements only, fast ones stay instant
    float antiDeadzone = 0.15f;       // minimum stick output while the gyro moves (beats game dead zones)
    bool invertX = false;
    bool invertY = false;

    bool operator==(const GyroSettings&) const = default;
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// DualSense gyro: raw counts per degree per second.
inline constexpr float kGyroCountsPerDegPerSec = 16.0f;

// Removes a stick's resting offset (drift) while keeping full deflection reachable.
Vec2 recenterStick(Vec2 raw, const std::array<float, 2>& center);

// Gyro -> right stick. Stateful for smoothing and toggle activation.
class GyroAim {
public:
    // `held`: the activation button is down; `pressedNow`: it went down this report.
    Vec2 update(const MotionState& motion, const GyroSettings& s, const std::array<float, 3>& bias, bool held,
                bool pressedNow, float dtSeconds);
    bool active() const { return active_; }
    void reset();

private:
    Vec2 smoothed_{};
    bool toggled_ = false;
    bool active_ = false;
};

// Maximum time constant of the RC low-pass at rcFilter = 1.0 (seconds).
inline constexpr float kRcMaxTimeConstant = 0.040f;
// Jitter at rcFilter = -1.0: sideways movement as a fraction of full stick throw.
inline constexpr float kRcMaxJitter = 0.06f;
// A new jitter swing starts every 5 ms (fixed clock: the same at 250 or 1000 Hz) ...
inline constexpr float kRcJitterStepSeconds = 0.005f;
// ... and moves there through an RC stage with this time constant.
inline constexpr float kRcJitterSmoothSeconds = 0.002f;
// The jitter never turns the aim further than this (only matters for tiny stick movements).
inline constexpr float kRcJitterMaxAngle = 0.5236f;  // 30 degrees
// The RC filter only runs once the stick is pushed past its dead zone, and at least this far,
// so stick noise at rest never triggers it.
inline constexpr float kRcMinActiveDeflection = 0.03f;

float clamp01(float v);
float clamp11(float v);

// Maps t (0..1, distance travelled past the dead zone) through a response curve.
float applyCurve(float t, Curve curve, float intensity, const std::vector<CurvePoint>& custom);

// Full stick chain: invert -> dead zone -> curve -> anti-dead zone -> outer dead zone.
Vec2 processStick(float x, float y, const StickSettings& s);

// Turbo / rapid fire timing: while active, alternates on and off so that a new press starts
// every `intervalMs`. Changes at most once per controller report.
class Turbo {
public:
    bool update(bool active, int intervalMs, float dtSeconds);
    void reset();

private:
    bool running_ = false;
    bool on_ = false;
    float timer_ = 0.0f;
};

// Hair trigger never fires closer to the top than this, so resting noise cannot fire it.
inline constexpr float kHairMinActivation = 0.02f;

// Analog trigger chain: dead zone / range -> anti-dead zone. (Hair trigger: see TriggerProcessor.)
float processTrigger(float value, const TriggerSettings& s);

// Per-trigger processing with the state a hair trigger needs. The hair trigger works like a
// "rapid trigger": it fires as soon as the trigger leaves its activation point, releases as soon
// as it comes back up by the reset distance and fires again as soon as it goes down by the same
// distance - without having to let the trigger go all the way back up.
class TriggerProcessor {
public:
    float apply(float value, const TriggerSettings& s, float dtSeconds = 0.0f);
    void reset();

private:
    float hair(float value, const TriggerSettings& s);

    bool pressed_ = false;
    float extreme_ = 0.0f;  // deepest point while pressed, shallowest point while released
    Turbo turbo_;
};

// True while the stick is being moved, i.e. pushed past its dead zone.
bool stickActive(float x, float y, const StickSettings& s);

// GameSir style RC filter. It only works while the stick is moved (`active`); at rest the stick is
// left alone, so nothing is added and letting go stops instantly.
//  strength > 0 (stabilizer): RC low-pass on the raw stick (time constant up to 40 ms), applied
//                             before processStick(). Lags slightly behind the thumb and smooths.
//  strength < 0 (jitter):     micro-jitter on the processed output, applied after processStick().
//                             Every 5 ms the aim swings to the other side by a random 50-100% of the
//                             amount, shaped by an RC stage. It only rotates the stick direction, so
//                             the stick length - the aim speed - stays exactly what the thumb does.
class RcFilter {
public:
    Vec2 apply(Vec2 raw, float strength, float dtSeconds, bool active);
    Vec2 jitter(Vec2 out, float strength, float dtSeconds, bool active);
    void reset();

private:
    float random01();

    Vec2 state_{};          // the stabilizer's RC low-pass
    float jitter_ = 0.0f;   // current jitter, -1..1, follows target_ through an RC stage
    float target_ = 0.0f;   // where the current swing is heading
    float side_ = 1.0f;     // side of the last swing
    float clock_ = 0.0f;    // time since the last swing
    uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace edgepad
