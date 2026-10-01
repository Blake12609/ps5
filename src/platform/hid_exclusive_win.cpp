// Exclusive HID access on Windows: the controller is opened without sharing, so while EdgePad has
// it no other program - games, Steam, the browser - can open it. Same I/O as hidapi's backend.
#include <windows.h>

// Some SDKs (MinGW) declare the HID functions without C linkage.
#if defined(_MSC_VER)
#pragma warning(push, 3)  // the SDK's HID headers use nameless unions
#endif
extern "C" {
#include <hidsdi.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstring>
#include <vector>

#include "platform/hid_transport.hpp"

namespace edgepad {
namespace {

// IOCTL_HID_GET_FEATURE from hidclass.h: CTL_CODE(FILE_DEVICE_KEYBOARD, 100, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
constexpr DWORD kIoctlHidGetFeature = 0x000B0192;
constexpr DWORD kWriteTimeoutMs = 1000;

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string errorText(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string out;
    for (const wchar_t* c = text; c != nullptr && *c != L'\0'; ++c) {
        if (*c != L'\r' && *c != L'\n') out.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    if (text != nullptr) LocalFree(text);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? "error " + std::to_string(code) : out;
}

class ExclusiveTransport final : public HidTransport {
public:
    ExclusiveTransport(HANDLE handle, size_t inputLength, size_t outputLength, size_t featureLength)
        : handle_(handle),
          inputLength_(std::max<size_t>(inputLength, 1)),
          outputLength_(outputLength),
          featureLength_(featureLength),
          readEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          ioEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          readBuffer_(inputLength_) {}

    ~ExclusiveTransport() override {
        if (readPending_) {
            CancelIoEx(handle_, &readOverlapped_);
            DWORD n = 0;
            GetOverlappedResult(handle_, &readOverlapped_, &n, TRUE);  // the buffer must outlive the read
        }
        CloseHandle(handle_);
        if (readEvent_ != nullptr) CloseHandle(readEvent_);
        if (ioEvent_ != nullptr) CloseHandle(ioEvent_);
    }
    ExclusiveTransport(const ExclusiveTransport&) = delete;
    ExclusiveTransport& operator=(const ExclusiveTransport&) = delete;

    int read(uint8_t* buffer, size_t size, int timeoutMs) override {
        DWORD n = 0;
        if (!readPending_) {
            std::fill(readBuffer_.begin(), readBuffer_.end(), uint8_t{0});
            readOverlapped_ = {};
            readOverlapped_.hEvent = readEvent_;
            ResetEvent(readEvent_);
            if (ReadFile(handle_, readBuffer_.data(), static_cast<DWORD>(readBuffer_.size()), &n, &readOverlapped_)) {
                return copyOut(buffer, size, n);
            }
            if (GetLastError() != ERROR_IO_PENDING) return fail(GetLastError());
            readPending_ = true;
        }
        const DWORD wait = WaitForSingleObject(readEvent_, timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
        if (wait == WAIT_TIMEOUT) return 0;  // the read stays pending for the next call
        if (wait != WAIT_OBJECT_0) return fail(GetLastError());
        readPending_ = false;
        if (!GetOverlappedResult(handle_, &readOverlapped_, &n, FALSE)) return fail(GetLastError());
        return copyOut(buffer, size, n);
    }

    bool write(const uint8_t* data, size_t size) override {
        // Windows wants exactly the device's output report length.
        std::vector<uint8_t> report(std::max(size, outputLength_), uint8_t{0});
        std::memcpy(report.data(), data, size);
        OVERLAPPED ol{};
        ol.hEvent = ioEvent_;
        ResetEvent(ioEvent_);
        DWORD n = 0;
        if (WriteFile(handle_, report.data(), static_cast<DWORD>(report.size()), &n, &ol)) return true;
        if (GetLastError() != ERROR_IO_PENDING) {
            fail(GetLastError());
            return false;
        }
        if (WaitForSingleObject(ioEvent_, kWriteTimeoutMs) != WAIT_OBJECT_0) {
            CancelIoEx(handle_, &ol);
            GetOverlappedResult(handle_, &ol, &n, TRUE);
            error_ = "write timed out";
            return false;
        }
        if (!GetOverlappedResult(handle_, &ol, &n, FALSE)) {
            fail(GetLastError());
            return false;
        }
        return true;
    }

    int getFeature(uint8_t* buffer, size_t size) override {
        std::vector<uint8_t> report(std::max(size, featureLength_), uint8_t{0});
        report[0] = buffer[0];
        OVERLAPPED ol{};
        ol.hEvent = ioEvent_;
        ResetEvent(ioEvent_);
        DWORD n = 0;
        BOOL ok = DeviceIoControl(handle_, kIoctlHidGetFeature, report.data(), static_cast<DWORD>(report.size()),
                                  report.data(), static_cast<DWORD>(report.size()), &n, &ol);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ioEvent_, kWriteTimeoutMs) != WAIT_OBJECT_0) CancelIoEx(handle_, &ol);
            ok = GetOverlappedResult(handle_, &ol, &n, TRUE);
        }
        if (!ok) return -1;
        const size_t count = std::min<size_t>({size, report.size(), static_cast<size_t>(n)});
        std::memcpy(buffer, report.data(), count);
        return static_cast<int>(count);
    }

    std::string error() const override { return error_.empty() ? std::string("unknown HID error") : error_; }

private:
    int copyOut(uint8_t* buffer, size_t size, DWORD n) {
        if (n == 0) return 0;
        // Like hidapi: devices without numbered reports get a leading 0 that is not part of the data.
        const uint8_t* src = readBuffer_.data();
        size_t count = n;
        if (src[0] == 0) {
            ++src;
            --count;
        }
        count = std::min(count, size);
        std::memcpy(buffer, src, count);
        return static_cast<int>(count);
    }

    int fail(DWORD code) {
        error_ = errorText(code);
        return -1;
    }

    HANDLE handle_;
    size_t inputLength_;
    size_t outputLength_;
    size_t featureLength_;
    HANDLE readEvent_;
    HANDLE ioEvent_;
    std::vector<uint8_t> readBuffer_;
    OVERLAPPED readOverlapped_{};
    bool readPending_ = false;
    std::string error_;
};

}  // namespace

std::unique_ptr<HidTransport> openExclusiveTransport(const std::string& path, std::string& error, bool& inUse) {
    inUse = false;
    // Share mode 0: nobody else may open the controller while this handle exists, and the open
    // fails if anyone already has it open.
    HANDLE handle = CreateFileW(widen(path).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        inUse = code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED;
        error = inUse ? "another program has the controller open" : "could not open controller: " + errorText(code);
        return nullptr;
    }
    size_t inputLength = 64;
    size_t outputLength = 0;
    size_t featureLength = 0;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(handle, &preparsed)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            inputLength = caps.InputReportByteLength;
            outputLength = caps.OutputReportByteLength;
            featureLength = caps.FeatureReportByteLength;
        }
        HidD_FreePreparsedData(preparsed);
    }
    return std::make_unique<ExclusiveTransport>(handle, inputLength, outputLength, featureLength);
}

}  // namespace edgepad
