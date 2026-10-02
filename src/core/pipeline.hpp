#pragma once

#include <array>
#include <optional>

#include "core/dualsense.hpp"
#include "core/processing.hpp"
#include "core/settings.hpp"
#include "core/types.hpp"

namespace edgepad {

struct PipelineEvents {
    bool profileChanged = false;
    bool enabledChanged = false;
};

// Buttons that act as "Fn" for the given mode.
ButtonMask fnSourceMask(FnMode mode);

// Touchpad zone button for a finger position.
Button touchpadZoneAt(const TouchPoint& touch, TouchpadZones zones);

// Lightbar colour, player LEDs (profile slot), adaptive trigger walls and forwarded rumble.
// `gameLightbar` is the colour a game set on the virtual DualShock 4, if any.
// The profile's lightbar color at `seconds` (its effect: static, breathing, rainbow, color cycle,
// battery level). `battery` is 0..100, or -1 when unknown.
std::array<uint8_t, 3> lightbarColor(const Profile& profile, double seconds, int battery = -1, bool charging = false);

dualsense::Effects effectsForConfig(const Config& cfg, uint8_t rumbleLarge, uint8_t rumbleSmall,
                                    const std::optional<std::array<uint8_t, 3>>& gameLightbar = std::nullopt,
                                    double seconds = 0.0, int battery = -1, bool charging = false);

// Turns raw controller input into the virtual controller state for the active profile:
// Fn combos (profile switching, remapping toggle), stick drift correction, RC filters,
// stick/trigger processing, gyro aiming, touchpad zones, shift layer, toggle / turbo and
// button remapping to controller buttons or keyboard keys / mouse buttons.
class Pipeline {
public:
    OutputState process(const InputState& in, Config& cfg, float dtSeconds, PipelineEvents* events = nullptr);
    void reset();
    bool gyroActive() const { return gyro_.active(); }

private:
    void resetLayers();

    ButtonMask previous_ = 0;      // raw buttons of the previous report
    ButtonMask suppressed_ = 0;    // swallowed by an Fn combo until released
    ButtonMask prevSources_ = 0;   // remappable sources (incl. touchpad zones) of the previous report
    ButtonMask shiftLatched_ = 0;  // sources pressed while the shift button was held
    std::optional<Button> zoneLatched_;
    ButtonMask toggled_ = 0;       // toggle bindings currently on
    ButtonMask turboRunning_ = 0;  // turbo bindings currently firing
    std::array<Turbo, kButtonCount> turbo_{};
    int lastProfile_ = -1;

    RcFilter leftFilter_;
    RcFilter rightFilter_;
    TriggerProcessor l2_;
    TriggerProcessor r2_;
    GyroAim gyro_;
};

}  // namespace edgepad
