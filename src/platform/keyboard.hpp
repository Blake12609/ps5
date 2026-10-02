#pragma once

#include <memory>
#include <string>

#include "core/keys.hpp"

namespace edgepad {

// Presses keyboard keys and mouse buttons for button bindings, moves the mouse for the touchpad.
class KeyboardOutput {
public:
    virtual ~KeyboardOutput() = default;
    virtual bool set(Key key, bool down) = 0;
    // Moves the mouse pointer by (dx, dy) pixels and turns the scroll wheel (notches, + = up).
    virtual bool move(int dx, int dy, int wheel) = 0;
};

// Windows: SendInput with hardware scan codes (what games read). Linux: a uinput keyboard/mouse.
std::unique_ptr<KeyboardOutput> createKeyboardOutput(std::string& error);

}  // namespace edgepad
