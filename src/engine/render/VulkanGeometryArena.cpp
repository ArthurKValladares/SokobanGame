#include "engine/render/VulkanGeometryArena.hpp"

#include "engine/render/GltfMesh.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <algorithm>
#ifdef SOKOBAN_ENABLE_TEST_HOOKS
#include <atomic>
#endif
#include <iterator>
#include <stdexcept>

namespace sokoban {
namespace {

constexpr VkDeviceSize vertexBlockBytes = 32ULL * 1024ULL * 1024ULL;
constexpr VkDeviceSize indexBlockBytes = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t geometryAlignment = 16;

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
std::atomic<bool> failNextIndexBlockCreation { false };
#endif

} // namespace

VulkanGeometryArena::~VulkanGeometryArena()
{
    destroy();
}

void VulkanGeometryArena::create(
    VulkanMemoryAllocator& memoryAllocator,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VulkanUploadRing& uploadRing)
{
    destroy();
    memoryAllocator_ = &memoryAllocator;
    device_ = device;
    commandPool_ = commandPool;
    graphicsQueue_ = graphicsQueue;
    uploadRing_ = &uploadRing;
}

void VulkanGeometryArena::destroy()
{
    if (device_) {
        for (Block& block : vertexBlocks_) {
            destroyBlock(block);
        }
        for (Block& block : indexBlocks_) {
            destroyBlock(block);
        }
    }
    vertexBlocks_.clear();
    indexBlocks_.clear();
    graphicsQueue_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    uploadRing_ = nullptr;
    device_ = VK_NULL_HANDLE;
    memoryAllocator_ = nullptr;
}

VulkanGeometryArena::Allocation VulkanGeometryArena::allocate(
    VkDeviceSize vertexBytes,
    VkDeviceSize indexBytes)
{
    if (!device_ || vertexBytes == 0 || indexBytes == 0) {
        throw std::runtime_error("Invalid device-local geometry allocation");
    }

    Allocation result;
    result.vertex = allocateSlice(
        vertexBlocks_,
        result.vertexBlock,
        vertexBytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBlockBytes,
        "Static geometry vertex block");
    try {
        result.index = allocateSlice(
            indexBlocks_,
            result.indexBlock,
            indexBytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            indexBlockBytes,
            "Static geometry index block");
    } catch (...) {
        Block& vertexBlock = vertexBlocks_[result.vertexBlock];
        vertexBlock.allocator.release(result.vertex);
        if (vertexBlock.allocator.usedBytes() == 0) {
            destroyBlock(vertexBlock);
        }
        throw;
    }
    return result;
}

void VulkanGeometryArena::release(Allocation& allocation)
{
    if (!allocation.valid()) {
        allocation = {};
        return;
    }
    if (allocation.vertexBlock >= vertexBlocks_.size() ||
        allocation.indexBlock >= indexBlocks_.size() ||
        !vertexBlocks_[allocation.vertexBlock].buffer ||
        !indexBlocks_[allocation.indexBlock].buffer) {
        throw std::invalid_argument("Geometry allocation points outside the arena");
    }
    Block& vertexBlock = vertexBlocks_[allocation.vertexBlock];
    Block& indexBlock = indexBlocks_[allocation.indexBlock];
    vertexBlock.allocator.release(allocation.vertex);
    indexBlock.allocator.release(allocation.index);
    if (vertexBlock.allocator.usedBytes() == 0) {
        destroyBlock(vertexBlock);
    }
    if (indexBlock.allocator.usedBytes() == 0) {
        destroyBlock(indexBlock);
    }
    allocation = {};
}

VulkanGeometryArena::Upload VulkanGeometryArena::beginUpload(
    const Allocation& allocation,
    const MeshData& mesh) const
{
    return beginUpload(
        allocation,
        mesh.vertices.data(),
        sizeof(MeshVertex) * static_cast<VkDeviceSize>(mesh.vertices.size()),
        mesh.indices.data(),
        sizeof(uint32_t) * static_cast<VkDeviceSize>(mesh.indices.size()));
}

VulkanGeometryArena::Upload VulkanGeometryArena::beginUpload(
    const Allocation& allocation,
    const void* vertexData,
    VkDeviceSize vertexBytes,
    const void* indexData,
    VkDeviceSize indexBytes) const
{
    if (!allocation.valid() || allocation.vertexBlock >= vertexBlocks_.size() ||
        allocation.indexBlock >= indexBlocks_.size() ||
        !vertexBlocks_[allocation.vertexBlock].buffer ||
        !indexBlocks_[allocation.indexBlock].buffer) {
        throw std::invalid_argument("Invalid geometry allocation upload");
    }
    if (!vertexData || !indexData || vertexBytes != allocation.vertex.size ||
        indexBytes != allocation.index.size) {
        throw std::invalid_argument("Geometry upload size does not match its allocation");
    }

    if (!uploadRing_) {
        throw std::logic_error("Geometry arena upload ring is not initialized");
    }

    Upload upload;
    try {
        const VkDeviceSize stagingBytes = vertexBytes + indexBytes;
        const auto staging = uploadRing_->reserve(stagingBytes, geometryAlignment);
        if (!staging) {
            throw std::runtime_error("Shared upload ring is full for geometry upload");
        }
        upload.staging = *staging;
        uploadRing_->write(
            upload.staging,
            0,
            vertexData,
            static_cast<std::size_t>(vertexBytes));
        uploadRing_->write(
            upload.staging,
            vertexBytes,
            indexData,
            static_cast<std::size_t>(indexBytes));

        upload.commandBuffer = vulkanResources::beginOneShotCommands(
            device_, commandPool_, "geometry upload");

        const VkBufferCopy vertexCopy {
            .srcOffset = upload.staging.offset,
            .dstOffset = allocation.vertex.offset,
            .size = vertexBytes,
        };
        const VkBufferCopy indexCopy {
            .srcOffset = upload.staging.offset + vertexBytes,
            .dstOffset = allocation.index.offset,
            .size = indexBytes,
        };
        vkCmdCopyBuffer(
            upload.commandBuffer,
            uploadRing_->buffer(),
            vertexBlocks_[allocation.vertexBlock].buffer,
            1,
            &vertexCopy);
        vkCmdCopyBuffer(
            upload.commandBuffer,
            uploadRing_->buffer(),
            indexBlocks_[allocation.indexBlock].buffer,
            1,
            &indexCopy);

        const VkBufferMemoryBarrier2 barriers[] {
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                .buffer = vertexBlocks_[allocation.vertexBlock].buffer,
                .offset = allocation.vertex.offset,
                .size = allocation.vertex.size,
            },
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT,
                .buffer = indexBlocks_[allocation.indexBlock].buffer,
                .offset = allocation.index.offset,
                .size = allocation.index.size,
            },
        };
        const VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = static_cast<uint32_t>(std::size(barriers)),
            .pBufferMemoryBarriers = barriers,
        };
        vkCmdPipelineBarrier2(upload.commandBuffer, &dependency);
        upload.fence = vulkanResources::submitOneShotCommands(
            device_, graphicsQueue_, upload.commandBuffer, "geometry upload");
        uploadRing_->commit(upload.staging);
        upload.submitted = true;
        return upload;
    } catch (...) {
        destroyUpload(upload);
        throw;
    }
}

