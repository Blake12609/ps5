#include "core/settings.hpp"

#include <algorithm>
#include <cctype>
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
    t.hairResetDistance = clampRange(t.hairResetDistance, kHairMinReset, 0.5f);
    t.turboIntervalMs = std::clamp(t.turboIntervalMs, kTurboMinMs, kTurboMaxMs);
    if (t.mode >= TriggerMode::Count) t.mode = TriggerMode::Analog;
    if (t.resistance >= TriggerResistance::Count) t.resistance = TriggerResistance::Off;
}

}  // namespace

std::string normalizeGameName(std::string_view name) {
    // Just the file name, lower case: "C:\\Games\\COD.exe" -> "cod.exe".
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.remove_prefix(1);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
    std::string out(name.substr(0, 128));
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

void normalizeGames(std::vector<std::string>& games) {
    std::vector<std::string> clean;
    for (const auto& g : games) {
        std::string name = normalizeGameName(g);
        if (!name.empty() && std::find(clean.begin(), clean.end(), name) == clean.end()) clean.push_back(std::move(name));
        if (clean.size() >= kMaxGames) break;
    }
    games = std::move(clean);
}

Binding Binding::toButton(edgepad::Button b) {
    Binding x;
    x.kind = Kind::Button;
    x.button = b;
    return x;
}

Binding Binding::toKey(edgepad::Key k) {
    Binding x;
    x.kind = Kind::Key;
    x.key = k;
    return x;
}

Binding Binding::disabled() { return Binding{}; }

Binding Binding::inherit() {
    Binding x;
    x.kind = Kind::Inherit;
    return x;
}

BindingMap defaultButtonMap() {
    BindingMap map{};
    for (int i = 0; i < kButtonCount; ++i) {
        const Button b = buttonAt(i);
        if (isRemapTarget(b)) map[static_cast<size_t>(i)] = Binding::toButton(b);
    }
    // Popular shooter layout for the back buttons: jump and crouch without leaving the right stick.
    map[static_cast<size_t>(index(Button::PaddleLeft))] = Binding::toButton(Button::Circle);
    map[static_cast<size_t>(index(Button::PaddleRight))] = Binding::toButton(Button::Cross);
    // Touchpad zones act like the touchpad click until they are bound to something else.
    for (Button zone : {Button::TouchLeft, Button::TouchRight, Button::TouchTopLeft, Button::TouchTopRight,
                        Button::TouchBottomLeft, Button::TouchBottomRight}) {
        map[static_cast<size_t>(index(zone))] = Binding::toButton(Button::Touchpad);
    }
    return map;
}

BindingMap defaultShiftMap() {
    BindingMap map{};
    map.fill(Binding::inherit());
    return map;
}

namespace {

void normalizeBinding(Binding& b, bool allowInherit) {
    if (b.kind == Binding::Kind::Inherit && !allowInherit) b.kind = Binding::Kind::Disabled;
    if (b.kind == Binding::Kind::Button && !isRemapTarget(b.button)) b.kind = Binding::Kind::Disabled;
    if (b.kind == Binding::Kind::Key && (b.key == Key::None || b.key >= Key::Count)) b.kind = Binding::Kind::Disabled;
    if (b.kind != Binding::Kind::Button) b.button = Button::Cross;
    if (b.kind != Binding::Kind::Key) b.key = Key::None;
    if (b.kind == Binding::Kind::Inherit || b.kind == Binding::Kind::Disabled) {
        b.toggle = false;
        b.turbo = false;
    }
    b.turboIntervalMs = std::clamp(b.turboIntervalMs, kTurboMinMs, kTurboMaxMs);
}

void normalizeGyro(GyroSettings& g) {
    if (g.activation >= GyroActivation::Count) g.activation = GyroActivation::Off;
    if (g.horizontalAxis >= GyroAxis::Count) g.horizontalAxis = GyroAxis::Yaw;
    if (!isPhysicalButton(g.button)) g.button = Button::L2;
    g.sensitivity = clampRange(g.sensitivity, 0.25f, 10.0f);
    g.verticalRatio = clampRange(g.verticalRatio, 0.0f, 2.0f);
    g.deadzone = clampRange(g.deadzone, 0.0f, 20.0f);
    g.smoothing = clampRange(g.smoothing, 0.0f, 1.0f);
    g.antiDeadzone = clampRange(g.antiDeadzone, 0.0f, 0.6f);
}

}  // namespace

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
        for (auto& b : p.buttons) normalizeBinding(b, false);
        for (auto& b : p.shiftButtons) normalizeBinding(b, true);
        if (p.shiftButton && (!isRemapSource(*p.shiftButton) || *p.shiftButton >= Button::Count)) p.shiftButton.reset();
        if (p.touchpadZones >= TouchpadZones::Count) p.touchpadZones = TouchpadZones::Off;
        if (p.zoneTrigger >= ZoneTrigger::Count) p.zoneTrigger = ZoneTrigger::Click;
        normalizeGyro(p.gyro);
        if (p.light.effect >= LightEffect::Count) p.light.effect = LightEffect::Static;
        p.light.colorCount = std::clamp(p.light.colorCount, 2, 4);
        p.light.periodSeconds = clampRange(p.light.periodSeconds, 0.5f, 30.0f);
        p.light.brightness = clampRange(p.light.brightness, 0.05f, 1.0f);
        normalizeGames(p.games);
        if (p.hotkey && std::find(kProfileHotkeys.begin(), kProfileHotkeys.end(), *p.hotkey) == kProfileHotkeys.end()) {
            p.hotkey.reset();
        }
    }
    if (settings.output >= OutputKind::Count) settings.output = OutputKind::Xbox360;
    if (settings.fnMode >= FnMode::Count) settings.fnMode = FnMode::Auto;
    settings.activeProfile = std::clamp(settings.activeProfile, 0, static_cast<int>(profiles.size()) - 1);
    for (float& c : settings.leftStickCenter) c = clampRange(c, -0.5f, 0.5f);
    for (float& c : settings.rightStickCenter) c = clampRange(c, -0.5f, 0.5f);
    for (float& b : settings.gyroBias) b = clampRange(b, -2000.0f, 2000.0f);
}

