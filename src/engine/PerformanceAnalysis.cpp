#include "engine/PerformanceAnalysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>

namespace sokoban {
namespace {

constexpr double bytesPerMiB = 1024.0 * 1024.0;

std::string milliseconds(double value)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << value << " ms";
    return text.str();
}

std::string percent(double fraction)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << fraction * 100.0 << '%';
    return text.str();
}

std::string mebibytes(uint64_t bytes)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(1)
         << static_cast<double>(bytes) / bytesPerMiB << " MiB";
    return text.str();
}

PerformancePriority priorityFor(double score)
{
    if (score >= 90.0) {
        return PerformancePriority::High;
    }
    if (score >= 55.0) {
        return PerformancePriority::Medium;
    }
    return PerformancePriority::Low;
}

void addFinding(
    PerformanceAnalysis& analysis,
    std::string id,
    std::string title,
    std::string evidence,
    std::string recommendation,
    double score)
{
    analysis.findings.push_back({
        .id = std::move(id),
        .title = std::move(title),
        .evidence = std::move(evidence),
        .recommendation = std::move(recommendation),
        .priority = priorityFor(score),
        .score = score,
    });
}

double representative(const RenderPhaseTiming& timing)
{
    if (!timing.available) {
        return 0.0;
    }
    return timing.p95Milliseconds > 0.0
        ? timing.p95Milliseconds
        : timing.latestMilliseconds;
}

// A frame-slot fence is also the renderer's normal GPU back-pressure point.
// Calling every such wait a CPU bottleneck sends optimization work toward
// deeper buffering, which can only move the wait and increase input latency.
// Model the steady-state wait from measured averages: GPU time minus the CPU
// work performed outside the fence. A close match means the fence is pacing
// submission behind real GPU work; a material excess remains actionable.
bool fenceWaitIsExpectedGpuBackPressure(const RenderStats& stats)
{
    if (!stats.cpuFrameTiming.available ||
        !stats.gpuFrameTiming.available ||
        !stats.frameFenceWaitTiming.available) {
        return false;
    }

    const double cpu = stats.cpuFrameTiming.averageMilliseconds;
    const double gpu = stats.gpuFrameTiming.averageMilliseconds;
    const double wait = stats.frameFenceWaitTiming.averageMilliseconds;
    if (cpu <= 0.0 || gpu <= 0.0 || wait <= 0.0) {
        return false;
    }

    const double cpuOutsideFence = std::max(0.0, cpu - wait);
    const double modeledWait = std::max(0.0, gpu - cpuOutsideFence);
    const double tolerance = std::max(0.5, gpu * 0.25);
    const bool modeledByGpuWork = modeledWait > 0.0 &&
        std::abs(wait - modeledWait) <= tolerance;
    const bool cpuAndGpuArePacedTogether = gpu >= cpu * 0.65;
    return modeledByGpuWork || cpuAndGpuArePacedTogether;
}

struct NamedTiming {
    std::string_view id;
    std::string_view title;
    const RenderPhaseTiming* timing = nullptr;
    std::string_view recommendation;
};

} // namespace

