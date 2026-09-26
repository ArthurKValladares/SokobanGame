#pragma once

#include "engine/Profiler.hpp"
#include "engine/render/RenderTypes.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace sokoban {

enum class PerformancePriority {
    Low,
    Medium,
    High,
};

struct PerformanceFinding {
    std::string id;
    std::string title;
    std::string evidence;
    std::string recommendation;
    PerformancePriority priority = PerformancePriority::Low;
    double score = 0.0;
};

struct PerformanceAnalysis {
    double frameBudgetMilliseconds = 16.667;
    std::vector<PerformanceFinding> findings;
};

// Converts the profiler's raw counters and timings into a short, ranked list
// of optimization candidates. The rules intentionally diagnose rather than
// gate: hardware-dependent measurements belong in evidence reports, while
// deterministic unit tests pin the classification logic.
[[nodiscard]] PerformanceAnalysis analyzePerformance(
    const RenderStats& stats,
    const CpuProfileFrame& cpuFrame,
    double frameBudgetMilliseconds = 16.667);

[[nodiscard]] const char* performancePriorityName(
    PerformancePriority priority) noexcept;

void writePerformanceAnalysisMarkdown(
    std::ostream& output,
    const PerformanceAnalysis& analysis);

} // namespace sokoban
