#include "core/usbip_session.hpp"

#include <algorithm>
#include <cstring>

#include "core/virtual_dualsense.hpp"

namespace edgepad::usbip {
namespace {

namespace vds = virtual_dualsense;

constexpr uint16_t kVersion = 0x0111;
constexpr uint16_t kOpReqDevlist = 0x8005, kOpRepDevlist = 0x0005;
constexpr uint16_t kOpReqImport = 0x8003, kOpRepImport = 0x0003;
constexpr uint32_t kStatusOk = 0, kStatusNoDevice = 4;
constexpr uint32_t kCmdSubmit = 1, kCmdUnlink = 2, kRetSubmit = 3, kRetUnlink = 4;
constexpr uint32_t kDirOut = 0;
constexpr size_t kHeaderSize = 48;
constexpr uint32_t kBusNum = 1, kDevNum = 1;
constexpr uint32_t kSpeedHigh = 3;  // USB 2.0, like the real DualSense
constexpr int32_t kErrPipe = -32;        // -EPIPE: stall
constexpr int32_t kErrConnReset = -104;  // -ECONNRESET: unlinked
constexpr int32_t kNonIsochronous = -1;
constexpr uint8_t kInEndpoint = vds::kInterruptInEndpoint & 0x0F;
constexpr uint8_t kOutEndpoint = vds::kInterruptOutEndpoint & 0x0F;

uint32_t be32(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
}
void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}
void putString(std::vector<uint8_t>& out, const char* text, size_t size) {
    const size_t n = std::min(std::strlen(text), size - 1);
    out.insert(out.end(), text, text + n);
    out.insert(out.end(), size - n, 0);
}

}  // namespace

Session::Session(Callbacks callbacks)
    : callbacks_(std::move(callbacks)), current_(vds::buildInputReport(OutputState{})) {}

void Session::deviceRecord(std::vector<uint8_t>& out) const {
    const auto dev = vds::deviceDescriptor();
    putString(out, "/sys/devices/edgepad/usb1/1-1", 256);
    putString(out, kBusId, 32);
    put32(out, kBusNum);
    put32(out, kDevNum);
    put32(out, kSpeedHigh);
    put16(out, vds::kVendorId);
    put16(out, vds::kProductId);
    put16(out, vds::kRelease);
    out.push_back(dev[4]);  // class, subclass, protocol
    out.push_back(dev[5]);
    out.push_back(dev[6]);
    out.push_back(1);  // configuration value
    out.push_back(1);  // configurations
    out.push_back(1);  // interfaces
}

bool Session::receive(const uint8_t* data, size_t size) {
    if (state_ == State::Closed) return false;
    in_.insert(in_.end(), data, data + size);
    for (;;) {
        const size_t before = in_.size();
        const bool ok = state_ == State::Handshake ? handleHandshake() : handleUrb();
        if (!ok) {
            state_ = State::Closed;
            return false;
        }
        if (in_.size() == before || in_.empty()) return true;  // need more bytes
    }
}

bool Session::handleHandshake() {
    if (in_.size() < 8) return true;
    const uint16_t code = be16(&in_[2]);
    if (code == kOpReqDevlist) {
        put16(out_, kVersion);
        put16(out_, kOpRepDevlist);
        put32(out_, kStatusOk);
        put32(out_, 1);  // one exported device
        deviceRecord(out_);
        out_.insert(out_.end(), {0x03, 0x00, 0x00, 0x00});  // its interface: HID
        in_.erase(in_.begin(), in_.begin() + 8);
        return true;  // the client closes the connection after reading the list
    }
    if (code != kOpReqImport) return false;
    if (in_.size() < 8 + 32) return true;
    char busid[33] = {};
    std::memcpy(busid, &in_[8], 32);
    in_.erase(in_.begin(), in_.begin() + 40);
    put16(out_, kVersion);
    put16(out_, kOpRepImport);
    if (std::strcmp(busid, kBusId) != 0) {
        put32(out_, kStatusNoDevice);
        return false;
    }
    put32(out_, kStatusOk);
    deviceRecord(out_);
    state_ = State::Attached;
    return true;
}

