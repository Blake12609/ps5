#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/usbip_session.hpp"

namespace edgepad {

// Serves the virtual DualSense to a USB/IP client (usbip-win2) on 127.0.0.1 only. One client at
// a time; the session's callbacks run on the server thread.
class UsbIpServer {
public:
    explicit UsbIpServer(usbip::Session::Callbacks callbacks);
    ~UsbIpServer();
    UsbIpServer(const UsbIpServer&) = delete;
    UsbIpServer& operator=(const UsbIpServer&) = delete;

    bool start(uint16_t port, std::string& error);
    void stop();
    // Sends a new input report (completes a waiting request right away).
    void inputReport(const std::array<uint8_t, 64>& report);
    // A client has attached the device.
    bool attached() const;
    uint16_t port() const { return port_; }

private:
    void run();
    void sendLocked(const std::vector<uint8_t>& bytes);

    usbip::Session::Callbacks callbacks_;
    mutable std::mutex mutex_;  // session_ and client_
    std::unique_ptr<usbip::Session> session_;
    std::intptr_t listen_ = -1;
    std::intptr_t client_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace edgepad
