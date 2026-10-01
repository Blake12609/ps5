// Virtual Xbox 360 / DualShock 4 controller through the ViGEmBus driver (Windows).
#include "platform/virtual_pad.hpp"

#include <windows.h>

#include <ViGEm/Client.h>

#include <algorithm>
#include <cmath>

#include "core/axis.hpp"
#include "core/dualsense.hpp"
#include "core/processing.hpp"

#if defined(_MSC_VER)
#pragma warning(disable : 4996)  // vigem_target_ds4_register_notification is marked deprecated
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace edgepad {
namespace {

// Exact 1:1 conversions (see core/axis.hpp): an unchanged stick reaches the game exactly as the
// controller sent it, full deflection included.
SHORT toThumb(float v) { return stickToThumb(v); }
BYTE toTrigger(float v) { return triggerToRaw(v); }
BYTE toDs4Axis(float v) { return stickToRaw(v); }

void encodeTouch(const TouchPoint& t, BYTE& isUpTrackingNum, BYTE (&data)[3]) {
    const auto encoded = dualsense::encodeDs4Touch(t);
    isUpTrackingNum = encoded[0];
    data[0] = encoded[1];
    data[1] = encoded[2];
    data[2] = encoded[3];
}

class ViGEmPad final : public VirtualPad {
public:
    ViGEmPad(PVIGEM_CLIENT client, PVIGEM_TARGET target, OutputKind kind, FeedbackCallback onFeedback)
        : client_(client), target_(target), kind_(kind), onFeedback_(std::move(onFeedback)) {}

    ~ViGEmPad() override {
        if (kind_ == OutputKind::Xbox360) {
            vigem_target_x360_unregister_notification(target_);
        } else {
            vigem_target_ds4_unregister_notification(target_);
        }
        vigem_target_remove(client_, target_);
        vigem_target_free(target_);
        vigem_disconnect(client_);
        vigem_free(client_);
    }

    void registerRumble() {
        if (kind_ == OutputKind::Xbox360) {
            vigem_target_x360_register_notification(client_, target_, &ViGEmPad::onX360, this);
        } else {
            vigem_target_ds4_register_notification(client_, target_, &ViGEmPad::onDs4, this);
        }
    }

    bool send(const OutputState& s) override {
        if (kind_ == OutputKind::Xbox360) return VIGEM_SUCCESS(vigem_target_x360_update(client_, target_, x360Report(s)));
        if (extendedReports_) {
            // Full report: sticks and buttons plus gyro, accelerometer and touchpad.
            const VIGEM_ERROR err = vigem_target_ds4_update_ex(client_, target_, ds4ReportEx(s));
            if (err != VIGEM_ERROR_NOT_SUPPORTED) return VIGEM_SUCCESS(err);
            extendedReports_ = false;  // very old ViGEmBus: fall back to the basic report
        }
        return VIGEM_SUCCESS(vigem_target_ds4_update(client_, target_, ds4Report(s)));
    }

    std::string name() const override {
        return kind_ == OutputKind::Xbox360 ? "Virtual Xbox 360 controller" : "Virtual PlayStation (DualShock 4) controller";
    }

private:
    static XUSB_REPORT x360Report(const OutputState& s) {
        XUSB_REPORT r{};
        USHORT b = 0;
        auto set = [&](Button button, USHORT flag) {
            if (has(s.buttons, button)) b |= flag;
        };
        set(Button::Cross, XUSB_GAMEPAD_A);
        set(Button::Circle, XUSB_GAMEPAD_B);
        set(Button::Square, XUSB_GAMEPAD_X);
        set(Button::Triangle, XUSB_GAMEPAD_Y);
        set(Button::L1, XUSB_GAMEPAD_LEFT_SHOULDER);
        set(Button::R1, XUSB_GAMEPAD_RIGHT_SHOULDER);
        set(Button::L3, XUSB_GAMEPAD_LEFT_THUMB);
        set(Button::R3, XUSB_GAMEPAD_RIGHT_THUMB);
        set(Button::Create, XUSB_GAMEPAD_BACK);
        set(Button::Touchpad, XUSB_GAMEPAD_BACK);
        set(Button::Options, XUSB_GAMEPAD_START);
        set(Button::PS, XUSB_GAMEPAD_GUIDE);
        set(Button::DpadUp, XUSB_GAMEPAD_DPAD_UP);
        set(Button::DpadDown, XUSB_GAMEPAD_DPAD_DOWN);
        set(Button::DpadLeft, XUSB_GAMEPAD_DPAD_LEFT);
        set(Button::DpadRight, XUSB_GAMEPAD_DPAD_RIGHT);
        r.wButtons = b;
        r.bLeftTrigger = toTrigger(s.l2);
        r.bRightTrigger = toTrigger(s.r2);
        r.sThumbLX = toThumb(s.lx);
        r.sThumbLY = toThumb(s.ly);
        r.sThumbRX = toThumb(s.rx);
        r.sThumbRY = toThumb(s.ry);
        return r;
    }

    static DS4_REPORT ds4Report(const OutputState& s) {
        DS4_REPORT r;
        DS4_REPORT_INIT(&r);
        USHORT b = 0;
        auto set = [&](Button button, int flag) {
            if (has(s.buttons, button)) b = static_cast<USHORT>(b | flag);
        };
        set(Button::Cross, DS4_BUTTON_CROSS);
        set(Button::Circle, DS4_BUTTON_CIRCLE);
        set(Button::Square, DS4_BUTTON_SQUARE);
        set(Button::Triangle, DS4_BUTTON_TRIANGLE);
        set(Button::L1, DS4_BUTTON_SHOULDER_LEFT);
        set(Button::R1, DS4_BUTTON_SHOULDER_RIGHT);
        set(Button::L3, DS4_BUTTON_THUMB_LEFT);
        set(Button::R3, DS4_BUTTON_THUMB_RIGHT);
        set(Button::Create, DS4_BUTTON_SHARE);
        set(Button::Options, DS4_BUTTON_OPTIONS);
        if (s.l2 > 0.02f) b = static_cast<USHORT>(b | DS4_BUTTON_TRIGGER_LEFT);
        if (s.r2 > 0.02f) b = static_cast<USHORT>(b | DS4_BUTTON_TRIGGER_RIGHT);
        r.wButtons = b;

        const bool up = has(s.buttons, Button::DpadUp), down = has(s.buttons, Button::DpadDown);
        const bool left = has(s.buttons, Button::DpadLeft), right = has(s.buttons, Button::DpadRight);
        DS4_DPAD_DIRECTIONS dpad = DS4_BUTTON_DPAD_NONE;
        if (up && right) dpad = DS4_BUTTON_DPAD_NORTHEAST;
        else if (up && left) dpad = DS4_BUTTON_DPAD_NORTHWEST;
        else if (down && right) dpad = DS4_BUTTON_DPAD_SOUTHEAST;
        else if (down && left) dpad = DS4_BUTTON_DPAD_SOUTHWEST;
        else if (up) dpad = DS4_BUTTON_DPAD_NORTH;
        else if (down) dpad = DS4_BUTTON_DPAD_SOUTH;
        else if (left) dpad = DS4_BUTTON_DPAD_WEST;
        else if (right) dpad = DS4_BUTTON_DPAD_EAST;
        DS4_SET_DPAD(&r, dpad);

        BYTE special = 0;
        if (has(s.buttons, Button::PS)) special |= DS4_SPECIAL_BUTTON_PS;
        if (has(s.buttons, Button::Touchpad)) special |= DS4_SPECIAL_BUTTON_TOUCHPAD;
        r.bSpecial = special;

        r.bTriggerL = toTrigger(s.l2);
        r.bTriggerR = toTrigger(s.r2);
        r.bThumbLX = toDs4Axis(s.lx);
        r.bThumbLY = toDs4Axis(-s.ly);  // DS4: 0 = up
        r.bThumbRX = toDs4Axis(s.rx);
        r.bThumbRY = toDs4Axis(-s.ry);
        return r;
    }

    DS4_REPORT_EX ds4ReportEx(const OutputState& s) {
        const DS4_REPORT basic = ds4Report(s);
        DS4_REPORT_EX ex{};
        auto& r = ex.Report;
        r.bThumbLX = basic.bThumbLX;
        r.bThumbLY = basic.bThumbLY;
        r.bThumbRX = basic.bThumbRX;
        r.bThumbRY = basic.bThumbRY;
        r.wButtons = basic.wButtons;
        r.bSpecial = basic.bSpecial;
        r.bTriggerL = basic.bTriggerL;
        r.bTriggerR = basic.bTriggerR;
        // DualSense sensor clock ticks every 1/3 us, the DualShock 4 one every 16/3 us.
        r.wTimestamp = static_cast<USHORT>(s.motion.timestamp / 16);
        // Both controllers use the same sensor units (1024 per deg/s, 8192 per g) and axes.
        r.wGyroX = s.motion.gyro[0];
        r.wGyroY = s.motion.gyro[1];
        r.wGyroZ = s.motion.gyro[2];
        r.wAccelX = s.motion.accel[0];
        r.wAccelY = s.motion.accel[1];
        r.wAccelZ = s.motion.accel[2];
        // Battery 0..10 in the low nibble, 0x10 = cable connected (the virtual pad is "wired").
        const int level = s.battery < 0 ? 10 : std::clamp(s.battery / 10, 0, 10);
        r.bBatteryLvlSpecial = static_cast<BYTE>(0x10 | level);
        r.bTouchPacketsN = 1;
        r.sCurrentTouch.bPacketCounter = touchPacket_++;
        encodeTouch(s.motion.touch[0], r.sCurrentTouch.bIsUpTrackingNum1, r.sCurrentTouch.bTouchData1);
        encodeTouch(s.motion.touch[1], r.sCurrentTouch.bIsUpTrackingNum2, r.sCurrentTouch.bTouchData2);
        return ex;
    }

    static VOID CALLBACK onX360(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR small, UCHAR, LPVOID user) {
        auto* self = static_cast<ViGEmPad*>(user);
        if (self->onFeedback_) self->onFeedback_(PadFeedback{large, small, false, {}});
    }

    static VOID CALLBACK onDs4(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR small, DS4_LIGHTBAR_COLOR color, LPVOID user) {
        auto* self = static_cast<ViGEmPad*>(user);
        if (self->onFeedback_) self->onFeedback_(PadFeedback{large, small, true, {color.Red, color.Green, color.Blue}});
    }

    PVIGEM_CLIENT client_;
    PVIGEM_TARGET target_;
    OutputKind kind_;
    FeedbackCallback onFeedback_;
    bool extendedReports_ = true;
    BYTE touchPacket_ = 0;
};

}  // namespace

const char* virtualPadDriverUrl() { return "https://github.com/nefarius/ViGEmBus/releases/latest"; }

PadCreateResult createVirtualPad(OutputKind kind, FeedbackCallback onFeedback) {
    PadCreateResult result;
    if (kind == OutputKind::None) {
        result.error = PadError::Unsupported;
        result.message = "Virtual controller disabled (monitor only)";
        return result;
    }

    PVIGEM_CLIENT client = vigem_alloc();
    if (client == nullptr) {
        result.error = PadError::Failed;
        result.message = "Out of memory while creating the ViGEm client";
        return result;
    }
    const VIGEM_ERROR connectError = vigem_connect(client);
    if (!VIGEM_SUCCESS(connectError)) {
        vigem_free(client);
        if (connectError == VIGEM_ERROR_BUS_NOT_FOUND) {
            result.error = PadError::DriverMissing;
            result.message = "ViGEmBus driver is not installed";
        } else {
            result.error = PadError::Failed;
            result.message = "Could not connect to ViGEmBus";
        }
        return result;
    }

    PVIGEM_TARGET target = kind == OutputKind::Xbox360 ? vigem_target_x360_alloc() : vigem_target_ds4_alloc();
    const VIGEM_ERROR addError = vigem_target_add(client, target);
    if (!VIGEM_SUCCESS(addError)) {
        vigem_target_free(target);
        vigem_disconnect(client);
        vigem_free(client);
        result.error = PadError::Failed;
        result.message = "ViGEmBus refused to create the virtual controller";
        return result;
    }

    auto pad = std::make_unique<ViGEmPad>(client, target, kind, std::move(onFeedback));
    pad->registerRumble();
    result.pad = std::move(pad);
    return result;
}

}  // namespace edgepad
