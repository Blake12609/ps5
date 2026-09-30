#include "platform/keyboard.hpp"

#include <windows.h>

#ifndef MAPVK_VK_TO_VSC_EX
#define MAPVK_VK_TO_VSC_EX 4
#endif

namespace edgepad {
namespace {

WORD virtualKey(Key k) {
    const int v = static_cast<int>(k);
    if (k >= Key::A && k <= Key::Z) return static_cast<WORD>('A' + (v - static_cast<int>(Key::A)));
    if (k >= Key::Num0 && k <= Key::Num9) return static_cast<WORD>('0' + (v - static_cast<int>(Key::Num0)));
    if (k >= Key::F1 && k <= Key::F12) return static_cast<WORD>(VK_F1 + (v - static_cast<int>(Key::F1)));
    if (k >= Key::Numpad0 && k <= Key::Numpad9) return static_cast<WORD>(VK_NUMPAD0 + (v - static_cast<int>(Key::Numpad0)));
    switch (k) {
        case Key::Escape: return VK_ESCAPE;
        case Key::Tab: return VK_TAB;
        case Key::CapsLock: return VK_CAPITAL;
        case Key::Shift: return VK_LSHIFT;
        case Key::Ctrl: return VK_LCONTROL;
        case Key::Alt: return VK_LMENU;
        case Key::Space: return VK_SPACE;
        case Key::Enter: return VK_RETURN;
        case Key::Backspace: return VK_BACK;
        case Key::Up: return VK_UP;
        case Key::Down: return VK_DOWN;
        case Key::Left: return VK_LEFT;
        case Key::Right: return VK_RIGHT;
        case Key::Insert: return VK_INSERT;
        case Key::Delete: return VK_DELETE;
        case Key::Home: return VK_HOME;
        case Key::End: return VK_END;
        case Key::PageUp: return VK_PRIOR;
        case Key::PageDown: return VK_NEXT;
        case Key::Minus: return VK_OEM_MINUS;
        case Key::Equals: return VK_OEM_PLUS;
        case Key::LeftBracket: return VK_OEM_4;
        case Key::RightBracket: return VK_OEM_6;
        case Key::Semicolon: return VK_OEM_1;
        case Key::Apostrophe: return VK_OEM_7;
        case Key::Grave: return VK_OEM_3;
        case Key::Backslash: return VK_OEM_5;
        case Key::Comma: return VK_OEM_COMMA;
        case Key::Period: return VK_OEM_PERIOD;
        case Key::Slash: return VK_OEM_2;
        default: return 0;
    }
}

class SendInputKeyboard final : public KeyboardOutput {
public:
    bool set(Key key, bool down) override {
        INPUT input{};
        if (isMouseButton(key)) {
            input.type = INPUT_MOUSE;
            switch (key) {
                case Key::MouseLeft: input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
                case Key::MouseRight: input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
                case Key::MouseMiddle: input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
                case Key::Mouse4:
                case Key::Mouse5:
                    input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
                    input.mi.mouseData = key == Key::Mouse4 ? XBUTTON1 : XBUTTON2;
                    break;
                default: return false;
            }
        } else {
            const WORD vk = virtualKey(key);
            if (vk == 0) return false;
            // Scan codes, not virtual keys: games using raw input / DirectInput only see scan codes.
            const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
            input.type = INPUT_KEYBOARD;
            input.ki.wScan = static_cast<WORD>(scan & 0xFF);
            input.ki.dwFlags = KEYEVENTF_SCANCODE;
            if ((scan & 0xFF00) == 0xE000) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            if (!down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
        }
        return SendInput(1, &input, sizeof(INPUT)) == 1;
    }
};

}  // namespace

std::unique_ptr<KeyboardOutput> createKeyboardOutput(std::string& /*error*/) {
    return std::make_unique<SendInputKeyboard>();
}

}  // namespace edgepad
