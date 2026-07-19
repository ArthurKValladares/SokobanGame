#include "engine/render/VulkanUiResources.hpp"

#include "engine/ui/FontAtlas.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace sokoban {
namespace {

struct StagingBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

void destroyStaging(VkDevice device, StagingBuffer& staging)
{
    if (staging.buffer) {
        vkDestroyBuffer(device, staging.buffer, nullptr);
    }
    if (staging.memory) {
        vkFreeMemory(device, staging.memory, nullptr);
    }
    staging = {};
}

StagingBuffer createStaging(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    const FontAtlas& font)
{
    StagingBuffer staging;
    const VkDeviceSize size = font.pixels().size();
    VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCheck(vkCreateBuffer(device, &bufferInfo, nullptr, &staging.buffer),
        "vkCreateBuffer UI font staging failed");
    try {
        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(device, staging.buffer, &requirements);
        VkMemoryAllocateInfo allocation {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = vulkanResources::findMemoryType(
                physicalDevice,
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkCheck(vkAllocateMemory(device, &allocation, nullptr, &staging.memory),
            "vkAllocateMemory UI font staging failed");
        vkCheck(vkBindBufferMemory(device, staging.buffer, staging.memory, 0),
            "vkBindBufferMemory UI font staging failed");
        void* mapped = nullptr;
        vkCheck(vkMapMemory(device, staging.memory, 0, size, 0, &mapped),
            "vkMapMemory UI font staging failed");
        std::memcpy(mapped, font.pixels().data(), font.pixels().size());
        vkUnmapMemory(device, staging.memory);
    } catch (...) {
        destroyStaging(device, staging);
        throw;
    }
    return staging;
}

} // namespace

VulkanUiResources::~VulkanUiResources()
{
    destroy();
}

void VulkanUiResources::create(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    const FontAtlas& font)
{
    destroy();
    if (font.width() == 0 || font.height() == 0 || font.pixels().empty()) {
        throw std::runtime_error("UI font atlas contains no pixels");
    }
    device_ = device;
    StagingBuffer staging;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    try {
        staging = createStaging(physicalDevice, device_, font);
        const VkImageCreateInfo imageInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8_UNORM,
            .extent = { font.width(), font.height(), 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        fontImage_ = vulkanResources::createImage(
            physicalDevice, device_, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT);

        const VkCommandBufferAllocateInfo allocateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkCheck(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
            "vkAllocateCommandBuffers UI font upload failed");
        const VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer UI font upload failed");

        const VkImageMemoryBarrier2 toTransfer {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = fontImage_.image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        const VkDependencyInfo toTransferDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toTransfer,
        };
        vkCmdPipelineBarrier2(commandBuffer, &toTransferDependency);

        const VkBufferImageCopy copy {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { font.width(), font.height(), 1 },
        };
        vkCmdCopyBufferToImage(
            commandBuffer,
            staging.buffer,
            fontImage_.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy);

        const VkImageMemoryBarrier2 toRead {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = fontImage_.image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        const VkDependencyInfo toReadDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toRead,
        };
        vkCmdPipelineBarrier2(commandBuffer, &toReadDependency);
        vkCheck(vkEndCommandBuffer(commandBuffer),
            "vkEndCommandBuffer UI font upload failed");

        const VkCommandBufferSubmitInfo commandInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commandBuffer,
        };
        const VkSubmitInfo2 submit {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
        };
        vkCheck(vkQueueSubmit2(graphicsQueue, 1, &submit, VK_NULL_HANDLE),
            "vkQueueSubmit2 UI font upload failed");
        vkCheck(vkQueueWaitIdle(graphicsQueue), "vkQueueWaitIdle UI font upload failed");
        vkFreeCommandBuffers(device_, commandPool, 1, &commandBuffer);
        commandBuffer = VK_NULL_HANDLE;
        destroyStaging(device_, staging);

        const VkSamplerCreateInfo samplerInfo {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = 0.0f,
        };
        vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &fontSampler_),
            "vkCreateSampler UI font failed");
    } catch (...) {
        if (commandBuffer) {
            vkFreeCommandBuffers(device_, commandPool, 1, &commandBuffer);
        }
        destroyStaging(device_, staging);
        destroy();
        throw;
    }
}

void VulkanUiResources::destroy()
{
    if (device_) {
        if (fontSampler_) {
            vkDestroySampler(device_, fontSampler_, nullptr);
        }
        vulkanResources::destroyImage(device_, fontImage_);
    }
    fontSampler_ = VK_NULL_HANDLE;
    fontImage_ = {};
    device_ = VK_NULL_HANDLE;
}

} // namespace sokoban
