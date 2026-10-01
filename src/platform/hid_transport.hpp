#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace edgepad {

// Raw HID access behind DualSenseDevice: hidapi normally, or (Windows) an exclusive handle that
// keeps every other program from opening the controller.
class HidTransport {
public:
    virtual ~HidTransport() = default;
    // Bytes read (report id first), 0 on timeout, -1 on error.
    virtual int read(uint8_t* buffer, size_t size, int timeoutMs) = 0;
    virtual bool write(const uint8_t* data, size_t size) = 0;
    // `buffer[0]` holds the report id on entry.
    virtual void getFeature(uint8_t* buffer, size_t size) = 0;
    virtual std::string error() const = 0;
};

std::unique_ptr<HidTransport> openHidapiTransport(const std::string& path, std::string& error);

#ifdef _WIN32
// Opens the controller with no sharing: while EdgePad has it, no game (or anything else) can open
// it. Fails with `inUse` set when another program already has it open.
std::unique_ptr<HidTransport> openExclusiveTransport(const std::string& path, std::string& error, bool& inUse);
#endif

}  // namespace edgepad