const Profile& Config::active() const {
    const int i = std::clamp(settings.activeProfile, 0, static_cast<int>(profiles.size()) - 1);
    return profiles[static_cast<size_t>(i)];
}

Profile& Config::active() {
    const int i = std::clamp(settings.activeProfile, 0, static_cast<int>(profiles.size()) - 1);
    return profiles[static_cast<size_t>(i)];
}

std::optional<int> profileForGame(const Config& cfg, std::string_view game) {
    const std::string name = normalizeGameName(game);
    if (name.empty()) return std::nullopt;
    for (size_t i = 0; i < cfg.profiles.size(); ++i) {
        const auto& games = cfg.profiles[i].games;
        if (std::find(games.begin(), games.end(), name) != games.end()) return static_cast<int>(i);
    }
    return std::nullopt;
}

std::string_view lightEffectId(LightEffect e) {
    switch (e) {
        case LightEffect::Breathing: return "breathing";
        case LightEffect::Rainbow: return "rainbow";
        case LightEffect::Cycle: return "cycle";
        case LightEffect::Battery: return "battery";
        case LightEffect::Static:
        case LightEffect::Count: break;
    }
    return "static";
}

std::string_view lightEffectLabel(LightEffect e) {
    switch (e) {
        case LightEffect::Breathing: return "Breathing";
        case LightEffect::Rainbow: return "Rainbow";
        case LightEffect::Cycle: return "Color cycle";
        case LightEffect::Battery: return "Battery level";
        case LightEffect::Static:
        case LightEffect::Count: break;
    }
    return "Static color";
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
        case OutputKind::DualSense: return "dualsense";
        case OutputKind::None: return "none";
        case OutputKind::Count: break;
    }
    return "xbox360";
}

std::string_view outputKindLabel(OutputKind k) {
    switch (k) {
        case OutputKind::Xbox360: return "Xbox 360";
        case OutputKind::DualShock4: return "PlayStation 4 (DualShock 4)";
        case OutputKind::DualSense: return "PlayStation 5 (DualSense)";
        case OutputKind::None: return "None (monitor only)";
        case OutputKind::Count: break;
    }
    return "Xbox 360";
}

std::string_view fnModeId(FnMode m) {
    switch (m) {
        case FnMode::Auto: return "auto";
        case FnMode::Edge: return "edge";
        case FnMode::LeftFn: return "left_fn";
        case FnMode::RightFn: return "right_fn";
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
        case FnMode::Edge: return "Both Edge Fn buttons";
        case FnMode::LeftFn: return "Left Edge Fn (right one is free to bind)";
        case FnMode::RightFn: return "Right Edge Fn (left one is free to bind)";
        case FnMode::Mute: return "Mute button (both Edge Fn free to bind)";
        case FnMode::Touchpad: return "Touchpad click";
        case FnMode::Disabled: return "Disabled";
        case FnMode::Count: break;
    }
    return "Auto (Edge Fn buttons + Mute)";
}

std::string_view touchpadZonesId(TouchpadZones z) {
    switch (z) {
        case TouchpadZones::Two: return "two";
        case TouchpadZones::Four: return "four";
        default: return "off";
    }
}

std::string_view touchpadZonesLabel(TouchpadZones z) {
    switch (z) {
        case TouchpadZones::Two: return "2 zones (left / right)";
        case TouchpadZones::Four: return "4 zones (quarters)";
        default: return "Off (one touchpad button)";
    }
}

std::string_view zoneTriggerId(ZoneTrigger t) { return t == ZoneTrigger::Touch ? "touch" : "click"; }
std::string_view zoneTriggerLabel(ZoneTrigger t) { return t == ZoneTrigger::Touch ? "Touch the zone" : "Click the zone"; }

std::string_view gyroActivationId(GyroActivation a) {
    switch (a) {
        case GyroActivation::Always: return "always";
        case GyroActivation::WhileHeld: return "while_held";
        case GyroActivation::Toggle: return "toggle";
        default: return "off";
    }
}

std::string_view gyroActivationLabel(GyroActivation a) {
    switch (a) {
        case GyroActivation::Always: return "Always on";
        case GyroActivation::WhileHeld: return "While a button is held";
        case GyroActivation::Toggle: return "Toggle with a button";
        default: return "Off";
    }
}

std::string_view gyroAxisId(GyroAxis a) { return a == GyroAxis::Roll ? "roll" : "yaw"; }
std::string_view gyroAxisLabel(GyroAxis a) {
    return a == GyroAxis::Roll ? "Roll (tilt the pad left / right)" : "Yaw (turn the pad left / right)";
}

std::string bindingLabel(const Binding& b) {
    switch (b.kind) {
        case Binding::Kind::Inherit: return "Same as normal";
        case Binding::Kind::Disabled: return "Disabled";
        case Binding::Kind::Button: return std::string(buttonLabel(b.button));
        case Binding::Kind::Key: return (isMouseButton(b.key) ? "" : "Key: ") + std::string(keyLabel(b.key));
    }
    return "Disabled";
}

uint8_t playerLedsForProfile(int index) {
    static constexpr std::array<uint8_t, 5> kPatterns{0x04, 0x0A, 0x15, 0x1B, 0x1F};
    if (index < 0) return 0;
    return kPatterns[static_cast<size_t>(index) % kPatterns.size()];
}

}  // namespace edgepad
