#pragma once

#include "engine/render/ImageData.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace sokoban {

class VulkanMemoryAllocator;

// Reads a rectangle of a rendered frame back to the CPU as RGBA.
//
// The source is the resolved scene colour image - the fully rendered frame
// after MSAA resolve, with every normal shading path applied - so a capture is
// exactly what the game drew, not a reconstruction of it. That is the whole
// point of baking tile thumbnails this way rather than re-rendering models in
// a bespoke pipeline that has to be kept in sync by hand.
//
// Blocking and allocation-heavy: intended for offline capture (the thumbnail
// bake), not per-frame use.
[[nodiscard]] ImageData captureImageRegion(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkImage sourceImage,
    VkFormat sourceFormat,
    VkImageLayout sourceLayout,
    VkOffset2D offset,
    VkExtent2D extent);

} // namespace sokoban
