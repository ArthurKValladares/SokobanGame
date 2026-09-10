#include "TestHarness.hpp"

#include "engine/render/FrameTimeTelemetry.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

bool near(double left, double right)
{
    return std::abs(left - right) < 0.000001;
}

void testSummaryAndPercentile()
{
    sokoban::FrameTimeTelemetry telemetry;
    CHECK_MESSAGE(!telemetry.summary().available(), "empty telemetry is unavailable");
    for (int milliseconds = 1; milliseconds <= 20; ++milliseconds) {
        telemetry.record(static_cast<double>(milliseconds));
    }
    const sokoban::FrameTimeSummary summary = telemetry.summary();
    CHECK_MESSAGE(summary.sampleCount == 20, "records every valid sample");
    CHECK_MESSAGE(near(summary.latestMilliseconds, 20.0), "retains the latest sample");
    CHECK_MESSAGE(near(summary.averageMilliseconds, 10.5), "reports arithmetic mean");
    CHECK_MESSAGE(near(summary.p95Milliseconds, 19.0), "reports nearest-rank p95");
    CHECK_MESSAGE(near(summary.maximumMilliseconds, 20.0), "reports worst frame");
}

void testRollingHistoryAndInvalidSamples()
{
    sokoban::FrameTimeTelemetry telemetry;
    telemetry.record(-1.0);
    telemetry.record(std::numeric_limits<double>::infinity());
    CHECK_MESSAGE(!telemetry.summary().available(), "rejects invalid frame durations");
    for (std::size_t index = 0;
         index < sokoban::FrameTimeTelemetry::historyCapacity + 1;
         ++index) {
        telemetry.record(static_cast<double>(index));
    }
    const sokoban::FrameTimeSummary summary = telemetry.summary();
    CHECK_MESSAGE(summary.sampleCount == sokoban::FrameTimeTelemetry::historyCapacity,
        "history stays bounded");
    CHECK_MESSAGE(near(summary.latestMilliseconds,
            static_cast<double>(sokoban::FrameTimeTelemetry::historyCapacity)),
        "rolling history retains newest sample");
    CHECK_MESSAGE(near(summary.maximumMilliseconds,
            static_cast<double>(sokoban::FrameTimeTelemetry::historyCapacity)),
        "rolling history drops the overwritten oldest sample");
}

void benchmarkSummaries()
{
    constexpr std::size_t frameCount = 20'000;
    constexpr std::size_t hiddenSummariesPerFrame = 3;
    constexpr std::size_t visibleSummariesPerFrame = 27;
    std::array<
        sokoban::FrameTimeTelemetry,
        visibleSummariesPerFrame> telemetry;
    for (std::size_t stream = 0; stream < telemetry.size(); ++stream) {
        for (std::size_t index = 0;
             index < sokoban::FrameTimeTelemetry::historyCapacity;
             ++index) {
            telemetry[stream].record(
                8.0 + static_cast<double>((index + stream) % 17) * 0.25);
        }
    }

    const auto run = [&](std::string_view label, std::size_t summariesPerFrame) {
        double checksum = 0.0;
        std::chrono::nanoseconds total {};
        std::chrono::nanoseconds maximum {};
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            for (std::size_t stream = 0;
                 stream < summariesPerFrame;
                 ++stream) {
                telemetry[stream].record(
                    8.0 + static_cast<double>((frame + stream) % 17) * 0.25);
            }
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t stream = 0;
                 stream < summariesPerFrame;
                 ++stream) {
                const sokoban::FrameTimeSummary summary =
                    telemetry[stream].summary();
                checksum += summary.p95Milliseconds;
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            total += elapsed;
            maximum = std::max(
                maximum,
                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));
        }
        const double totalMilliseconds =
            std::chrono::duration<double, std::milli>(total).count();
        std::cout << label << ": " << summariesPerFrame
                  << " summaries/frame across " << frameCount
                  << " frames; total " << totalMilliseconds
                  << " ms; average " << totalMilliseconds /
                         static_cast<double>(frameCount)
                  << " ms/frame; maximum "
                  << std::chrono::duration<double, std::milli>(maximum).count()
                  << " ms/frame; checksum " << checksum << '\n';
    };

    run("stats hidden", hiddenSummariesPerFrame);
    run("stats visible", visibleSummariesPerFrame);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
        benchmarkSummaries();
        return 0;
    }
    testSummaryAndPercentile();
    testRollingHistoryAndInvalidSamples();
    if (failures == 0) {
        std::cout << "FrameTimeTelemetryTests: passed\n";
        return 0;
    }
    return 1;
}