bool Session::handleUrb() {
    if (in_.size() < kHeaderSize) return true;
    const uint8_t* p = in_.data();
    Header h;
    h.command = be32(p);
    h.seqnum = be32(p + 4);
    h.devid = be32(p + 8);
    h.direction = be32(p + 12);
    h.ep = be32(p + 16);
    if (h.command == kCmdUnlink) {
        const uint32_t target = be32(p + 20);
        in_.erase(in_.begin(), in_.begin() + kHeaderSize);
        const auto it = std::find_if(pendingIn_.begin(), pendingIn_.end(),
                                     [&](const PendingIn& r) { return r.seqnum == target; });
        if (it != pendingIn_.end()) {
            pendingIn_.erase(it);
            retUnlink(h.seqnum, kErrConnReset);
        } else {
            retUnlink(h.seqnum, 0);  // already completed
        }
        return true;
    }
    if (h.command != kCmdSubmit) return false;
    h.flags = be32(p + 20);
    h.length = static_cast<int32_t>(be32(p + 24));
    h.startFrame = static_cast<int32_t>(be32(p + 28));
    h.packets = static_cast<int32_t>(be32(p + 32));
    h.interval = static_cast<int32_t>(be32(p + 36));
    std::copy(p + 40, p + 48, h.setup.begin());
    if (h.length < 0 || h.length > (1 << 20)) return false;
    const size_t payload = h.direction == kDirOut ? static_cast<size_t>(h.length) : 0;
    const size_t isoDescriptors = h.packets > 0 ? static_cast<size_t>(h.packets) * 16 : 0;
    if (in_.size() < kHeaderSize + payload + isoDescriptors) return true;
    std::vector<uint8_t> data(in_.begin() + kHeaderSize, in_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize + payload));
    in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize + payload + isoDescriptors));
    if (isoDescriptors > 0) {
        retSubmit(h.seqnum, kErrPipe, nullptr, 0);  // no isochronous endpoints
        return true;
    }
    submit(h, data.data());
    return true;
}

void Session::submit(const Header& h, const uint8_t* data) {
    if (h.ep == 0) {
        control(h, data);
    } else if (h.ep == kInEndpoint && h.direction != kDirOut) {
        pendingIn_.push_back({h.seqnum, h.length});
        completeInterruptIn();
    } else if (h.ep == kOutEndpoint && h.direction == kDirOut) {
        if (callbacks_.outputReport && h.length > 0) callbacks_.outputReport(data, static_cast<size_t>(h.length));
        retSubmit(h.seqnum, 0, nullptr, 0, h.length);
    } else {
        retSubmit(h.seqnum, kErrPipe, nullptr, 0);
    }
}

