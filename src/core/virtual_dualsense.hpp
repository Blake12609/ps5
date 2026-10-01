#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/dualsense.hpp"
#include "core/types.hpp"

// A virtual wired DualSense (PS5 controller), made to look exactly like the real one: the same USB
// descriptors and HID report descriptor, input reports in the DualSense's own format built from the
// real controller's report, and output reports (adaptive triggers, rumble, lightbar...) that games
// send passed on to the real controller. Games see a genuine PS5 controller, no translation to an
// Xbox or PS4 controller in between.
namespace edgepad::virtual_dualsense {

inline constexpr uint16_t kVendorId = dualsense::kSonyVendorId;
inline constexpr uint16_t kProductId = dualsense::kDualSenseProductId;
inline constexpr uint16_t kRelease = 0x0100;
// USB serial number of the virtual controller, so EdgePad never mistakes it for the real one.
inline constexpr const char* kSerialNumber = "EdgePad virtual DualSense";

inline constexpr uint8_t kInterruptInEndpoint = 0x81;   // input reports
inline constexpr uint8_t kInterruptOutEndpoint = 0x02;  // output reports
inline constexpr size_t kInputReportSize = 64;          // report id 0x01 + 63 bytes
inline constexpr size_t kOutputReportSize = 48;         // report id 0x02 + 47 bytes

// The DualSense's USB HID report descriptor, byte for byte (273 bytes).
const std::vector<uint8_t>& reportDescriptor();
// USB descriptors: device, configuration (one HID interface, interrupt IN + OUT), strings.
std::vector<uint8_t> deviceDescriptor();
std::vector<uint8_t> configurationDescriptor();
std::vector<uint8_t> hidDescriptor();
// Empty for an unknown index. Index 0 is the language list.
std::vector<uint8_t> stringDescriptor(uint8_t index);
// Size of a feature report as the report descriptor declares it (report id included), 0 if unknown.
size_t featureReportSize(uint8_t reportId);

// A plausible feature report for when there is no real controller to copy it from (demo mode):
// nominal calibration (0x05), a fixed MAC address (0x09), firmware info (0x20). Empty otherwise.
std::vector<uint8_t> defaultFeatureReport(uint8_t reportId);

// USB input report 0x01. Starts from the real controller's own report (`s.raw`), so every byte
// EdgePad does not change - sequence number, sensors, touchpad, battery, unknown bytes - reaches
// the game as the controller sent it; sticks, triggers and buttons are EdgePad's output. Edge-only
// bits (Fn buttons, back buttons) are cleared: the virtual controller is a regular DualSense.
std::array<uint8_t, kInputReportSize> buildInputReport(const OutputState& s);

// What EdgePad lets games control on the real controller.
struct OutputFilter {
    bool rumble = true;    // compatible vibration / haptics
    bool lights = true;    // lightbar, player LEDs
    bool triggers = true;  // adaptive trigger effects
};

// A game's output report (USB report 0x02) with the parts the settings keep for EdgePad removed.
// Returns the 47 common bytes ready for dualsense::wrapOutputReport, or nothing if nothing is left.
std::optional<std::array<uint8_t, dualsense::kOutputCommonSize>> filterGameOutput(const uint8_t* report, size_t size,
                                                                                  const OutputFilter& filter);

// Which parts of the controller a (filtered) game output report changes (dualsense::kPart* bits).
uint8_t partsChanged(const std::array<uint8_t, dualsense::kOutputCommonSize>& common);

}  // namespace edgepad::virtual_dualsense
