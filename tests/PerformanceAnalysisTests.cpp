#include "TestHarness.hpp"

#include "engine/PerformanceAnalysis.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string_view>

namespace {

sokoban::RenderPhaseTiming timing(
    double average,
    double p95,
    double p99 = 0.0,
    double deviation = 0.0)
{
    return {
        .available = true,
        .samples = 120,
        .latestMilliseconds = average,
        .averageMilliseconds = average,
        .minimumMilliseconds = average * 0.8,
        .medianMilliseconds = average,
        .p95Milliseconds = p95,
        .p99Milliseconds = p99 == 0.0 ? p95 : p99,
        .maximumMilliseconds = std::max(p95, p99),
        .standardDeviationMilliseconds = deviation,
    };
}

const sokoban::PerformanceFinding* find(
    const sokoban::PerformanceAnalysis& analysis,
    std::string_view id)
{
    const auto found = std::ranges::find(
        analysis.findings, id, &sokoban::PerformanceFinding::id);
    return found == analysis.findings.end() ? nullptr : &*found;
}

void testCpuAndSynchronizationFindingsAreRanked()
{
    sokoban::RenderStats stats;
    stats.cpuFrameTiming = timing(19.0, 23.0, 31.0, 4.5);
    stats.gpuFrameTiming = timing(7.0, 8.0);
    stats.frameFenceWaitTiming = timing(5.0, 6.0);
    stats.scenePreparationTiming = timing(4.0, 5.0);

    sokoban::CpuProfileFrame profile;
    profile.durationMilliseconds = 24.0;
    profile.hotPaths.push_back({
        .name = "Renderer.Prepare scene",
        .calls = 1,
        .inclusiveMilliseconds = 8.0,
        .exclusiveMilliseconds = 5.0,
        .maximumMilliseconds = 8.0,
    });

    const sokoban::PerformanceAnalysis analysis =
        sokoban::analyzePerformance(stats, profile, 16.667);
    CHECK(find(analysis, "cpu-frame-budget") != nullptr);
    CHECK(find(analysis, "cpu-frame-jitter") != nullptr);
    CHECK(find(analysis, "fence-wait") != nullptr);
    CHECK(find(analysis, "scene-preparation") != nullptr);
    CHECK(find(analysis, "cpu-hot-path") != nullptr);
    CHECK(!analysis.findings.empty());
    CHECK(analysis.findings.front().priority ==
        sokoban::PerformancePriority::High);
}

void testGpuPhaseAndSubmissionFindings()
{
    sokoban::RenderStats stats;
    stats.cpuFrameTiming = timing(8.0, 9.0);
    stats.gpuFrameTiming = timing(20.0, 24.0);
    stats.gpuSsaoTiming = timing(10.0, 12.0);
    stats.drawCalls = 1000;
    stats.triangles = 20'000;
    stats.imageBarriers = 120;

    const sokoban::PerformanceAnalysis analysis =
        sokoban::analyzePerformance(stats, {});
    CHECK(find(analysis, "gpu-frame-budget") != nullptr);
    CHECK(find(analysis, "gpu-ssao") != nullptr);
    CHECK(find(analysis, "draw-granularity") != nullptr);
    CHECK(find(analysis, "barrier-count") != nullptr);
}

void testExpectedGpuBackPressureIsNotReportedAsCpuWork()
{
    sokoban::RenderStats stats;
    stats.cpuFrameTiming = timing(8.2, 12.8);
    stats.gpuFrameTiming = timing(8.0, 8.7);
    stats.frameFenceWaitTiming = timing(7.35, 9.2);
    stats.gpuAtmosphereTiming = timing(4.8, 5.2);

    const sokoban::PerformanceAnalysis analysis =
        sokoban::analyzePerformance(stats, {});
    CHECK(find(analysis, "fence-wait") == nullptr);
    CHECK(find(analysis, "gpu-atmosphere") != nullptr);

    // After a GPU optimization, timestamped work can be slightly shorter
    // than the queue/fence cadence while still clearly pacing the renderer.
    stats.cpuFrameTiming = timing(8.1, 9.8);
    stats.gpuFrameTiming = timing(6.4, 7.0);
    stats.frameFenceWaitTiming = timing(7.0, 8.2);
    const sokoban::PerformanceAnalysis optimized =
        sokoban::analyzePerformance(stats, {});
    CHECK(find(optimized, "fence-wait") == nullptr);
}

void testFenceWaitBeyondGpuBackPressureRemainsActionable()
{
    sokoban::RenderStats stats;
    stats.cpuFrameTiming = timing(16.0, 18.0);
    stats.gpuFrameTiming = timing(6.0, 7.0);
    stats.frameFenceWaitTiming = timing(10.0, 12.0);

    const sokoban::PerformanceAnalysis analysis =
        sokoban::analyzePerformance(stats, {});
    CHECK(find(analysis, "fence-wait") != nullptr);
}

void testFifoPresentationPacingIsNotReportedAsCpuWork()
{
    sokoban::RenderStats stats;
    stats.fifoPresentationEnabled = true;
    stats.cpuFrameTiming = timing(8.0, 8.7);
    stats.gpuFrameTiming = timing(4.8, 5.0);
    stats.frameFenceWaitTiming = timing(7.3, 7.9);

    const sokoban::PerformanceAnalysis analysis =
        sokoban::analyzePerformance(stats, {});
    CHECK(find(analysis, "fence-wait") == nullptr);

    // The same unexplained wait remains visible in a non-FIFO diagnostic run.
    stats.fifoPresentationEnabled = false;
    const sokoban::PerformanceAnalysis uncapped =
        sokoban::analyzePerformance(stats, {});
    CHECK(find(uncapped, "fence-wait") != nullptr);
}

void testCacheAndMemoryFindings()
{
    sokoban::RenderStats stats;
    stats.reusedRenderableBounds = 100;
    stats.rebuiltRenderableBounds = 900;
    stats.gpuMemoryHeapCount = 1;
    stats.gpuMemoryHeaps[0] = {
        .deviceLocal = true,
        .blockBytes = 900,
        .allocationBytes = 850,
        .usageBytes = 900ULL * 1024ULL * 1024ULL,
        .budgetBytes = 1000ULL * 1024ULL * 1024ULL,
    };
    stats.gpuMemoryBlockBytes = 512ULL * 1024ULL * 1024ULL;
    stats.gpuMemoryAllocationBytes = 256ULL * 1024ULL * 1024ULL;
    stats.gpuMemoryPeakAllocationBytes = 128ULL * 1024ULL * 1024ULL;
    stats.gpuMemoryTotalAllocatedBytes = 768ULL * 1024ULL * 1024ULL;
    stats.gpuLifetimeAllocations = 2'000;

    const sokoban::PerformanceAnalysis analysis =
        sokoban::analyzePerformance(stats, {});
    CHECK(find(analysis, "bounds-cache-reuse") != nullptr);
    CHECK(find(analysis, "gpu-memory-pressure") != nullptr);
    CHECK(find(analysis, "gpu-memory-slack") != nullptr);
    CHECK(find(analysis, "gpu-allocation-churn") != nullptr);

    std::ostringstream report;
    sokoban::writePerformanceAnalysisMarkdown(report, analysis);
    CHECK(report.str().find("Ranked optimization candidates") !=
        std::string::npos);
    CHECK(report.str().find("Next experiment") != std::string::npos);
}

void testHealthyCaptureDoesNotInventFindings()
{
    sokoban::RenderStats stats;
    stats.cpuFrameTiming = timing(5.0, 6.0);
    stats.gpuFrameTiming = timing(6.0, 7.0);
    stats.drawCalls = 40;
    stats.triangles = 25'000;

    const sokoban::PerformanceAnalysis analysis =
        sokoban::analyzePerformance(stats, {});
    CHECK(analysis.findings.empty());
}

} // namespace

int main()
{
    testCpuAndSynchronizationFindingsAreRanked();
    testGpuPhaseAndSubmissionFindings();
    testExpectedGpuBackPressureIsNotReportedAsCpuWork();
    testFenceWaitBeyondGpuBackPressureRemainsActionable();
    testFifoPresentationPacingIsNotReportedAsCpuWork();
    testCacheAndMemoryFindings();
    testHealthyCaptureDoesNotInventFindings();
    if (failures == 0) {
        std::cout << "PerformanceAnalysisTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    return 1;
}