void Session::control(const Header& h, const uint8_t* data) {
    const uint8_t requestType = h.setup[0];
    const uint8_t request = h.setup[1];
    const uint16_t value = static_cast<uint16_t>(h.setup[2] | (h.setup[3] << 8));
    const uint16_t length = static_cast<uint16_t>(h.setup[6] | (h.setup[7] << 8));
    const bool deviceToHost = (requestType & 0x80) != 0;
    const uint8_t type = requestType & 0x60;       // standard, class, vendor
    const uint8_t recipient = requestType & 0x1F;  // device, interface, endpoint, other

    auto reply = [&](const std::vector<uint8_t>& d) {
        const size_t n = std::min({d.size(), size_t{length}, static_cast<size_t>(std::max(h.length, 0))});
        retSubmit(h.seqnum, 0, d.data(), n);
    };
    auto ok = [&] { retSubmit(h.seqnum, 0, nullptr, 0, deviceToHost ? 0 : h.length); };
    auto stall = [&] { retSubmit(h.seqnum, kErrPipe, nullptr, 0); };

    if (type == 0x00) {  // standard requests
        switch (request) {
            case 0x06: {  // GET_DESCRIPTOR
                const uint8_t kind = static_cast<uint8_t>(value >> 8);
                const uint8_t index = static_cast<uint8_t>(value & 0xFF);
                std::vector<uint8_t> d;
                if (kind == 0x01) d = vds::deviceDescriptor();
                else if (kind == 0x02) d = vds::configurationDescriptor();
                else if (kind == 0x03) d = vds::stringDescriptor(index);
                else if (kind == 0x06) d = {10, 0x06, 0x00, 0x02, 0, 0, 0, 64, 1, 0};  // device qualifier
                else if (kind == 0x21) d = vds::hidDescriptor();
                else if (kind == 0x22) d = vds::reportDescriptor();
                if (d.empty()) return stall();
                return reply(d);
            }
            case 0x00:  // GET_STATUS
                return reply({static_cast<uint8_t>(recipient == 0 ? 0x01 : 0x00), 0x00});  // self powered
            case 0x08: return reply({1});  // GET_CONFIGURATION
            case 0x0A: return reply({0});  // GET_INTERFACE
            default: return deviceToHost ? stall() : ok();  // SET_ADDRESS / CONFIGURATION / INTERFACE / FEATURE
        }
    }
    if (type == 0x20 && recipient == 0x01) {  // HID class requests
        const uint8_t reportType = static_cast<uint8_t>(value >> 8);
        const uint8_t reportId = static_cast<uint8_t>(value & 0xFF);
        switch (request) {
            case 0x01:  // GET_REPORT
                if (reportType == 1 && reportId == current_[0]) {
                    return reply(std::vector<uint8_t>(current_.begin(), current_.end()));
                }
                if (reportType == 3 && callbacks_.featureReport) {
                    if (auto report = callbacks_.featureReport(reportId)) return reply(*report);
                }
                return stall();
            case 0x02: return reply({0});  // GET_IDLE
            case 0x03: return reply({1});  // GET_PROTOCOL: report protocol
            case 0x09:                     // SET_REPORT
                if (reportType == 2 && callbacks_.outputReport && h.length > 0) {
                    callbacks_.outputReport(data, static_cast<size_t>(h.length));
                }
                return ok();
            default: return deviceToHost ? stall() : ok();  // SET_IDLE, SET_PROTOCOL
        }
    }
    return deviceToHost ? stall() : ok();  // port requests, vendor requests
}

void Session::inputReport(const std::array<uint8_t, 64>& report) {
    current_ = report;
    unsent_ = report;
    completeInterruptIn();
}

void Session::completeInterruptIn() {
    while (!pendingIn_.empty() && unsent_) {
        const PendingIn r = pendingIn_.front();
        pendingIn_.pop_front();
        const size_t n = std::min(unsent_->size(), static_cast<size_t>(std::max(r.length, 0)));
        retSubmit(r.seqnum, 0, unsent_->data(), n);
        unsent_.reset();
    }
}

void Session::retSubmit(uint32_t seqnum, int32_t status, const uint8_t* data, size_t length, int32_t outLength) {
    put32(out_, kRetSubmit);
    put32(out_, seqnum);
    put32(out_, 0);  // devid, direction, ep: zero in replies
    put32(out_, 0);
    put32(out_, 0);
    put32(out_, static_cast<uint32_t>(status));
    put32(out_, static_cast<uint32_t>(data != nullptr ? static_cast<int32_t>(length) : outLength));
    put32(out_, 0);  // start frame
    put32(out_, static_cast<uint32_t>(kNonIsochronous));
    put32(out_, 0);  // error count
    out_.insert(out_.end(), 8, 0);
    if (data != nullptr) out_.insert(out_.end(), data, data + length);
}

void Session::retUnlink(uint32_t seqnum, int32_t status) {
    put32(out_, kRetUnlink);
    put32(out_, seqnum);
    put32(out_, 0);
    put32(out_, 0);
    put32(out_, 0);
    put32(out_, static_cast<uint32_t>(status));
    out_.insert(out_.end(), 24, 0);
}

std::vector<uint8_t> Session::takeOutput() {
    std::vector<uint8_t> out;
    out.swap(out_);
    return out;
}

}  // namespace edgepad::usbip
