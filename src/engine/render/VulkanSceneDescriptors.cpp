#include "engine/render/VulkanSceneDescriptors.hpp"

#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/LightingConfig.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace sokoban {

VulkanSceneDescriptors::~VulkanSceneDescriptors()
{
    destroy();
}

void VulkanSceneDescriptors::create(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    const Resources& resources,
    uint32_t setCount)
{
    destroy();
    if (resources.modelTextures.empty()) {
        throw std::runtime_error("Asset catalog must contain at least one model texture");
    }
    if (setCount == 0) {
        throw std::runtime_error("Scene descriptors require at least one set");
    }
    device_ = device;
    frameSetCount_ = setCount;
    setCount *= 2;
    modelTextureCount_ = static_cast<uint32_t>(resources.modelTextures.size());

    try {
        std::array<VkDescriptorSetLayoutBinding, sceneSingleImageBindings + 2>
            bindings {
            VkDescriptorSetLayoutBinding {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = modelTextureCount_,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 4,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 5,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 6,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 7,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                    VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 8,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        vkCheck(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout_),
            "vkCreateDescriptorSetLayout failed");
        vulkanDebug::setObjectName(
            device_,
            VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
            layout_,
            "Scene descriptor set layout");

        std::array<VkDescriptorPoolSize, 2> poolSizes {
            VkDescriptorPoolSize {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount =
                    (modelTextureCount_ + sceneSingleImageBindings) * setCount,
            },
            VkDescriptorPoolSize {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = setCount,
            },
        };
        VkDescriptorPoolCreateInfo poolInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = setCount,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };
        vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool_),
            "vkCreateDescriptorPool failed");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_DESCRIPTOR_POOL, pool_, "Scene descriptor pool");

        std::vector<VkDescriptorSetLayout> layouts(setCount, layout_);
        sets_.resize(setCount);
        VkDescriptorSetAllocateInfo allocateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool_,
            .descriptorSetCount = setCount,
            .pSetLayouts = layouts.data(),
        };
        vkCheck(vkAllocateDescriptorSets(device_, &allocateInfo, sets_.data()),
            "vkAllocateDescriptorSets failed");
        lightingBuffers_.reserve(setCount);
        for (uint32_t i = 0; i < setCount; ++i) {
            lightingBuffers_.push_back(createLightingBuffer(physicalDevice));
        }
        update(resources);
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanSceneDescriptors::update(const Resources& resources) const
{
    for (uint32_t i = 0; i < frameSetCount_; ++i) {
        update(i, resources);
    }
}

void VulkanSceneDescriptors::update(
    uint32_t setIndex,
    const Resources& resources) const
{
    updateInternal(setIndex * 2, resources);
    updateInternal(setIndex * 2 + 1, resources);
}

