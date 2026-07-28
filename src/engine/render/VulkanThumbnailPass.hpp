#pragma once

#include "engine/TileTypes.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace sokoban {

// Supplies the level editor's palette with a picture of each tile type.
//
// The pictures are *baked offline* (see TileThumbnailBake and the
// --bake-tile-thumbnails mode) by rendering each tile through the game's own
// frame path and screenshotting it. This class only loads those PNGs and hands
// them to ImGui.
//
// It used to render the models here instead, in a bespoke pipeline with its
// own shaders. That could not help but drift from the real thing - it had no
// shadows or SSAO, and its material handling had to be kept in step with
// triangle.frag.glsl by hand - and it looked wrong. Loading a screenshot is
// both simpler and exact by construction.
//
// A tile with no baked file simply has no thumbnail; the palette falls back to
// a colour swatch.
class VulkanThumbnailPass {
public:
    VulkanThumbnailPass() = default;
    ~VulkanThumbnailPass();

    VulkanThumbnailPass(const VulkanThumbnailPass&) = delete;
    VulkanThumbnailPass& operator=(const VulkanThumbnailPass&) = delete;

    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::filesystem::path assetRoot);
    void destroy();

    [[nodiscard]] bool valid() const { return device_ != VK_NULL_HANDLE; }

    // Loads the baked thumbnail for `tile` on first use and returns a handle
    // ImGui can draw, or null when there is no baked file. Callers must treat
    // null as "draw something else".
    [[nodiscard]] VkDescriptorSet thumbnailFor(TileType tile);

    // Drops every loaded thumbnail, so a re-bake can be picked up without
    // restarting.
    void invalidate();

private:
    struct Thumbnail {
        vulkanResources::OwnedImage image {};
        // No sampler: this ImGui version's AddTexture takes only a view and a
        // layout, and uses its own sampler.
        VkDescriptorSet imguiTexture = VK_NULL_HANDLE;
        // Distinguishes "looked and found nothing" from "not looked at yet",
        // so a missing file is not retried every frame.
        bool missing = false;
    };

    [[nodiscard]] bool loadThumbnail(TileType tile, Thumbnail& target);
    void destroyThumbnail(Thumbnail& thumbnail);

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::filesystem::path assetRoot_;
    std::unordered_map<int, Thumbnail> cache_;
};

} // namespace sokoban
