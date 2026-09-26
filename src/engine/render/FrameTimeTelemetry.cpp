#include "engine/render/FrameTimeTelemetry.hpp"

#include <algorithm>
#include <cmath>
#include <span>

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
    std::ranges::sort(std::span(sorted).first(sampleCount_));
    result.averageMilliseconds = total / static_cast<double>(sampleCount_);
    result.minimumMilliseconds = sorted[0];
    const std::size_t medianIndex = (sampleCount_ - 1) / 2;
    result.medianMilliseconds = sampleCount_ % 2 == 0
        ? (sorted[medianIndex] + sorted[medianIndex + 1]) * 0.5
        : sorted[medianIndex];
    const std::size_t p95Index =
        (sampleCount_ * 95 + 99) / 100 - 1;
    result.p95Milliseconds = sorted[p95Index];
    const std::size_t p99Index =
        (sampleCount_ * 99 + 99) / 100 - 1;
    result.p99Milliseconds = sorted[p99Index];
    result.maximumMilliseconds = sorted[sampleCount_ - 1];
    double squaredDifferenceTotal = 0.0;
    for (std::size_t index = 0; index < sampleCount_; ++index) {
        const double difference =
            samples_[index] - result.averageMilliseconds;
        squaredDifferenceTotal += difference * difference;
    }
    result.standardDeviationMilliseconds = std::sqrt(
        squaredDifferenceTotal / static_cast<double>(sampleCount_));
    return result;
}

} // namespace sokoban
