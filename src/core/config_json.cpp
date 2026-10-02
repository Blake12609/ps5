#include "core/config_json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "core/dualsense.hpp"

#include <nlohmann/json.hpp>

namespace edgepad {
namespace {

using nlohmann::json;

template <typename Enum>
std::string_view enumId(Enum v);

template <>
std::string_view enumId(Curve v) { return curveId(v); }
template <>
std::string_view enumId(OutputKind v) { return outputKindId(v); }
template <>
std::string_view enumId(FnMode v) { return fnModeId(v); }
template <>
std::string_view enumId(DeadzoneShape v) { return v == DeadzoneShape::Axial ? "axial" : "radial"; }
template <>
std::string_view enumId(TriggerMode v) { return v == TriggerMode::HairTrigger ? "hair" : "analog"; }
template <>
std::string_view enumId(TriggerResistance v) { return v == TriggerResistance::Wall ? "wall" : "off"; }
template <>
std::string_view enumId(TouchpadZones v) { return touchpadZonesId(v); }
template <>
std::string_view enumId(ZoneTrigger v) { return zoneTriggerId(v); }
template <>
std::string_view enumId(GyroActivation v) { return gyroActivationId(v); }
template <>
std::string_view enumId(GyroAxis v) { return gyroAxisId(v); }
template <>
std::string_view enumId(LightEffect v) { return lightEffectId(v); }

json rgb(const std::array<uint8_t, 3>& c) { return {c[0], c[1], c[2]}; }

void readRgb(const json& j, std::array<uint8_t, 3>& out) {
    if (!j.is_array() || j.size() != 3) return;
    for (size_t i = 0; i < 3; ++i) {
        if (j[i].is_number_integer()) out[i] = static_cast<uint8_t>(std::clamp(j[i].get<int>(), 0, 255));
    }
}

template <typename Enum>
void readEnum(const json& j, const char* key, Enum& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return;
    const auto value = it->template get<std::string>();
    for (int i = 0; i < static_cast<int>(Enum::Count); ++i) {
        if (enumId(static_cast<Enum>(i)) == value) {
            out = static_cast<Enum>(i);
            return;
        }
    }
}

void readFloat(const json& j, const char* key, float& out) {
    auto it = j.find(key);
    if (it != j.end() && it->is_number()) out = it->get<float>();
}

void readInt(const json& j, const char* key, int& out) {
    auto it = j.find(key);
    if (it != j.end() && it->is_number_integer()) out = it->get<int>();
}

void readBool(const json& j, const char* key, bool& out) {
    auto it = j.find(key);
    if (it != j.end() && it->is_boolean()) out = it->get<bool>();
}

void readString(const json& j, const char* key, std::string& out) {
    auto it = j.find(key);
    if (it != j.end() && it->is_string()) out = it->get<std::string>();
}

const json* child(const json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_object()) ? &*it : nullptr;
}

json stickToJson(const StickSettings& s) {
    json curve = json::array();
    for (const auto& p : s.customCurve) curve.push_back({p.x, p.y});
    return {
        {"deadzone", s.deadzone},
        {"outer_deadzone", s.outerDeadzone},
        {"anti_deadzone", s.antiDeadzone},
        {"shape", enumId(s.shape)},
        {"curve", enumId(s.curve)},
        {"curve_intensity", s.curveIntensity},
        {"custom_curve", curve},
        {"rc_filter", s.rcFilter},
        {"invert_x", s.invertX},
        {"invert_y", s.invertY},
    };
}

void stickFromJson(const json& j, StickSettings& s) {
    readFloat(j, "deadzone", s.deadzone);
    readFloat(j, "outer_deadzone", s.outerDeadzone);
    readFloat(j, "anti_deadzone", s.antiDeadzone);
    readEnum(j, "shape", s.shape);
    readEnum(j, "curve", s.curve);
    readFloat(j, "curve_intensity", s.curveIntensity);
    readFloat(j, "rc_filter", s.rcFilter);
    readBool(j, "invert_x", s.invertX);
    readBool(j, "invert_y", s.invertY);
    auto it = j.find("custom_curve");
    if (it != j.end() && it->is_array()) {
        s.customCurve.clear();
        for (const auto& p : *it) {
            if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number()) {
                s.customCurve.push_back({p[0].get<float>(), p[1].get<float>()});
            }
        }
    }
}

