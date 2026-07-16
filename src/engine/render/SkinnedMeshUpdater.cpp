#include "engine/render/SkinnedMeshUpdater.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace sokoban {
namespace {

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

} // namespace

SkinnedMeshUpdater::~SkinnedMeshUpdater()
{
    destroy();
}

void SkinnedMeshUpdater::create(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    SkinnedMeshData sourceMesh,
    const GltfAnimationClip& initialClip)
{
    destroy();
    physicalDevice_ = physicalDevice;
    device_ = device;

    try {
        const MeshData initialMesh = skinGltfMesh(sourceMesh, initialClip, 0.0f);
        if (initialMesh.vertices.empty() || initialMesh.indices.empty()) {
            throw std::runtime_error("Skinned glTF mesh contains no geometry");
        }

        sourceMesh_ = std::move(sourceMesh);
        vertexCount_ = static_cast<uint32_t>(initialMesh.vertices.size());
        indexCount_ = static_cast<uint32_t>(initialMesh.indices.size());
        vertexBuffer_ = createBuffer(
            sizeof(MeshVertex) * initialMesh.vertices.size(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        indexBuffer_ = createBuffer(
            sizeof(uint32_t) * initialMesh.indices.size(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        uploadVertices(initialMesh.vertices);
        const VkDeviceSize indexBytes = sizeof(uint32_t) * initialMesh.indices.size();
        void* mapped = nullptr;
        vkCheck(vkMapMemory(device_, indexBuffer_.memory, 0, indexBytes, 0, &mapped),
            "vkMapMemory skinned index buffer failed");
        std::memcpy(mapped, initialMesh.indices.data(), static_cast<std::size_t>(indexBytes));
        vkUnmapMemory(device_, indexBuffer_.memory);
    } catch (...) {
        destroy();
        throw;
    }
}

void SkinnedMeshUpdater::destroy()
{
    if (device_) {
        if (indexBuffer_.buffer) {
            vkDestroyBuffer(device_, indexBuffer_.buffer, nullptr);
        }
        if (indexBuffer_.memory) {
            vkFreeMemory(device_, indexBuffer_.memory, nullptr);
        }
        if (vertexBuffer_.buffer) {
            vkDestroyBuffer(device_, vertexBuffer_.buffer, nullptr);
        }
        if (vertexBuffer_.memory) {
            vkFreeMemory(device_, vertexBuffer_.memory, nullptr);
        }
    }

    indexBuffer_ = {};
    vertexBuffer_ = {};
    sourceMesh_ = {};
    indexCount_ = 0;
    vertexCount_ = 0;
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
}

void SkinnedMeshUpdater::update(const AnimationController::SkinningRequest& request)
{
    if (!valid() || request.toClip == nullptr) {
        return;
    }

    const MeshData skinnedMesh = request.blended()
        ? skinGltfMeshBlended(
            sourceMesh_,
            *request.fromClip,
            request.fromTimeSeconds,
            *request.toClip,
            request.toTimeSeconds,
            request.blend)
        : skinGltfMesh(sourceMesh_, *request.toClip, request.toTimeSeconds);
    uploadVertices(skinnedMesh.vertices);
}

bool SkinnedMeshUpdater::valid() const
{
    return vertexBuffer_.buffer && vertexBuffer_.memory &&
        indexBuffer_.buffer && indexBuffer_.memory &&
        vertexCount_ > 0 && indexCount_ > 0;
}

SkinnedMeshUpdater::MeshView SkinnedMeshUpdater::mesh() const
{
    if (!valid()) {
        throw std::runtime_error("Skinned model mesh has not been created");
    }
    return {
        .vertexBuffer = vertexBuffer_.buffer,
        .indexBuffer = indexBuffer_.buffer,
        .indexCount = indexCount_,
    };
}

uint32_t SkinnedMeshUpdater::findMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1U << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("No suitable Vulkan memory type found for skinned mesh");
}

SkinnedMeshUpdater::OwnedBuffer SkinnedMeshUpdater::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties) const
{
    OwnedBuffer result;
    VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &result.buffer),
        "vkCreateBuffer skinned mesh failed");

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(device_, result.buffer, &requirements);
    uint32_t memoryTypeIndex = 0;
    try {
        memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
    } catch (...) {
        vkDestroyBuffer(device_, result.buffer, nullptr);
        throw;
    }
    VkMemoryAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };
    const VkResult allocationResult = vkAllocateMemory(device_, &allocationInfo, nullptr, &result.memory);
    if (allocationResult != VK_SUCCESS) {
        vkDestroyBuffer(device_, result.buffer, nullptr);
        vkCheck(allocationResult, "vkAllocateMemory skinned mesh failed");
    }
    const VkResult bindResult = vkBindBufferMemory(device_, result.buffer, result.memory, 0);
    if (bindResult != VK_SUCCESS) {
        vkFreeMemory(device_, result.memory, nullptr);
        vkDestroyBuffer(device_, result.buffer, nullptr);
        vkCheck(bindResult, "vkBindBufferMemory skinned mesh failed");
    }
    return result;
}

void SkinnedMeshUpdater::uploadVertices(const std::vector<MeshVertex>& vertices) const
{
    if (vertices.size() != vertexCount_) {
        throw std::runtime_error("Animated mesh vertex count changed during skinning");
    }

    const VkDeviceSize vertexBytes = sizeof(MeshVertex) * vertices.size();
    void* mapped = nullptr;
    vkCheck(vkMapMemory(device_, vertexBuffer_.memory, 0, vertexBytes, 0, &mapped),
        "vkMapMemory skinned vertex buffer failed");
    std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(device_, vertexBuffer_.memory);
}

} // namespace sokoban
