#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/settings.hpp"
#include "core/types.hpp"

namespace edgepad {

// Game rumble coming back from the virtual controller (0..255 per motor).
using RumbleCallback = std::function<void(uint8_t largeMotor, uint8_t smallMotor)>;

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
PadCreateResult createVirtualPad(OutputKind kind, RumbleCallback onRumble);

// Where users can get the driver needed for virtual controllers on this platform.
const char* virtualPadDriverUrl();

}  // namespace edgepad
