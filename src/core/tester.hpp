#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The controller tester: measurements of the raw controller, before any EdgePad processing.
namespace edgepad::tester {

// Stick circularity and resting noise. Rotate the stick slowly around its edge: for every
// direction (72 sectors of 5 degrees) the farthest point is kept.
class StickTest {
public:
    static constexpr int kSectors = 72;

    void add(float x, float y);
    void reset();

    // Share of directions reached at the edge, 0..1.
    float coverage() const;
    // Average distance of the edge from a perfect circle, as a fraction (0.03 = 3%).
    float averageError() const;
    // Farthest radius per direction (0 where the stick did not get to the edge yet).
    const std::array<float, kSectors>& outline() const { return outline_; }
    // Range of the resting position while the stick is left alone (raw values).
    bool restSeen() const { return restSamples_ > 0; }
    float restRangeX() const { return restSamples_ > 0 ? restMaxX_ - restMinX_ : 0.0f; }
    float restRangeY() const { return restSamples_ > 0 ? restMaxY_ - restMinY_ : 0.0f; }

private:
    std::array<float, kSectors> outline_{};
    int restSamples_ = 0;
    float restMinX_ = 0, restMaxX_ = 0, restMinY_ = 0, restMaxY_ = 0;
};

// Trigger travel actually reached.
class TriggerTest {
public:
    void add(float value);
    void reset() { *this = TriggerTest{}; }
    bool seen() const { return seen_; }
    float min() const { return min_; }
    float max() const { return max_; }
    // Both ends reached, in the controller's own steps (0 and 255).
    bool fullRange() const { return seen_ && min_ < 0.5f / 255.0f && max_ > 254.5f / 255.0f; }

private:
    bool seen_ = false;
    float min_ = 1.0f, max_ = 0.0f;
};

// Report timing: how often the controller reports and how long EdgePad takes per report.
class ReportStats {
public:
    static constexpr size_t kHistory = 256;

    struct Summary {
        int samples = 0;
        float rateHz = 0.0f;
        float averageMs = 0.0f, minMs = 0.0f, maxMs = 0.0f, jitterMs = 0.0f;  // between reports
        float processingAverageUs = 0.0f, processingMaxUs = 0.0f;              // EdgePad's own time
    };

    void add(float intervalMs, float processingUs);
    void reset() { *this = ReportStats{}; }
    Summary summary() const;
    // The intervals, oldest first (only the last `count()` are valid).
    std::array<float, kHistory> history() const;
    size_t count() const { return count_; }

private:
    std::array<float, kHistory> intervals_{};
    std::array<float, kHistory> processing_{};
    size_t next_ = 0;
    size_t count_ = 0;
};

}  // namespace edgepad::tester
