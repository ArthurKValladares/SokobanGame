#include "engine/render/FrameTimeTelemetry.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(double left, double right)
{
    return std::abs(left - right) < 0.000001;
}

void testSummaryAndPercentile()
{
    sokoban::FrameTimeTelemetry telemetry;
    check(!telemetry.summary().available(), "empty telemetry is unavailable");
    for (int milliseconds = 1; milliseconds <= 20; ++milliseconds) {
        telemetry.record(static_cast<double>(milliseconds));
    }
    const sokoban::FrameTimeSummary summary = telemetry.summary();
    check(summary.sampleCount == 20, "records every valid sample");
    check(near(summary.latestMilliseconds, 20.0), "retains the latest sample");
    check(near(summary.averageMilliseconds, 10.5), "reports arithmetic mean");
    check(near(summary.p95Milliseconds, 19.0), "reports nearest-rank p95");
    check(near(summary.maximumMilliseconds, 20.0), "reports worst frame");
}

void testRollingHistoryAndInvalidSamples()
{
    sokoban::FrameTimeTelemetry telemetry;
    telemetry.record(-1.0);
    telemetry.record(std::numeric_limits<double>::infinity());
    check(!telemetry.summary().available(), "rejects invalid frame durations");
    for (std::size_t index = 0;
         index < sokoban::FrameTimeTelemetry::historyCapacity + 1;
         ++index) {
        telemetry.record(static_cast<double>(index));
    }
    const sokoban::FrameTimeSummary summary = telemetry.summary();
    check(summary.sampleCount == sokoban::FrameTimeTelemetry::historyCapacity,
        "history stays bounded");
    check(near(summary.latestMilliseconds,
            static_cast<double>(sokoban::FrameTimeTelemetry::historyCapacity)),
        "rolling history retains newest sample");
    check(near(summary.maximumMilliseconds,
            static_cast<double>(sokoban::FrameTimeTelemetry::historyCapacity)),
        "rolling history drops the overwritten oldest sample");
}

} // namespace

int main()
{
    testSummaryAndPercentile();
    testRollingHistoryAndInvalidSamples();
    if (failures == 0) {
        std::cout << "FrameTimeTelemetryTests: passed\n";
        return 0;
    }
    return 1;
}
