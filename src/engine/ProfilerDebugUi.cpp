#include "engine/ProfilerDebugUi.hpp"

#include "engine/Profiler.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanRenderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace sokoban {
namespace {

constexpr std::size_t historyCapacity = 240;
constexpr double bytesPerMiB = 1024.0 * 1024.0;

float toMiB(uint64_t bytes)
{
    return static_cast<float>(static_cast<double>(bytes) / bytesPerMiB);
}

void appendBounded(std::vector<float>& values, float value)
{
    if (values.size() == historyCapacity) {
        std::move(values.begin() + 1, values.end(), values.begin());
        values.back() = value;
    } else {
        values.push_back(value);
    }
}

ImU32 profileColor(const std::string& name, float alpha = 1.0f)
{
    const std::size_t hash = std::hash<std::string> {}(name);
    const float hue = static_cast<float>(hash % 360U) / 360.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.92f, red, green, blue);
    return ImGui::ColorConvertFloat4ToU32({ red, green, blue, alpha });
}

void drawTimingHistory(
    const std::vector<float>& total,
    const std::vector<float>& renderer,
    const std::vector<float>& gpu,
    float budget)
{
    const ImVec2 size {
        std::max(260.0f, ImGui::GetContentRegionAvail().x), 150.0f };
    ImGui::InvisibleButton("Timing history", size);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        minimum, maximum, ImGui::GetColorU32(ImGuiCol_FrameBg));
    draw->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border));

    float scale = std::max(1.0f, budget * 1.5f);
    for (const std::vector<float>* values : { &total, &renderer, &gpu }) {
        for (float value : *values) {
            if (std::isfinite(value)) {
                scale = std::max(scale, value * 1.1f);
            }
        }
    }
    const auto point = [&](std::size_t index, float value, std::size_t count) {
        const float x = count <= 1
            ? minimum.x
            : minimum.x + static_cast<float>(index) /
                  static_cast<float>(count - 1) * size.x;
        const float y = maximum.y - std::clamp(value / scale, 0.0f, 1.0f) * size.y;
        return ImVec2 { x, y };
    };
    const float budgetY = point(0, budget, 1).y;
    draw->AddLine(
        { minimum.x, budgetY },
        { maximum.x, budgetY },
        IM_COL32(240, 185, 70, 190));

    const auto line = [&](const std::vector<float>& values, ImU32 color) {
        for (std::size_t index = 1; index < values.size(); ++index) {
            if (std::isfinite(values[index - 1]) && std::isfinite(values[index])) {
                draw->AddLine(
                    point(index - 1, values[index - 1], values.size()),
                    point(index, values[index], values.size()),
                    color,
                    1.8f);
            }
        }
    };
    line(total, IM_COL32(105, 205, 255, 255));
    line(renderer, IM_COL32(120, 235, 145, 255));
    line(gpu, IM_COL32(210, 125, 255, 255));
    draw->AddText(
        { minimum.x + 6.0f, minimum.y + 5.0f },
        IM_COL32(200, 200, 200, 255),
        (std::to_string(static_cast<int>(std::ceil(scale))) + " ms").c_str());

    if (ImGui::IsItemHovered() && !total.empty()) {
        const float fraction = std::clamp(
            (ImGui::GetIO().MousePos.x - minimum.x) / size.x, 0.0f, 1.0f);
        const std::size_t index = std::min(
            total.size() - 1,
            static_cast<std::size_t>(fraction * static_cast<float>(total.size())));
        ImGui::SetTooltip(
            "Frame -%zu\nTotal CPU %.3f ms\nRenderer CPU %.3f ms\nGPU %.3f ms",
            total.size() - 1 - index,
            total[index],
            renderer[index],
            gpu[index]);
    }
    ImGui::TextColored(
        ImVec4(0.41f, 0.80f, 1.0f, 1.0f), "Total CPU");
    ImGui::SameLine();
    ImGui::TextColored(
        ImVec4(0.47f, 0.92f, 0.57f, 1.0f), "Renderer CPU");
    ImGui::SameLine();
    ImGui::TextColored(
        ImVec4(0.82f, 0.49f, 1.0f, 1.0f), "GPU");
    ImGui::SameLine();
    ImGui::TextColored(
        ImVec4(0.94f, 0.72f, 0.27f, 1.0f), "Budget %.2f ms", budget);
}

