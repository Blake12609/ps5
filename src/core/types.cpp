#include "core/types.hpp"

#include <array>

namespace edgepad {
namespace {

struct ButtonInfo {
    std::string_view id;
    std::string_view label;
};

constexpr std::array<ButtonInfo, kButtonCount> kButtons{{
    {"cross", "Cross"},
    {"circle", "Circle"},
    {"square", "Square"},
    {"triangle", "Triangle"},
    {"l1", "L1"},
    {"r1", "R1"},
    {"l2", "L2 (full press)"},
    {"r2", "R2 (full press)"},
    {"l3", "L3"},
    {"r3", "R3"},
    {"create", "Create"},
    {"options", "Options"},
    {"ps", "PS"},
    {"touchpad", "Touchpad click"},
    {"mute", "Mute"},
    {"dpad_up", "D-pad up"},
    {"dpad_down", "D-pad down"},
    {"dpad_left", "D-pad left"},
    {"dpad_right", "D-pad right"},
    {"paddle_left", "Left back button"},
    {"paddle_right", "Right back button"},
    {"fn_left", "Left Fn"},
    {"fn_right", "Right Fn"},
}};

}  // namespace

std::string_view buttonId(Button b) {
    const int i = index(b);
    return (i >= 0 && i < kButtonCount) ? kButtons[static_cast<size_t>(i)].id : std::string_view{"none"};
}

std::string_view buttonLabel(Button b) {
    const int i = index(b);
    return (i >= 0 && i < kButtonCount) ? kButtons[static_cast<size_t>(i)].label : std::string_view{"None"};
}

std::optional<Button> buttonFromId(std::string_view id) {
    for (int i = 0; i < kButtonCount; ++i) {
        if (kButtons[static_cast<size_t>(i)].id == id) return buttonAt(i);
    }
    return std::nullopt;
}

bool isRemapSource(Button b) { return b != Button::L2 && b != Button::R2 && b != Button::Count; }

bool isRemapTarget(Button b) {
    switch (b) {
        case Button::Mute:
        case Button::PaddleLeft:
        case Button::PaddleRight:
        case Button::FnLeft:
        case Button::FnRight:
        case Button::Count:
            return false;
        default:
            return true;
    }
}

}  // namespace edgepad
