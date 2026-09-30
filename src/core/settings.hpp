#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/keys.hpp"
#include "core/processing.hpp"
#include "core/types.hpp"

namespace edgepad {

enum class OutputKind : uint8_t { Xbox360, DualShock4, None, Count };

// Which physical button acts as "Fn" for profile switching (Fn + face button).
enum class FnMode : uint8_t {
    Auto,      // Edge Fn buttons + Mute button (works on the regular DualSense too)
    Edge,      // Both Edge Fn buttons
    LeftFn,    // Left Edge Fn only (the right one can be bound like any button)
    RightFn,   // Right Edge Fn only
    Mute,      // Mute button only
    Touchpad,  // Touchpad click
    Disabled,
    Count
};

// Touchpad split into extra buttons.
enum class TouchpadZones : uint8_t { Off, Two, Four, Count };
enum class ZoneTrigger : uint8_t { Click, Touch, Count };  // click the zone, or just touch it

// What one controller button does.
struct Binding {
    enum class Kind : uint8_t {
        Inherit,   // shift layer only: same as the normal layer
        Disabled,  // does nothing
        Button,    // a button on the virtual controller
        Key,       // a keyboard key or mouse button
    };
    Kind kind = Kind::Disabled;
    edgepad::Button button = edgepad::Button::Cross;
    edgepad::Key key = edgepad::Key::None;
    bool toggle = false;       // tap to switch on, tap again to switch off
    bool turbo = false;        // repeat presses while active
    int turboIntervalMs = 50;  // time between turbo presses, 1..100 ms

    static Binding toButton(edgepad::Button b);
    static Binding toKey(edgepad::Key k);
    static Binding disabled();
    static Binding inherit();

    bool operator==(const Binding&) const = default;
};

inline constexpr int kTurboMinMs = 1;
inline constexpr int kTurboMaxMs = 100;

// Binding table indexed by source button.
using BindingMap = std::array<Binding, kButtonCount>;
BindingMap defaultButtonMap();
BindingMap defaultShiftMap();  // everything "same as normal"

struct Profile {
    std::string name = "Default";
    std::optional<Button> hotkey = Button::Cross;  // Fn + hotkey activates this profile
    std::array<uint8_t, 3> lightbar{0, 90, 255};
    StickSettings leftStick;
    StickSettings rightStick;
    TriggerSettings l2;
    TriggerSettings r2;
    bool swapSticks = false;
    BindingMap buttons = defaultButtonMap();
    std::optional<Button> shiftButton;             // hold for the shift layer
    BindingMap shiftButtons = defaultShiftMap();
    TouchpadZones touchpadZones = TouchpadZones::Off;
    ZoneTrigger zoneTrigger = ZoneTrigger::Click;
    GyroSettings gyro;

    bool operator==(const Profile&) const = default;
};

struct Settings {
    OutputKind output = OutputKind::Xbox360;
    FnMode fnMode = FnMode::Auto;
    bool rumble = true;         // forward game rumble to the controller
    bool gameLightbar = false;  // DualShock 4 output: let games set the lightbar colour
    bool autoUpdate = true;  // download and install new releases automatically
    bool enabled = true;     // false = raw passthrough (Fn + Options toggles)
    int activeProfile = 0;
    // Calibration of this controller (resting offsets).
    std::array<float, 2> leftStickCenter{};
    std::array<float, 2> rightStickCenter{};
    std::array<float, 3> gyroBias{};  // raw gyro counts at rest

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
std::string_view touchpadZonesId(TouchpadZones z);
std::string_view touchpadZonesLabel(TouchpadZones z);
std::string_view zoneTriggerId(ZoneTrigger t);
std::string_view zoneTriggerLabel(ZoneTrigger t);
std::string_view gyroActivationId(GyroActivation a);
std::string_view gyroActivationLabel(GyroActivation a);
std::string_view gyroAxisId(GyroAxis a);
std::string_view gyroAxisLabel(GyroAxis a);
// Human readable description of a binding, e.g. "Cross", "Key: Space", "Disabled".
std::string bindingLabel(const Binding& b);

// Player LED pattern shown for a profile slot (same patterns the PS5 uses).
uint8_t playerLedsForProfile(int index);

}  // namespace edgepad
