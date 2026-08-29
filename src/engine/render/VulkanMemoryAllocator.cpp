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
    return {
        .blockCount = statistics.total.statistics.blockCount,
        .allocationCount = statistics.total.statistics.allocationCount,
        .blockBytes = statistics.total.statistics.blockBytes,
        .allocationBytes = statistics.total.statistics.allocationBytes,
    };
}

} // namespace sokoban
