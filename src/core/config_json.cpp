#include "core/config_json.hpp"

#include <algorithm>

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
}

json profileToJson(const Profile& p) {
    json buttons = json::object();
    for (int i = 0; i < kButtonCount; ++i) {
        const Button src = buttonAt(i);
        if (!isRemapSource(src)) continue;
        const auto& target = p.buttons[static_cast<size_t>(i)];
        buttons[std::string(buttonId(src))] = target ? std::string(buttonId(*target)) : std::string("none");
    }
    return {
        {"name", p.name},
        {"hotkey", p.hotkey ? json(std::string(buttonId(*p.hotkey))) : json(nullptr)},
        {"lightbar", {p.lightbar[0], p.lightbar[1], p.lightbar[2]}},
        {"swap_sticks", p.swapSticks},
        {"left_stick", stickToJson(p.leftStick)},
        {"right_stick", stickToJson(p.rightStick)},
        {"l2", triggerToJson(p.l2)},
        {"r2", triggerToJson(p.r2)},
        {"buttons", buttons},
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
    if (auto it = j.find("lightbar"); it != j.end() && it->is_array() && it->size() == 3) {
        for (size_t i = 0; i < 3; ++i) {
            if ((*it)[i].is_number_integer()) p.lightbar[i] = static_cast<uint8_t>(std::clamp((*it)[i].get<int>(), 0, 255));
        }
    }
    readBool(j, "swap_sticks", p.swapSticks);
    if (const json* c = child(j, "left_stick")) stickFromJson(*c, p.leftStick);
    if (const json* c = child(j, "right_stick")) stickFromJson(*c, p.rightStick);
    if (const json* c = child(j, "l2")) triggerFromJson(*c, p.l2);
    if (const json* c = child(j, "r2")) triggerFromJson(*c, p.r2);
    if (const json* c = child(j, "buttons")) {
        for (const auto& [key, value] : c->items()) {
            const auto src = buttonFromId(key);
            if (!src || !isRemapSource(*src) || !value.is_string()) continue;
            const auto target = buttonFromId(value.get<std::string>());
            p.buttons[static_cast<size_t>(index(*src))] = (target && isRemapTarget(*target)) ? target : std::nullopt;
        }
    }
    return p;
}

}  // namespace

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
             {"game_lightbar", cfg.settings.gameLightbar},
             {"auto_update", cfg.settings.autoUpdate},
             {"enabled", cfg.settings.enabled},
             {"active_profile", cfg.settings.activeProfile},
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
        readBool(*s, "game_lightbar", cfg.settings.gameLightbar);
        readBool(*s, "auto_update", cfg.settings.autoUpdate);
        readBool(*s, "enabled", cfg.settings.enabled);
        readInt(*s, "active_profile", cfg.settings.activeProfile);
    }
    if (auto it = root.find("profiles"); it != root.end() && it->is_array() && !it->empty()) {
        cfg.profiles.clear();
        for (const auto& p : *it) {
            if (p.is_object()) cfg.profiles.push_back(profileFromJson(p));
        }
    }
    cfg.normalize();
    return cfg;
}

}  // namespace edgepad
