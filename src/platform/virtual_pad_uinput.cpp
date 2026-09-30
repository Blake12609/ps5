// Virtual Xbox 360 style controller through /dev/uinput (Linux).
#include "platform/virtual_pad.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <map>

#include "core/processing.hpp"

namespace edgepad {
namespace {

struct KeyBinding {
    Button button;
    int code;
};

constexpr KeyBinding kKeys[] = {
    {Button::Cross, BTN_A},       {Button::Circle, BTN_B},       {Button::Square, BTN_X},
    {Button::Triangle, BTN_Y},    {Button::L1, BTN_TL},          {Button::R1, BTN_TR},
    {Button::Create, BTN_SELECT}, {Button::Options, BTN_START},  {Button::PS, BTN_MODE},
    {Button::L3, BTN_THUMBL},     {Button::R3, BTN_THUMBR},      {Button::Touchpad, BTN_SELECT},
};

constexpr int kKeyCodes[] = {BTN_A,      BTN_B,     BTN_X,    BTN_Y,      BTN_TL,    BTN_TR,
                             BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};

class UInputPad final : public VirtualPad {
public:
    UInputPad(int fd, OutputKind kind) : fd_(fd), kind_(kind) {}
    ~UInputPad() override {
        ioctl(fd_, UI_DEV_DESTROY);
        ::close(fd_);
    }

    bool send(const OutputState& s) override {
        bool ok = true;
        for (int code : kKeyCodes) {
            int value = 0;
            for (const auto& k : kKeys) value |= (k.code == code && has(s.buttons, k.button)) ? 1 : 0;
            ok &= emit(EV_KEY, code, value);
        }

        auto thumb = [](float v) { return static_cast<int>(std::lround(clamp11(v) * 32767.0f)); };
        auto trigger = [](float v) { return static_cast<int>(std::lround(clamp01(v) * 255.0f)); };
        ok &= emit(EV_ABS, ABS_X, thumb(s.lx));
        ok &= emit(EV_ABS, ABS_Y, -thumb(s.ly));  // evdev: down is positive
        ok &= emit(EV_ABS, ABS_RX, thumb(s.rx));
        ok &= emit(EV_ABS, ABS_RY, -thumb(s.ry));
        ok &= emit(EV_ABS, ABS_Z, trigger(s.l2));
        ok &= emit(EV_ABS, ABS_RZ, trigger(s.r2));
        const int hatX = (has(s.buttons, Button::DpadRight) ? 1 : 0) - (has(s.buttons, Button::DpadLeft) ? 1 : 0);
        const int hatY = (has(s.buttons, Button::DpadDown) ? 1 : 0) - (has(s.buttons, Button::DpadUp) ? 1 : 0);
        ok &= emit(EV_ABS, ABS_HAT0X, hatX);
        ok &= emit(EV_ABS, ABS_HAT0Y, hatY);
        ok &= emit(EV_SYN, SYN_REPORT, 0, /*force=*/true);
        return ok;
    }

    std::string name() const override {
        return kind_ == OutputKind::DualShock4 ? "Virtual controller (uinput, Xbox layout)"
                                              : "Virtual Xbox 360 controller (uinput)";
    }

private:
    bool emit(int type, int code, int value, bool force = false) {
        const int key = (type << 16) | code;
        if (!force) {
            auto it = last_.find(key);
            if (it != last_.end() && it->second == value) return true;
            last_[key] = value;
        }
        input_event ev{};
        ev.type = static_cast<__u16>(type);
        ev.code = static_cast<__u16>(code);
        ev.value = value;
        return ::write(fd_, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev));
    }

    int fd_;
    OutputKind kind_;
    std::map<int, int> last_;
};

bool setupAbs(int fd, int code, int min, int max, int fuzz, int flat) {
    uinput_abs_setup abs{};
    abs.code = static_cast<__u16>(code);
    abs.absinfo.minimum = min;
    abs.absinfo.maximum = max;
    abs.absinfo.fuzz = fuzz;
    abs.absinfo.flat = flat;
    return ioctl(fd, UI_SET_ABSBIT, code) == 0 && ioctl(fd, UI_ABS_SETUP, &abs) == 0;
}

}  // namespace

const char* virtualPadDriverUrl() { return "https://www.kernel.org/doc/html/latest/input/uinput.html"; }

PadCreateResult createVirtualPad(OutputKind kind, RumbleCallback /*onRumble*/) {
    PadCreateResult result;
    if (kind == OutputKind::None) {
        result.error = PadError::Unsupported;
        result.message = "Virtual controller disabled (monitor only)";
        return result;
    }

    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const int err = errno;
        result.error = (err == EACCES || err == EPERM) ? PadError::PermissionDenied : PadError::DriverMissing;
        result.message = std::string("Cannot open /dev/uinput: ") + std::strerror(err) +
                         (err == EACCES ? " (install packaging/linux/70-edgepad.rules)" : "");
        return result;
    }

    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0 &&
              ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
    for (int code : kKeyCodes) {
        ok = ok && ioctl(fd, UI_SET_KEYBIT, code) == 0;
    }
    ok = ok && setupAbs(fd, ABS_X, -32768, 32767, 16, 0) && setupAbs(fd, ABS_Y, -32768, 32767, 16, 0) &&
         setupAbs(fd, ABS_RX, -32768, 32767, 16, 0) && setupAbs(fd, ABS_RY, -32768, 32767, 16, 0) &&
         setupAbs(fd, ABS_Z, 0, 255, 0, 0) && setupAbs(fd, ABS_RZ, 0, 255, 0, 0) &&
         setupAbs(fd, ABS_HAT0X, -1, 1, 0, 0) && setupAbs(fd, ABS_HAT0Y, -1, 1, 0, 0);

    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x045E;   // Microsoft
    setup.id.product = 0x028E;  // Xbox 360 controller: recognised by SDL, Steam and most games
    setup.id.version = 0x0110;
    std::strncpy(setup.name, "EdgePad Virtual Controller", UINPUT_MAX_NAME_SIZE - 1);
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        const int err = errno;
        ::close(fd);
        result.error = PadError::Failed;
        result.message = std::string("uinput setup failed: ") + std::strerror(err);
        return result;
    }
    result.pad = std::make_unique<UInputPad>(fd, kind);
    return result;
}

}  // namespace edgepad
