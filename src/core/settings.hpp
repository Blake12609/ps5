#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/processing.hpp"
#include "core/types.hpp"

namespace edgepad {

enum class OutputKind : uint8_t { Xbox360, DualShock4, None, Count };

// Which physical button acts as "Fn" for profile switching (Fn + face button).
enum class FnMode : uint8_t {
    Auto,      // Edge Fn buttons + Mute button (works on the regular DualSense too)
    Edge,      // Edge Fn buttons only
    Mute,      // Mute button only
    Touchpad,  // Touchpad click
    Disabled,
    Count
};

// Remap table indexed by source button; nullopt disables the button.
using ButtonMap = std::array<std::optional<Button>, kButtonCount>;
ButtonMap defaultButtonMap();

struct Profile {
    std::string name = "Default";
    std::optional<Button> hotkey = Button::Cross;  // Fn + hotkey activates this profile
    std::array<uint8_t, 3> lightbar{0, 90, 255};
    StickSettings leftStick;
    StickSettings rightStick;
    TriggerSettings l2;
    TriggerSettings r2;
    bool swapSticks = false;
    ButtonMap buttons = defaultButtonMap();

    bool operator==(const Profile&) const = default;
};

struct Settings {
    OutputKind output = OutputKind::Xbox360;
    FnMode fnMode = FnMode::Auto;
    bool rumble = true;      // forward game rumble to the controller
    bool autoUpdate = true;  // download and install new releases automatically
    bool enabled = true;     // false = raw passthrough (Fn + Options toggles)
    int activeProfile = 0;

    bool operator==(const Settings&) const = default;
};

inline constexpr size_t kMaxProfiles = 16;

struct Config {
    Settings settings;
    std::vector<Profile> profiles;

    static Config defaults();
    // Clamps every value into range and repairs structural problems.
    void normalize();
    const Profile& active() const;
    Profile& active();

    bool operator==(const Config&) const = default;
};

// Hotkeys that can be combined with Fn, like the DualSense Edge profile switcher.
inline constexpr std::array<Button, 4> kProfileHotkeys{Button::Cross, Button::Circle, Button::Square,
                                                       Button::Triangle};

std::string_view curveId(Curve c);
std::string_view curveLabel(Curve c);
std::string_view outputKindId(OutputKind k);
std::string_view outputKindLabel(OutputKind k);
std::string_view fnModeId(FnMode m);
std::string_view fnModeLabel(FnMode m);

// Player LED pattern shown for a profile slot (same patterns the PS5 uses).
uint8_t playerLedsForProfile(int index);

}  // namespace edgepad
