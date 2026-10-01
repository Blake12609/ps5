#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/settings.hpp"
#include "core/types.hpp"

namespace edgepad {

// What a game sends back to the virtual controller.
struct PadFeedback {
    uint8_t largeMotor = 0;  // 0..255
    uint8_t smallMotor = 0;
    bool hasLightbar = false;  // only a virtual DualShock 4 has a lightbar
    std::array<uint8_t, 3> lightbar{};
};
using FeedbackCallback = std::function<void(const PadFeedback&)>;

class VirtualPad {
public:
    virtual ~VirtualPad() = default;
    virtual bool send(const OutputState& state) = 0;
    virtual std::string name() const = 0;
    // Shown to the user while the virtual controller does not fully work yet (empty when fine).
    virtual std::string warning() const { return {}; }

    // Virtual DualSense only:
    // the real controller's feature reports (calibration, MAC, firmware; report id first)...
    virtual void setFeatureReports(std::map<uint8_t, std::vector<uint8_t>> /*reports*/) {}
    // ...and the output reports games sent (adaptive triggers, rumble, lightbar...) for the real one.
    virtual std::vector<std::vector<uint8_t>> takeOutputReports() { return {}; }
};

// Shared by the virtual DualSense backends: feature reports for the host and the output reports
// games send. Thread safe (the host talks to the virtual controller on its own thread).
class DualSenseBridge {
public:
    void setFeatureReports(std::map<uint8_t, std::vector<uint8_t>> reports);
    // The real controller's report, or a plausible default (demo mode), or nothing.
    std::optional<std::vector<uint8_t>> featureReport(uint8_t reportId) const;
    // Queues a game's output report. Consecutive reports that change the same things are merged
    // (only the newest counts), so a game updating rumble every frame cannot pile up writes.
    void pushOutputReport(const uint8_t* data, size_t size);
    std::vector<std::vector<uint8_t>> takeOutputReports();

private:
    mutable std::mutex mutex_;
    std::map<uint8_t, std::vector<uint8_t>> features_;
    std::deque<std::vector<uint8_t>> outputs_;
};

enum class PadError { None, DriverMissing, PermissionDenied, Failed, Unsupported };

struct PadCreateResult {
    std::unique_ptr<VirtualPad> pad;
    PadError error = PadError::None;
    std::string message;
};

// Creates the virtual controller games will see. Windows: ViGEmBus (Xbox 360 / DualShock 4) or
// usbip-win2 (DualSense). Linux: uinput, or UHID for the DualSense.
PadCreateResult createVirtualPad(OutputKind kind, FeedbackCallback onFeedback);
// The virtual DualSense backend (called by createVirtualPad).
PadCreateResult createVirtualDualSense();

// Where users can get the driver needed for this kind of virtual controller.
const char* virtualPadDriverUrl(OutputKind kind);

}  // namespace edgepad