json triggerToJson(const TriggerSettings& t) {
    return {
        {"deadzone", t.deadzone},
        {"max_range", t.maxRange},
        {"anti_deadzone", t.antiDeadzone},
        {"mode", enumId(t.mode)},
        {"resistance", enumId(t.resistance)},
        {"resistance_position", t.resistancePosition},
        {"resistance_strength", t.resistanceStrength},
        {"hair_reset_distance", t.hairResetDistance},
        {"turbo", t.turbo},
        {"turbo_ms", t.turboIntervalMs},
        {"turbo_random_ms", t.turboRandomMs},
    };
}

void triggerFromJson(const json& j, TriggerSettings& t) {
    readFloat(j, "deadzone", t.deadzone);
    readFloat(j, "max_range", t.maxRange);
    readFloat(j, "anti_deadzone", t.antiDeadzone);
    readEnum(j, "mode", t.mode);
    readEnum(j, "resistance", t.resistance);
    readFloat(j, "resistance_position", t.resistancePosition);
    readInt(j, "resistance_strength", t.resistanceStrength);
    readFloat(j, "hair_reset_distance", t.hairResetDistance);
    readBool(j, "turbo", t.turbo);
    readInt(j, "turbo_ms", t.turboIntervalMs);
    readInt(j, "turbo_random_ms", t.turboRandomMs);
}

// Binding target as text: "cross", "none", "inherit", "key:space", "key:mouse_left".
std::string bindingTarget(const Binding& b) {
    switch (b.kind) {
        case Binding::Kind::Inherit: return "inherit";
        case Binding::Kind::Disabled: return "none";
        case Binding::Kind::Button: return std::string(buttonId(b.button));
        case Binding::Kind::Key: return "key:" + std::string(keyId(b.key));
    }
    return "none";
}

std::optional<Binding> bindingFromTarget(const std::string& text) {
    if (text == "inherit") return Binding::inherit();
    if (text == "none") return Binding::disabled();
    if (text.rfind("key:", 0) == 0) {
        if (auto k = keyFromId(std::string_view(text).substr(4))) return Binding::toKey(*k);
        return std::nullopt;
    }
    if (auto b = buttonFromId(text)) return Binding::toButton(*b);
    return std::nullopt;
}

// Plain string when there are no extras, otherwise
// {"to": ..., "toggle": ..., "turbo": ..., "turbo_ms": ..., "turbo_random_ms": ...}.
json bindingToJson(const Binding& b) {
    if (!b.toggle && !b.turbo) return bindingTarget(b);
    json j = {{"to", bindingTarget(b)}};
    if (b.toggle) j["toggle"] = true;
    if (b.turbo) {
        j["turbo"] = true;
        j["turbo_ms"] = b.turboIntervalMs;
        if (b.turboRandomMs > 0) j["turbo_random_ms"] = b.turboRandomMs;
    }
    return j;
}

std::optional<Binding> bindingFromJson(const json& j) {
    if (j.is_string()) return bindingFromTarget(j.get<std::string>());
    if (!j.is_object()) return std::nullopt;
    auto to = j.find("to");
    if (to == j.end() || !to->is_string()) return std::nullopt;
    auto b = bindingFromTarget(to->get<std::string>());
    if (!b) return std::nullopt;
    readBool(j, "toggle", b->toggle);
    readBool(j, "turbo", b->turbo);
    readInt(j, "turbo_ms", b->turboIntervalMs);
    readInt(j, "turbo_random_ms", b->turboRandomMs);
    return b;
}

