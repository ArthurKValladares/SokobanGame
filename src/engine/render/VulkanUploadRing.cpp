#include "engine/render/VulkanUploadRing.hpp"

#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <cstring>
#include <stdexcept>

namespace sokoban {

VulkanUploadRing::~VulkanUploadRing()
{
    destroy();
}

void VulkanUploadRing::create(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize capacity)
{
    destroy();
    if (!physicalDevice || !device || capacity == 0) {
        throw std::invalid_argument("Invalid Vulkan upload ring configuration");
    }

    physicalDevice_ = physicalDevice;
    device_ = device;
    try {
        const VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = capacity,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer_),
            "vkCreateBuffer upload ring failed");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_BUFFER, buffer_, "Shared upload ring");

        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(device_, buffer_, &requirements);
        const VkMemoryAllocateInfo allocationInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkCheck(vkAllocateMemory(device_, &allocationInfo, nullptr, &memory_),
            "vkAllocateMemory upload ring failed");
        vkCheck(vkBindBufferMemory(device_, buffer_, memory_, 0),
            "vkBindBufferMemory upload ring failed");

        void* mapped = nullptr;
        vkCheck(vkMapMemory(device_, memory_, 0, capacity, 0, &mapped),
            "vkMapMemory upload ring failed");
        mapped_ = static_cast<std::byte*>(mapped);
        allocator_ = UploadRingAllocator(capacity);
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanUploadRing::destroy()
{
    if (device_ && mapped_) {
        vkUnmapMemory(device_, memory_);
    }
    if (device_ && buffer_) {
        vkDestroyBuffer(device_, buffer_, nullptr);
    }
    if (device_ && memory_) {
        vkFreeMemory(device_, memory_, nullptr);
    }
    allocator_ = UploadRingAllocator();
    mapped_ = nullptr;
    memory_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
}

std::optional<VulkanUploadRing::Reservation> VulkanUploadRing::reserve(
    VkDeviceSize size,
    VkDeviceSize alignment)
{
    if (!buffer_) {
        throw std::logic_error("Upload ring is not initialized");
    }
    return allocator_.reserve(size, alignment);
}

void VulkanUploadRing::write(
    Reservation reservation,
    VkDeviceSize destinationOffset,
    const void* source,
    std::size_t size)
{
    if (!reservation.valid() || !source ||
        destinationOffset > reservation.size ||
        size > reservation.size - destinationOffset) {
        throw std::invalid_argument("Invalid upload-ring write");
    }
    std::memcpy(mapped_ + reservation.offset + destinationOffset, source, size);
}

void VulkanUploadRing::commit(Reservation reservation)
{
    allocator_.commit(reservation);
}

void VulkanUploadRing::complete(Reservation reservation)
{
    allocator_.complete(reservation);
}

void VulkanUploadRing::abandon(Reservation reservation)
{
    allocator_.abandon(reservation);
}

uint32_t VulkanUploadRing::findMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((typeFilter & (1U << index)) &&
            (memoryProperties.memoryTypes[index].propertyFlags & properties) ==
                properties) {
            return index;
        }
    }
    throw std::runtime_error("No suitable memory type for upload ring");
}

} // namespace sokoban
