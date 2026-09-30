#pragma once

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

// Lightbar colour, player LEDs (profile slot), adaptive trigger walls and forwarded rumble.
dualsense::Effects effectsForConfig(const Config& cfg, uint8_t rumbleLarge, uint8_t rumbleSmall);

// Turns raw controller input into the virtual controller state for the active profile.
// Handles Fn combos (Fn + face button = switch profile, Fn + Options = toggle remapping),
// RC filters, stick/trigger processing and button remapping.
class Pipeline {
public:
    OutputState process(const InputState& in, Config& cfg, float dtSeconds, PipelineEvents* events = nullptr);
    void reset();

private:
    ButtonMask previous_ = 0;
    ButtonMask suppressed_ = 0;
    RcFilter leftFilter_;
    RcFilter rightFilter_;
};

}  // namespace edgepad
