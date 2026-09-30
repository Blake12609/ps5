#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/dualsense.hpp"
#include "core/types.hpp"

struct hid_device_;

namespace edgepad {

struct HidDeviceInfo {
    std::string path;
    uint16_t productId = 0;
};

bool hidInit();
void hidShutdown();

// Lists connected DualSense and DualSense Edge controllers (USB and Bluetooth).
std::vector<HidDeviceInfo> enumerateControllers();

// A DualSense / DualSense Edge opened through hidapi.
class DualSenseDevice {
public:
    enum class ReadResult { Data, Timeout, Error };

    DualSenseDevice() = default;
    ~DualSenseDevice();
    DualSenseDevice(const DualSenseDevice&) = delete;
    DualSenseDevice& operator=(const DualSenseDevice&) = delete;

    bool open(const HidDeviceInfo& info, std::string& error);
    void close();
    bool isOpen() const { return dev_ != nullptr; }

    ReadResult read(InputState& state, int timeoutMs);
    // Sends lightbar / LEDs / trigger effects / rumble. Skips the write when nothing changed.
    bool sendEffects(const dualsense::Effects& effects);

    dualsense::Connection connection() const { return connection_; }
    uint16_t productId() const { return info_.productId; }
    bool isEdge() const { return info_.productId == dualsense::kDualSenseEdgeProductId; }
    const std::string& lastError() const { return error_; }

private:
    hid_device_* dev_ = nullptr;
    HidDeviceInfo info_;
    dualsense::Connection connection_ = dualsense::Connection::Unknown;
    uint8_t sequence_ = 0;
    bool needsLightbarSetup_ = true;
    std::optional<dualsense::Effects> lastSent_;
    std::string error_;
};

}  // namespace edgepad
