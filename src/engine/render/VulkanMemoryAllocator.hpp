#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

struct VmaAllocator_T;
struct VmaAllocation_T;

namespace sokoban {

using VulkanAllocation = VmaAllocation_T*;

struct VulkanMemoryHeapStatistics {
    bool deviceLocal = false;
    VkDeviceSize blockBytes = 0;
    VkDeviceSize allocationBytes = 0;
    VkDeviceSize usageBytes = 0;
    VkDeviceSize budgetBytes = 0;
};

struct VulkanMemoryStatistics {
    uint32_t blockCount = 0;
    uint32_t allocationCount = 0;
    VkDeviceSize blockBytes = 0;
    VkDeviceSize allocationBytes = 0;
    VkDeviceSize peakAllocationBytes = 0;
    VkDeviceSize totalAllocatedBytes = 0;
    VkDeviceSize totalFreedBytes = 0;
    VkDeviceSize imageBytes = 0;
    VkDeviceSize bufferBytes = 0;
    VkDeviceSize deviceLocalBytes = 0;
    VkDeviceSize hostVisibleBytes = 0;
    uint32_t imageCount = 0;
    uint32_t bufferCount = 0;
    uint64_t lifetimeAllocations = 0;
    uint64_t lifetimeFrees = 0;
    uint32_t heapCount = 0;
    std::array<VulkanMemoryHeapStatistics, VK_MAX_MEMORY_HEAPS> heaps {};
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
    void recordAllocation(VulkanAllocation allocation, bool image) const;
    void recordFree(VulkanAllocation allocation, bool image) const noexcept;

    VmaAllocator_T* allocator_ = nullptr;
    mutable std::atomic<uint64_t> trackedBytes_ { 0 };
    mutable std::atomic<uint64_t> peakTrackedBytes_ { 0 };
    mutable std::atomic<uint64_t> totalAllocatedBytes_ { 0 };
    mutable std::atomic<uint64_t> totalFreedBytes_ { 0 };
    mutable std::atomic<uint64_t> imageBytes_ { 0 };
    mutable std::atomic<uint64_t> bufferBytes_ { 0 };
    mutable std::atomic<uint64_t> deviceLocalBytes_ { 0 };
    mutable std::atomic<uint64_t> hostVisibleBytes_ { 0 };
    mutable std::atomic<uint32_t> imageCount_ { 0 };
    mutable std::atomic<uint32_t> bufferCount_ { 0 };
    mutable std::atomic<uint64_t> lifetimeAllocations_ { 0 };
    mutable std::atomic<uint64_t> lifetimeFrees_ { 0 };
};

} // namespace sokoban
