#include "engine/render/VulkanMemoryAllocator.hpp"

#include "engine/render/VulkanResourceUtils.hpp"

#define VMA_VULKAN_VERSION 1003000
#include <vk_mem_alloc.h>

#include <string>

namespace sokoban {

VulkanMemoryAllocator::~VulkanMemoryAllocator()
{
    destroy();
}

void VulkanMemoryAllocator::create(
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device)
{
    destroy();
    const VmaAllocatorCreateInfo createInfo {
        .physicalDevice = physicalDevice,
        .device = device,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    vkCheck(
        vmaCreateAllocator(&createInfo, &allocator_),
        "vmaCreateAllocator failed");
    trackedBytes_.store(0, std::memory_order_relaxed);
    peakTrackedBytes_.store(0, std::memory_order_relaxed);
    totalAllocatedBytes_.store(0, std::memory_order_relaxed);
    totalFreedBytes_.store(0, std::memory_order_relaxed);
    imageBytes_.store(0, std::memory_order_relaxed);
    bufferBytes_.store(0, std::memory_order_relaxed);
    deviceLocalBytes_.store(0, std::memory_order_relaxed);
    hostVisibleBytes_.store(0, std::memory_order_relaxed);
    imageCount_.store(0, std::memory_order_relaxed);
    bufferCount_.store(0, std::memory_order_relaxed);
    lifetimeAllocations_.store(0, std::memory_order_relaxed);
    lifetimeFrees_.store(0, std::memory_order_relaxed);
}

void VulkanMemoryAllocator::destroy() noexcept
{
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
}

void VulkanMemoryAllocator::createDeviceImage(
    const VkImageCreateInfo& imageInfo,
    VkImage& image,
    VulkanAllocation& allocation,
    std::string_view debugName) const
{
    const VmaAllocationCreateInfo allocationInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    vkCheck(
        vmaCreateImage(
            allocator_,
            &imageInfo,
            &allocationInfo,
            &image,
            &allocation,
            nullptr),
        "vmaCreateImage failed");
    recordAllocation(allocation, true);
    if (!debugName.empty()) {
        const std::string ownedName(debugName);
        vmaSetAllocationName(allocator_, allocation, ownedName.c_str());
    }
}

void VulkanMemoryAllocator::destroyImage(
    VkImage image,
    VulkanAllocation allocation) const noexcept
{
    if (image || allocation) {
        recordFree(allocation, true);
        vmaDestroyImage(allocator_, image, allocation);
    }
}

void VulkanMemoryAllocator::createBuffer(
    const VkBufferCreateInfo& bufferInfo,
    VulkanMemoryUsage usage,
    VkBuffer& buffer,
    VulkanAllocation& allocation,
    void** mappedData,
    std::string_view debugName) const
{
    VmaAllocationCreateInfo allocationInfo {};
    switch (usage) {
    case VulkanMemoryUsage::DeviceLocal:
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case VulkanMemoryUsage::HostSequentialWrite:
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocationInfo.requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case VulkanMemoryUsage::HostReadback:
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocationInfo.requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    }

    VmaAllocationInfo resultInfo {};
    vkCheck(
        vmaCreateBuffer(
            allocator_,
            &bufferInfo,
            &allocationInfo,
            &buffer,
            &allocation,
            &resultInfo),
        "vmaCreateBuffer failed");
    recordAllocation(allocation, false);
    if (mappedData) {
        *mappedData = resultInfo.pMappedData;
    }
    if (!debugName.empty()) {
        const std::string ownedName(debugName);
        vmaSetAllocationName(allocator_, allocation, ownedName.c_str());
    }
}

void VulkanMemoryAllocator::destroyBuffer(
    VkBuffer buffer,
    VulkanAllocation allocation) const noexcept
{
    if (buffer || allocation) {
        recordFree(allocation, false);
        vmaDestroyBuffer(allocator_, buffer, allocation);
    }
}

VulkanMemoryStatistics VulkanMemoryAllocator::statistics() const
{
    if (!allocator_) {
        return {};
    }
    VmaTotalStatistics statistics {};
    vmaCalculateStatistics(allocator_, &statistics);
    VulkanMemoryStatistics result {
        .blockCount = statistics.total.statistics.blockCount,
        .allocationCount = statistics.total.statistics.allocationCount,
        .blockBytes = statistics.total.statistics.blockBytes,
        .allocationBytes = statistics.total.statistics.allocationBytes,
        .peakAllocationBytes = peakTrackedBytes_.load(std::memory_order_relaxed),
        .totalAllocatedBytes = totalAllocatedBytes_.load(std::memory_order_relaxed),
        .totalFreedBytes = totalFreedBytes_.load(std::memory_order_relaxed),
        .imageBytes = imageBytes_.load(std::memory_order_relaxed),
        .bufferBytes = bufferBytes_.load(std::memory_order_relaxed),
        .deviceLocalBytes = deviceLocalBytes_.load(std::memory_order_relaxed),
        .hostVisibleBytes = hostVisibleBytes_.load(std::memory_order_relaxed),
        .imageCount = imageCount_.load(std::memory_order_relaxed),
        .bufferCount = bufferCount_.load(std::memory_order_relaxed),
        .lifetimeAllocations = lifetimeAllocations_.load(std::memory_order_relaxed),
        .lifetimeFrees = lifetimeFrees_.load(std::memory_order_relaxed),
    };
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets {};
    vmaGetHeapBudgets(allocator_, budgets.data());
    const VkPhysicalDeviceMemoryProperties* properties = nullptr;
    vmaGetMemoryProperties(allocator_, &properties);
    if (properties) {
        result.heapCount = properties->memoryHeapCount;
        for (uint32_t index = 0; index < result.heapCount; ++index) {
            result.heaps[index] = {
                .deviceLocal =
                    (properties->memoryHeaps[index].flags &
                     VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0,
                .blockBytes = budgets[index].statistics.blockBytes,
                .allocationBytes = budgets[index].statistics.allocationBytes,
                .usageBytes = budgets[index].usage,
                .budgetBytes = budgets[index].budget,
            };
        }
    }
    return result;
}

void VulkanMemoryAllocator::recordAllocation(
    VulkanAllocation allocation, bool image) const
{
    if (!allocation) {
        return;
    }
    VmaAllocationInfo info {};
    vmaGetAllocationInfo(allocator_, allocation, &info);
    const uint64_t bytes = info.size;
    const uint64_t current =
        trackedBytes_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    uint64_t peak = peakTrackedBytes_.load(std::memory_order_relaxed);
    while (peak < current && !peakTrackedBytes_.compare_exchange_weak(
               peak, current, std::memory_order_relaxed)) {
    }
    totalAllocatedBytes_.fetch_add(bytes, std::memory_order_relaxed);
    lifetimeAllocations_.fetch_add(1, std::memory_order_relaxed);
    if (image) {
        imageBytes_.fetch_add(bytes, std::memory_order_relaxed);
        imageCount_.fetch_add(1, std::memory_order_relaxed);
    } else {
        bufferBytes_.fetch_add(bytes, std::memory_order_relaxed);
        bufferCount_.fetch_add(1, std::memory_order_relaxed);
    }
    VkMemoryPropertyFlags properties = 0;
    vmaGetAllocationMemoryProperties(allocator_, allocation, &properties);
    if ((properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
        deviceLocalBytes_.fetch_add(bytes, std::memory_order_relaxed);
    }
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
        hostVisibleBytes_.fetch_add(bytes, std::memory_order_relaxed);
    }
}

void VulkanMemoryAllocator::recordFree(
    VulkanAllocation allocation, bool image) const noexcept
{
    if (!allocator_ || !allocation) {
        return;
    }
    VmaAllocationInfo info {};
    vmaGetAllocationInfo(allocator_, allocation, &info);
    const uint64_t bytes = info.size;
    trackedBytes_.fetch_sub(bytes, std::memory_order_relaxed);
    totalFreedBytes_.fetch_add(bytes, std::memory_order_relaxed);
    lifetimeFrees_.fetch_add(1, std::memory_order_relaxed);
    if (image) {
        imageBytes_.fetch_sub(bytes, std::memory_order_relaxed);
        imageCount_.fetch_sub(1, std::memory_order_relaxed);
    } else {
        bufferBytes_.fetch_sub(bytes, std::memory_order_relaxed);
        bufferCount_.fetch_sub(1, std::memory_order_relaxed);
    }
    VkMemoryPropertyFlags properties = 0;
    vmaGetAllocationMemoryProperties(allocator_, allocation, &properties);
    if ((properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
        deviceLocalBytes_.fetch_sub(bytes, std::memory_order_relaxed);
    }
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
        hostVisibleBytes_.fetch_sub(bytes, std::memory_order_relaxed);
    }
}

} // namespace sokoban