void drawFlameChart(const CpuProfileFrame& frame)
{
    if (frame.events.empty()) {
        ImGui::TextDisabled("CPU timeline is waiting for a captured frame.");
        return;
    }
    std::map<uint32_t, uint16_t> maximumDepth;
    for (const CpuProfileEvent& event : frame.events) {
        maximumDepth[event.threadIndex] = std::max(
            maximumDepth[event.threadIndex], event.depth);
    }
    std::map<uint32_t, float> threadY;
    constexpr float rowHeight = 19.0f;
    constexpr float threadGap = 8.0f;
    float height = 0.0f;
    for (const CpuProfileThread& thread : frame.threads) {
        if (!maximumDepth.contains(thread.index)) {
            continue;
        }
        threadY[thread.index] = height;
        height += (maximumDepth[thread.index] + 1U) * rowHeight + threadGap;
    }
    height = std::clamp(height, 80.0f, 420.0f);
    const float width = std::max(280.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("CPU timeline", { width, height });
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(minimum, maximum, true);
    draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_FrameBg));
    const double timelineMilliseconds = std::max(0.001, frame.durationMilliseconds);
    for (int division = 1; division < 4; ++division) {
        const float x = minimum.x + width * static_cast<float>(division) / 4.0f;
        draw->AddLine(
            { x, minimum.y }, { x, maximum.y },
            ImGui::GetColorU32(ImGuiCol_Border, 0.45f));
    }

    const CpuProfileEvent* hovered = nullptr;
    for (const CpuProfileEvent& event : frame.events) {
        const auto found = threadY.find(event.threadIndex);
        if (found == threadY.end()) {
            continue;
        }
        const float x0 = minimum.x + static_cast<float>(
            std::max(0.0, event.startMilliseconds) /
            timelineMilliseconds * width);
        const float x1 = minimum.x + static_cast<float>(
            std::max(0.0, event.startMilliseconds + event.durationMilliseconds) /
            timelineMilliseconds * width);
        const float y0 = minimum.y + found->second +
            static_cast<float>(event.depth) * rowHeight;
        const ImVec2 eventMin { x0, y0 };
        const ImVec2 eventMax { std::max(x0 + 1.0f, x1), y0 + rowHeight - 2.0f };
        draw->AddRectFilled(eventMin, eventMax, profileColor(event.name), 2.0f);
        if (eventMax.x - eventMin.x > 34.0f) {
            draw->AddText(
                { eventMin.x + 3.0f, eventMin.y + 2.0f },
                IM_COL32(20, 24, 28, 255), event.name.c_str());
        }
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x >= eventMin.x && mouse.x <= eventMax.x &&
            mouse.y >= eventMin.y && mouse.y <= eventMax.y) {
            hovered = &event;
        }
    }
    draw->PopClipRect();
    draw->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border));
    if (hovered && ImGui::IsItemHovered()) {
        const auto thread = std::ranges::find_if(
            frame.threads,
            [&](const CpuProfileThread& value) {
                return value.index == hovered->threadIndex;
            });
        ImGui::SetTooltip(
            "%s\n%s, depth %u\nStart %.3f ms\nDuration %.3f ms",
            hovered->name.c_str(),
            thread != frame.threads.end() ? thread->name.c_str() : "Thread",
            static_cast<unsigned>(hovered->depth),
            hovered->startMilliseconds,
            hovered->durationMilliseconds);
    }
}

void drawHotPaths(const CpuProfileFrame& frame)
{
    if (frame.hotPaths.empty()) {
        ImGui::TextDisabled("No CPU scopes in the latest frame.");
        return;
    }
    if (!ImGui::BeginTable(
            "Hot paths",
            5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_ScrollY,
            { 0.0f, 260.0f })) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn("Exclusive", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Inclusive", ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 68.0f);
    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableHeadersRow();
    const double denominator = std::max(0.001, frame.durationMilliseconds);
    for (const CpuHotPath& path : frame.hotPaths) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(path.name.c_str());
        ImGui::TableSetColumnIndex(1);
        const float fraction = static_cast<float>(
            std::clamp(path.exclusiveMilliseconds / denominator, 0.0, 1.0));
        const std::string overlay =
            std::to_string(path.exclusiveMilliseconds).substr(0, 5) + " ms";
        ImGui::ProgressBar(fraction, { -1.0f, 0.0f }, overlay.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.3f ms", path.inclusiveMilliseconds);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.3f", path.maximumMilliseconds);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%llu", static_cast<unsigned long long>(path.calls));
    }
    ImGui::EndTable();
}

struct PhaseRow {
    const char* name;
    const RenderPhaseTiming* timing;
};

