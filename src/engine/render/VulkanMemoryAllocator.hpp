#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string_view>

struct VmaAllocator_T;
struct VmaAllocation_T;

namespace sokoban {

using VulkanAllocation = VmaAllocation_T*;

struct VulkanMemoryStatistics {
    uint32_t blockCount = 0;
    uint32_t allocationCount = 0;
    VkDeviceSize blockBytes = 0;
    VkDeviceSize allocationBytes = 0;
};

enum class VulkanMemoryUsage {
    DeviceLocal,
    HostSequentialWrite,
    HostReadback,
};

// Owns the process-wide Vulkan Memory Allocator instance for one logical
// device. Render resources receive this object explicitly, keeping allocation
// policy centralized without introducing hidden global state.
class VulkanMemoryAllocator {
public:
    VulkanMemoryAllocator() = default;
    ~VulkanMemoryAllocator();

    VulkanMemoryAllocator(const VulkanMemoryAllocator&) = delete;
    VulkanMemoryAllocator& operator=(const VulkanMemoryAllocator&) = delete;

    void create(
        VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device);
    void destroy() noexcept;

    void createDeviceImage(
        const VkImageCreateInfo& imageInfo,
        VkImage& image,
        VulkanAllocation& allocation,
        std::string_view debugName = {}) const;
    void destroyImage(
        VkImage image,
        VulkanAllocation allocation) const noexcept;
    void createBuffer(
        const VkBufferCreateInfo& bufferInfo,
        VulkanMemoryUsage usage,
        VkBuffer& buffer,
        VulkanAllocation& allocation,
        void** mappedData = nullptr,
        std::string_view debugName = {}) const;
    void destroyBuffer(
        VkBuffer buffer,
        VulkanAllocation allocation) const noexcept;

    [[nodiscard]] VulkanMemoryStatistics statistics() const;
    [[nodiscard]] bool valid() const { return allocator_ != nullptr; }

private:
    VmaAllocator_T* allocator_ = nullptr;
};

} // namespace sokoban