PerformanceAnalysis analyzePerformance(
    const RenderStats& stats,
    const CpuProfileFrame& cpuFrame,
    double frameBudgetMilliseconds)
{
    PerformanceAnalysis result;
    result.frameBudgetMilliseconds =
        std::max(0.1, frameBudgetMilliseconds);

    const double cpu = representative(stats.cpuFrameTiming);
    const double gpu = representative(stats.gpuFrameTiming);
    const bool cpuOverBudget = stats.cpuFrameTiming.available &&
        cpu > result.frameBudgetMilliseconds;
    const bool gpuOverBudget = stats.gpuFrameTiming.available &&
        gpu > result.frameBudgetMilliseconds;

    if (cpuOverBudget || gpuOverBudget) {
        if (cpuOverBudget && (!gpuOverBudget || cpu > gpu * 1.15)) {
            addFinding(
                result,
                "cpu-frame-budget",
                "CPU frame time exceeds the target",
                "CPU p95/latest is " + milliseconds(cpu) +
                    " against a " + milliseconds(result.frameBudgetMilliseconds) +
                    " budget" + (stats.gpuFrameTiming.available
                        ? "; GPU is " + milliseconds(gpu)
                        : "."),
                "Start with the largest exclusive CPU hot path and the largest renderer CPU phase; capture a trace before changing code.",
                100.0 + (cpu / result.frameBudgetMilliseconds - 1.0) * 35.0);
        } else if (gpuOverBudget && (!cpuOverBudget || gpu > cpu * 1.15)) {
            addFinding(
                result,
                "gpu-frame-budget",
                "GPU frame time exceeds the target",
                "GPU p95/latest is " + milliseconds(gpu) +
                    " against a " + milliseconds(result.frameBudgetMilliseconds) +
                    " budget" + (stats.cpuFrameTiming.available
                        ? "; renderer CPU is " + milliseconds(cpu)
                        : "."),
                "Optimize the dominant GPU pass first, then verify at the same resolution, render scale, and MSAA setting.",
                100.0 + (gpu / result.frameBudgetMilliseconds - 1.0) * 35.0);
        } else {
            addFinding(
                result,
                "frame-budget",
                "CPU and GPU both exceed the frame budget",
                "CPU is " + milliseconds(cpu) + " and GPU is " +
                    milliseconds(gpu) + " against a " +
                    milliseconds(result.frameBudgetMilliseconds) + " budget.",
                "Treat this as a pipeline-wide workload issue: reduce CPU submission cost and GPU shading/load independently, then remeasure overlap.",
                110.0 + std::max(cpu, gpu) /
                    result.frameBudgetMilliseconds * 10.0);
        }
    }

    if (stats.cpuFrameTiming.available) {
        const double jitter = stats.cpuFrameTiming.standardDeviationMilliseconds;
        const double average = std::max(
            0.001, stats.cpuFrameTiming.averageMilliseconds);
        const bool dispersed = jitter > average * 0.20;
        const bool longTail = stats.cpuFrameTiming.p99Milliseconds >
            std::max(result.frameBudgetMilliseconds,
                stats.cpuFrameTiming.p95Milliseconds * 1.35);
        if (dispersed || longTail) {
            addFinding(
                result,
                "cpu-frame-jitter",
                "CPU frame pacing has a long tail",
                "CPU standard deviation is " + milliseconds(jitter) +
                    ", p95 is " + milliseconds(stats.cpuFrameTiming.p95Milliseconds) +
                    ", and p99 is " + milliseconds(stats.cpuFrameTiming.p99Milliseconds) + ".",
                "Inspect the slowest trace frames for waits, asset publication, allocator growth, and uneven task chunks instead of optimizing only the average.",
                longTail ? 82.0 : 58.0);
        }
    }

    const std::array cpuPhases {
        NamedTiming {
            "fence-wait", "Frame synchronization is consuming CPU time",
            &stats.frameFenceWaitTiming,
            "Investigate frames-in-flight pressure, long GPU work, and avoidable queue-idle or fence waits." },
        NamedTiming {
            "scene-preparation", "Scene preparation is a CPU hot phase",
            &stats.scenePreparationTiming,
            "Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance." },
        NamedTiming {
            "command-recording", "Command recording is a CPU hot phase",
            &stats.commandRecordingTiming,
            "Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost." },
        NamedTiming {
            "asset-maintenance", "Asset maintenance is a CPU hot phase",
            &stats.assetMaintenanceTiming,
            "Batch publication and reclamation work and move non-critical maintenance away from latency-sensitive frames." },
        NamedTiming {
            "image-acquisition", "Swapchain acquisition is a CPU hot phase",
            &stats.imageAcquisitionTiming,
            "Correlate acquisition stalls with present mode, GPU saturation, and window-system pacing." },
    };
    const bool expectedGpuBackPressure =
        fenceWaitIsExpectedGpuBackPressure(stats);
    if (stats.cpuFrameTiming.available && cpu > 0.0) {
        for (const NamedTiming& phase : cpuPhases) {
            if (phase.id == "fence-wait" && expectedGpuBackPressure) {
                continue;
            }
            const double phaseTime = representative(*phase.timing);
            const double share = phaseTime / cpu;
            if (phase.timing->available && phaseTime >= 0.35 && share >= 0.15) {
                addFinding(
                    result,
                    std::string(phase.id),
                    std::string(phase.title),
                    std::string(phase.title) + ": " + milliseconds(phaseTime) +
                        " (" + percent(share) + " of the renderer CPU frame).",
                    std::string(phase.recommendation),
                    45.0 + share * 180.0 +
                        (cpuOverBudget ? 20.0 : 0.0));
            }
        }
    }

    const std::array gpuPhases {
        NamedTiming {
            "gpu-shadows", "Shadow rendering is the dominant GPU pass",
            &stats.gpuShadowTiming,
            "Reduce shadow caster volume, cache stable point-light faces, and tune shadow resolution before simplifying unrelated passes." },
        NamedTiming {
            "gpu-scene", "Main scene rendering is the dominant GPU pass",
            &stats.gpuSceneTiming,
            "Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture." },
        NamedTiming {
            "gpu-ssao", "SSAO is a dominant GPU pass",
            &stats.gpuSsaoTiming,
            "Measure occlusion resolution, sample count, snapshot bandwidth, and composite cost separately." },
        NamedTiming {
            "gpu-atmosphere", "Atmosphere is a dominant GPU pass",
            &stats.gpuAtmosphereTiming,
            "Reduce ray-march work or render resolution and verify quality with matched evidence captures." },
        NamedTiming {
            "gpu-output", "Output and UI are a dominant GPU pass",
            &stats.gpuOutputTiming,
            "Inspect fullscreen bandwidth, tone mapping, UI overdraw, and final target transitions." },
    };
    if (stats.gpuFrameTiming.available && gpu > 0.0) {
        for (const NamedTiming& phase : gpuPhases) {
            const double phaseTime = representative(*phase.timing);
            const double share = phaseTime / gpu;
            if (phase.timing->available && phaseTime >= 0.5 && share >= 0.22) {
                addFinding(
                    result,
                    std::string(phase.id),
                    std::string(phase.title),
                    std::string(phase.title) + ": " + milliseconds(phaseTime) +
                        " (" + percent(share) + " of the GPU frame).",
                    std::string(phase.recommendation),
                    48.0 + share * 160.0 +
                        (gpuOverBudget ? 20.0 : 0.0));
            }
        }
    }

    if (stats.drawCalls >= 256 && stats.triangles != 0) {
        const double trianglesPerDraw = static_cast<double>(stats.triangles) /
            static_cast<double>(stats.drawCalls);
        if (trianglesPerDraw < 64.0) {
            std::ostringstream evidence;
            evidence << stats.drawCalls << " draws submit " << stats.triangles
                     << " triangles (" << std::fixed << std::setprecision(1)
                     << trianglesPerDraw << " triangles/draw).";
            addFinding(
                result,
                "draw-granularity",
                "Draw submission is unusually fine grained",
                evidence.str(),
                "Prioritize instancing, compatible material sorting, and eliminating tiny or duplicate submissions; verify that draw count and command-recording time fall together.",
                62.0 + std::min(40.0,
                    static_cast<double>(stats.drawCalls) / 100.0));
        }
    }

    const uint64_t boundsTotal = static_cast<uint64_t>(
        stats.reusedRenderableBounds) + stats.rebuiltRenderableBounds;
    if (boundsTotal >= 256) {
        const double reuse = static_cast<double>(stats.reusedRenderableBounds) /
            static_cast<double>(boundsTotal);
        if (reuse < 0.75) {
            addFinding(
                result,
                "bounds-cache-reuse",
                "Renderable-bounds cache reuse is low",
                std::to_string(stats.reusedRenderableBounds) + " of " +
                    std::to_string(boundsTotal) + " bounds were reused (" +
                    percent(reuse) + ").",
                "Keep stable renderable identities and bounds revisions across frames; investigate systems that rebuild unchanged presentation data.",
                46.0 + (0.75 - reuse) * 100.0);
        }
    }

    if (stats.imageBarriers >= 96) {
        addFinding(
            result,
            "barrier-count",
            "The frame records many image barriers",
            std::to_string(stats.imageBarriers) +
                " image barriers were recorded in one frame.",
            "Use a GPU capture to identify redundant transitions and opportunities to keep resources in a compatible layout across adjacent passes.",
            48.0 + std::min(35.0,
                static_cast<double>(stats.imageBarriers - 96) / 4.0));
    }

    for (uint32_t index = 0; index < stats.gpuMemoryHeapCount; ++index) {
        const RenderMemoryHeapStats& heap = stats.gpuMemoryHeaps[index];
        if (heap.budgetBytes == 0) {
            continue;
        }
        const double pressure = static_cast<double>(heap.usageBytes) /
            static_cast<double>(heap.budgetBytes);
        if (pressure >= 0.85) {
            addFinding(
                result,
                "gpu-memory-pressure",
                "A Vulkan memory heap is close to its budget",
                "Heap " + std::to_string(index) + " uses " +
                    mebibytes(heap.usageBytes) + " of " +
                    mebibytes(heap.budgetBytes) + " (" +
                    percent(pressure) + ").",
                "Reduce resident textures and render targets, tighten streaming residency, and remeasure peak usage during scene transitions.",
                75.0 + (pressure - 0.85) * 220.0);
        }
    }

    if (stats.gpuMemoryBlockBytes > 0 &&
        stats.gpuMemoryBlockBytes > stats.gpuMemoryAllocationBytes) {
        const uint64_t unused =
            stats.gpuMemoryBlockBytes - stats.gpuMemoryAllocationBytes;
        const double unusedShare = static_cast<double>(unused) /
            static_cast<double>(stats.gpuMemoryBlockBytes);
        if (unused >= 64ULL * 1024ULL * 1024ULL && unusedShare >= 0.25) {
            addFinding(
                result,
                "gpu-memory-slack",
                "GPU allocator blocks contain substantial unused space",
                mebibytes(unused) + " of " +
                    mebibytes(stats.gpuMemoryBlockBytes) + " is uncommitted (" +
                    percent(unusedShare) + ").",
                "Inspect VMA block sizing and pool segregation; distinguish healthy retained capacity from fragmentation with repeated load/unload captures.",
                52.0 + unusedShare * 75.0);
        }
    }

    if (stats.gpuLifetimeAllocations >= 1000 &&
        stats.gpuMemoryPeakAllocationBytes > 0 &&
        stats.gpuMemoryTotalAllocatedBytes /
                stats.gpuMemoryPeakAllocationBytes >= 4) {
        addFinding(
            result,
            "gpu-allocation-churn",
            "GPU allocation lifetime traffic is high",
            mebibytes(stats.gpuMemoryTotalAllocatedBytes) + " was allocated across " +
                std::to_string(stats.gpuLifetimeAllocations) +
                " operations versus a " +
                mebibytes(stats.gpuMemoryPeakAllocationBytes) + " peak.",
            "Reuse transient buffers and images, pool same-shaped resources, and correlate churn deltas with asset publication and renderer reconfiguration.",
            64.0 + std::min(30.0,
                static_cast<double>(stats.gpuMemoryTotalAllocatedBytes) /
                    static_cast<double>(stats.gpuMemoryPeakAllocationBytes)));
    }

    if (!cpuFrame.hotPaths.empty() && cpuFrame.durationMilliseconds > 0.0) {
        const auto hot = std::ranges::max_element(
            cpuFrame.hotPaths,
            {},
            &CpuHotPath::exclusiveMilliseconds);
        const double share = hot->exclusiveMilliseconds /
            cpuFrame.durationMilliseconds;
        if (hot->exclusiveMilliseconds >= 0.25 && share >= 0.12) {
            addFinding(
                result,
                "cpu-hot-path",
                "A CPU scope dominates exclusive frame time",
                "`" + hot->name + "` accounts for " +
                    milliseconds(hot->exclusiveMilliseconds) + " exclusive (" +
                    percent(share) + ") across " +
                    std::to_string(hot->calls) + " call(s).",
                "Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.",
                55.0 + share * 160.0);
        }
    }

    std::ranges::sort(
        result.findings,
        [](const PerformanceFinding& left, const PerformanceFinding& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.id < right.id;
        });
    return result;
}

const char* performancePriorityName(PerformancePriority priority) noexcept
{
    switch (priority) {
    case PerformancePriority::Low:
        return "low";
    case PerformancePriority::Medium:
        return "medium";
    case PerformancePriority::High:
        return "high";
    }
    return "unknown";
}

void writePerformanceAnalysisMarkdown(
    std::ostream& output,
    const PerformanceAnalysis& analysis)
{
    output << "\n## Ranked optimization candidates\n\n";
    if (analysis.findings.empty()) {
        output << "No rule-based bottleneck was detected in this capture. "
                  "Compare this baseline with a heavier representative scene "
                  "before concluding that no optimization is needed.\n";
        return;
    }
    for (std::size_t index = 0; index < analysis.findings.size(); ++index) {
        const PerformanceFinding& finding = analysis.findings[index];
        output << index + 1 << ". **" << finding.title << "** ("
               << performancePriorityName(finding.priority) << ", score "
               << std::fixed << std::setprecision(1) << finding.score << ")\n"
               << "   - Evidence: " << finding.evidence << "\n"
               << "   - Next experiment: " << finding.recommendation << "\n";
    }
}

} // namespace sokoban
