#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sokoban {

class VulkanRenderer;

class ProfilerDebugUi {
public:
    void draw(const VulkanRenderer& renderer);

private:
    void appendHistory(
        uint64_t frameIndex,
        float totalCpu,
        float rendererCpu,
        float gpu,
        float processResidentMiB,
        float gpuAllocatedMiB,
        uint64_t totalAllocatedBytes,
        uint64_t totalFreedBytes);

    uint64_t lastHistoryFrame_ = 0;
    std::vector<float> totalCpuHistory_;
    std::vector<float> rendererCpuHistory_;
    std::vector<float> gpuHistory_;
    std::vector<float> processMemoryHistory_;
    std::vector<float> gpuMemoryHistory_;
    float frameBudgetMilliseconds_ = 16.667f;
    uint64_t previousTotalAllocatedBytes_ = 0;
    uint64_t previousTotalFreedBytes_ = 0;
    uint64_t recentAllocatedBytes_ = 0;
    uint64_t recentFreedBytes_ = 0;
    std::string exportStatus_;
};

} // namespace sokoban
