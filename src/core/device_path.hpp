#pragma once

#include <string>

namespace edgepad {

// Windows: the device instance id (what HidHide's block list holds, e.g. "HID\VID_054C&PID_0CE6&MI_03\7&2A5B1C4&0&0000")
// of a HID device interface path ("\\?\HID#VID_054C&PID_0CE6&MI_03#7&2a5b1c4&0&0000#{4d1e55b2-...}").
// Empty when the path does not look like one.
std::string instanceIdFromHidPath(const std::string& path);

}  // namespace edgepad
