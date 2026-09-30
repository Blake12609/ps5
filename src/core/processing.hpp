#pragma once

#include <array>
#include <cstdint>
#include <vector>

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
    float deadzone = 0.05f;       // inner dead zone, fraction of full throw
    float outerDeadzone = 0.02f;  // outer edge treated as full deflection
    float antiDeadzone = 0.0f;    // minimum output once past the dead zone (cancels in-game dead zones)
    DeadzoneShape shape = DeadzoneShape::Radial;
    Curve curve = Curve::Default;
    float curveIntensity = 0.5f;  // 0..1, how strongly the curve bends
    std::vector<CurvePoint> customCurve{{0.25f, 0.15f}, {0.5f, 0.4f}, {0.75f, 0.7f}};
    // GameSir style RC filter, -1..1, only active while the stick is moved.
    // Positive: low-pass "stabilizer" smoothing. Negative: "jitter", a microscopic alternating wobble.
    float rcFilter = 0.0f;
    bool invertX = false;
    bool invertY = false;

    bool operator==(const StickSettings&) const = default;
};

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
    float hairResetDistance = 0.04f;

    bool operator==(const TriggerSettings&) const = default;
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Maximum time constant of the RC low-pass at rcFilter = 1.0 (seconds).
inline constexpr float kRcMaxTimeConstant = 0.040f;
// Maximum jitter amplitude at rcFilter = -1.0 (fraction of full stick throw).
inline constexpr float kRcMaxJitter = 0.06f;
// The jitter wobble changes side on a fixed clock, so it looks the same at any polling rate.
inline constexpr float kRcJitterFlipSeconds = 0.005f;
// The RC filter only runs once the stick is pushed past its dead zone, and at least this far,
// so stick noise at rest never triggers it.
inline constexpr float kRcMinActiveDeflection = 0.03f;

float clamp01(float v);
float clamp11(float v);

// Maps t (0..1, distance travelled past the dead zone) through a response curve.
float applyCurve(float t, Curve curve, float intensity, const std::vector<CurvePoint>& custom);

// Full stick chain: invert -> dead zone -> curve -> anti-dead zone -> outer dead zone.
Vec2 processStick(float x, float y, const StickSettings& s);

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
    float apply(float value, const TriggerSettings& s);
    void reset();

private:
    bool pressed_ = false;
    float extreme_ = 0.0f;  // deepest point while pressed, shallowest point while released
};

// True while the stick is being moved, i.e. pushed past its dead zone.
bool stickActive(float x, float y, const StickSettings& s);

// GameSir style RC filter. It only works while the stick is moved (`active`); at rest the
// stick is left alone, so there is no smoothing tail and no jitter when you let go.
class RcFilter {
public:
    // Stabilizer (strength > 0): RC low-pass on the raw stick, applied before processStick().
    Vec2 smooth(Vec2 raw, float strength, float dtSeconds, bool active);
    // Jitter (strength < 0): a small side-to-side wobble across the aim direction, applied to the
    // processed output. It never shortens the stick vector, so aim speed is unchanged and the
    // output never drops back into the game's dead zone.
    Vec2 jitter(Vec2 out, float strength, float dtSeconds, bool active);
    void reset();

private:
    Vec2 state_{};
    bool primed_ = false;
    float jitterClock_ = 0.0f;
    float jitterSide_ = 1.0f;
};

}  // namespace edgepad
