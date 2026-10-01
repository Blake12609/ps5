#include "core/virtual_dualsense.hpp"
#include "platform/virtual_pad.hpp"

namespace edgepad {

void DualSenseBridge::setFeatureReports(std::map<uint8_t, std::vector<uint8_t>> reports) {
    std::lock_guard lock(mutex_);
    features_ = std::move(reports);
}

std::optional<std::vector<uint8_t>> DualSenseBridge::featureReport(uint8_t reportId) const {
    std::vector<uint8_t> report;
    {
        std::lock_guard lock(mutex_);
        if (const auto it = features_.find(reportId); it != features_.end()) report = it->second;
    }
    if (report.empty()) report = virtual_dualsense::defaultFeatureReport(reportId);
    if (report.empty()) return std::nullopt;
    // Exactly the size the report descriptor declares (Bluetooth copies carry a CRC at the end).
    if (const size_t size = virtual_dualsense::featureReportSize(reportId); size > 0) report.resize(size, 0);
    return report;
}

void DualSenseBridge::pushOutputReport(const uint8_t* data, size_t size) {
    if (data == nullptr || size < virtual_dualsense::kOutputReportSize) return;
    std::vector<uint8_t> report(data, data + size);
    std::lock_guard lock(mutex_);
    // Flag bytes 1, 2 and 39 say what a report changes.
    auto sameParts = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
        return a[1] == b[1] && a[2] == b[2] && a[39] == b[39];
    };
    if (!outputs_.empty() && sameParts(outputs_.back(), report)) {
        outputs_.back() = std::move(report);
    } else {
        outputs_.push_back(std::move(report));
        if (outputs_.size() > 32) outputs_.pop_front();
    }
}

std::vector<std::vector<uint8_t>> DualSenseBridge::takeOutputReports() {
    std::lock_guard lock(mutex_);
    std::vector<std::vector<uint8_t>> out(std::make_move_iterator(outputs_.begin()),
                                          std::make_move_iterator(outputs_.end()));
    outputs_.clear();
    return out;
}

}  // namespace edgepad
