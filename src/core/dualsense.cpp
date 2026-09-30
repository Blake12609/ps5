#include "core/dualsense.hpp"

#include <algorithm>
#include <cmath>

namespace edgepad::dualsense {
namespace {

// Offsets inside the common input report body (after the report id / BT header).
constexpr size_t kLeftX = 0;
constexpr size_t kLeftY = 1;
constexpr size_t kRightX = 2;
constexpr size_t kRightY = 3;
constexpr size_t kL2 = 4;
constexpr size_t kR2 = 5;
constexpr size_t kButtons0 = 7;
constexpr size_t kButtons1 = 8;
constexpr size_t kButtons2 = 9;
constexpr size_t kGyro = 15;       // 3 x int16 LE
constexpr size_t kAccel = 21;      // 3 x int16 LE
constexpr size_t kTimestamp = 27;  // uint32 LE
constexpr size_t kTouch = 32;      // 2 x 4 byte touch points
constexpr size_t kStatus = 52;
constexpr size_t kFullBodySize = 53;
constexpr size_t kUsbInputReportSize = 64;

// Offsets inside the common output report body.
constexpr size_t kOutFlags0 = 0;
constexpr size_t kOutFlags1 = 1;
constexpr size_t kOutMotorRight = 2;
constexpr size_t kOutMotorLeft = 3;
constexpr size_t kOutRightTrigger = 10;
constexpr size_t kOutLeftTrigger = 21;
constexpr size_t kOutFlags2 = 38;
constexpr size_t kOutLightbarSetup = 41;
constexpr size_t kOutPlayerLeds = 43;
constexpr size_t kOutLightbarRed = 44;
constexpr size_t kOutCommonSize = 47;

constexpr uint8_t kFlag0CompatibleVibration = 0x01;
constexpr uint8_t kFlag0HapticsSelect = 0x02;
constexpr uint8_t kFlag0RightTrigger = 0x04;
constexpr uint8_t kFlag0LeftTrigger = 0x08;
constexpr uint8_t kFlag1LightbarControl = 0x04;
constexpr uint8_t kFlag1PlayerIndicator = 0x10;
constexpr uint8_t kFlag2LightbarSetup = 0x02;
constexpr uint8_t kLightbarSetupLightOut = 0x02;
constexpr uint8_t kBtOutputTag = 0x10;
constexpr uint8_t kBtCrcSeedOutput = 0xA2;

float axis(uint8_t raw) { return clamp11((static_cast<float>(raw) - 128.0f) / 127.0f); }

ButtonMask dpadButtons(uint8_t hat) {
    switch (hat & 0x0F) {
        case 0: return bit(Button::DpadUp);
        case 1: return bit(Button::DpadUp) | bit(Button::DpadRight);
        case 2: return bit(Button::DpadRight);
        case 3: return bit(Button::DpadDown) | bit(Button::DpadRight);
        case 4: return bit(Button::DpadDown);
        case 5: return bit(Button::DpadDown) | bit(Button::DpadLeft);
        case 6: return bit(Button::DpadLeft);
        case 7: return bit(Button::DpadUp) | bit(Button::DpadLeft);
        default: return 0;
    }
}

ButtonMask decodeButtons(uint8_t b0, uint8_t b1, uint8_t b2, bool extended) {
    ButtonMask m = dpadButtons(b0);
    if (b0 & 0x10) m |= bit(Button::Square);
    if (b0 & 0x20) m |= bit(Button::Cross);
    if (b0 & 0x40) m |= bit(Button::Circle);
    if (b0 & 0x80) m |= bit(Button::Triangle);
    if (b1 & 0x01) m |= bit(Button::L1);
    if (b1 & 0x02) m |= bit(Button::R1);
    if (b1 & 0x04) m |= bit(Button::L2);
    if (b1 & 0x08) m |= bit(Button::R2);
    if (b1 & 0x10) m |= bit(Button::Create);
    if (b1 & 0x20) m |= bit(Button::Options);
    if (b1 & 0x40) m |= bit(Button::L3);
    if (b1 & 0x80) m |= bit(Button::R3);
    if (b2 & 0x01) m |= bit(Button::PS);
    if (b2 & 0x02) m |= bit(Button::Touchpad);
    if (extended) {
        if (b2 & 0x04) m |= bit(Button::Mute);
        // DualSense Edge only.
        if (b2 & 0x10) m |= bit(Button::FnLeft);
        if (b2 & 0x20) m |= bit(Button::FnRight);
        if (b2 & 0x40) m |= bit(Button::PaddleLeft);
        if (b2 & 0x80) m |= bit(Button::PaddleRight);
    }
    return m;
}

int16_t le16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

TouchPoint decodeTouch(const uint8_t* p) {
    TouchPoint t;
    t.active = (p[0] & 0x80) == 0;
    t.id = p[0] & 0x7F;
    t.x = static_cast<uint16_t>(p[1] | ((p[2] & 0x0F) << 8));
    t.y = static_cast<uint16_t>((p[2] >> 4) | (p[3] << 4));
    return t;
}

InputState parseFullBody(const uint8_t* body, size_t available) {
    InputState s;
    s.lx = axis(body[kLeftX]);
    s.ly = -axis(body[kLeftY]);  // HID: 0 = up. Internally +y is up.
    s.rx = axis(body[kRightX]);
    s.ry = -axis(body[kRightY]);
    s.l2 = static_cast<float>(body[kL2]) / 255.0f;
    s.r2 = static_cast<float>(body[kR2]) / 255.0f;
    s.buttons = decodeButtons(body[kButtons0], body[kButtons1], body[kButtons2], true);
    if (available >= kTouch + 8) {
        for (size_t i = 0; i < 3; ++i) {
            s.motion.gyro[i] = le16(body + kGyro + 2 * i);
            s.motion.accel[i] = le16(body + kAccel + 2 * i);
        }
        s.motion.timestamp = le32(body + kTimestamp);
        s.motion.touch[0] = decodeTouch(body + kTouch);
        s.motion.touch[1] = decodeTouch(body + kTouch + 4);
    }
    if (available >= kFullBodySize) {
        const uint8_t status = body[kStatus];
        const int level = status & 0x0F;
        const int charge = (status & 0xF0) >> 4;
        s.charging = charge == 0x1;
        s.battery = charge == 0x2 ? 100 : std::min(level * 10 + 5, 100);
    }
    return s;
}

}  // namespace

bool isSupportedProduct(uint16_t productId) {
    return productId == kDualSenseProductId || productId == kDualSenseEdgeProductId;
}

const char* productName(uint16_t productId) {
    if (productId == kDualSenseEdgeProductId) return "DualSense Edge";
    if (productId == kDualSenseProductId) return "DualSense";
    return "Unknown controller";
}

std::optional<ParsedReport> parseInputReport(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return std::nullopt;
    const uint8_t id = data[0];

    // USB: report 0x01, 64 bytes, body right after the id. Over Bluetooth, Windows pads the
    // short 0x01 report to the 78 byte maximum, so a longer 0x01 report is the BT simple one.
    if (id == kUsbInputReportId && length >= 1 + kFullBodySize && length <= kUsbInputReportSize) {
        return ParsedReport{parseFullBody(data + 1, length - 1), Connection::Usb};
    }
    // Bluetooth full report: 0x31, 78 bytes, one extra header byte.
    if (id == kBtInputReportId && length >= 2 + kButtons2 + 1) {
        return ParsedReport{parseFullBody(data + 2, length - 2), Connection::Bluetooth};
    }
    // Bluetooth "simple" report 0x01 (before the full report mode is enabled).
    if (id == kUsbInputReportId && length >= 10) {
        InputState s;
        s.lx = axis(data[1]);
        s.ly = -axis(data[2]);
        s.rx = axis(data[3]);
        s.ry = -axis(data[4]);
        s.buttons = decodeButtons(data[5], data[6], data[7], false);
        s.l2 = static_cast<float>(data[8]) / 255.0f;
        s.r2 = static_cast<float>(data[9]) / 255.0f;
        return ParsedReport{s, Connection::Bluetooth};
    }
    return std::nullopt;
}

TriggerEffect triggerEffectOff() {
    TriggerEffect e{};
    e[0] = 0x05;
    return e;
}

TriggerEffect triggerEffectWall(float position, int strength) {
    // "Feedback" effect: ten zones along the trigger, each with a 3-bit force.
    const int start = std::clamp(static_cast<int>(std::lround(clamp01(position) * 10.0f)), 0, 9);
    strength = std::clamp(strength, 1, 8);
    const uint32_t force = static_cast<uint32_t>((strength - 1) & 0x07);
    uint32_t forceZones = 0;
    uint16_t activeZones = 0;
    for (int zone = start; zone < 10; ++zone) {
        forceZones |= force << (3 * zone);
        activeZones = static_cast<uint16_t>(activeZones | (1u << zone));
    }
    TriggerEffect e{};
    e[0] = 0x21;
    e[1] = static_cast<uint8_t>(activeZones & 0xFF);
    e[2] = static_cast<uint8_t>(activeZones >> 8);
    e[3] = static_cast<uint8_t>(forceZones & 0xFF);
    e[4] = static_cast<uint8_t>((forceZones >> 8) & 0xFF);
    e[5] = static_cast<uint8_t>((forceZones >> 16) & 0xFF);
    e[6] = static_cast<uint8_t>((forceZones >> 24) & 0xFF);
    return e;
}

TriggerEffect triggerEffectFor(const TriggerSettings& settings) {
    if (settings.resistance == TriggerResistance::Wall) {
        return triggerEffectWall(settings.resistancePosition, settings.resistanceStrength);
    }
    return triggerEffectOff();
}

std::vector<uint8_t> buildOutputReport(const Effects& fx, Connection connection, uint8_t sequence,
                                       bool lightbarSetup) {
    std::vector<uint8_t> report;
    size_t offset = 0;
    if (connection == Connection::Bluetooth) {
        report.assign(kBtOutputReportSize, 0);
        report[0] = kBtOutputReportId;
        report[1] = static_cast<uint8_t>((sequence & 0x0F) << 4);
        report[2] = kBtOutputTag;
        offset = 3;
    } else {
        report.assign(kUsbOutputReportSize, 0);
        report[0] = kUsbOutputReportId;
        offset = 1;
    }
    uint8_t* c = report.data() + offset;

    if (lightbarSetup) {
        c[kOutFlags2] = kFlag2LightbarSetup;
        c[kOutLightbarSetup] = kLightbarSetupLightOut;
    } else {
        c[kOutFlags0] = kFlag0CompatibleVibration | kFlag0HapticsSelect | kFlag0RightTrigger | kFlag0LeftTrigger;
        c[kOutFlags1] = kFlag1LightbarControl | kFlag1PlayerIndicator;
        c[kOutMotorRight] = fx.rumbleRight;
        c[kOutMotorLeft] = fx.rumbleLeft;
        std::copy(fx.rightTrigger.begin(), fx.rightTrigger.end(), c + kOutRightTrigger);
        std::copy(fx.leftTrigger.begin(), fx.leftTrigger.end(), c + kOutLeftTrigger);
        c[kOutPlayerLeds] = fx.playerLeds;
        c[kOutLightbarRed + 0] = fx.lightbar[0];
        c[kOutLightbarRed + 1] = fx.lightbar[1];
        c[kOutLightbarRed + 2] = fx.lightbar[2];
    }
    static_assert(kOutLightbarRed + 3 == kOutCommonSize);

    if (connection == Connection::Bluetooth) {
        const uint8_t seed = kBtCrcSeedOutput;
        uint32_t crc = crc32(&seed, 1);
        crc = crc32(report.data(), kBtOutputReportSize - 4, crc);
        report[kBtOutputReportSize - 4] = static_cast<uint8_t>(crc & 0xFF);
        report[kBtOutputReportSize - 3] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        report[kBtOutputReportSize - 2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
        report[kBtOutputReportSize - 1] = static_cast<uint8_t>((crc >> 24) & 0xFF);
    }
    return report;
}

std::array<uint8_t, 4> encodeDs4Touch(const TouchPoint& touch) {
    const unsigned x = std::min<unsigned>(touch.x, 1919);
    const unsigned y = std::min<unsigned>(touch.y * 942u / 1079u, 942);
    return {
        static_cast<uint8_t>((touch.active ? 0x00 : 0x80) | (touch.id & 0x7F)),
        static_cast<uint8_t>(x & 0xFF),
        static_cast<uint8_t>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4)),
        static_cast<uint8_t>((y >> 4) & 0xFF),
    };
}

uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc) {
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

}  // namespace edgepad::dualsense
