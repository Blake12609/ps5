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

enum class OutputKind : uint8_t { Xbox360, DualShock4, DualSense, None, Count };

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
    int turboRandomMs = 0;     // the time between presses varies at random over a range this wide

    static Binding toButton(edgepad::Button b);
    static Binding toKey(edgepad::Key k);
    static Binding disabled();
    static Binding inherit();

    bool operator==(const Binding&) const = default;
};

inline constexpr int kTurboMinMs = 1;
inline constexpr int kTurboMaxMs = 100;
inline constexpr int kHoldMinMs = 150;
inline constexpr int kHoldMaxMs = 1500;
// How long a short press of a button with a hold action is sent for (after it is let go).
inline constexpr float kHoldTapSeconds = 0.06f;

// Binding table indexed by source button.
using BindingMap = std::array<Binding, kButtonCount>;
BindingMap defaultButtonMap();
BindingMap defaultShiftMap();  // everything "same as normal"

// Lightbar (the lights around the touchpad) animation of a profile.
enum class LightEffect : uint8_t { Static, Breathing, Rainbow, Cycle, Battery, Count };

struct LightbarSettings {
    LightEffect effect = LightEffect::Static;
    // Colors after the profile color for the Cycle effect (the profile color is the first).
    std::array<std::array<uint8_t, 3>, 3> extraColors{{{255, 0, 120}, {0, 255, 140}, {255, 190, 0}}};
    int colorCount = 3;          // colors in the cycle, 2..4
    float periodSeconds = 4.0f;  // one breath / one trip around the rainbow / one cycle, 0.5..30 s
    float brightness = 1.0f;     // 0..1 (0 = lightbar off)

    bool operator==(const LightbarSettings&) const = default;
};

struct Profile {
    std::string name = "Default";
    std::optional<Button> hotkey = Button::Cross;  // Fn + hotkey activates this profile
    std::array<uint8_t, 3> lightbar{0, 90, 255};
    LightbarSettings light;
    // Games (executable file names, e.g. "cod.exe") that switch to this profile automatically.
    std::vector<std::string> games;
    StickSettings leftStick;
    StickSettings rightStick;
    TriggerSettings l2;
    TriggerSettings r2;
    bool swapSticks = false;
    BindingMap buttons = defaultButtonMap();
    std::optional<Button> shiftButton;             // hold for the shift layer
    BindingMap shiftButtons = defaultShiftMap();
    // Hold actions (normal layer): holding a button for `holdMs` or longer sends its hold binding
    // instead, a shorter press sends its normal binding as a quick tap. Disabled = no hold action
    // (the button then works exactly as before, with no delay).
    BindingMap holdButtons{};
    int holdMs = 300;
    TouchpadZones touchpadZones = TouchpadZones::Off;
    ZoneTrigger zoneTrigger = ZoneTrigger::Click;
    // Touchpad as a mouse: one finger moves the pointer, a click is a left click (with two fingers
    // on the pad a right click), two fingers scroll. Replaces the touchpad zones and the touchpad
    // button while on.
    bool touchpadMouse = false;
    float touchpadMouseSpeed = 1.0f;  // 0.25..4
    GyroSettings gyro;

    bool operator==(const Profile&) const = default;
};

struct Settings {
    OutputKind output = OutputKind::Xbox360;
    FnMode fnMode = FnMode::Auto;
    bool rumble = true;         // forward game rumble to the controller
    float rumbleStrength = 1.0f;  // game rumble scaled to this, 0.1..1 (like the PS5's vibration intensity)
    bool lowBatteryAlert = true;  // the lightbar pulses red at 10% battery or less (not while charging)
    bool gameLightbar = false;  // DualShock 4 output: let games set the lightbar colour
    bool autoUpdate = true;  // download and install new releases automatically
    // Hide the real controller from games (HidHide or exclusive access on Windows, an input grab on
    // Linux) so they only see EdgePad's virtual controller: no double input.
    bool hideController = false;
    // Switch to a profile while one of its games is in front (Profile::games).
    bool autoProfiles = true;
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

inline constexpr size_t kMaxGames = 32;  // per profile

// A game's executable as profiles store it: the file name, lower case ("C:\Games\COD.exe" -> "cod.exe").
std::string normalizeGameName(std::string_view name);
void normalizeGames(std::vector<std::string>& games);
// The profile with `game` in its list, if any.
std::optional<int> profileForGame(const Config& cfg, std::string_view game);

std::string_view lightEffectId(LightEffect e);
std::string_view lightEffectLabel(LightEffect e);
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
