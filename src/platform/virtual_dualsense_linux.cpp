// Virtual DualSense on Linux through UHID: the kernel creates a HID device from EdgePad's report
// descriptor and its own DualSense driver (hid-playstation) takes it over - games, Steam and SDL see
// a wired PS5 controller. Feature reports the driver asks for come from the real controller, and
// output reports (lightbar, rumble, adaptive triggers) go back to it.
#include <fcntl.h>
#include <linux/uhid.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>

#include "core/virtual_dualsense.hpp"
#include "platform/virtual_pad.hpp"

namespace edgepad {
namespace {

namespace vds = virtual_dualsense;

bool writeEvent(int fd, const uhid_event& ev) { return ::write(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev)); }

class UhidDualSense final : public VirtualPad {
public:
    explicit UhidDualSense(int fd) : fd_(fd) {}

    ~UhidDualSense() override {
        stop_ = true;
        if (reader_.joinable()) reader_.join();
        uhid_event ev{};
        ev.type = UHID_DESTROY;
        writeEvent(fd_, ev);
        ::close(fd_);
    }

    bool create(std::string& error) {
        uhid_event ev{};
        ev.type = UHID_CREATE2;
        auto& c = ev.u.create2;
        std::strncpy(reinterpret_cast<char*>(c.name), "Sony Interactive Entertainment DualSense Wireless Controller",
                     sizeof(c.name) - 1);
        std::strncpy(reinterpret_cast<char*>(c.phys), "edgepad/virtual-dualsense", sizeof(c.phys) - 1);
        // The serial number marks it as EdgePad's own, so EdgePad never opens it as the real one.
        std::strncpy(reinterpret_cast<char*>(c.uniq), vds::kSerialNumber, sizeof(c.uniq) - 1);
        const auto& rd = vds::reportDescriptor();
        c.rd_size = static_cast<__u16>(rd.size());
        std::copy(rd.begin(), rd.end(), c.rd_data);
        c.bus = BUS_USB;
        c.vendor = vds::kVendorId;
        c.product = vds::kProductId;
        c.version = vds::kRelease;
        if (!writeEvent(fd_, ev)) {
            error = std::string("UHID: ") + std::strerror(errno);
            return false;
        }
        reader_ = std::thread([this] { readLoop(); });
        return true;
    }

    bool send(const OutputState& s) override {
        const auto report = vds::buildInputReport(s);
        uhid_event ev{};
        ev.type = UHID_INPUT2;
        ev.u.input2.size = static_cast<__u16>(report.size());
        std::copy(report.begin(), report.end(), ev.u.input2.data);
        return writeEvent(fd_, ev);
    }

    std::string name() const override { return "Virtual PS5 controller (DualSense)"; }

    void setFeatureReports(std::map<uint8_t, std::vector<uint8_t>> reports) override {
        bridge_.setFeatureReports(std::move(reports));
    }
    std::vector<std::vector<uint8_t>> takeOutputReports() override { return bridge_.takeOutputReports(); }

private:
    void readLoop() {
        while (!stop_) {
            pollfd p{fd_, POLLIN, 0};
            if (::poll(&p, 1, 100) <= 0) continue;
            uhid_event ev{};
            if (::read(fd_, &ev, sizeof(ev)) <= 0) continue;
            switch (ev.type) {
                case UHID_OUTPUT:
                    if (ev.u.output.rtype == UHID_OUTPUT_REPORT) {
                        bridge_.pushOutputReport(ev.u.output.data, ev.u.output.size);
                    }
                    break;
                case UHID_GET_REPORT: {
                    uhid_event reply{};
                    reply.type = UHID_GET_REPORT_REPLY;
                    reply.u.get_report_reply.id = ev.u.get_report.id;
                    const auto report = ev.u.get_report.rtype == UHID_FEATURE_REPORT
                                            ? bridge_.featureReport(ev.u.get_report.rnum)
                                            : std::nullopt;
                    if (report && report->size() <= UHID_DATA_MAX) {
                        reply.u.get_report_reply.size = static_cast<__u16>(report->size());
                        std::copy(report->begin(), report->end(), reply.u.get_report_reply.data);
                    } else {
                        reply.u.get_report_reply.err = EIO;
                    }
                    writeEvent(fd_, reply);
                    break;
                }
                case UHID_SET_REPORT: {
                    if (ev.u.set_report.rtype == UHID_OUTPUT_REPORT) {
                        bridge_.pushOutputReport(ev.u.set_report.data, ev.u.set_report.size);
                    }
                    uhid_event reply{};
                    reply.type = UHID_SET_REPORT_REPLY;
                    reply.u.set_report_reply.id = ev.u.set_report.id;
                    writeEvent(fd_, reply);
                    break;
                }
                default:
                    break;  // start / stop / open / close
            }
        }
    }

    int fd_;
    DualSenseBridge bridge_;
    std::atomic<bool> stop_{false};
    std::thread reader_;
};

}  // namespace

PadCreateResult createVirtualDualSense() {
    PadCreateResult result;
    const int fd = ::open("/dev/uhid", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        const int err = errno;
        result.error = (err == EACCES || err == EPERM) ? PadError::PermissionDenied : PadError::DriverMissing;
        result.message = std::string("Cannot open /dev/uhid: ") + std::strerror(err) +
                         (err == EACCES ? " (install packaging/linux/70-edgepad.rules)" : "");
        return result;
    }
    auto pad = std::make_unique<UhidDualSense>(fd);
    std::string error;
    if (!pad->create(error)) {
        result.error = PadError::Failed;
        result.message = error;
        return result;
    }
    result.pad = std::move(pad);
    return result;
}

}  // namespace edgepad
