#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace edgepad {

// Exact 1:1 conversions between the controller's 8-bit values and the -1..1 (sticks) / 0..1
// (triggers) ranges used internally. The stick centre (128) is exactly 0, both ends reach exactly
// -1 and +1, and every raw step converts back to the same raw value - so a stick that EdgePad
// does not change leaves it exactly as the controller sent it.

inline float stickFromRaw(uint8_t raw) {
    const float d = static_cast<float>(raw) - 128.0f;
    return d < 0.0f ? d / 128.0f : d / 127.0f;  // 128 steps below the centre, 127 above
}

inline uint8_t stickToRaw(float v) {
    if (std::isnan(v)) return 128;
    v = std::clamp(v, -1.0f, 1.0f);
    return static_cast<uint8_t>(128 + std::lround(v * (v < 0.0f ? 128.0f : 127.0f)));
}

// XInput / evdev thumb stick range: -32768..32767 with the centre at 0.
inline int16_t stickToThumb(float v) {
    if (std::isnan(v)) return 0;
    v = std::clamp(v, -1.0f, 1.0f);
    return static_cast<int16_t>(std::lround(v * (v < 0.0f ? 32768.0f : 32767.0f)));
}

inline float triggerFromRaw(uint8_t raw) { return static_cast<float>(raw) / 255.0f; }

inline uint8_t triggerToRaw(float v) {
    if (!(v > 0.0f)) return 0;  // also catches NaN
    return static_cast<uint8_t>(std::lround(std::min(v, 1.0f) * 255.0f));
}

}  // namespace edgepad
