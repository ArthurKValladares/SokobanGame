#pragma once

#include "engine/render/VulkanModelResources.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/RenderTypes.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace sokoban {

class VulkanSceneDescriptors {
public:
    struct ImageBinding {
        VkSampler sampler = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        [[nodiscard]] bool valid() const { return sampler && imageView; }
    };

    struct Resources {
        ImageBinding shadow;
        ImageBinding pointShadows;
        ImageBinding sceneColor;
        ImageBinding sceneDepth;
        ImageBinding ssao;
        ImageBinding uiFont;
        ImageBinding titleBackground;
        std::vector<VulkanModelResources::TextureView> modelTextures;
        VulkanModelResources::SkinningBufferView skinning;
        VulkanModelResources::DrawInstanceBufferView drawInstances;
    };

    VulkanSceneDescriptors() = default;
    ~VulkanSceneDescriptors();

    VulkanSceneDescriptors(const VulkanSceneDescriptors&) = delete;
    VulkanSceneDescriptors& operator=(const VulkanSceneDescriptors&) = delete;

    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        const Resources& resources,
        uint32_t setCount = 1);
    void update(const Resources& resources) const;
    void update(uint32_t setIndex, const Resources& resources) const;
    void destroy();
    // Writes the whole per-frame uniform: camera, sun transform, lights.
    void updateFrame(
        uint32_t setIndex,
        const RenderFrameData::Lighting& lighting,
        const SceneCamera& camera,
        bool preview = false) const;

    [[nodiscard]] VkDescriptorSetLayout layout() const { return layout_; }
    [[nodiscard]] const VkDescriptorSet& set(
        uint32_t setIndex = 0,
        bool preview = false) const;

private:
    struct OwnedBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    [[nodiscard]] OwnedBuffer createFrameBuffer(
        VkPhysicalDevice physicalDevice) const;
    void updateInternal(
        uint32_t internalSetIndex,
        const Resources& resources) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;
    std::vector<OwnedBuffer> frameBuffers_;
    uint32_t frameSetCount_ = 0;
    uint32_t modelTextureCount_ = 0;
};

} // namespace sokoban