json bindingMapToJson(const BindingMap& map, bool skipInherit, bool skipDisabled = false) {
    json out = json::object();
    for (int i = 0; i < kButtonCount; ++i) {
        const Button src = buttonAt(i);
        if (!isRemapSource(src)) continue;
        const Binding& b = map[static_cast<size_t>(i)];
        if (skipInherit && b.kind == Binding::Kind::Inherit) continue;
        if (skipDisabled && b.kind == Binding::Kind::Disabled) continue;
        out[std::string(buttonId(src))] = bindingToJson(b);
    }
    return out;
}

void bindingMapFromJson(const json& j, BindingMap& map) {
    for (const auto& [key, value] : j.items()) {
        const auto src = buttonFromId(key);
        if (!src || !isRemapSource(*src)) continue;
        if (auto b = bindingFromJson(value)) {
            map[static_cast<size_t>(index(*src))] = *b;
        } else {
            map[static_cast<size_t>(index(*src))] = Binding::disabled();
        }
    }
}

json gyroToJson(const GyroSettings& g) {
    return {
        {"activation", enumId(g.activation)},
        {"button", std::string(buttonId(g.button))},
        {"button2", g.button2 ? json(std::string(buttonId(*g.button2))) : json(nullptr)},
        {"sensitivity", g.sensitivity},
        {"vertical_ratio", g.verticalRatio},
        {"horizontal_axis", enumId(g.horizontalAxis)},
        {"deadzone", g.deadzone},
        {"smoothing", g.smoothing},
        {"anti_deadzone", g.antiDeadzone},
        {"precision", g.precision},
        {"precision_speed", g.precisionSpeed},
        {"invert_x", g.invertX},
        {"invert_y", g.invertY},
    };
}

void gyroFromJson(const json& j, GyroSettings& g) {
    readEnum(j, "activation", g.activation);
    if (auto it = j.find("button"); it != j.end() && it->is_string()) {
        if (auto b = buttonFromId(it->get<std::string>())) g.button = *b;
    }
    if (auto it = j.find("button2"); it != j.end()) {
        g.button2.reset();
        if (it->is_string()) g.button2 = buttonFromId(it->get<std::string>());
    }
    readFloat(j, "sensitivity", g.sensitivity);
    readFloat(j, "vertical_ratio", g.verticalRatio);
    readEnum(j, "horizontal_axis", g.horizontalAxis);
    readFloat(j, "deadzone", g.deadzone);
    readFloat(j, "smoothing", g.smoothing);
    readFloat(j, "anti_deadzone", g.antiDeadzone);
    readFloat(j, "precision", g.precision);
    readFloat(j, "precision_speed", g.precisionSpeed);
    readBool(j, "invert_x", g.invertX);
    readBool(j, "invert_y", g.invertY);
}

template <size_t N>
json floats(const std::array<float, N>& values) {
    json out = json::array();
    for (float v : values) out.push_back(v);
    return out;
}

template <size_t N>
void readFloats(const json& j, const char* key, std::array<float, N>& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != N) return;
    for (size_t i = 0; i < N; ++i) {
        if ((*it)[i].is_number()) out[i] = (*it)[i].template get<float>();
    }
}

