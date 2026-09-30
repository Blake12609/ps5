#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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

// Can the button be used as a remap source? (L2/R2 are analog and handled by trigger settings)
bool isRemapSource(Button b);
// Can the button be produced by the virtual controller?
bool isRemapTarget(Button b);

// Raw controller state. Sticks: -1..1 with +x right and +y up. Triggers: 0..1.
struct InputState {
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    float l2 = 0.0f, r2 = 0.0f;
    ButtonMask buttons = 0;
    int battery = -1;  // percent, -1 when unknown
    bool charging = false;
};

// State sent to the virtual controller.
struct OutputState {
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    float l2 = 0.0f, r2 = 0.0f;
    ButtonMask buttons = 0;

    bool operator==(const OutputState&) const = default;
};

}  // namespace edgepad
