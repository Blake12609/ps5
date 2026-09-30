#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

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
};

enum class PadError { None, DriverMissing, PermissionDenied, Failed, Unsupported };

struct PadCreateResult {
    std::unique_ptr<VirtualPad> pad;
    PadError error = PadError::None;
    std::string message;
};

// Creates the virtual controller games will see. Windows: ViGEmBus. Linux: uinput.
PadCreateResult createVirtualPad(OutputKind kind, FeedbackCallback onFeedback);

// Where users can get the driver needed for virtual controllers on this platform.
const char* virtualPadDriverUrl();

}  // namespace edgepad
