#pragma once

#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/ui/Ui.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace sokoban {

class VulkanModelResources;
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
        float wireframeLineWidth = 1.0f;
        uint64_t statsFrameIndex = 0;
        uint64_t pipelineRebuilds = 0;
        uint64_t swapchainRecreations = 0;
        uint64_t swapchainRecreationDeferrals = 0;
    };

    [[nodiscard]] RenderStats record(
        Resources resources,
        const FrameConfiguration& configuration,
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene,
        const UiDrawData& uiDrawData) const;
};

} // namespace sokoban