json profileToJson(const Profile& p) {
    return {
        {"name", p.name},
        {"hotkey", p.hotkey ? json(std::string(buttonId(*p.hotkey))) : json(nullptr)},
        {"lightbar", {p.lightbar[0], p.lightbar[1], p.lightbar[2]}},
        {"swap_sticks", p.swapSticks},
        {"left_stick", stickToJson(p.leftStick)},
        {"right_stick", stickToJson(p.rightStick)},
        {"l2", triggerToJson(p.l2)},
        {"r2", triggerToJson(p.r2)},
        {"buttons", bindingMapToJson(p.buttons, false)},
        {"shift_button", p.shiftButton ? json(std::string(buttonId(*p.shiftButton))) : json(nullptr)},
        {"shift_buttons", bindingMapToJson(p.shiftButtons, true)},
        {"hold_buttons", bindingMapToJson(p.holdButtons, false, true)},
        {"hold_ms", p.holdMs},
        {"touchpad_mouse", p.touchpadMouse},
        {"touchpad_mouse_speed", p.touchpadMouseSpeed},
        {"touchpad_zones", enumId(p.touchpadZones)},
        {"zone_trigger", enumId(p.zoneTrigger)},
        {"gyro", gyroToJson(p.gyro)},
        {"light",
         {
             {"effect", enumId(p.light.effect)},
             {"colors", {rgb(p.light.extraColors[0]), rgb(p.light.extraColors[1]), rgb(p.light.extraColors[2])}},
             {"color_count", p.light.colorCount},
             {"period", p.light.periodSeconds},
             {"brightness", p.light.brightness},
         }},
        {"games", p.games},
    };
}

Profile profileFromJson(const json& j) {
    Profile p;
    readString(j, "name", p.name);
    if (auto it = j.find("hotkey"); it != j.end()) {
        if (it->is_null()) {
            p.hotkey.reset();
        } else if (it->is_string()) {
            p.hotkey = buttonFromId(it->get<std::string>());
        }
    }
    if (auto it = j.find("lightbar"); it != j.end()) readRgb(*it, p.lightbar);
    if (const json* c = child(j, "light")) {
        readEnum(*c, "effect", p.light.effect);
        if (auto it = c->find("colors"); it != c->end() && it->is_array()) {
            for (size_t i = 0; i < std::min<size_t>(it->size(), p.light.extraColors.size()); ++i) {
                readRgb((*it)[i], p.light.extraColors[i]);
            }
        }
        readInt(*c, "color_count", p.light.colorCount);
        readFloat(*c, "period", p.light.periodSeconds);
        readFloat(*c, "brightness", p.light.brightness);
    }
    if (auto it = j.find("games"); it != j.end() && it->is_array()) {
        p.games.clear();
        for (const auto& g : *it) {
            if (g.is_string()) p.games.push_back(g.get<std::string>());
        }
    }
    readBool(j, "swap_sticks", p.swapSticks);
    if (const json* c = child(j, "left_stick")) stickFromJson(*c, p.leftStick);
    if (const json* c = child(j, "right_stick")) stickFromJson(*c, p.rightStick);
    if (const json* c = child(j, "l2")) triggerFromJson(*c, p.l2);
    if (const json* c = child(j, "r2")) triggerFromJson(*c, p.r2);
    if (const json* c = child(j, "buttons")) bindingMapFromJson(*c, p.buttons);
    if (auto it = j.find("shift_button"); it != j.end()) {
        p.shiftButton = it->is_string() ? buttonFromId(it->get<std::string>()) : std::nullopt;
    }
    if (const json* c = child(j, "shift_buttons")) bindingMapFromJson(*c, p.shiftButtons);
    if (const json* c = child(j, "hold_buttons")) bindingMapFromJson(*c, p.holdButtons);
    readInt(j, "hold_ms", p.holdMs);
    readBool(j, "touchpad_mouse", p.touchpadMouse);
    readFloat(j, "touchpad_mouse_speed", p.touchpadMouseSpeed);
    readEnum(j, "touchpad_zones", p.touchpadZones);
    readEnum(j, "zone_trigger", p.zoneTrigger);
    if (const json* c = child(j, "gyro")) gyroFromJson(*c, p.gyro);
    return p;
}

// What `value` changes compared to `base`: objects are compared key by key, anything else as a whole.
json difference(const json& base, const json& value) {
    if (!base.is_object() || !value.is_object()) return value;
    json out = json::object();
    for (const auto& [key, v] : value.items()) {
        const auto it = base.find(key);
        if (it == base.end()) {
            out[key] = v;
        } else if (*it != v) {
            out[key] = (it->is_object() && v.is_object()) ? difference(*it, v) : v;
        }
    }
    return out;
}

