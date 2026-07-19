#pragma once

#include "engine/render/VulkanModelResources.hpp"

#include <vulkan/vulkan.h>

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
        ImageBinding sceneColor;
        ImageBinding sceneDepth;
        ImageBinding ssao;
        ImageBinding uiFont;
        std::vector<VulkanModelResources::TextureView> modelTextures;
    };

    VulkanSceneDescriptors() = default;
    ~VulkanSceneDescriptors();

    VulkanSceneDescriptors(const VulkanSceneDescriptors&) = delete;
    VulkanSceneDescriptors& operator=(const VulkanSceneDescriptors&) = delete;

    void create(VkDevice device, const Resources& resources);
    void update(const Resources& resources) const;
    void destroy();

    [[nodiscard]] VkDescriptorSetLayout layout() const { return layout_; }
    [[nodiscard]] const VkDescriptorSet& set() const { return set_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    uint32_t modelTextureCount_ = 0;
};

} // namespace sokoban