bool VulkanGeometryArena::uploadComplete(const Upload& upload) const
{
    if (!upload.submitted || !upload.fence) {
        return false;
    }
    const VkResult status = vkGetFenceStatus(device_, upload.fence);
    if (status == VK_NOT_READY) {
        return false;
    }
    vkCheck(status, "vkGetFenceStatus geometry upload failed");
    return true;
}

void VulkanGeometryArena::destroyUpload(Upload& upload) const
{
    if (upload.staging.valid() && uploadRing_) {
        if (upload.submitted) {
            uploadRing_->complete(upload.staging);
        } else {
            uploadRing_->abandon(upload.staging);
        }
    }
    if (upload.fence) {
        vkDestroyFence(device_, upload.fence, nullptr);
    }
    if (upload.commandBuffer) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &upload.commandBuffer);
    }
    upload = {};
}

VkBuffer VulkanGeometryArena::vertexBuffer(const Allocation& allocation) const
{
    return allocation.valid() && allocation.vertexBlock < vertexBlocks_.size()
        ? vertexBlocks_[allocation.vertexBlock].buffer
        : VK_NULL_HANDLE;
}

VkBuffer VulkanGeometryArena::indexBuffer(const Allocation& allocation) const
{
    return allocation.valid() && allocation.indexBlock < indexBlocks_.size()
        ? indexBlocks_[allocation.indexBlock].buffer
        : VK_NULL_HANDLE;
}

