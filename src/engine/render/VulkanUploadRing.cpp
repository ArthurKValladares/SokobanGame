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
    VulkanMemoryAllocator& memoryAllocator,
    VkDevice device,
    VkDeviceSize capacity)
{
    destroy();
    if (!memoryAllocator.valid() || !device || capacity == 0) {
        throw std::invalid_argument("Invalid Vulkan upload ring configuration");
    }

    memoryAllocator_ = &memoryAllocator;
    device_ = device;
    try {
        const VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = capacity,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        void* mapped = nullptr;
        memoryAllocator_->createBuffer(
            bufferInfo,
            VulkanMemoryUsage::HostSequentialWrite,
            buffer_,
            allocation_,
            &mapped,
            "Shared upload ring");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_BUFFER, buffer_, "Shared upload ring");
        mapped_ = static_cast<std::byte*>(mapped);
        allocator_ = UploadRingAllocator(capacity);
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanUploadRing::destroy()
{
    if (memoryAllocator_) {
        memoryAllocator_->destroyBuffer(buffer_, allocation_);
    }
    allocator_ = UploadRingAllocator();
    mapped_ = nullptr;
    allocation_ = nullptr;
    buffer_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    memoryAllocator_ = nullptr;
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

} // namespace sokoban
