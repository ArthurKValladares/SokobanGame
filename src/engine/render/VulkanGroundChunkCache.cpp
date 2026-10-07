#include "engine/render/VulkanGroundChunkCache.hpp"
#include "engine/render/VulkanResourceUtils.hpp"
#include <cstring>
#include <stdexcept>

namespace sokoban {
namespace {
constexpr VkDeviceSize geometryLimit = 8ULL * 1024 * 1024;
constexpr VkDeviceSize retainedLimit = 32ULL * 1024 * 1024;
}
void VulkanGroundChunkCache::create(VulkanMemoryAllocator& allocator, VkDevice device,
    VkCommandPool pool, VkQueue queue)
{
    allocator_ = &allocator; device_ = device; pool_ = pool; queue_ = queue;
}
void VulkanGroundChunkCache::release(Entry& entry) noexcept
{
    entry.staging.destroy(allocator_);
    if (entry.command) vkFreeCommandBuffers(device_, pool_, 1, &entry.command);
    if (entry.fence) vkDestroyFence(device_, entry.fence, nullptr);
    if (allocator_) allocator_->destroyBuffer(entry.buffer, entry.allocation);
    entry.command = VK_NULL_HANDLE; entry.fence = VK_NULL_HANDLE;
    entry.buffer = VK_NULL_HANDLE; entry.allocation = nullptr;
}
void VulkanGroundChunkCache::destroy() noexcept
{
    if (current_) release(*current_);
    for (auto& entry : retired_) release(*entry);
    current_.reset(); retired_.clear(); allocator_ = nullptr;
}
bool VulkanGroundChunkCache::finishUpload(Entry& entry)
{
    if (entry.ready) return true;
    const VkResult status = vkGetFenceStatus(device_, entry.fence);
    if (status == VK_NOT_READY) return false;
    vkCheck(status, "Ground chunk upload fence failed");
    entry.staging.destroy(allocator_);
    vkFreeCommandBuffers(device_, pool_, 1, &entry.command);
    vkDestroyFence(device_, entry.fence, nullptr);
    entry.command = VK_NULL_HANDLE; entry.fence = VK_NULL_HANDLE;
    entry.ready = true;
    return true;
}
void VulkanGroundChunkCache::collect()
{
    for (auto it = retired_.begin(); it != retired_.end();) {
        if ((*it)->pendingFrameMask == 0 && finishUpload(**it)) {
            release(**it); it = retired_.erase(it);
        } else ++it;
    }
}
void VulkanGroundChunkCache::completeFrame(uint32_t frameIndex)
{
    for (auto& entry : retired_) entry->pendingFrameMask &= ~(1U << frameIndex);
    collect();
}
uint64_t VulkanGroundChunkCache::residentBytes() const noexcept
{
    uint64_t bytes = current_ ? current_->bytes : 0;
    for (const auto& entry : retired_) bytes += entry->bytes;
    return bytes;
}
uint64_t VulkanGroundChunkCache::stagingBytes() const noexcept
{
    uint64_t bytes = current_ && current_->staging.handle() ? current_->bytes : 0;
    for (const auto& entry : retired_) {
        if (entry->staging.handle()) bytes += entry->bytes;
    }
    return bytes;
}
bool VulkanGroundChunkCache::readyFor(const GroundChunkGeometry* geometry) const noexcept
{
    return geometry && current_ && current_->geometry.get() == geometry && current_->ready;
}
VulkanGroundChunkCache::MeshView VulkanGroundChunkCache::mesh(
    const GroundChunkGeometry* geometry, std::size_t chunk) const
{
    return readyFor(geometry) && chunk < current_->meshes.size()
        ? current_->meshes[chunk] : MeshView {};
}
bool VulkanGroundChunkCache::update(std::shared_ptr<const GroundChunkGeometry> geometry,
    uint32_t pendingFrameMask)
{
    collect();
    if (current_) finishUpload(*current_);
    if (!geometry || geometry->chunks.empty()) return false;
    if (current_ && current_->geometry == geometry) return finishUpload(*current_);
    VkDeviceSize size = 0;
    for (const auto& chunk : geometry->chunks) {
        size += chunk.vertices.size() * sizeof(GroundChunkVertex) + chunk.indices.size() * sizeof(uint32_t);
    }
    if (size == 0 || size > geometryLimit || residentBytes() + size > retainedLimit) return false;
    retired_.reserve(retired_.size() + 1);
    auto entry = std::make_unique<Entry>();
    entry->geometry = std::move(geometry); entry->bytes = size;
    try {
        const VkBufferCreateInfo info { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        allocator_->createBuffer(info, VulkanMemoryUsage::DeviceLocal,
            entry->buffer, entry->allocation, nullptr, "Ground chunks");
        entry->staging.create(*allocator_, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "Ground chunk upload");
        auto* destination = static_cast<std::byte*>(entry->staging.mapped());
        VkDeviceSize cursor = 0;
        for (const auto& chunk : entry->geometry->chunks) {
            const VkDeviceSize vertexBytes = chunk.vertices.size() * sizeof(GroundChunkVertex);
            const VkDeviceSize indexBytes = chunk.indices.size() * sizeof(uint32_t);
            entry->meshes.push_back({ entry->buffer, cursor, cursor + vertexBytes,
                static_cast<uint32_t>(chunk.indices.size()) });
            std::memcpy(destination + cursor, chunk.vertices.data(), static_cast<std::size_t>(vertexBytes));
            std::memcpy(destination + cursor + vertexBytes, chunk.indices.data(), static_cast<std::size_t>(indexBytes));
            cursor += vertexBytes + indexBytes;
        }
        const VkCommandBufferAllocateInfo commandInfo { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool_, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
        vkCheck(vkAllocateCommandBuffers(device_, &commandInfo, &entry->command), "Ground chunk command allocation failed");
        const VkCommandBufferBeginInfo beginInfo { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkCheck(vkBeginCommandBuffer(entry->command, &beginInfo), "Ground chunk command begin failed");
        const VkBufferCopy copy { .srcOffset = 0, .dstOffset = 0, .size = size };
        vkCmdCopyBuffer(entry->command, entry->staging.handle(), entry->buffer, 1, &copy);
        const VkBufferMemoryBarrier barrier { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = entry->buffer, .offset = 0, .size = size };
        vkCmdPipelineBarrier(entry->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
        vkCheck(vkEndCommandBuffer(entry->command), "Ground chunk command end failed");
        const VkFenceCreateInfo fenceInfo { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCheck(vkCreateFence(device_, &fenceInfo, nullptr, &entry->fence), "Ground chunk fence failed");
        const VkSubmitInfo submit { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1, .pCommandBuffers = &entry->command };
        vkCheck(vkQueueSubmit(queue_, 1, &submit, entry->fence), "Ground chunk upload failed");
    } catch (...) { release(*entry); throw; }
    if (current_) {
        current_->pendingFrameMask = pendingFrameMask;
        retired_.push_back(std::move(current_));
    }
    current_ = std::move(entry); ++uploadCount_;
    return false;
}
} // namespace sokoban