void VulkanSceneDescriptors::updateInternal(
    uint32_t internalSetIndex,
    const Resources& resources) const
{
    const VkDescriptorSet descriptorSet =
        internalSetIndex < sets_.size()
        ? sets_[internalSetIndex]
        : VK_NULL_HANDLE;
    if (!descriptorSet) {
        throw std::runtime_error("Scene descriptors have not been created");
    }
    if (!resources.shadow.valid() ||
        !resources.pointShadows.valid() ||
        !resources.sceneColor.valid() ||
        !resources.sceneDepth.valid() ||
        !resources.ssao.valid() ||
        !resources.uiFont.valid() ||
        !resources.titleBackground.valid() ||
        resources.modelTextures.size() != modelTextureCount_) {
        throw std::runtime_error("Scene descriptor resources are incomplete");
    }

    std::vector<VkDescriptorImageInfo> modelImages;
    modelImages.reserve(resources.modelTextures.size());
    for (const VulkanModelResources::TextureView texture : resources.modelTextures) {
        if (!texture.valid()) {
            throw std::runtime_error("Model texture descriptor is incomplete");
        }
        modelImages.push_back({
            .sampler = texture.sampler,
            .imageView = texture.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
    }

    const VkDescriptorImageInfo shadow {
        .sampler = resources.shadow.sampler,
        .imageView = resources.shadow.imageView,
        .imageLayout = resources.shadow.imageLayout,
    };
    const VkDescriptorImageInfo pointShadows {
        .sampler = resources.pointShadows.sampler,
        .imageView = resources.pointShadows.imageView,
        .imageLayout = resources.pointShadows.imageLayout,
    };
    const VkDescriptorBufferInfo lighting {
        .buffer = lightingBuffers_[internalSetIndex].buffer,
        .offset = 0,
        .range = sizeof(SceneLightingUniform),
    };
    const VkDescriptorImageInfo sceneColor {
        .sampler = resources.sceneColor.sampler,
        .imageView = resources.sceneColor.imageView,
        .imageLayout = resources.sceneColor.imageLayout,
    };
    const VkDescriptorImageInfo sceneDepth {
        .sampler = resources.sceneDepth.sampler,
        .imageView = resources.sceneDepth.imageView,
        .imageLayout = resources.sceneDepth.imageLayout,
    };
    const VkDescriptorImageInfo ssao {
        .sampler = resources.ssao.sampler,
        .imageView = resources.ssao.imageView,
        .imageLayout = resources.ssao.imageLayout,
    };
    const VkDescriptorImageInfo uiFont {
        .sampler = resources.uiFont.sampler,
        .imageView = resources.uiFont.imageView,
        .imageLayout = resources.uiFont.imageLayout,
    };
    const VkDescriptorImageInfo titleBackground {
        .sampler = resources.titleBackground.sampler,
        .imageView = resources.titleBackground.imageView,
        .imageLayout = resources.titleBackground.imageLayout,
    };
    std::array<VkWriteDescriptorSet, 9> writes {
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &uiFont,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 4,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &titleBackground,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &shadow,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &sceneColor,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 2,
            .descriptorCount = static_cast<uint32_t>(modelImages.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = modelImages.data(),
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 5,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &sceneDepth,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 6,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &ssao,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 7,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &lighting,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 8,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &pointShadows,
        },
    };
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VulkanSceneDescriptors::destroy()
{
    if (device_) {
        for (OwnedBuffer& buffer : lightingBuffers_) {
            if (buffer.mapped) {
                vkUnmapMemory(device_, buffer.memory);
            }
            if (buffer.buffer) {
                vkDestroyBuffer(device_, buffer.buffer, nullptr);
            }
            if (buffer.memory) {
                vkFreeMemory(device_, buffer.memory, nullptr);
            }
        }
        if (pool_) {
            vkDestroyDescriptorPool(device_, pool_, nullptr);
        }
        if (layout_) {
            vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
        }
    }
    sets_.clear();
    lightingBuffers_.clear();
    frameSetCount_ = 0;
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    modelTextureCount_ = 0;
    device_ = VK_NULL_HANDLE;
}

VulkanSceneDescriptors::OwnedBuffer
VulkanSceneDescriptors::createLightingBuffer(
    VkPhysicalDevice physicalDevice) const
{
    OwnedBuffer result;
    const VkBufferCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(SceneLightingUniform),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCheck(vkCreateBuffer(device_, &info, nullptr, &result.buffer),
        "vkCreateBuffer scene lighting failed");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_BUFFER, result.buffer, "Scene lighting buffer");
    try {
        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(device_, result.buffer, &requirements);
        const VkMemoryAllocateInfo allocation {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = vulkanResources::findMemoryType(
                physicalDevice,
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkCheck(vkAllocateMemory(
                    device_, &allocation, nullptr, &result.memory),
            "vkAllocateMemory scene lighting failed");
        vulkanDebug::setObjectName(
            device_,
            VK_OBJECT_TYPE_DEVICE_MEMORY,
            result.memory,
            "Scene lighting buffer memory");
        vkCheck(vkBindBufferMemory(
                    device_, result.buffer, result.memory, 0),
            "vkBindBufferMemory scene lighting failed");
        vkCheck(vkMapMemory(
                    device_, result.memory, 0,
                    sizeof(SceneLightingUniform), 0, &result.mapped),
            "vkMapMemory scene lighting failed");
        std::memset(result.mapped, 0, sizeof(SceneLightingUniform));
    } catch (...) {
        if (result.memory) {
            vkFreeMemory(device_, result.memory, nullptr);
        }
        if (result.buffer) {
            vkDestroyBuffer(device_, result.buffer, nullptr);
        }
        throw;
    }
    return result;
}

void VulkanSceneDescriptors::updateLighting(
    uint32_t setIndex,
    const RenderFrameData::Lighting& lighting,
    const ShadowRenderLayout& shadowLayout,
    bool preview) const
{
    const uint32_t internalSetIndex = setIndex * 2 + (preview ? 1U : 0U);
    if (internalSetIndex >= lightingBuffers_.size() ||
        !lightingBuffers_[internalSetIndex].mapped) {
        throw std::runtime_error("Scene lighting buffer is unavailable");
    }
    SceneLightingUniform uniform {
        .sunShadowRightAndHalfWidth = {
            shadowLayout.lightRight.x,
            shadowLayout.lightRight.y,
            shadowLayout.lightRight.z,
            shadowLayout.halfWidth,
        },
        .sunShadowUpAndHalfHeight = {
            shadowLayout.lightUp.x,
            shadowLayout.lightUp.y,
            shadowLayout.lightUp.z,
            shadowLayout.halfHeight,
        },
        .sunShadowForwardAndDepthRange = {
            shadowLayout.lightForward.x,
            shadowLayout.lightForward.y,
            shadowLayout.lightForward.z,
            std::max(
                shadowLayout.farthestDepth - shadowLayout.nearestDepth,
                0.001f),
        },
        .sunShadowCenterAndNearestDepth = {
            shadowLayout.center.x,
            shadowLayout.center.y,
            shadowLayout.center.z,
            shadowLayout.nearestDepth,
        },
    };
    const std::size_t count = std::min(
        lighting.pointLightCount,
        RenderFrameData::pointLightCapacity);
    for (std::size_t i = 0; i < count; ++i) {
        const RenderFrameData::PointLight& light = lighting.pointLights[i];
        const bool shadowMapRendered = light.castsShadows &&
            light.intensity > 0.0f &&
            light.range > config::pointShadowNearPlane;
        uniform.pointLights[i] = {
            .positionAndRange = {
                light.position.x, light.position.y, light.position.z,
                std::max(light.range, 0.05f),
            },
            .colorAndIntensity = {
                std::max(light.color.x, 0.0f),
                std::max(light.color.y, 0.0f),
                std::max(light.color.z, 0.0f),
                std::max(light.intensity, 0.0f),
            },
            .shadowOptions = {
                shadowMapRendered ? 1.0f : 0.0f,
                static_cast<float>(i),
                std::max(light.shadowBias, 0.0f),
                std::clamp(light.shadowOpacity, 0.0f, 1.0f),
            },
        };
    }
    uniform.pointLightMeta.x = static_cast<float>(count);
    std::memcpy(
        lightingBuffers_[internalSetIndex].mapped,
        &uniform,
        sizeof(uniform));
}

const VkDescriptorSet& VulkanSceneDescriptors::set(
    uint32_t setIndex,
    bool preview) const
{
    const uint32_t internalSetIndex = setIndex * 2 + (preview ? 1U : 0U);
    if (internalSetIndex >= sets_.size()) {
        static const VkDescriptorSet nullSet = VK_NULL_HANDLE;
        return nullSet;
    }
    return sets_[internalSetIndex];
}

} // namespace sokoban
