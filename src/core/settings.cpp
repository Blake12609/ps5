#include "core/settings.hpp"

#include <algorithm>
#include <cmath>

namespace edgepad {
namespace {

float clampRange(float v, float lo, float hi) {
    if (std::isnan(v)) return lo;
    return std::clamp(v, lo, hi);
}

void normalizeStick(StickSettings& s) {
    s.deadzone = clampRange(s.deadzone, 0.0f, 0.9f);
    s.outerDeadzone = clampRange(s.outerDeadzone, 0.0f, 0.5f);
    s.antiDeadzone = clampRange(s.antiDeadzone, 0.0f, 0.9f);
    s.curveIntensity = clampRange(s.curveIntensity, 0.0f, 1.0f);
    s.rcFilter = clampRange(s.rcFilter, -1.0f, 1.0f);
    if (s.shape >= DeadzoneShape::Count) s.shape = DeadzoneShape::Radial;
    if (s.curve >= Curve::Count) s.curve = Curve::Default;
    for (auto& p : s.customCurve) {
        p.x = clampRange(p.x, 0.01f, 0.99f);
        p.y = clampRange(p.y, 0.0f, 1.0f);
    }
    std::sort(s.customCurve.begin(), s.customCurve.end(),
              [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    if (s.customCurve.size() > 16) s.customCurve.resize(16);
}

void normalizeTrigger(TriggerSettings& t) {
    t.deadzone = clampRange(t.deadzone, 0.0f, 0.9f);
    t.maxRange = clampRange(t.maxRange, 0.05f, 1.0f);
    if (t.maxRange < t.deadzone + 0.02f) t.maxRange = std::min(1.0f, t.deadzone + 0.02f);
    t.antiDeadzone = clampRange(t.antiDeadzone, 0.0f, 0.9f);
    t.resistancePosition = clampRange(t.resistancePosition, 0.0f, 0.9f);
    t.resistanceStrength = std::clamp(t.resistanceStrength, 1, 8);
    if (t.mode >= TriggerMode::Count) t.mode = TriggerMode::Analog;
    if (t.resistance >= TriggerResistance::Count) t.resistance = TriggerResistance::Off;
}

}  // namespace

ButtonMap defaultButtonMap() {
    ButtonMap map{};
    for (int i = 0; i < kButtonCount; ++i) {
        const Button b = buttonAt(i);
        if (isRemapTarget(b)) map[static_cast<size_t>(i)] = b;
    }
    // Popular shooter layout for the back buttons: jump and crouch without leaving the right stick.
    map[static_cast<size_t>(index(Button::PaddleLeft))] = Button::Circle;
    map[static_cast<size_t>(index(Button::PaddleRight))] = Button::Cross;
    return map;
}

Config Config::defaults() {
    Config cfg;

    Profile standard;
    standard.name = "Default";
    standard.hotkey = Button::Cross;
    standard.lightbar = {0, 90, 255};
    cfg.profiles.push_back(standard);

    Profile fps;
    fps.name = "FPS";
    fps.hotkey = Button::Circle;
    fps.lightbar = {255, 40, 40};
    fps.leftStick.antiDeadzone = 0.10f;
    fps.rightStick.antiDeadzone = 0.12f;
    fps.rightStick.curve = Curve::Precise;
    fps.rightStick.curveIntensity = 0.35f;
    fps.rightStick.rcFilter = 0.15f;
    fps.l2.mode = TriggerMode::HairTrigger;
    fps.r2.mode = TriggerMode::HairTrigger;
    cfg.profiles.push_back(fps);

    Profile racing;
    racing.name = "Racing";
    racing.hotkey = Button::Square;
    racing.lightbar = {40, 220, 90};
    racing.leftStick.deadzone = 0.03f;
    racing.leftStick.curve = Curve::Dynamic;
    racing.leftStick.curveIntensity = 0.3f;
    racing.l2.resistance = TriggerResistance::Wall;
    racing.l2.resistancePosition = 0.3f;
    racing.l2.resistanceStrength = 4;
    cfg.profiles.push_back(racing);

    return cfg;
}

void Config::normalize() {
    if (profiles.empty()) profiles = defaults().profiles;
    if (profiles.size() > kMaxProfiles) profiles.resize(kMaxProfiles);
    for (auto& p : profiles) {
        if (p.name.empty()) p.name = "Profile";
        if (p.name.size() > 64) p.name.resize(64);
        normalizeStick(p.leftStick);
        normalizeStick(p.rightStick);
        normalizeTrigger(p.l2);
        normalizeTrigger(p.r2);
        for (int i = 0; i < kButtonCount; ++i) {
            auto& target = p.buttons[static_cast<size_t>(i)];
            if (target && !isRemapTarget(*target)) target.reset();
        }
        if (p.hotkey && std::find(kProfileHotkeys.begin(), kProfileHotkeys.end(), *p.hotkey) == kProfileHotkeys.end()) {
            p.hotkey.reset();
        }
    }
    if (settings.output >= OutputKind::Count) settings.output = OutputKind::Xbox360;
    if (settings.fnMode >= FnMode::Count) settings.fnMode = FnMode::Auto;
    settings.activeProfile = std::clamp(settings.activeProfile, 0, static_cast<int>(profiles.size()) - 1);
}

const Profile& Config::active() const {
    const int i = std::clamp(settings.activeProfile, 0, static_cast<int>(profiles.size()) - 1);
    return profiles[static_cast<size_t>(i)];
}

Profile& Config::active() {
    const int i = std::clamp(settings.activeProfile, 0, static_cast<int>(profiles.size()) - 1);
    return profiles[static_cast<size_t>(i)];
}

std::string_view curveId(Curve c) {
    switch (c) {
        case Curve::Default: return "default";
        case Curve::Quick: return "quick";
        case Curve::Precise: return "precise";
        case Curve::Steady: return "steady";
        case Curve::Digital: return "digital";
        case Curve::Dynamic: return "dynamic";
        case Curve::Custom: return "custom";
        case Curve::Count: break;
    }
    return "default";
}

std::string_view curveLabel(Curve c) {
    switch (c) {
        case Curve::Default: return "Default (linear)";
        case Curve::Quick: return "Quick";
        case Curve::Precise: return "Precise";
        case Curve::Steady: return "Steady";
        case Curve::Digital: return "Digital";
        case Curve::Dynamic: return "Dynamic";
        case Curve::Custom: return "Custom";
        case Curve::Count: break;
    }
    return "Default (linear)";
}

std::string_view outputKindId(OutputKind k) {
    switch (k) {
        case OutputKind::Xbox360: return "xbox360";
        case OutputKind::DualShock4: return "ds4";
        case OutputKind::None: return "none";
        case OutputKind::Count: break;
    }
    return "xbox360";
}

std::string_view outputKindLabel(OutputKind k) {
    switch (k) {
        case OutputKind::Xbox360: return "Xbox 360 (best game support)";
        case OutputKind::DualShock4: return "PlayStation (DualShock 4)";
        case OutputKind::None: return "None (monitor only)";
        case OutputKind::Count: break;
    }
    return "Xbox 360 (best game support)";
}

std::string_view fnModeId(FnMode m) {
    switch (m) {
        case FnMode::Auto: return "auto";
        case FnMode::Edge: return "edge";
        case FnMode::Mute: return "mute";
        case FnMode::Touchpad: return "touchpad";
        case FnMode::Disabled: return "disabled";
        case FnMode::Count: break;
    }
    return "auto";
}

std::string_view fnModeLabel(FnMode m) {
    switch (m) {
        case FnMode::Auto: return "Auto (Edge Fn buttons + Mute)";
        case FnMode::Edge: return "Edge Fn buttons only";
        case FnMode::Mute: return "Mute button";
        case FnMode::Touchpad: return "Touchpad click";
        case FnMode::Disabled: return "Disabled";
        case FnMode::Count: break;
    }
    return "Auto (Edge Fn buttons + Mute)";
}

uint8_t playerLedsForProfile(int index) {
    static constexpr std::array<uint8_t, 5> kPatterns{0x04, 0x0A, 0x15, 0x1B, 0x1F};
    if (index < 0) return 0;
    return kPatterns[static_cast<size_t>(index) % kPatterns.size()];
}

}  // namespace edgepad
