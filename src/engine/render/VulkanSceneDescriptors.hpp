#pragma once

#include "engine/render/VulkanModelResources.hpp"
#include "engine/render/GpuMappedBuffer.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/RenderTypes.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sokoban {

// The two descriptor set layouts that every scene descriptor set is allocated
// from and every pipeline layout is built against. They depend on the binding
// table and on the size of the texture heap, never on a render target, so one
// instance is shared across render-resource generations: a window resize
// allocates new sets from the same layouts, and the pipelines built against
// them stay valid. Held through shared_ptr by the descriptors and by the
// pipeline factory, so the layouts outlive whichever of the two goes last.
class VulkanSceneDescriptorLayouts {
public:
    VulkanSceneDescriptorLayouts(VkDevice device, uint32_t modelTextureCount);
    ~VulkanSceneDescriptorLayouts();

    VulkanSceneDescriptorLayouts(const VulkanSceneDescriptorLayouts&) = delete;
    VulkanSceneDescriptorLayouts& operator=(
        const VulkanSceneDescriptorLayouts&) = delete;

    [[nodiscard]] VkDescriptorSetLayout scene() const { return scene_; }
    [[nodiscard]] VkDescriptorSetLayout textures() const { return textures_; }
    [[nodiscard]] uint32_t modelTextureCount() const
    {
        return modelTextureCount_;
    }

private:
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout scene_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout textures_ = VK_NULL_HANDLE;
    uint32_t modelTextureCount_ = 0;
};

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
        ImageBinding previewSceneColor;
        // The scene target itself, read by the tonemap pass. sceneColor above
        // is the *copy* the scene shaders sample mid-pass; this is the image
        // they render into.
        ImageBinding sceneHdrColor;
        ImageBinding sceneDepth;
        ImageBinding ssao;
        ImageBinding atmosphere;
        // The first bloom target feeds the vertical blur; the second is the
        // finished bloom texture sampled by tonemapping.
        ImageBinding bloomExtract;
        ImageBinding bloom;
        ImageBinding uiFont;
        ImageBinding uiCurves;
        ImageBinding titleBackground;
        std::vector<VulkanModelResources::TextureView> modelTextures;
        VulkanModelResources::SkinningBufferView skinning;
        VulkanModelResources::DrawInstanceBufferView drawInstances;
        VulkanModelResources::MaterialBufferView materials;
        std::vector<VkDescriptorBufferInfo> waterCells;
    };

    VulkanSceneDescriptors() = default;
    ~VulkanSceneDescriptors();

    VulkanSceneDescriptors(const VulkanSceneDescriptors&) = delete;
    VulkanSceneDescriptors& operator=(const VulkanSceneDescriptors&) = delete;

    // `reusableLayouts` are adopted when they fit this texture heap; pass the
    // previous generation's layouts() so pipelines built against them can be
    // kept. Otherwise new layouts are created.
    void create(
        VulkanMemoryAllocator& allocator,
        VkDevice device,
        const Resources& resources,
        uint32_t setCount = 1,
        std::shared_ptr<const VulkanSceneDescriptorLayouts> reusableLayouts =
            nullptr);
    void update(const Resources& resources) const;
    void update(uint32_t setIndex, const Resources& resources) const;
    void destroy();
    // Writes per-view camera, lighting, water ripples, and ground scratches.
    void updateFrame(
        uint32_t setIndex,
        const RenderFrameData::Lighting& lighting,
        const SceneCamera& camera,
        bool preview = false,
        std::span<const RenderFrameData::WaterRipple> waterRipples = {},
        std::span<const RenderFrameData::RockScratch> rockScratches = {},
        std::span<const RenderFrameData::Tile> waterContactTiles = {},
        std::span<const RenderFrameData::WaterSurface> waterContactSurfaces = {}) const;

    [[nodiscard]] const std::shared_ptr<const VulkanSceneDescriptorLayouts>&
    layouts() const
    {
        return layouts_;
    }
    [[nodiscard]] const VkDescriptorSet& set(
        uint32_t setIndex = 0,
        bool preview = false) const;
    [[nodiscard]] const VkDescriptorSet& textureSet(
        uint32_t setIndex = 0) const;

private:
    [[nodiscard]] GpuMappedBuffer createFrameBuffer() const;
    void updateInternal(
        uint32_t internalSetIndex,
        const Resources& resources) const;
    void updateTextureSet(
        uint32_t setIndex,
        const Resources& resources) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VulkanMemoryAllocator* allocator_ = nullptr;
    std::shared_ptr<const VulkanSceneDescriptorLayouts> layouts_;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;
    std::vector<VkDescriptorSet> textureSets_;
    std::vector<GpuMappedBuffer> frameBuffers_;
    uint32_t frameSetCount_ = 0;
    uint32_t modelTextureCount_ = 0;
};

} // namespace sokoban