void drawPhaseBars(
    const char* tableId,
    const std::vector<PhaseRow>& phases,
    double frameMilliseconds)
{
    if (!ImGui::BeginTable(
            tableId,
            4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("Latest", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("P95", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("Worst", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableHeadersRow();
    for (const PhaseRow& row : phases) {
        if (!row.timing->available) {
            continue;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(row.name);
        ImGui::TableSetColumnIndex(1);
        const float fraction = static_cast<float>(std::clamp(
            row.timing->latestMilliseconds / std::max(0.001, frameMilliseconds),
            0.0,
            1.0));
        char label[32] {};
        std::snprintf(
            label, sizeof(label), "%.3f ms", row.timing->latestMilliseconds);
        ImGui::ProgressBar(fraction, { -1.0f, 0.0f }, label);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.3f", row.timing->p95Milliseconds);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.3f", row.timing->maximumMilliseconds);
    }
    ImGui::EndTable();
}

void drawMemoryHistory(
    const std::vector<float>& process,
    const std::vector<float>& gpu)
{
    if (!process.empty()) {
        ImGui::PlotLines(
            "Process resident MiB",
            process.data(),
            static_cast<int>(process.size()),
            0,
            nullptr,
            0.0f,
            std::numeric_limits<float>::max(),
            { 0.0f, 72.0f });
    }
    if (!gpu.empty()) {
        ImGui::PlotLines(
            "GPU allocations MiB",
            gpu.data(),
            static_cast<int>(gpu.size()),
            0,
            nullptr,
            0.0f,
            std::numeric_limits<float>::max(),
            { 0.0f, 72.0f });
    }
}

} // namespace

void ProfilerDebugUi::appendHistory(
    uint64_t frameIndex,
    float totalCpu,
    float rendererCpu,
    float gpu,
    float processResidentMiB,
    float gpuAllocatedMiB,
    uint64_t totalAllocatedBytes,
    uint64_t totalFreedBytes)
{
    if (frameIndex == 0 || frameIndex == lastHistoryFrame_) {
        return;
    }
    lastHistoryFrame_ = frameIndex;
    recentAllocatedBytes_ = previousTotalAllocatedBytes_ == 0
        ? 0
        : totalAllocatedBytes - previousTotalAllocatedBytes_;
    recentFreedBytes_ = previousTotalFreedBytes_ == 0
        ? 0
        : totalFreedBytes - previousTotalFreedBytes_;
    previousTotalAllocatedBytes_ = totalAllocatedBytes;
    previousTotalFreedBytes_ = totalFreedBytes;
    appendBounded(totalCpuHistory_, totalCpu);
    appendBounded(rendererCpuHistory_, rendererCpu);
    appendBounded(gpuHistory_, gpu);
    appendBounded(processMemoryHistory_, processResidentMiB);
    appendBounded(gpuMemoryHistory_, gpuAllocatedMiB);
}

void ProfilerDebugUi::draw(const VulkanRenderer& renderer)
{
    CpuProfiler& profiler = CpuProfiler::instance();
    const CpuProfileFrame frame = profiler.latestFrame();
    const RenderStats stats = renderer.renderStats();
    if (!profiler.paused()) {
        appendHistory(
            stats.frameIndex,
            static_cast<float>(frame.frameIndex == stats.frameIndex
                ? frame.durationMilliseconds
                : std::numeric_limits<float>::quiet_NaN()),
            static_cast<float>(stats.cpuFrameTiming.available
                ? stats.cpuFrameTiming.latestMilliseconds
                : std::numeric_limits<float>::quiet_NaN()),
            static_cast<float>(stats.gpuFrameTiming.available
                ? stats.gpuFrameTiming.latestMilliseconds
                : std::numeric_limits<float>::quiet_NaN()),
            toMiB(stats.processResidentBytes),
            toMiB(stats.gpuMemoryAllocationBytes),
            stats.gpuMemoryTotalAllocatedBytes,
            stats.gpuMemoryTotalFreedBytes);
    }

    bool enabled = profiler.enabled();
    if (ImGui::Checkbox("CPU instrumentation", &enabled)) {
        profiler.setEnabled(enabled);
    }
    ImGui::SameLine();
    bool paused = profiler.paused();
    if (ImGui::Checkbox("Pause capture", &paused)) {
        profiler.setPaused(paused);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear history")) {
        profiler.clearCapture();
        totalCpuHistory_.clear();
        rendererCpuHistory_.clear();
        gpuHistory_.clear();
        processMemoryHistory_.clear();
        gpuMemoryHistory_.clear();
        lastHistoryFrame_ = 0;
        previousTotalAllocatedBytes_ = 0;
        previousTotalFreedBytes_ = 0;
        recentAllocatedBytes_ = 0;
        recentFreedBytes_ = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Chrome trace")) {
        const std::filesystem::path path =
            std::filesystem::current_path() / "profiling" / "cpu-trace.json";
        std::string error;
        exportStatus_ = profiler.exportChromeTrace(path, &error)
            ? "Exported " + path.string()
            : error;
    }
    if (!exportStatus_.empty()) {
        ImGui::TextWrapped("%s", exportStatus_.c_str());
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat(
        "Frame budget",
        &frameBudgetMilliseconds_,
        4.0f,
        50.0f,
        "%.2f ms",
        ImGuiSliderFlags_AlwaysClamp);

    if (frame.frameIndex != 0) {
        ImGui::Text(
            "Frame %llu: %.3f ms total CPU, %zu events, %zu threads%s",
            static_cast<unsigned long long>(frame.frameIndex),
            frame.durationMilliseconds,
            frame.events.size(),
            frame.threads.size(),
            frame.droppedEvents ? " (events dropped)" : "");
    }
    if (stats.cpuFrameTiming.available) {
        ImGui::Text(
            "Renderer CPU median %.3f, p95 %.3f, p99 %.3f ms (jitter %.3f ms)",
            stats.cpuFrameTiming.medianMilliseconds,
            stats.cpuFrameTiming.p95Milliseconds,
            stats.cpuFrameTiming.p99Milliseconds,
            stats.cpuFrameTiming.standardDeviationMilliseconds);
    }
    if (stats.gpuFrameTiming.available) {
        ImGui::Text(
            "GPU median %.3f, p95 %.3f, p99 %.3f ms (jitter %.3f ms)",
            stats.gpuFrameTiming.medianMilliseconds,
            stats.gpuFrameTiming.p95Milliseconds,
            stats.gpuFrameTiming.p99Milliseconds,
            stats.gpuFrameTiming.standardDeviationMilliseconds);
    }
    ImGui::Text(
        "%u draws, %u triangles, %u pipeline binds, %u barriers",
        stats.drawCalls,
        stats.triangles,
        stats.pipelineBinds,
        stats.imageBarriers);
    drawTimingHistory(
        totalCpuHistory_, rendererCpuHistory_, gpuHistory_,
        frameBudgetMilliseconds_);

    if (ImGui::CollapsingHeader(
            "CPU Timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawFlameChart(frame);
    }
    if (ImGui::CollapsingHeader(
            "CPU Hot Paths", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawHotPaths(frame);
    }

    if (ImGui::CollapsingHeader(
            "Renderer CPU Breakdown", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::vector<PhaseRow> phases {
            { "Asset scheduling", &stats.assetSchedulingTiming },
            { "Fence wait", &stats.frameFenceWaitTiming },
            { "Asset maintenance", &stats.assetMaintenanceTiming },
            { "Image acquisition", &stats.imageAcquisitionTiming },
            { "Command recording", &stats.commandRecordingTiming },
            { "  Recorder setup", &stats.recorderSetupTiming },
            { "  Shadows", &stats.shadowCommandRecordingTiming },
            { "  Scene", &stats.sceneCommandRecordingTiming },
            { "  SSAO", &stats.ssaoCommandRecordingTiming },
            { "  Atmosphere", &stats.atmosphereCommandRecordingTiming },
            { "  Preview", &stats.previewCommandRecordingTiming },
            { "  Output/UI", &stats.outputCommandRecordingTiming },
            { "Submit/present", &stats.submitPresentTiming },
        };
        drawPhaseBars(
            "Renderer CPU phases",
            phases,
            stats.cpuFrameTiming.available
                ? stats.cpuFrameTiming.latestMilliseconds
                : frame.durationMilliseconds);
    }

    if (ImGui::CollapsingHeader(
            "GPU Breakdown", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!stats.gpuTimestampsSupported) {
            ImGui::TextDisabled(
                "This graphics queue does not expose Vulkan timestamps.");
        } else if (!stats.gpuFrameTiming.available) {
            ImGui::TextDisabled("GPU timestamp results are pending.");
        } else {
            const std::vector<PhaseRow> phases {
                { "Shadows", &stats.gpuShadowTiming },
                { "Scene", &stats.gpuSceneTiming },
                { "  Raster/resolve", &stats.gpuSceneRasterTiming },
                { "  Depth publish", &stats.gpuSceneDepthPublishTiming },
                { "  Translucency", &stats.gpuSceneTranslucencyTiming },
                { "SSAO", &stats.gpuSsaoTiming },
                { "  Scene snapshot", &stats.gpuSsaoSnapshotTiming },
                { "  Occlusion", &stats.gpuSsaoOcclusionTiming },
                { "  Composite", &stats.gpuSsaoCompositeTiming },
                { "Atmosphere", &stats.gpuAtmosphereTiming },
                { "Output/UI", &stats.gpuOutputTiming },
            };
            drawPhaseBars(
                "GPU phases", phases,
                stats.gpuFrameTiming.latestMilliseconds);
        }
    }

    if (ImGui::CollapsingHeader(
            "Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (stats.processMemoryAvailable) {
            ImGui::Text(
                "Process resident %.1f MiB (peak %.1f), private/virtual %.1f MiB",
                toMiB(stats.processResidentBytes),
                toMiB(stats.processPeakResidentBytes),
                toMiB(stats.processPrivateBytes));
        } else {
            ImGui::TextDisabled("Process memory counters unavailable.");
        }
        ImGui::Text(
            "GPU allocations %.1f MiB (peak %.1f) in %u allocations / %u blocks",
            toMiB(stats.gpuMemoryAllocationBytes),
            toMiB(stats.gpuMemoryPeakAllocationBytes),
            stats.gpuMemoryAllocationCount,
            stats.gpuMemoryBlockCount);
        const uint64_t unusedBlockBytes =
            stats.gpuMemoryBlockBytes > stats.gpuMemoryAllocationBytes
            ? stats.gpuMemoryBlockBytes - stats.gpuMemoryAllocationBytes
            : 0;
        ImGui::Text(
            "VMA blocks %.1f MiB; %.1f MiB uncommitted within blocks",
            toMiB(stats.gpuMemoryBlockBytes),
            toMiB(unusedBlockBytes));
        ImGui::Text(
            "Images %.1f MiB / %u, buffers %.1f MiB / %u",
            toMiB(stats.gpuImageBytes),
            stats.gpuImageAllocationCount,
            toMiB(stats.gpuBufferBytes),
            stats.gpuBufferAllocationCount);
        ImGui::Text(
            "Device-local %.1f MiB, host-visible %.1f MiB",
            toMiB(stats.gpuDeviceLocalBytes),
            toMiB(stats.gpuHostVisibleBytes));
        ImGui::Text(
            "Allocation churn %.1f MiB allocated / %.1f MiB freed; %llu / %llu operations",
            toMiB(stats.gpuMemoryTotalAllocatedBytes),
            toMiB(stats.gpuMemoryTotalFreedBytes),
            static_cast<unsigned long long>(stats.gpuLifetimeAllocations),
            static_cast<unsigned long long>(stats.gpuLifetimeFrees));
        ImGui::Text(
            "Latest sample churn +%.2f / -%.2f MiB",
            toMiB(recentAllocatedBytes_),
            toMiB(recentFreedBytes_));
        if (stats.gpuMemoryHeapCount != 0 && ImGui::BeginTable(
                "Vulkan heaps",
                5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Heap");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Usage");
            ImGui::TableSetupColumn("Budget");
            ImGui::TableSetupColumn("Pressure");
            ImGui::TableHeadersRow();
            for (uint32_t index = 0;
                 index < stats.gpuMemoryHeapCount;
                 ++index) {
                const RenderMemoryHeapStats& heap =
                    stats.gpuMemoryHeaps[index];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", index);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(
                    heap.deviceLocal ? "Device local" : "Host");
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.1f MiB", toMiB(heap.usageBytes));
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.1f MiB", toMiB(heap.budgetBytes));
                ImGui::TableSetColumnIndex(4);
                const float pressure = heap.budgetBytes == 0
                    ? 0.0f
                    : static_cast<float>(
                          static_cast<double>(heap.usageBytes) /
                          static_cast<double>(heap.budgetBytes));
                char pressureLabel[24] {};
                std::snprintf(
                    pressureLabel,
                    sizeof(pressureLabel),
                    "%.1f%%",
                    static_cast<double>(pressure) * 100.0);
                ImGui::ProgressBar(
                    std::clamp(pressure, 0.0f, 1.0f),
                    { -1.0f, 0.0f },
                    pressureLabel);
            }
            ImGui::EndTable();
        }
        drawMemoryHistory(processMemoryHistory_, gpuMemoryHistory_);
    }
}

} // namespace sokoban