VkDeviceSize VulkanGeometryArena::vertexOffset(const Allocation& allocation) const
{
    return allocation.vertex.offset;
}

VkDeviceSize VulkanGeometryArena::indexOffset(const Allocation& allocation) const
{
    return allocation.index.offset;
}

VulkanGeometryArena::Statistics VulkanGeometryArena::statistics() const noexcept
{
    const auto summarize = [](const std::vector<Block>& blocks) {
        BufferStatistics result;
        for (const Block& block : blocks) {
            if (block.buffer) {
                result.usedBytes += block.allocator.usedBytes();
                result.reservedBytes += block.allocator.capacity();
                ++result.blockCount;
            }
        }
        return result;
    };
    return { .vertex = summarize(vertexBlocks_), .index = summarize(indexBlocks_) };
}

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
void VulkanGeometryArena::failNextIndexBlockCreationForTesting()
{
    failNextIndexBlockCreation.store(true, std::memory_order_relaxed);
}
#endif

VulkanGeometryArena::Block VulkanGeometryArena::createBlock(
    VkDeviceSize capacity,
    VkBufferUsageFlags usage,
    const char* name) const
{
    Block result;
    // Reserve the host free-list entry before creating the GPU resource.
    result.allocator = GeometrySuballocator(capacity);
    VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    try {
        memoryAllocator_->createBuffer(
            bufferInfo,
            VulkanMemoryUsage::DeviceLocal,
            result.buffer,
            result.allocation,
            nullptr,
            name);
        vulkanDebug::setObjectName(device_, VK_OBJECT_TYPE_BUFFER, result.buffer, name);
    } catch (...) {
        // createBuffer can fail after assigning the handles, for example while
        // allocating its debug name. Clean up that partially created resource.
        destroyBlock(result);
        throw;
    }
    return result;
}

GeometrySuballocator::Allocation VulkanGeometryArena::allocateSlice(
    std::vector<Block>& blocks,
    uint32_t& blockIndex,
    VkDeviceSize bytes,
    VkBufferUsageFlags usage,
    VkDeviceSize preferredBlockBytes,
    const char* name)
{
    uint32_t vacantIndex = UINT32_MAX;
    for (uint32_t index = 0; index < blocks.size(); ++index) {
        if (!blocks[index].buffer) {
            if (vacantIndex == UINT32_MAX) {
                vacantIndex = index;
            }
            continue;
        }
        if (const auto allocation = blocks[index].allocator.allocate(
                bytes, geometryAlignment)) {
            blockIndex = index;
            return *allocation;
        }
    }
#ifdef SOKOBAN_ENABLE_TEST_HOOKS
    if ((usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0 &&
        failNextIndexBlockCreation.exchange(false, std::memory_order_relaxed)) {
        throw std::runtime_error("Injected geometry index block creation failure");
    }
#endif
    if (vacantIndex == UINT32_MAX) {
        if (blocks.size() >= UINT32_MAX) {
            throw std::length_error("Geometry arena has exhausted block indices");
        }
        vacantIndex = static_cast<uint32_t>(blocks.size());
        // Grow the host vector before owning a new VkBuffer, so insertion
        // failure cannot leak GPU storage. Empty slots preserve live indices.
        blocks.emplace_back();
    }
    const VkDeviceSize capacity = std::max(preferredBlockBytes, bytes);
    Block& block = blocks[vacantIndex];
    block = createBlock(capacity, usage, name);
    try {
        const auto allocation = block.allocator.allocate(bytes, geometryAlignment);
        if (!allocation) {
            throw std::runtime_error("New geometry arena block could not satisfy allocation");
        }
        blockIndex = vacantIndex;
        return *allocation;
    } catch (...) {
        destroyBlock(block);
        throw;
    }
}

void VulkanGeometryArena::destroyBlock(Block& block) const noexcept
{
    if (block.buffer || block.allocation) {
        memoryAllocator_->destroyBuffer(block.buffer, block.allocation);
    }
    block.buffer = VK_NULL_HANDLE;
    block.allocation = nullptr;
    block.allocator = GeometrySuballocator(0);
}

} // namespace sokoban
