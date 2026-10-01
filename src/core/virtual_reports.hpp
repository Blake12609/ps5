#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/types.hpp"

// The reports EdgePad hands the virtual controller driver (ViGEmBus), which passes them on to games
// unchanged. Built here, in platform independent code, so the exact bytes a game receives are
// unit tested.
namespace edgepad::virtual_reports {

// ---------------------------------------------------------------------------------------------
// Motion calibration. Games (SDL, Steam Input, ...) turn raw gyro / accelerometer values into
// degrees per second and g with the calibration the controller reports: value = (raw - bias) * scale.
// The real DualSense and the virtual DualShock 4 report different calibrations, so EdgePad converts
// each value until the game computes the same motion from the virtual controller as from the real one.
// ---------------------------------------------------------------------------------------------
struct MotionCalibration {
    std::array<float, 6> bias{};   // raw counts: gyro pitch, yaw, roll, accel x, y, z
    std::array<float, 6> scale{};  // degrees per second (gyro) or g (accel) per raw count
};

// Nominal sensor scale (1/16 deg/s, 1/8192 g per count), used when a calibration is missing or bad.
MotionCalibration nominalCalibration();
// The DualSense's calibration feature report 0x05 (first byte = report id), read like SDL does.
MotionCalibration parseDualSenseCalibration(const uint8_t* report, size_t length);
// The fixed calibration ViGEmBus's virtual DualShock 4 reports (USB feature report 0x02), read like
// SDL reads a wired DualShock 4.
const MotionCalibration& virtualDs4Calibration();
// Converts raw gyro / accelerometer values from one calibration to the other.
MotionState remapMotion(const MotionState& motion, const MotionCalibration& from, const MotionCalibration& to);

// ---------------------------------------------------------------------------------------------
// DualShock 4: USB input report 0x01 without its report id (ViGEm's DS4_REPORT_EX.ReportBuffer).
// ---------------------------------------------------------------------------------------------
inline constexpr size_t kDs4ReportSize = 63;

namespace ds4 {
// Byte 4 (low nibble: d-pad hat) and byte 5. Same values as ViGEm's DS4_BUTTONS.
inline constexpr uint16_t kSquare = 1u << 4, kCross = 1u << 5, kCircle = 1u << 6, kTriangle = 1u << 7;
inline constexpr uint16_t kL1 = 1u << 8, kR1 = 1u << 9, kL2 = 1u << 10, kR2 = 1u << 11;
inline constexpr uint16_t kShare = 1u << 12, kOptions = 1u << 13, kL3 = 1u << 14, kR3 = 1u << 15;
inline constexpr uint8_t kHatNone = 8;  // 0 = up, clockwise to 7 = up-left
// Byte 6: PS and touchpad click in bits 0-1, a report counter in bits 2-7.
inline constexpr uint8_t kPs = 1u << 0, kTouchpadClick = 1u << 1;
}  // namespace ds4

// `counter`: increases with every report, like on a real DualShock 4.
std::array<uint8_t, kDs4ReportSize> buildDs4Report(const OutputState& s, uint8_t counter);

// ---------------------------------------------------------------------------------------------
// Xbox 360 (XInput) report. Same values as ViGEm's XUSB_REPORT / XUSB_BUTTON.
// ---------------------------------------------------------------------------------------------
namespace xusb {
inline constexpr uint16_t kDpadUp = 0x0001, kDpadDown = 0x0002, kDpadLeft = 0x0004, kDpadRight = 0x0008;
inline constexpr uint16_t kStart = 0x0010, kBack = 0x0020, kLeftThumb = 0x0040, kRightThumb = 0x0080;
inline constexpr uint16_t kLeftShoulder = 0x0100, kRightShoulder = 0x0200, kGuide = 0x0400;
inline constexpr uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
}  // namespace xusb

struct XusbReport {
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    int16_t thumbLX = 0, thumbLY = 0, thumbRX = 0, thumbRY = 0;  // +y = up
    bool operator==(const XusbReport&) const = default;
};

XusbReport buildXusbReport(const OutputState& s);

}  // namespace edgepad::virtual_reports
