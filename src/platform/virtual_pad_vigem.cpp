// Virtual Xbox 360 / DualShock 4 controller through the ViGEmBus driver (Windows).
#include "platform/virtual_pad.hpp"

#include <windows.h>

#include <ViGEm/Client.h>

#include <cstring>

#include "core/virtual_reports.hpp"

#if defined(_MSC_VER)
#pragma warning(disable : 4996)  // vigem_target_ds4_register_notification is marked deprecated
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace edgepad {
namespace {

namespace vr = virtual_reports;

// The reports are built (and unit tested) in core/virtual_reports; they must match ViGEm's layout.
static_assert(sizeof(DS4_REPORT_EX) == vr::kDs4ReportSize, "DS4_REPORT_EX is the raw report body");
static_assert(DS4_BUTTON_SQUARE == vr::ds4::kSquare && DS4_BUTTON_CROSS == vr::ds4::kCross &&
              DS4_BUTTON_CIRCLE == vr::ds4::kCircle && DS4_BUTTON_TRIANGLE == vr::ds4::kTriangle &&
              DS4_BUTTON_SHOULDER_LEFT == vr::ds4::kL1 && DS4_BUTTON_SHOULDER_RIGHT == vr::ds4::kR1 &&
              DS4_BUTTON_TRIGGER_LEFT == vr::ds4::kL2 && DS4_BUTTON_TRIGGER_RIGHT == vr::ds4::kR2 &&
              DS4_BUTTON_SHARE == vr::ds4::kShare && DS4_BUTTON_OPTIONS == vr::ds4::kOptions &&
              DS4_BUTTON_THUMB_LEFT == vr::ds4::kL3 && DS4_BUTTON_THUMB_RIGHT == vr::ds4::kR3 &&
              DS4_BUTTON_DPAD_NONE == vr::ds4::kHatNone && DS4_SPECIAL_BUTTON_PS == vr::ds4::kPs &&
              DS4_SPECIAL_BUTTON_TOUCHPAD == vr::ds4::kTouchpadClick,
              "DualShock 4 button bits");
static_assert(XUSB_GAMEPAD_A == vr::xusb::kA && XUSB_GAMEPAD_B == vr::xusb::kB && XUSB_GAMEPAD_X == vr::xusb::kX &&
              XUSB_GAMEPAD_Y == vr::xusb::kY && XUSB_GAMEPAD_LEFT_SHOULDER == vr::xusb::kLeftShoulder &&
              XUSB_GAMEPAD_RIGHT_SHOULDER == vr::xusb::kRightShoulder && XUSB_GAMEPAD_LEFT_THUMB == vr::xusb::kLeftThumb &&
              XUSB_GAMEPAD_RIGHT_THUMB == vr::xusb::kRightThumb && XUSB_GAMEPAD_BACK == vr::xusb::kBack &&
              XUSB_GAMEPAD_START == vr::xusb::kStart && XUSB_GAMEPAD_GUIDE == vr::xusb::kGuide &&
              XUSB_GAMEPAD_DPAD_UP == vr::xusb::kDpadUp && XUSB_GAMEPAD_DPAD_DOWN == vr::xusb::kDpadDown &&
              XUSB_GAMEPAD_DPAD_LEFT == vr::xusb::kDpadLeft && XUSB_GAMEPAD_DPAD_RIGHT == vr::xusb::kDpadRight,
              "Xbox 360 button bits");

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
        if (kind_ == OutputKind::Xbox360) {
            const vr::XusbReport x = vr::buildXusbReport(s);
            XUSB_REPORT r{};
            r.wButtons = x.buttons;
            r.bLeftTrigger = x.leftTrigger;
            r.bRightTrigger = x.rightTrigger;
            r.sThumbLX = x.thumbLX;
            r.sThumbLY = x.thumbLY;
            r.sThumbRX = x.thumbRX;
            r.sThumbRY = x.thumbRY;
            return VIGEM_SUCCESS(vigem_target_x360_update(client_, target_, r));
        }
        const auto bytes = vr::buildDs4Report(s, counter_);
        counter_ = static_cast<uint8_t>((counter_ + 1) & 0x3F);
        if (extendedReports_) {
            // Full report (sticks, buttons, gyro, accelerometer, touchpad): ViGEmBus copies these
            // bytes into the virtual controller's USB report unchanged.
            DS4_REPORT_EX ex{};
            std::memcpy(ex.ReportBuffer, bytes.data(), bytes.size());
            const VIGEM_ERROR err = vigem_target_ds4_update_ex(client_, target_, ex);
            if (err != VIGEM_ERROR_NOT_SUPPORTED) return VIGEM_SUCCESS(err);
            extendedReports_ = false;  // very old ViGEmBus: fall back to the basic report
        }
        DS4_REPORT r{};
        r.bThumbLX = bytes[0];
        r.bThumbLY = bytes[1];
        r.bThumbRX = bytes[2];
        r.bThumbRY = bytes[3];
        r.wButtons = static_cast<USHORT>(bytes[4] | (bytes[5] << 8));
        r.bSpecial = bytes[6];
        r.bTriggerL = bytes[7];
        r.bTriggerR = bytes[8];
        return VIGEM_SUCCESS(vigem_target_ds4_update(client_, target_, r));
    }

    std::string name() const override {
        return kind_ == OutputKind::Xbox360 ? "Virtual Xbox 360 controller" : "Virtual PlayStation (DualShock 4) controller";
    }

private:
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
    uint8_t counter_ = 0;
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
