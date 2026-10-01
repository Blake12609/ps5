#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/dualsense.hpp"
#include "core/types.hpp"
#include "core/virtual_reports.hpp"

namespace edgepad {

class HidTransport;

struct HidDeviceInfo {
    std::string path;
    uint16_t productId = 0;
};

bool hidInit();
void hidShutdown();

// Lists connected DualSense and DualSense Edge controllers (USB and Bluetooth).
std::vector<HidDeviceInfo> enumerateControllers();

// A DualSense / DualSense Edge controller.
class DualSenseDevice {
public:
    enum class ReadResult { Data, Timeout, Error };

    DualSenseDevice();
    ~DualSenseDevice();
    DualSenseDevice(const DualSenseDevice&) = delete;
    DualSenseDevice& operator=(const DualSenseDevice&) = delete;

    // `exclusive` (Windows): open it so no other program can, which hides it from games. Fails with
    // `inUse` set when another program already has it open.
    bool open(const HidDeviceInfo& info, std::string& error, bool exclusive = false, bool* inUse = nullptr);
    void close();
    bool isOpen() const { return dev_ != nullptr; }
    bool isExclusive() const { return exclusive_; }
    const std::string& path() const { return info_.path; }

    ReadResult read(InputState& state, int timeoutMs);
    // Sends lightbar / LEDs / trigger effects / rumble. Skips the write when nothing changed.
    bool sendEffects(const dualsense::Effects& effects);
    // Writes the 47 common bytes of an output report (e.g. one a game sent to the virtual
    // DualSense), wrapped for the connection.
    bool sendOutput(const uint8_t* common);
    // Feature reports read when the controller was opened (report id first): calibration (0x05),
    // pairing info (0x09), firmware info (0x20), hardware info (0x22).
    const std::map<uint8_t, std::vector<uint8_t>>& featureReports() const { return features_; }
    // True when `effects` differ from what the controller last got (a write is due).
    bool effectsChanged(const dualsense::Effects& effects) const { return !lastSent_ || *lastSent_ != effects; }

    dualsense::Connection connection() const { return connection_; }
    uint16_t productId() const { return info_.productId; }
    bool isEdge() const { return info_.productId == dualsense::kDualSenseEdgeProductId; }
    const std::string& lastError() const { return error_; }
    // The controller's motion sensor calibration (nominal values if it could not be read).
    const virtual_reports::MotionCalibration& motionCalibration() const { return motionCalibration_; }

private:
    std::unique_ptr<HidTransport> dev_;
    bool exclusive_ = false;
    HidDeviceInfo info_;
    dualsense::Connection connection_ = dualsense::Connection::Unknown;
    uint8_t sequence_ = 0;
    bool needsLightbarSetup_ = true;
    std::optional<dualsense::Effects> lastSent_;
    virtual_reports::MotionCalibration motionCalibration_ = virtual_reports::nominalCalibration();
    std::map<uint8_t, std::vector<uint8_t>> features_;
    std::string error_;
};

}  // namespace edgepad
