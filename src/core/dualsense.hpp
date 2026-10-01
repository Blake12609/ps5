#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/processing.hpp"
#include "core/types.hpp"

namespace edgepad::dualsense {

inline constexpr uint16_t kSonyVendorId = 0x054C;
inline constexpr uint16_t kDualSenseProductId = 0x0CE6;
inline constexpr uint16_t kDualSenseEdgeProductId = 0x0DF2;

inline constexpr uint8_t kUsbInputReportId = 0x01;
inline constexpr uint8_t kBtInputReportId = 0x31;
inline constexpr uint8_t kUsbOutputReportId = 0x02;
inline constexpr uint8_t kBtOutputReportId = 0x31;
inline constexpr uint8_t kCalibrationFeatureReportId = 0x05;  // reading it enables full BT reports

inline constexpr size_t kUsbOutputReportSize = 48;
inline constexpr size_t kBtOutputReportSize = 78;

enum class Connection : uint8_t { Unknown, Usb, Bluetooth };

struct ParsedReport {
    InputState state;
    Connection connection = Connection::Unknown;
};

bool isSupportedProduct(uint16_t productId);
const char* productName(uint16_t productId);

// Parses a raw input report (first byte = report id). Returns nullopt for unknown reports.
std::optional<ParsedReport> parseInputReport(const uint8_t* data, size_t length);

using TriggerEffect = std::array<uint8_t, 11>;

TriggerEffect triggerEffectOff();
// Resistance "wall" from `position` (0..1 of travel) to the end, strength 1..8.
// Gives the feel of the DualSense Edge trigger stops on a regular DualSense.
TriggerEffect triggerEffectWall(float position, int strength);
TriggerEffect triggerEffectFor(const TriggerSettings& settings);

// Parts of the controller an effects write changes; the others keep whatever they were set to
// (with a virtual DualSense, games drive some of them directly).
inline constexpr uint8_t kPartRumble = 1;
inline constexpr uint8_t kPartTriggers = 2;
inline constexpr uint8_t kPartLights = 4;  // lightbar and player LEDs
inline constexpr uint8_t kAllParts = kPartRumble | kPartTriggers | kPartLights;

struct Effects {
    std::array<uint8_t, 3> lightbar{0, 0, 255};
    uint8_t playerLeds = 0;
    TriggerEffect leftTrigger = triggerEffectOff();
    TriggerEffect rightTrigger = triggerEffectOff();
    uint8_t rumbleLeft = 0;   // large / low frequency motor
    uint8_t rumbleRight = 0;  // small / high frequency motor
    uint8_t parts = kAllParts;

    bool operator==(const Effects&) const = default;
};

// Output report body shared by USB and Bluetooth: USB report 0x02 without its id.
inline constexpr size_t kOutputCommonSize = 47;
// Wraps the common output bytes for the connection (Bluetooth: report 0x31 with sequence and CRC).
std::vector<uint8_t> wrapOutputReport(const uint8_t* common, Connection connection, uint8_t sequence);

// Builds a complete output report. `sequence` is only used over Bluetooth.
// `lightbarSetup` releases the lightbar from its power-on animation (send once after connecting).
std::vector<uint8_t> buildOutputReport(const Effects& effects, Connection connection, uint8_t sequence,
                                       bool lightbarSetup);

// Encodes a touchpad finger for a DualShock 4 report: {tracking byte, 3 data bytes}.
// Rescales from the DualSense touchpad (1920 x 1080) to the DualShock 4 one (1920 x 943).
std::array<uint8_t, 4> encodeDs4Touch(const TouchPoint& touch);

// CRC-32 (IEEE, reflected) as used by DualSense Bluetooth reports.
uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc = 0);

}  // namespace edgepad::dualsense
