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
    float deadzone = 0.0f;  // start of the effective range
    float maxRange = 1.0f;  // end of the effective range (software trigger stop)
    float antiDeadzone = 0.0f;
    TriggerMode mode = TriggerMode::Analog;
    TriggerResistance resistance = TriggerResistance::Off;  // adaptive trigger wall
    float resistancePosition = 0.5f;                        // 0..1 of the trigger travel
    int resistanceStrength = 6;                             // 1..8

    bool operator==(const TriggerSettings&) const = default;
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Maximum time constant of the RC low-pass at rcFilter = 1.0 (seconds).
inline constexpr float kRcMaxTimeConstant = 0.040f;
// Maximum jitter amplitude at rcFilter = -1.0 (fraction of full stick throw).
inline constexpr float kRcMaxJitter = 0.03f;
// The RC filter only runs once the stick is pushed past its dead zone, and at least this far,
// so stick noise at rest never triggers it.
inline constexpr float kRcMinActiveDeflection = 0.03f;

float clamp01(float v);
float clamp11(float v);

// Maps t (0..1, distance travelled past the dead zone) through a response curve.
float applyCurve(float t, Curve curve, float intensity, const std::vector<CurvePoint>& custom);

// Full stick chain: invert -> dead zone -> curve -> anti-dead zone -> outer dead zone.
Vec2 processStick(float x, float y, const StickSettings& s);

// Trigger chain: dead zone / range -> anti-dead zone, or hair trigger.
float processTrigger(float value, const TriggerSettings& s);

// True while the stick is being moved, i.e. pushed past its dead zone.
bool stickActive(float x, float y, const StickSettings& s);

// GameSir style RC filter. It only works while the stick is moved (`active`); at rest the
// stick is left alone, so there is no smoothing tail and no jitter when you let go.
class RcFilter {
public:
    // Stabilizer (strength > 0): RC low-pass on the raw stick, applied before processStick().
    Vec2 smooth(Vec2 raw, float strength, float dtSeconds, bool active);
    // Jitter (strength < 0): alternating wobble on the processed output, applied after processStick().
    Vec2 jitter(Vec2 out, float strength, bool active);
    void reset();

private:
    Vec2 state_{};
    bool primed_ = false;
    uint32_t tick_ = 0;
};

}  // namespace edgepad
