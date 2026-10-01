// Virtual DualSense on Windows through usbip-win2 (https://github.com/vadimgrn/usbip-win2), a
// Microsoft-signed USB/IP client driver. EdgePad serves a virtual USB DualSense on 127.0.0.1 and
// asks usbip-win2 to attach it; Windows then sees a wired DualSense and handles it with its own
// USB and HID drivers, exactly like a real one plugged in. Games get a genuine PS5 controller.
#include <windows.h>

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <string>
#include <thread>

#include "core/virtual_dualsense.hpp"
#include "platform/usbip_server.hpp"
#include "platform/virtual_pad.hpp"

namespace edgepad {
namespace {

constexpr const wchar_t* kLocation = L"-r 127.0.0.1 -b 1-1";

std::wstring usbipExecutable() {
    wchar_t buffer[MAX_PATH];
    for (const wchar_t* var : {L"ProgramW6432", L"ProgramFiles"}) {
        const DWORD n = GetEnvironmentVariableW(var, buffer, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::wstring path = std::wstring(buffer) + L"\\USBip\\usbip.exe";
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
        }
    }
    if (SearchPathW(nullptr, L"usbip.exe", nullptr, MAX_PATH, buffer, nullptr) > 0) return buffer;
    return {};
}

struct RunResult {
    bool ran = false;
    DWORD exitCode = 1;
    std::string output;
};

// Runs usbip.exe without a console window and returns its output.
RunResult run(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs) {
    RunResult result;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 1 << 16)) return result;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    const BOOL started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (started) {
        result.ran = true;
        if (WaitForSingleObject(pi.hProcess, timeoutMs) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
        GetExitCodeProcess(pi.hProcess, &result.exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        char chunk[512];
        DWORD n = 0;
        while (ReadFile(readPipe, chunk, sizeof(chunk), &n, nullptr) && n > 0) result.output.append(chunk, n);
    }
    CloseHandle(readPipe);
    while (!result.output.empty() && std::isspace(static_cast<unsigned char>(result.output.back()))) {
        result.output.pop_back();
    }
    return result;
}

// The port usbip-win2 plugged EdgePad's device into, from "usbip port" (0 if not attached).
int attachedPort(const std::wstring& exe) {
    const RunResult r = run(exe, L"port", 5000);
    int port = 0;
    size_t at = 0;
    while ((at = r.output.find("Port ", at)) != std::string::npos) {
        const int candidate = std::atoi(r.output.c_str() + at + 5);
        const size_t next = r.output.find("Port ", at + 5);
        const std::string block = r.output.substr(at, next == std::string::npos ? std::string::npos : next - at);
        if (block.find("127.0.0.1:3240/1-1") != std::string::npos) port = candidate;
        at += 5;
    }
    return port;
}

class UsbIpDualSense final : public VirtualPad {
public:
    explicit UsbIpDualSense(std::wstring usbip)
        : usbip_(std::move(usbip)),
          server_({[this](const uint8_t* d, size_t n) { bridge_.pushOutputReport(d, n); },
                   [this](uint8_t id) { return bridge_.featureReport(id); }}) {}

    ~UsbIpDualSense() override {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (attacher_.joinable()) attacher_.join();
        // Unplug cleanly while the server still answers; without it usbip-win2 would keep trying
        // to reconnect after EdgePad closes.
        if (const int port = port_ > 0 ? port_ : attachedPort(usbip_); port > 0) {
            run(usbip_, L"detach -p " + std::to_wstring(port), 5000);
        }
        server_.stop();
    }

    bool start(std::string& error) {
        if (!server_.start(usbip::kTcpPort, error)) return false;
        attacher_ = std::thread([this] { attachLoop(); });
        return true;
    }

    bool send(const OutputState& s) override {
        server_.inputReport(virtual_dualsense::buildInputReport(s));
        return true;
    }

    std::string name() const override {
        return server_.attached() ? "Virtual PS5 controller (DualSense)" : "Virtual PS5 controller (connecting...)";
    }

    std::string warning() const override {
        std::lock_guard lock(mutex_);
        return server_.attached() ? std::string() : problem_;
    }

    void setFeatureReports(std::map<uint8_t, std::vector<uint8_t>> reports) override {
        bridge_.setFeatureReports(std::move(reports));
    }
    std::vector<std::vector<uint8_t>> takeOutputReports() override { return bridge_.takeOutputReports(); }

private:
    bool waitFor(std::chrono::milliseconds time, bool untilAttached) {
        std::unique_lock lock(mutex_);
        return wake_.wait_for(lock, time, [&] { return stop_ || (untilAttached && server_.attached()); });
    }

    void attachLoop() {
        // usbip-win2 may still be retrying to reach an EdgePad that closed without detaching.
        // Give it a moment to reconnect by itself, then stop its retries before attaching anew.
        if (waitFor(std::chrono::milliseconds(1500), true) && stop_) return;
        if (!server_.attached()) run(usbip_, std::wstring(L"attach ") + kLocation + L" --stop", 5000);
        while (!stop_) {
            if (server_.attached()) {
                waitFor(std::chrono::seconds(2), false);
                continue;
            }
            const RunResult r = run(usbip_, std::wstring(L"attach ") + kLocation + L" -t", 10000);
            if (r.ran && r.exitCode == 0) {
                port_ = std::atoi(r.output.c_str());
                if (waitFor(std::chrono::seconds(5), true) && stop_) return;
            }
            if (!server_.attached()) {
                std::lock_guard lock(mutex_);
                problem_ = !r.ran ? "Could not run usbip.exe (usbip-win2)"
                                  : "usbip-win2 could not attach the virtual PS5 controller" +
                                        (r.output.empty() ? std::string() : ": " + r.output);
            }
            waitFor(std::chrono::seconds(5), false);
        }
    }

    std::wstring usbip_;
    DualSenseBridge bridge_;
    UsbIpServer server_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::string problem_;
    int port_ = 0;
    std::thread attacher_;
};

}  // namespace

PadCreateResult createVirtualDualSense() {
    PadCreateResult result;
    const std::wstring usbip = usbipExecutable();
    if (usbip.empty()) {
        result.error = PadError::DriverMissing;
        result.message = "usbip-win2 is not installed";
        return result;
    }
    auto pad = std::make_unique<UsbIpDualSense>(usbip);
    std::string error;
    if (!pad->start(error)) {
        result.error = PadError::Failed;
        result.message = "Virtual PS5 controller: " + error;
        return result;
    }
    result.pad = std::move(pad);
    return result;
}

}  // namespace edgepad
