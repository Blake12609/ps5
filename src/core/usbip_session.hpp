#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The server side of the USB/IP protocol (https://docs.kernel.org/usb/usbip_protocol.html) for one
// virtual USB device: the virtual DualSense. A USB/IP client (usbip-win2 on Windows) attaches it and
// the operating system then treats it like a real USB device plugged into a port. No sockets here:
// bytes go in with receive() and come out of takeOutput(), so the protocol is fully unit tested.
namespace edgepad::usbip {

inline constexpr uint16_t kTcpPort = 3240;
inline constexpr const char* kBusId = "1-1";

class Session {
public:
    struct Callbacks {
        // An output report from the host (interrupt OUT or SET_REPORT), report id first.
        std::function<void(const uint8_t*, size_t)> outputReport;
        // A feature report for GET_REPORT, report id first; nothing = not available (stall).
        std::function<std::optional<std::vector<uint8_t>>(uint8_t reportId)> featureReport;
    };

    explicit Session(Callbacks callbacks);

    // Bytes from the client. Returns false when the connection should be closed.
    bool receive(const uint8_t* data, size_t size);
    // A new input report: completes a waiting interrupt IN request right away, otherwise it goes out
    // with the next one. Only the newest report waits.
    void inputReport(const std::array<uint8_t, 64>& report);
    // Bytes to send to the client.
    std::vector<uint8_t> takeOutput();
    // The client attached the device (OP_REQ_IMPORT accepted).
    bool attached() const { return state_ == State::Attached; }

private:
    enum class State { Handshake, Attached, Closed };

    struct Header {
        uint32_t command = 0, seqnum = 0, devid = 0, direction = 0, ep = 0;
        uint32_t flags = 0;
        int32_t length = 0, startFrame = 0, packets = 0, interval = 0;
        std::array<uint8_t, 8> setup{};
    };

    bool handleHandshake();
    bool handleUrb();
    void submit(const Header& h, const uint8_t* data);
    void control(const Header& h, const uint8_t* data);
    void retSubmit(uint32_t seqnum, int32_t status, const uint8_t* data, size_t length, int32_t outLength = 0);
    void retUnlink(uint32_t seqnum, int32_t status);
    void completeInterruptIn();
    void deviceRecord(std::vector<uint8_t>& out) const;

    Callbacks callbacks_;
    State state_ = State::Handshake;
    std::vector<uint8_t> in_;   // received, not yet handled
    std::vector<uint8_t> out_;  // to send
    struct PendingIn {
        uint32_t seqnum;
        int32_t length;
    };
    std::deque<PendingIn> pendingIn_;  // interrupt IN requests waiting for a report
    std::optional<std::array<uint8_t, 64>> unsent_;  // newest report not yet delivered
    std::array<uint8_t, 64> current_{};               // newest report (for GET_REPORT input)
};

}  // namespace edgepad::usbip
