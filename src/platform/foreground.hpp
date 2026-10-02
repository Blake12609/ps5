#pragma once

#include <memory>
#include <string>

namespace edgepad {

// Which program's window is in front, for automatic profiles. Cheap enough to ask twice a second:
// the program's name is only looked up again when another window comes to the front.
//  Windows: the foreground window's process.
//  Linux:   X11's active window (_NET_ACTIVE_WINDOW / _NET_WM_PID), which also covers games running
//           through Wine / Proton and XWayland. Pure Wayland desktops do not say which window is active.
class ForegroundWatcher {
public:
    struct App {
        std::string name;   // executable file name ("cod.exe"), empty when unknown
        bool self = false;  // EdgePad's own window
    };

    ForegroundWatcher();
    ~ForegroundWatcher();
    ForegroundWatcher(const ForegroundWatcher&) = delete;
    ForegroundWatcher& operator=(const ForegroundWatcher&) = delete;

    App current();
    // False when this desktop cannot tell which window is in front.
    bool supported() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace edgepad
