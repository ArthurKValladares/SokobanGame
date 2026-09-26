#pragma once

#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

namespace sokoban {

// Owns the half-resolution integration target used by volumetric atmosphere.
// The expensive ray march writes scattering + transmittance here; a cheap
// full-resolution pass then composites it over the unblurred scene.
class VulkanAtmospherePass {
public:
    VulkanAtmospherePass() = default;
    ~VulkanAtmospherePass();

    VulkanAtmospherePass(const VulkanAtmospherePass&) = delete;
    VulkanAtmospherePass& operator=(const VulkanAtmospherePass&) = delete;

    void create(
        VulkanMemoryAllocator& allocator,
        VkDevice device,
        VkExtent2D renderExtent,
        VkFormat format);
    void destroy();

    void prepareTarget(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    void publishTarget(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);

    [[nodiscard]] bool valid() const;
    [[nodiscard]] VkImageView imageView() const { return image_.view; }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }
    [[nodiscard]] VkExtent2D extent() const { return extent_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VulkanMemoryAllocator* allocator_ = nullptr;
    VkExtent2D extent_ {};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    vulkanResources::OwnedImage image_ {};
    VkSampler sampler_ = VK_NULL_HANDLE;
    bool readable_ = false;
};

} // namespace sokoban
