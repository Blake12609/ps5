#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace edgepad {

// Keyboard keys and mouse buttons a controller button can be bound to.
enum class Key : uint8_t {
    None,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Escape, Tab, CapsLock, Shift, Ctrl, Alt, Space, Enter, Backspace,
    Up, Down, Left, Right, Insert, Delete, Home, End, PageUp, PageDown,
    Minus, Equals, LeftBracket, RightBracket, Semicolon, Apostrophe, Grave, Backslash, Comma, Period, Slash,
    Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    MouseLeft, MouseRight, MouseMiddle, Mouse4, Mouse5,
    Count
};

inline constexpr int kKeyCount = static_cast<int>(Key::Count);

std::string_view keyId(Key k);     // stable id for config files, e.g. "space", "mouse_left"
std::string_view keyLabel(Key k);  // e.g. "Space", "Mouse left"
std::optional<Key> keyFromId(std::string_view id);
bool isMouseButton(Key k);

// Set of pressed keys.
class KeyMask {
public:
    void set(Key k, bool on = true) {
        const auto i = static_cast<unsigned>(k);
        if (on) {
            bits_[i / 64] |= uint64_t{1} << (i % 64);
        } else {
            bits_[i / 64] &= ~(uint64_t{1} << (i % 64));
        }
    }
    bool test(Key k) const {
        const auto i = static_cast<unsigned>(k);
        return (bits_[i / 64] >> (i % 64)) & 1u;
    }
    bool any() const { return bits_[0] != 0 || bits_[1] != 0; }
    bool operator==(const KeyMask&) const = default;

private:
    std::array<uint64_t, 2> bits_{};
};
static_assert(kKeyCount <= 128, "KeyMask holds 128 keys");

}  // namespace edgepad
