#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "core/keys.hpp"

namespace edgepad {

// Logical controller buttons, named after the DualSense / DualSense Edge.
enum class Button : uint8_t {
    Cross,
    Circle,
    Square,
    Triangle,
    L1,
    R1,
    L2,  // digital click of the analog trigger
    R2,
    L3,
    R3,
    Create,
    Options,
    PS,
    Touchpad,
    Mute,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    PaddleLeft,   // DualSense Edge back button (left)
    PaddleRight,  // DualSense Edge back button (right)
    FnLeft,       // DualSense Edge function button (left)
    FnRight,      // DualSense Edge function button (right)
    // Touchpad zones (virtual buttons made from where the touchpad is clicked or touched).
    TouchLeft,
    TouchRight,
    TouchTopLeft,
    TouchTopRight,
    TouchBottomLeft,
    TouchBottomRight,
    Count
};

inline constexpr int kButtonCount = static_cast<int>(Button::Count);

using ButtonMask = uint32_t;
static_assert(kButtonCount <= 32, "ButtonMask must hold every button");

constexpr ButtonMask bit(Button b) { return ButtonMask{1} << static_cast<unsigned>(b); }
constexpr bool has(ButtonMask mask, Button b) { return (mask & bit(b)) != 0; }
constexpr int index(Button b) { return static_cast<int>(b); }
constexpr Button buttonAt(int i) { return static_cast<Button>(i); }

std::string_view buttonId(Button b);     // stable id used in config files, e.g. "paddle_left"
std::string_view buttonLabel(Button b);  // human readable, e.g. "Left back button"
std::optional<Button> buttonFromId(std::string_view id);

// A real button on the controller (not a virtual touchpad zone).
bool isPhysicalButton(Button b);
// Can the button be used as a remap source? (L2/R2 are analog and handled by trigger settings)
bool isRemapSource(Button b);
// Can the button be produced by the virtual controller?
bool isRemapTarget(Button b);

// One finger on the touchpad (DualSense resolution 1920 x 1080).
struct TouchPoint {
    bool active = false;
    uint8_t id = 0;  // tracking number, changes for every new touch
    uint16_t x = 0;
    uint16_t y = 0;

    bool operator==(const TouchPoint&) const = default;
};

// Motion sensors and touchpad, passed through to a virtual PlayStation controller.
struct MotionState {
    std::array<int16_t, 3> gyro{};   // raw sensor units: pitch, yaw, roll
    std::array<int16_t, 3> accel{};  // raw sensor units: x, y, z
    uint32_t timestamp = 0;          // sensor clock, 1/3 microsecond ticks
    std::array<TouchPoint, 2> touch{};

    bool operator==(const MotionState&) const = default;
};

// The controller's own input report body (the 63 bytes after the USB report id; Bluetooth full
// reports carry the same layout). Kept so a virtual DualSense can pass on every byte EdgePad does
// not change, unknown ones included.
struct RawReport {
    std::array<uint8_t, 63> body{};
    bool valid = false;

    bool operator==(const RawReport&) const = default;
};

// Raw controller state. Sticks: -1..1 with +x right and +y up. Triggers: 0..1.
struct InputState {
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    float l2 = 0.0f, r2 = 0.0f;
    ButtonMask buttons = 0;
    MotionState motion;
    int battery = -1;  // percent, -1 when unknown
    bool charging = false;
    RawReport raw;
};

// State sent to the virtual controller.
struct OutputState {
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    float l2 = 0.0f, r2 = 0.0f;
    ButtonMask buttons = 0;
    MotionState motion;
    KeyMask keys;  // keyboard keys / mouse buttons to hold down
    int battery = -1;
    bool charging = false;
    RawReport raw;  // the controller's report this state was made from

    bool operator==(const OutputState&) const = default;
};

}  // namespace edgepad
