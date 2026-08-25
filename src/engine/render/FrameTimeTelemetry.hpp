#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sokoban {

struct FrameTimeSummary {
    uint32_t sampleCount = 0;
    double latestMilliseconds = 0.0;
    double averageMilliseconds = 0.0;
    double p95Milliseconds = 0.0;
    double maximumMilliseconds = 0.0;

    [[nodiscard]] bool available() const { return sampleCount != 0; }
};

// Fixed-size frame-time history suitable for the render thread. It avoids
// allocations while still exposing enough context to distinguish a sustained
// regression from a one-off hitch.
class FrameTimeTelemetry {
public:
    static constexpr std::size_t historyCapacity = 120;

    void reset() noexcept;
    void record(double milliseconds) noexcept;
    [[nodiscard]] FrameTimeSummary summary() const;

private:
    std::array<double, historyCapacity> samples_ {};
    std::size_t sampleCount_ = 0;
    std::size_t nextSample_ = 0;
    double latestMilliseconds_ = 0.0;
};

} // namespace sokoban
