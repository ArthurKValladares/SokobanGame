#include "engine/render/FrameTimeTelemetry.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {

void FrameTimeTelemetry::reset() noexcept
{
    samples_ = {};
    sampleCount_ = 0;
    nextSample_ = 0;
    latestMilliseconds_ = 0.0;
}

void FrameTimeTelemetry::record(double milliseconds) noexcept
{
    if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
        return;
    }
    samples_[nextSample_] = milliseconds;
    nextSample_ = (nextSample_ + 1) % samples_.size();
    sampleCount_ = std::min(sampleCount_ + 1, samples_.size());
    latestMilliseconds_ = milliseconds;
}

FrameTimeSummary FrameTimeTelemetry::summary() const
{
    FrameTimeSummary result {
        .sampleCount = static_cast<uint32_t>(sampleCount_),
        .latestMilliseconds = latestMilliseconds_,
    };
    if (sampleCount_ == 0) {
        return result;
    }
    std::array<double, historyCapacity> sorted {};
    double total = 0.0;
    for (std::size_t index = 0; index < sampleCount_; ++index) {
        const double sample = samples_[index];
        sorted[index] = sample;
        total += sample;
    }
    std::sort(sorted.begin(), sorted.begin() + sampleCount_);
    result.averageMilliseconds = total / static_cast<double>(sampleCount_);
    const std::size_t p95Index =
        (sampleCount_ * 95 + 99) / 100 - 1;
    result.p95Milliseconds = sorted[p95Index];
    result.maximumMilliseconds = sorted[sampleCount_ - 1];
    return result;
}

} // namespace sokoban
