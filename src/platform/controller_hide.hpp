#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace edgepad {

// Hides the real controller from games so they only see EdgePad's virtual controller (no double
// input, no game reading the controller in its own way next to EdgePad).
//  Windows: HidHide when it is installed - the controller is hidden from every program except
//           EdgePad. Without HidHide, EdgePad opens the controller exclusively instead, which works
//           as long as no other program has it open already.
//  Linux:   grabs the controller's input devices, so programs reading them get nothing.
class ControllerHider {
public:
    // `stateFile` remembers changes to HidHide's settings, so they are undone even after a crash.
    explicit ControllerHider(std::filesystem::path stateFile);
    ~ControllerHider();  // restore()
    ControllerHider(const ControllerHider&) = delete;
    ControllerHider& operator=(const ControllerHider&) = delete;

    // True when the controller can only be hidden by opening it exclusively (Windows without a
    // usable HidHide). `reason` says why HidHide is not used.
    bool needsExclusiveOpen(std::string& reason);
    // Hides the opened controller at `hidPath` (not needed after an exclusive open). On success
    // `message` describes what was done, on failure why it could not be hidden.
    bool hide(const std::string& hidPath, std::string& message);
    // Makes the controller visible to other programs again.
    void restore();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace edgepad
