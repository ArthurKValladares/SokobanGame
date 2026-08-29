#pragma once

#include "engine/render/UploadRingAllocator.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace sokoban {

// A shared host-visible transfer source buffer mapped for its whole lifetime.
// Upload ranges remain reserved until their submission fence completes, so the
// CPU never overwrites data still consumed by the GPU.
class VulkanUploadRing {
public:
    using Reservation = UploadRingAllocator::Reservation;

    VulkanUploadRing() = default;
    ~VulkanUploadRing();

    VulkanUploadRing(const VulkanUploadRing&) = delete;
    VulkanUploadRing& operator=(const VulkanUploadRing&) = delete;

    void create(
        VulkanMemoryAllocator& memoryAllocator,
        VkDevice device,
        VkDeviceSize capacity = 64ULL * 1024ULL * 1024ULL);
    void destroy();

    [[nodiscard]] std::optional<Reservation> reserve(
        VkDeviceSize size,
        VkDeviceSize alignment);
    void write(
        Reservation reservation,
        VkDeviceSize destinationOffset,
        const void* source,
        std::size_t size);
    void commit(Reservation reservation);
    void complete(Reservation reservation);
    void abandon(Reservation reservation);

    [[nodiscard]] VkBuffer buffer() const { return buffer_; }
    [[nodiscard]] VkDeviceSize capacity() const { return allocator_.capacity(); }
    [[nodiscard]] VkDeviceSize usedBytes() const { return allocator_.usedBytes(); }

private:
    VulkanMemoryAllocator* memoryAllocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VulkanAllocation allocation_ = nullptr;
    std::byte* mapped_ = nullptr;
    UploadRingAllocator allocator_;
};

} // namespace sokoban
