#include "core/keys.hpp"

namespace edgepad {
namespace {

struct KeyInfo {
    std::string_view id;
    std::string_view label;
};

constexpr std::array<KeyInfo, kKeyCount> kKeys{{
    {"none", "None"},
    {"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}, {"e", "E"}, {"f", "F"}, {"g", "G"}, {"h", "H"}, {"i", "I"},
    {"j", "J"}, {"k", "K"}, {"l", "L"}, {"m", "M"}, {"n", "N"}, {"o", "O"}, {"p", "P"}, {"q", "Q"}, {"r", "R"},
    {"s", "S"}, {"t", "T"}, {"u", "U"}, {"v", "V"}, {"w", "W"}, {"x", "X"}, {"y", "Y"}, {"z", "Z"},
    {"0", "0"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"}, {"6", "6"}, {"7", "7"}, {"8", "8"}, {"9", "9"},
    {"f1", "F1"}, {"f2", "F2"}, {"f3", "F3"}, {"f4", "F4"}, {"f5", "F5"}, {"f6", "F6"},
    {"f7", "F7"}, {"f8", "F8"}, {"f9", "F9"}, {"f10", "F10"}, {"f11", "F11"}, {"f12", "F12"},
    {"escape", "Esc"}, {"tab", "Tab"}, {"caps_lock", "Caps Lock"}, {"shift", "Shift"}, {"ctrl", "Ctrl"},
    {"alt", "Alt"}, {"space", "Space"}, {"enter", "Enter"}, {"backspace", "Backspace"},
    {"up", "Arrow up"}, {"down", "Arrow down"}, {"left", "Arrow left"}, {"right", "Arrow right"},
    {"insert", "Insert"}, {"delete", "Delete"}, {"home", "Home"}, {"end", "End"},
    {"page_up", "Page Up"}, {"page_down", "Page Down"},
    {"minus", "-"}, {"equals", "="}, {"left_bracket", "["}, {"right_bracket", "]"}, {"semicolon", ";"},
    {"apostrophe", "'"}, {"grave", "`"}, {"backslash", "\\"}, {"comma", ","}, {"period", "."}, {"slash", "/"},
    {"numpad_0", "Numpad 0"}, {"numpad_1", "Numpad 1"}, {"numpad_2", "Numpad 2"}, {"numpad_3", "Numpad 3"},
    {"numpad_4", "Numpad 4"}, {"numpad_5", "Numpad 5"}, {"numpad_6", "Numpad 6"}, {"numpad_7", "Numpad 7"},
    {"numpad_8", "Numpad 8"}, {"numpad_9", "Numpad 9"},
    {"mouse_left", "Mouse left"}, {"mouse_right", "Mouse right"}, {"mouse_middle", "Mouse middle"},
    {"mouse_4", "Mouse 4 (back)"}, {"mouse_5", "Mouse 5 (forward)"},
}};

}  // namespace

std::string_view keyId(Key k) {
    const auto i = static_cast<size_t>(k);
    return i < kKeys.size() ? kKeys[i].id : std::string_view{"none"};
}

std::string_view keyLabel(Key k) {
    const auto i = static_cast<size_t>(k);
    return i < kKeys.size() ? kKeys[i].label : std::string_view{"None"};
}

std::optional<Key> keyFromId(std::string_view id) {
    for (size_t i = 1; i < kKeys.size(); ++i) {
        if (kKeys[i].id == id) return static_cast<Key>(i);
    }
    return std::nullopt;
}

bool isMouseButton(Key k) { return k >= Key::MouseLeft && k <= Key::Mouse5; }

}  // namespace edgepad
