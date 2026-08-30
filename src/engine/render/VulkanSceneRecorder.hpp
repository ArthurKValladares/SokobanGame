#pragma once

#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/PointShadowFaceCache.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/ui/Ui.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace sokoban {

class VulkanModelResources;
class VulkanGpuProfiler;
class VulkanPipelineFactory;
class VulkanSceneDescriptors;
class VulkanShadowPass;
class VulkanSsaoPass;
class VulkanSwapchainResources;

// Owns Vulkan command encoding for one prepared scene. It does not own any
// Vulkan handles; the renderer supplies the resources whose lifetimes bracket
// each record() call.
class VulkanSceneRecorder {
public:
    struct Resources {
        VkDevice device = VK_NULL_HANDLE;
        VulkanGpuProfiler& gpuProfiler;
        VulkanSwapchainResources& swapchain;
        VulkanShadowPass& shadowPass;
        VulkanSsaoPass& ssaoPass;
        VulkanSceneDescriptors& sceneDescriptors;
        VulkanPipelineFactory& pipelines;
        VulkanModelResources& modelResources;
    };

    struct FrameConfiguration {
        uint32_t descriptorFrameIndex = 0;
        uint32_t activeSamples = 1;
        bool wireframeEnabled = false;
        // Back-face culling for glTF model draws. Tile quads are excluded:
        // they are already rejected on the CPU by IsoScenePreparer and their
        // winding after CPU projection is not guaranteed. Exposed as a
        // developer toggle because a model authored with inverted winding
        // disappears rather than degrading, and that is worth being able to
        // A/B in one click rather than by editing code.
        bool modelBackfaceCulling = true;
        float wireframeLineWidth = 1.0f;
        uint64_t statsFrameIndex = 0;
        uint64_t pipelineRebuilds = 0;
        uint64_t swapchainRecreations = 0;
        uint64_t swapchainRecreationDeferrals = 0;
        uint64_t renderResourceReconfigurations = 0;
        uint64_t presentQueueRetirementWaits = 0;
        uint32_t retiredRenderResourceSets = 0;
        bool rendererReconfigurationPending = false;
        bool developerWorkspaceVisible = false;
    };

    [[nodiscard]] RenderStats record(
        Resources resources,
        const FrameConfiguration& configuration,
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene,
        const RenderFrameData* previewFrameData,
        const PreparedRenderScene* previewScene,
        const UiDrawData& uiDrawData) const;

    void setPointShadowCacheEnabled(bool enabled)
    {
        pointShadowCacheEnabled_ = enabled;
        if (!enabled) {
            for (std::size_t lightIndex = 0;
                 lightIndex < RenderFrameData::pointLightCapacity;
                 ++lightIndex) {
                pointShadowFaceCache_.invalidate(lightIndex);
            }
        }
    }

private:
    bool pointShadowCacheEnabled_ = true;
    mutable PointShadowFaceCache pointShadowFaceCache_;
    mutable std::array<std::vector<PointShadowModelState>,
        RenderFrameData::pointLightCapacity> pointShadowModelStateScratch_;
};

} // namespace sokoban
