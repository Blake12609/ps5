#include "platform/keyboard.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace edgepad {
namespace {

int linuxCode(Key k) {
    static constexpr int kLetters[] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
                                       KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
                                       KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    static constexpr int kDigits[] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
    static constexpr int kF[] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
                                 KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12};
    static constexpr int kPad[] = {KEY_KP0, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4,
                                   KEY_KP5, KEY_KP6, KEY_KP7, KEY_KP8, KEY_KP9};
    const int v = static_cast<int>(k);
    if (k >= Key::A && k <= Key::Z) return kLetters[v - static_cast<int>(Key::A)];
    if (k >= Key::Num0 && k <= Key::Num9) return kDigits[v - static_cast<int>(Key::Num0)];
    if (k >= Key::F1 && k <= Key::F12) return kF[v - static_cast<int>(Key::F1)];
    if (k >= Key::Numpad0 && k <= Key::Numpad9) return kPad[v - static_cast<int>(Key::Numpad0)];
    switch (k) {
        case Key::Escape: return KEY_ESC;
        case Key::Tab: return KEY_TAB;
        case Key::CapsLock: return KEY_CAPSLOCK;
        case Key::Shift: return KEY_LEFTSHIFT;
        case Key::Ctrl: return KEY_LEFTCTRL;
        case Key::Alt: return KEY_LEFTALT;
        case Key::Space: return KEY_SPACE;
        case Key::Enter: return KEY_ENTER;
        case Key::Backspace: return KEY_BACKSPACE;
        case Key::Up: return KEY_UP;
        case Key::Down: return KEY_DOWN;
        case Key::Left: return KEY_LEFT;
        case Key::Right: return KEY_RIGHT;
        case Key::Insert: return KEY_INSERT;
        case Key::Delete: return KEY_DELETE;
        case Key::Home: return KEY_HOME;
        case Key::End: return KEY_END;
        case Key::PageUp: return KEY_PAGEUP;
        case Key::PageDown: return KEY_PAGEDOWN;
        case Key::Minus: return KEY_MINUS;
        case Key::Equals: return KEY_EQUAL;
        case Key::LeftBracket: return KEY_LEFTBRACE;
        case Key::RightBracket: return KEY_RIGHTBRACE;
        case Key::Semicolon: return KEY_SEMICOLON;
        case Key::Apostrophe: return KEY_APOSTROPHE;
        case Key::Grave: return KEY_GRAVE;
        case Key::Backslash: return KEY_BACKSLASH;
        case Key::Comma: return KEY_COMMA;
        case Key::Period: return KEY_DOT;
        case Key::Slash: return KEY_SLASH;
        case Key::MouseLeft: return BTN_LEFT;
        case Key::MouseRight: return BTN_RIGHT;
        case Key::MouseMiddle: return BTN_MIDDLE;
        case Key::Mouse4: return BTN_SIDE;
        case Key::Mouse5: return BTN_EXTRA;
        default: return -1;
    }
}

class UInputKeyboard final : public KeyboardOutput {
public:
    explicit UInputKeyboard(int fd) : fd_(fd) {}
    ~UInputKeyboard() override {
        ioctl(fd_, UI_DEV_DESTROY);
        ::close(fd_);
    }

    bool set(Key key, bool down) override {
        const int code = linuxCode(key);
        if (code < 0) return false;
        return emit(EV_KEY, code, down ? 1 : 0) && emit(EV_SYN, SYN_REPORT, 0);
    }

    bool move(int dx, int dy, int wheel) override {
        if (dx == 0 && dy == 0 && wheel == 0) return true;
        bool ok = true;
        if (dx != 0) ok = emit(EV_REL, REL_X, dx) && ok;
        if (dy != 0) ok = emit(EV_REL, REL_Y, dy) && ok;
        if (wheel != 0) ok = emit(EV_REL, REL_WHEEL, wheel) && ok;
        return emit(EV_SYN, SYN_REPORT, 0) && ok;
    }

private:
    bool emit(int type, int code, int value) {
        input_event ev{};
        ev.type = static_cast<__u16>(type);
        ev.code = static_cast<__u16>(code);
        ev.value = value;
        return ::write(fd_, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev));
    }

    int fd_;
};

}  // namespace

std::unique_ptr<KeyboardOutput> createKeyboardOutput(std::string& error) {
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        error = std::string("Cannot open /dev/uinput for key bindings: ") + std::strerror(errno);
        return nullptr;
    }
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0 &&
              ioctl(fd, UI_SET_EVBIT, EV_REL) == 0 && ioctl(fd, UI_SET_RELBIT, REL_X) == 0 &&
              ioctl(fd, UI_SET_RELBIT, REL_Y) == 0 &&  // a real mouse: pointer movement for the touchpad
              ioctl(fd, UI_SET_RELBIT, REL_WHEEL) == 0;
    for (int i = 1; i < kKeyCount && ok; ++i) {
        const int code = linuxCode(static_cast<Key>(i));
        if (code >= 0) ok = ioctl(fd, UI_SET_KEYBIT, code) == 0;
    }
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1209;
    setup.id.product = 0xED6E;
    setup.id.version = 1;
    std::strncpy(setup.name, "EdgePad Virtual Keyboard", UINPUT_MAX_NAME_SIZE - 1);
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        error = std::string("uinput keyboard setup failed: ") + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    return std::make_unique<UInputKeyboard>(fd);
}

}  // namespace edgepad
