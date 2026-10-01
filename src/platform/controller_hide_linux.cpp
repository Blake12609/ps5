// Hiding the controller on Linux: the kernel's DualSense driver turns the controller into input
// devices (gamepad, motion sensors, touchpad). EdgePad grabs them (EVIOCGRAB), so their events
// only reach EdgePad - games, joydev and the desktop get nothing. EdgePad itself reads hidraw,
// which a grab does not affect.
#include "platform/controller_hide.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

namespace edgepad {

struct ControllerHider::Impl {
    std::vector<int> grabbed;

    void release() {
        for (int fd : grabbed) {
            ioctl(fd, EVIOCGRAB, 0);
            ::close(fd);
        }
        grabbed.clear();
    }
};

ControllerHider::ControllerHider(std::filesystem::path) : impl_(std::make_unique<Impl>()) {}

ControllerHider::~ControllerHider() { restore(); }

bool ControllerHider::needsExclusiveOpen(std::string& reason) {
    reason.clear();
    return false;
}

bool ControllerHider::hide(const std::string& hidPath, std::string& message) {
    namespace fs = std::filesystem;
    impl_->release();
    std::error_code ec;
    // /dev/hidrawN -> /sys/class/hidraw/hidrawN/device: the HID device, its input devices below it.
    const fs::path hidDevice = fs::path("/sys/class/hidraw") / fs::path(hidPath).filename() / "device" / "input";
    int found = 0;
    bool denied = false;
    for (const auto& input : fs::directory_iterator(hidDevice, ec)) {
        std::error_code ec2;
        for (const auto& node : fs::directory_iterator(input.path(), ec2)) {
            const std::string name = node.path().filename().string();
            if (name.rfind("event", 0) != 0) continue;
            ++found;
            const std::string dev = "/dev/input/" + name;
            const int fd = ::open(dev.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                denied |= errno == EACCES || errno == EPERM;
                continue;
            }
            if (ioctl(fd, EVIOCGRAB, 1) == 0) {
                impl_->grabbed.push_back(fd);
            } else {
                ::close(fd);  // another program grabbed it first
            }
        }
    }
    if (found == 0) {
        message = "the controller has no input devices to hide";
        return false;
    }
    if (impl_->grabbed.empty()) {
        message = denied ? "no permission for the controller's input devices (install 70-edgepad.rules)"
                         : "another program has grabbed the controller";
        return false;
    }
    message = "Hidden from games";
    if (static_cast<int>(impl_->grabbed.size()) < found) {
        message += " (" + std::to_string(impl_->grabbed.size()) + " of " + std::to_string(found) +
                   " input devices; install 70-edgepad.rules for the rest)";
    }
    return true;
}

void ControllerHider::restore() { impl_->release(); }

}  // namespace edgepad
