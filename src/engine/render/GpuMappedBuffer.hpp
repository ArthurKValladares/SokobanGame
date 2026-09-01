#pragma once

#include "engine/render/VulkanMemoryAllocator.hpp"

#include <vulkan/vulkan.h>

#include <string_view>

namespace sokoban {

// What a shader is handed to read one of these buffers: a handle, and how much
// of it is live this frame.
//
// This existed three times - SkinningBufferView, DrawInstanceBufferView,
// MaterialBufferView - with identical fields and an identical validity test.
// Three names for one shape bought nothing: none of them could be passed where
// another was expected anyway, because the descriptor binding decides what a
// buffer means, not the C++ type of the handle.
struct GpuBufferView {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize range = 0;

    // A zero range is not a small buffer, it is an absent one: the descriptor
    // update skips a binding that reports invalid rather than writing an empty
    // one.
    [[nodiscard]] bool valid() const { return buffer && range > 0; }
};

// A host-visible buffer the CPU writes straight into and a shader reads.
//
// The skinning palette, the draw-instance array and the material table were
// three structs with the same three fields and three create/destroy pairs that
// differed only in a size expression, a member name and a debug label. The
// duplication was not the length - it was that each copy separately got the
// usage flags, the sharing mode, the memory-usage class and the destroy-order
// right, and a fourth buffer added later would have had to get them right
// again.
//
// Creation deliberately does not catch. Every caller already wraps creation and
// its initial fill in one try/catch that calls destroy() and rethrows, and
// catching here as well would destroy twice.
class GpuMappedBuffer {
public:
    void create(
        const VulkanMemoryAllocator& allocator,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        std::string_view debugName)
    {
        const VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        allocator.createBuffer(
            bufferInfo,
            VulkanMemoryUsage::HostSequentialWrite,
            buffer_,
            allocation_,
            &mapped_,
            debugName);
    }

    // Takes the allocator by pointer because teardown runs on a half-built
    // object too, where there may not be one left to destroy with.
    void destroy(const VulkanMemoryAllocator* allocator) noexcept
    {
        if (allocator != nullptr) {
            allocator->destroyBuffer(buffer_, allocation_);
        }
        buffer_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
        mapped_ = nullptr;
    }

    [[nodiscard]] VkBuffer handle() const { return buffer_; }
    [[nodiscard]] void* mapped() const { return mapped_; }

    // The live prefix a shader should read, which is rarely the whole buffer:
    // the caller knows how many entries this frame filled.
    [[nodiscard]] GpuBufferView view(VkDeviceSize range) const
    {
        return GpuBufferView { .buffer = buffer_, .range = range };
    }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VulkanAllocation allocation_ = nullptr;
    void* mapped_ = nullptr;
};

} // namespace sokoban
