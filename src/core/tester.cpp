#include "core/tester.hpp"

#include <algorithm>
#include <cmath>

namespace edgepad::tester {
namespace {

constexpr float kEdge = 0.6f;  // farther out than this counts as "at the edge"
constexpr float kRest = 0.15f;  // closer than this counts as "left alone"

}  // namespace

void StickTest::add(float x, float y) {
    const float r = std::hypot(x, y);
    if (r < kRest) {
        if (restSamples_ == 0) {
            restMinX_ = restMaxX_ = x;
            restMinY_ = restMaxY_ = y;
        }
        restMinX_ = std::min(restMinX_, x);
        restMaxX_ = std::max(restMaxX_, x);
        restMinY_ = std::min(restMinY_, y);
        restMaxY_ = std::max(restMaxY_, y);
        ++restSamples_;
    }
    if (r < kEdge) return;
    float angle = std::atan2(y, x);
    if (angle < 0.0f) angle += 6.2831853f;
    const int sector = std::clamp(static_cast<int>(angle / 6.2831853f * kSectors), 0, kSectors - 1);
    auto& farthest = outline_[static_cast<size_t>(sector)];
    farthest = std::max(farthest, r);
}

void StickTest::reset() { *this = StickTest{}; }

float StickTest::coverage() const {
    const auto reached = std::count_if(outline_.begin(), outline_.end(), [](float r) { return r > 0.0f; });
    return static_cast<float>(reached) / kSectors;
}

float StickTest::averageError() const {
    float sum = 0.0f;
    int n = 0;
    for (float r : outline_) {
        if (r <= 0.0f) continue;
        sum += std::fabs(r - 1.0f);
        ++n;
    }
    return n > 0 ? sum / static_cast<float>(n) : 0.0f;
}

void TriggerTest::add(float value) {
    seen_ = true;
    min_ = std::min(min_, value);
    max_ = std::max(max_, value);
}

void ReportStats::add(float intervalMs, float processingUs) {
    intervals_[next_] = intervalMs;
    processing_[next_] = processingUs;
    next_ = (next_ + 1) % kHistory;
    count_ = std::min(count_ + 1, kHistory);
}

ReportStats::Summary ReportStats::summary() const {
    Summary s;
    s.samples = static_cast<int>(count_);
    if (count_ == 0) return s;
    float sum = 0.0f, sumSq = 0.0f, procSum = 0.0f;
    s.minMs = intervals_[0];
    s.maxMs = intervals_[0];
    for (size_t i = 0; i < count_; ++i) {
        const float v = intervals_[i];
        sum += v;
        sumSq += v * v;
        s.minMs = std::min(s.minMs, v);
        s.maxMs = std::max(s.maxMs, v);
        procSum += processing_[i];
        s.processingMaxUs = std::max(s.processingMaxUs, processing_[i]);
    }
    const float n = static_cast<float>(count_);
    s.averageMs = sum / n;
    s.jitterMs = std::sqrt(std::max(0.0f, sumSq / n - s.averageMs * s.averageMs));
    s.rateHz = s.averageMs > 0.0f ? 1000.0f / s.averageMs : 0.0f;
    s.processingAverageUs = procSum / n;
    return s;
}

std::array<float, ReportStats::kHistory> ReportStats::history() const {
    std::array<float, kHistory> out{};
    for (size_t i = 0; i < count_; ++i) {
        out[kHistory - count_ + i] = intervals_[(next_ + kHistory - count_ + i) % kHistory];
    }
    return out;
}

}  // namespace edgepad::tester
