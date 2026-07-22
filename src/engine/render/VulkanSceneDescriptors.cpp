#include "engine/render/VulkanSceneDescriptors.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace sokoban {
namespace {

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

} // namespace

VulkanSceneDescriptors::~VulkanSceneDescriptors()
{
    destroy();
}

void VulkanSceneDescriptors::create(
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
    modelTextureCount_ = static_cast<uint32_t>(resources.modelTextures.size());

    try {
        std::array<VkDescriptorSetLayoutBinding, 7> bindings {
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
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        vkCheck(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout_),
            "vkCreateDescriptorSetLayout failed");

        VkDescriptorPoolSize poolSize {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = (modelTextureCount_ + 6) * setCount,
        };
        VkDescriptorPoolCreateInfo poolInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = setCount,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool_),
            "vkCreateDescriptorPool failed");

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
        update(resources);
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanSceneDescriptors::update(const Resources& resources) const
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(sets_.size()); ++i) {
        update(i, resources);
    }
}

void VulkanSceneDescriptors::update(
    uint32_t setIndex,
    const Resources& resources) const
{
    const VkDescriptorSet descriptorSet = set(setIndex);
    if (!descriptorSet) {
        throw std::runtime_error("Scene descriptors have not been created");
    }
    if (!resources.shadow.valid() ||
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
    std::array<VkWriteDescriptorSet, 7> writes {
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
    };
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VulkanSceneDescriptors::destroy()
{
    if (device_) {
        if (pool_) {
            vkDestroyDescriptorPool(device_, pool_, nullptr);
        }
        if (layout_) {
            vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
        }
    }
    sets_.clear();
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    modelTextureCount_ = 0;
    device_ = VK_NULL_HANDLE;
}

const VkDescriptorSet& VulkanSceneDescriptors::set(uint32_t setIndex) const
{
    if (setIndex >= sets_.size()) {
        static const VkDescriptorSet nullSet = VK_NULL_HANDLE;
        return nullSet;
    }
    return sets_[setIndex];
}

} // namespace sokoban