// `base` with `changes` laid over it (null values included, unlike a JSON merge patch).
void overlay(json& base, const json& changes) {
    for (const auto& [key, v] : changes.items()) {
        if (v.is_object() && base.contains(key) && base[key].is_object()) {
            overlay(base[key], v);
        } else {
            base[key] = v;
        }
    }
}

constexpr char kBase64Url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr std::string_view kShareCodePrefix = "EP1-";

std::string base64Url(const std::string& bytes) {
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char c : bytes) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(kBase64Url[(buffer >> bits) & 0x3F]);
        }
    }
    if (bits > 0) out.push_back(kBase64Url[(buffer << (6 - bits)) & 0x3F]);
    return out;
}

std::optional<std::string> fromBase64Url(std::string_view text) {
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        const char* at = std::strchr(kBase64Url, c);
        if (c == '\0' || at == nullptr) return std::nullopt;
        buffer = (buffer << 6) | static_cast<uint32_t>(at - kBase64Url);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

uint32_t textCrc(const std::string& text) {
    return dualsense::crc32(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

}  // namespace

std::string profileShareCode(const Profile& profile) {
    const std::string text = difference(profileToJson(Profile{}), profileToJson(profile)).dump();
    std::string bytes = text;
    const uint32_t crc = textCrc(text);
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<char>((crc >> (8 * i)) & 0xFF));
    return std::string(kShareCodePrefix) + base64Url(bytes);
}

std::optional<Profile> profileFromShareCode(std::string_view code, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<Profile> {
        if (error) *error = why;
        return std::nullopt;
    };
    std::string clean;
    for (char c : code) {
        if (!std::isspace(static_cast<unsigned char>(c))) clean.push_back(c);
    }
    if (clean.size() < kShareCodePrefix.size() ||
        !std::equal(kShareCodePrefix.begin(), kShareCodePrefix.end(), clean.begin(),
                    [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b)); })) {
        return fail("not an EdgePad profile code (they start with EP1-)");
    }
    const auto bytes = fromBase64Url(std::string_view(clean).substr(kShareCodePrefix.size()));
    if (!bytes || bytes->size() < 4) return fail("the code is incomplete or contains invalid characters");
    const std::string text = bytes->substr(0, bytes->size() - 4);
    uint32_t crc = 0;
    for (int i = 0; i < 4; ++i) crc |= uint32_t{static_cast<uint8_t>((*bytes)[text.size() + static_cast<size_t>(i)])} << (8 * i);
    if (crc != textCrc(text)) return fail("the code is damaged or incomplete (check that all of it was copied)");
    const json changes = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!changes.is_object()) return fail("the code does not contain a profile");
    json full = profileToJson(Profile{});
    overlay(full, changes);
    Config cfg;
    cfg.profiles = {profileFromJson(full)};
    cfg.normalize();
    return cfg.profiles.front();
}

std::string configToJson(const Config& cfg) {
    json profiles = json::array();
    for (const auto& p : cfg.profiles) profiles.push_back(profileToJson(p));
    const json root = {
        {"version", kConfigFormatVersion},
        {"settings",
         {
             {"output", enumId(cfg.settings.output)},
             {"fn_mode", enumId(cfg.settings.fnMode)},
             {"rumble", cfg.settings.rumble},
             {"rumble_strength", cfg.settings.rumbleStrength},
             {"low_battery_alert", cfg.settings.lowBatteryAlert},
             {"game_lightbar", cfg.settings.gameLightbar},
             {"auto_update", cfg.settings.autoUpdate},
             {"hide_controller", cfg.settings.hideController},
             {"auto_profiles", cfg.settings.autoProfiles},
             {"enabled", cfg.settings.enabled},
             {"active_profile", cfg.settings.activeProfile},
             {"left_stick_center", floats(cfg.settings.leftStickCenter)},
             {"right_stick_center", floats(cfg.settings.rightStickCenter)},
             {"gyro_bias", floats(cfg.settings.gyroBias)},
         }},
        {"profiles", profiles},
    };
    return root.dump(2);
}

