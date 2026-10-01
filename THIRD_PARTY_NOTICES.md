# Third-party software

EdgePad binaries statically include the following libraries. Each is used under its own license:

| Library | License | Source |
| --- | --- | --- |
| hidapi | BSD-3-Clause (one of its license options) | https://github.com/libusb/hidapi |
| ViGEmClient (Windows) | MIT | https://github.com/nefarius/ViGEmClient |
| Dear ImGui | MIT | https://github.com/ocornut/imgui |
| GLFW | zlib/libpng | https://github.com/glfw/glfw |
| nlohmann/json | MIT | https://github.com/nlohmann/json |
| doctest (tests only, not shipped) | MIT | https://github.com/doctest/doctest |

The ViGEmBus and HidHide drivers are separate downloads from their authors and are not bundled.
EdgePad talks to an installed HidHide through its documented control interface (the I/O control codes
from HidHide's `HidHideIoctlContract.h`, MIT, https://github.com/nefarius/HidHide).
