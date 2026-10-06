#pragma once

#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>
#include <array>

namespace sokoban {

class FontAtlas;
struct ImageData;

class VulkanUiResources {
public:
    VulkanUiResources() = default;
    ~VulkanUiResources();

    VulkanUiResources(const VulkanUiResources&) = delete;
    VulkanUiResources& operator=(const VulkanUiResources&) = delete;

    void create(
        VulkanMemoryAllocator& allocator,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        const FontAtlas& font,
        const ImageData& titleBackground);
    void destroy();
    // The caller has waited this frame slot's fence. Uploads are ordered on
    // the graphics queue before sampling; glyph insertion never waits idle.
    void recordFontUpdates(VkCommandBuffer commandBuffer, uint32_t frameIndex);

    [[nodiscard]] VkImageView fontImageView() const { return fontImage_.view; }
    [[nodiscard]] VkImageView titleBackgroundImageView() const
    {
        return titleBackgroundImage_.view;
    }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }
    [[nodiscard]] VkImageView curveImageView() const { return curveImage_.view; }
    [[nodiscard]] VkSampler curveSampler() const { return curveSampler_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VulkanMemoryAllocator* allocator_ = nullptr;
    vulkanResources::OwnedImage fontImage_ {};
    vulkanResources::OwnedImage curveImage_ {};
    vulkanResources::OwnedImage titleBackgroundImage_ {};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkSampler curveSampler_ = VK_NULL_HANDLE;
    const FontAtlas* font_ = nullptr;
    uint64_t fontRevision_ = 0, curveRevision_ = 0;
    struct Upload {
        VkBuffer buffer = VK_NULL_HANDLE;
        ::VmaAllocation_T* allocation = nullptr;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;
    };
    std::array<Upload, 2> uploads_ {};
};

} // namespace sokoban