Config configFromJson(const std::string& text, std::string* warning) {
    Config cfg = Config::defaults();
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        if (warning) *warning = "config file is not valid JSON, using defaults";
        cfg.normalize();
        return cfg;
    }
    if (const json* s = child(root, "settings")) {
        readEnum(*s, "output", cfg.settings.output);
        readEnum(*s, "fn_mode", cfg.settings.fnMode);
        readBool(*s, "rumble", cfg.settings.rumble);
        readFloat(*s, "rumble_strength", cfg.settings.rumbleStrength);
        readBool(*s, "low_battery_alert", cfg.settings.lowBatteryAlert);
        readBool(*s, "game_lightbar", cfg.settings.gameLightbar);
        readBool(*s, "auto_update", cfg.settings.autoUpdate);
        readBool(*s, "hide_controller", cfg.settings.hideController);
        readBool(*s, "auto_profiles", cfg.settings.autoProfiles);
        readBool(*s, "enabled", cfg.settings.enabled);
        readInt(*s, "active_profile", cfg.settings.activeProfile);
        readFloats(*s, "left_stick_center", cfg.settings.leftStickCenter);
        readFloats(*s, "right_stick_center", cfg.settings.rightStickCenter);
        readFloats(*s, "gyro_bias", cfg.settings.gyroBias);
    }
    if (auto it = root.find("profiles"); it != root.end() && it->is_array() && !it->empty()) {
        cfg.profiles.clear();
        for (const auto& p : *it) {
            if (p.is_object()) cfg.profiles.push_back(profileFromJson(p));
        }
    }
    int version = 1;
    readInt(root, "version", version);
    if (version < 2) {
        // Files from before version 2 saved the old 4% hair trigger reset default: move it to the new 1%.
        for (auto& p : cfg.profiles) {
            for (TriggerSettings* t : {&p.l2, &p.r2}) {
                if (std::fabs(t->hairResetDistance - 0.04f) < 1e-4f) t->hairResetDistance = kHairDefaultReset;
            }
        }
    }
    if (version < 3) {
        // Before version 3 every stick started with a 5% dead zone and a 2% outer dead zone, on top
        // of the game's own. Sticks still on exactly those defaults (and with no anti-dead zone,
        // which needs an inner dead zone) move to the new 1:1 defaults.
        for (auto& p : cfg.profiles) {
            for (StickSettings* st : {&p.leftStick, &p.rightStick}) {
                if (std::fabs(st->deadzone - 0.05f) < 1e-4f && std::fabs(st->outerDeadzone - 0.02f) < 1e-4f &&
                    st->antiDeadzone <= 0.0f) {
                    st->deadzone = 0.0f;
                    st->outerDeadzone = 0.0f;
                }
            }
        }
    }
    if (version < 4) {
        // Before version 4 Mute could not be sent to a game and was saved as "nothing". With the
        // virtual DualSense it can: the old default becomes Mute -> Mute.
        for (auto& p : cfg.profiles) {
            Binding& mute = p.buttons[static_cast<size_t>(index(Button::Mute))];
            if (mute.kind == Binding::Kind::Disabled) mute = Binding::toButton(Button::Mute);
        }
    }
    if (version < 5) {
        // Before version 5 an anti-dead zone needed a 5% inner dead zone (the FPS profile shipped
        // with 5% / 2%), which added dead zone instead of removing it. It needs none now.
        for (auto& p : cfg.profiles) {
            for (StickSettings* st : {&p.leftStick, &p.rightStick}) {
                if (st->antiDeadzone > 0.0f && std::fabs(st->deadzone - 0.05f) < 1e-4f &&
                    std::fabs(st->outerDeadzone - 0.02f) < 1e-4f) {
                    st->deadzone = 0.0f;
                    st->outerDeadzone = 0.0f;
                }
            }
        }
    }
    cfg.normalize();
    return cfg;
}

}  // namespace edgepad
