#include "engine/render/VulkanSceneDescriptors.hpp"

#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/LightingConfig.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace sokoban {

namespace {

// The scene set's layout, as data.
//
// This was twelve VkDescriptorSetLayoutBinding aggregates written out longhand,
// eighty-five lines of which the only varying parts were these three fields.
// The length was the smaller problem. The descriptor pool has to be sized per
// descriptor type, and those sizes were three separate hand-kept numbers -
// `sceneSingleImageBindings`, an implicit 1 for the uniform buffer, and a bare
// literal 3 for the storage buffers. Adding a binding meant remembering to
// change the matching one, with nothing to say which that was. They are counted
// off the table now.
//
// The binding numbers are the contract with the shaders, which declare them by
// hand; they are not indices into this array and their order here is only
// convention.
//
// Binding 2 is deliberately vacant. No shader declares it and no C++ writes
// it, and this file is the only record of that: the slot was retired and the
// numbering was not compacted, because every later binding is named by hand in
// the shaders that use it and renumbering would mean editing all of them to no
// effect. **Do not reuse 2.** A new binding continues at 13; a reused 2 would
// silently match any shader still carrying an old declaration.
//
// The array's size is deduced from the table rather than written beside it, so
// adding a row cannot leave a count behind.
struct SceneBinding {
    uint32_t binding;
    VkDescriptorType type;
    VkShaderStageFlags stages;
};

constexpr auto sceneBindings = std::to_array<SceneBinding>({
    SceneBinding { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
    // Fragment too since T1: the material half of a draw's parameters is read
    // here rather than pushed.
    SceneBinding { 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
    SceneBinding { 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
    // Fragment only for now. A vertex stage that wanted to fold a material into
    // its transform would have to be added here as well as declared there.
    SceneBinding { 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
});

// The type a binding was declared with. Returning MAX_ENUM for an unknown
// binding is deliberate: it is not a valid descriptor type, so a caller that
// forgets to check gets a validation error naming the binding rather than a
// silently wrong write.
[[nodiscard]] constexpr VkDescriptorType sceneBindingType(uint32_t binding)
{
    for (const SceneBinding& entry : sceneBindings) {
        if (entry.binding == binding) {
            return entry.type;
        }
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

[[nodiscard]] constexpr bool isImageBinding(VkDescriptorType type)
{
    return type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

// One row of the update below: which binding, and which of the two info
// structs feeds it. The descriptor *type* is deliberately absent - it comes
// from the layout table above, so a binding cannot be declared one type there
// and written as another here.
struct SceneWriteSource {
    uint32_t binding = 0;
    const VkDescriptorImageInfo* image = nullptr;
    const VkDescriptorBufferInfo* buffer = nullptr;
};

// Pinned at compile time, because sceneBindingType is what the writes below
// now trust for every descriptor type. Binding 2 is the vacant one, and its
// answer being MAX_ENUM rather than a plausible default is what turns a
// mistyped binding into a thrown error instead of a wrong write.
static_assert(sceneBindingType(0) == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
static_assert(sceneBindingType(7) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
static_assert(sceneBindingType(9) == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
static_assert(sceneBindingType(12) == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
static_assert(sceneBindingType(2) == VK_DESCRIPTOR_TYPE_MAX_ENUM);
static_assert(sceneBindingType(13) == VK_DESCRIPTOR_TYPE_MAX_ENUM);
static_assert(isImageBinding(sceneBindingType(0)));
static_assert(!isImageBinding(sceneBindingType(7)));
static_assert(!isImageBinding(sceneBindingType(9)));

// How many of the set's bindings ask for `type`, which is what the descriptor
// pool needs to be told.
[[nodiscard]] constexpr uint32_t bindingsOfType(VkDescriptorType type)
{
    uint32_t count = 0;
    for (const SceneBinding& binding : sceneBindings) {
        if (binding.type == type) {
            ++count;
        }
    }
    return count;
}

// sceneSingleImageBindings is also read by VulkanDeviceContext to size the
// texture heap's share of the pool, so it stays a named constant rather than
// becoming a call - but it has to keep agreeing with the table.
static_assert(
    bindingsOfType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        == sceneSingleImageBindings,
    "sceneSingleImageBindings must equal the number of sampler bindings in "
    "sceneBindings; the descriptor pool and the texture heap are both sized "
    "from it");

// Every binding here is one descriptor. The texture heap is the only
// variable-count binding and it lives in its own set, which is what a variable
// count requires.
[[nodiscard]] constexpr std::array<
    VkDescriptorSetLayoutBinding, sceneBindings.size()>
sceneLayoutBindings()
{
    std::array<VkDescriptorSetLayoutBinding, sceneBindings.size()> layout {};
    for (std::size_t index = 0; index < sceneBindings.size(); ++index) {
        layout[index] = VkDescriptorSetLayoutBinding {
            .binding = sceneBindings[index].binding,
            .descriptorType = sceneBindings[index].type,
            .descriptorCount = 1,
            .stageFlags = sceneBindings[index].stages,
        };
    }
    return layout;
}

} // namespace

VulkanSceneDescriptors::~VulkanSceneDescriptors()
{
    destroy();
}

void VulkanSceneDescriptors::create(
    VulkanMemoryAllocator& allocator,
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
    allocator_ = &allocator;
    frameSetCount_ = setCount;
    const uint32_t sceneSetCount = setCount * 2;
    modelTextureCount_ = static_cast<uint32_t>(resources.modelTextures.size());

    try {
        static constexpr auto bindings = sceneLayoutBindings();
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

        const VkDescriptorSetLayoutBinding textureBinding {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = modelTextureCount_,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        const VkDescriptorBindingFlags textureBindingFlags =
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo {
            .sType =
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = 1,
            .pBindingFlags = &textureBindingFlags,
        };
        const VkDescriptorSetLayoutCreateInfo textureLayoutInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &bindingFlagsInfo,
            .bindingCount = 1,
            .pBindings = &textureBinding,
        };
        VkDescriptorSetVariableDescriptorCountLayoutSupport variableCountSupport {
            .sType =
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT,
        };
        VkDescriptorSetLayoutSupport textureLayoutSupport {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT,
            .pNext = &variableCountSupport,
        };
        vkGetDescriptorSetLayoutSupport(
            device_, &textureLayoutInfo, &textureLayoutSupport);
        if (textureLayoutSupport.supported != VK_TRUE ||
            variableCountSupport.maxVariableDescriptorCount <
                modelTextureCount_) {
            throw std::runtime_error(
                "Texture descriptor layout requires " +
                std::to_string(modelTextureCount_) +
                " variable descriptors, but the logical device supports " +
                std::to_string(
                    variableCountSupport.maxVariableDescriptorCount));
        }
        vkCheck(
            vkCreateDescriptorSetLayout(
                device_, &textureLayoutInfo, nullptr, &textureLayout_),
            "vkCreateDescriptorSetLayout texture heap failed");
        vulkanDebug::setObjectName(
            device_,
            VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
            textureLayout_,
            "Texture heap descriptor set layout");

        std::array<VkDescriptorPoolSize, 3> poolSizes {
            VkDescriptorPoolSize {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount =
                    bindingsOfType(
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) *
                        sceneSetCount +
                    modelTextureCount_ * frameSetCount_,
            },
            VkDescriptorPoolSize {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount =
                    bindingsOfType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) *
                    sceneSetCount,
            },
            VkDescriptorPoolSize {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount =
                    bindingsOfType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) *
                    sceneSetCount,
            },
        };
        VkDescriptorPoolCreateInfo poolInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = sceneSetCount + frameSetCount_,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };
        vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool_),
            "vkCreateDescriptorPool failed");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_DESCRIPTOR_POOL, pool_, "Scene descriptor pool");

        std::vector<VkDescriptorSetLayout> layouts(sceneSetCount, layout_);
        sets_.resize(sceneSetCount);
        VkDescriptorSetAllocateInfo allocateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool_,
            .descriptorSetCount = sceneSetCount,
            .pSetLayouts = layouts.data(),
        };
        vkCheck(vkAllocateDescriptorSets(device_, &allocateInfo, sets_.data()),
            "vkAllocateDescriptorSets failed");

        std::vector<VkDescriptorSetLayout> textureLayouts(
            frameSetCount_, textureLayout_);
        std::vector<uint32_t> textureDescriptorCounts(
            frameSetCount_, modelTextureCount_);
        const VkDescriptorSetVariableDescriptorCountAllocateInfo variableCounts {
            .sType =
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
            .descriptorSetCount = frameSetCount_,
            .pDescriptorCounts = textureDescriptorCounts.data(),
        };
        textureSets_.resize(frameSetCount_);
        const VkDescriptorSetAllocateInfo textureAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = &variableCounts,
            .descriptorPool = pool_,
            .descriptorSetCount = frameSetCount_,
            .pSetLayouts = textureLayouts.data(),
        };
        vkCheck(
            vkAllocateDescriptorSets(
                device_, &textureAllocateInfo, textureSets_.data()),
            "vkAllocateDescriptorSets texture heap failed");

        frameBuffers_.reserve(sceneSetCount);
        for (uint32_t i = 0; i < sceneSetCount; ++i) {
            frameBuffers_.push_back(createFrameBuffer());
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
    updateTextureSet(setIndex, resources);
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
    // Named rather than or-ed together, for two reasons: an eleven-clause
    // condition is one a reader has to check against the writes below by eye,
    // and "resources are incomplete" told whoever hit it nothing about which
    // one. A missing entry here is a descriptor written from a null handle,
    // which validation catches but only on a validation build.
    const std::array<std::pair<const char*, bool>, 11> required {
        std::pair { "shadow", resources.shadow.valid() },
        std::pair { "pointShadows", resources.pointShadows.valid() },
        std::pair { "sceneColor", resources.sceneColor.valid() },
        std::pair { "sceneHdrColor", resources.sceneHdrColor.valid() },
        std::pair { "sceneDepth", resources.sceneDepth.valid() },
        std::pair { "ssao", resources.ssao.valid() },
        std::pair { "uiFont", resources.uiFont.valid() },
        std::pair { "titleBackground", resources.titleBackground.valid() },
        std::pair { "skinning", resources.skinning.valid() },
        std::pair { "drawInstances", resources.drawInstances.valid() },
        std::pair { "materials", resources.materials.valid() },
    };
    for (const auto& [name, present] : required) {
        if (!present) {
            throw std::runtime_error(
                std::string("Scene descriptor resource is missing: ") + name);
        }
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
        .buffer = frameBuffers_[internalSetIndex].handle(),
        .offset = 0,
        .range = sizeof(SceneFrameUniform),
    };
    const VkDescriptorBufferInfo skinning {
        .buffer = resources.skinning.buffer,
        .offset = 0,
        .range = resources.skinning.range,
    };
    const VkDescriptorBufferInfo drawInstances {
        .buffer = resources.drawInstances.buffer,
        .offset = 0,
        .range = resources.drawInstances.range,
    };
    const VkDescriptorBufferInfo materials {
        .buffer = resources.materials.buffer,
        .offset = 0,
        .range = resources.materials.range,
    };
    const VkDescriptorImageInfo sceneColor {
        .sampler = resources.sceneColor.sampler,
        .imageView = resources.sceneColor.imageView,
        .imageLayout = resources.sceneColor.imageLayout,
    };
    const VkDescriptorImageInfo sceneHdrColor {
        .sampler = resources.sceneHdrColor.sampler,
        .imageView = resources.sceneHdrColor.imageView,
        .imageLayout = resources.sceneHdrColor.imageLayout,
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
    // Twelve hand-written VkWriteDescriptorSet blocks, in the binding order
    // 3, 4, 0, 1, 5, 6, ... - each repeating its binding number and its
    // descriptor type beside the one thing that actually differed. The order
    // was not meaningful: writes to distinct descriptors within a single
    // vkUpdateDescriptorSets are independent, so this now runs in the order
    // the layout declares, which is the order a reader checks it against.
    const std::array<SceneWriteSource, sceneBindings.size()> sources {
        SceneWriteSource { 0, &shadow, nullptr },
        SceneWriteSource { 1, &sceneColor, nullptr },
        SceneWriteSource { 3, &uiFont, nullptr },
        SceneWriteSource { 4, &titleBackground, nullptr },
        SceneWriteSource { 5, &sceneDepth, nullptr },
        SceneWriteSource { 6, &ssao, nullptr },
        SceneWriteSource { 7, nullptr, &lighting },
        SceneWriteSource { 8, &pointShadows, nullptr },
        SceneWriteSource { 9, nullptr, &skinning },
        SceneWriteSource { 10, nullptr, &drawInstances },
        SceneWriteSource { 11, &sceneHdrColor, nullptr },
        SceneWriteSource { 12, nullptr, &materials },
    };

    std::array<VkWriteDescriptorSet, sceneBindings.size()> writes {};
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const SceneWriteSource& source = sources[index];
        const VkDescriptorType type = sceneBindingType(source.binding);
        // Three ways a row can be wrong that the hand-written version could
        // not notice: a binding the layout does not declare, an image row on
        // a buffer binding, and a row with nothing to write. All of them
        // produce a descriptor set the shaders read garbage through, so they
        // are worth a throw rather than a silent write.
        if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
            throw std::runtime_error(
                "Scene descriptor write names binding "
                + std::to_string(source.binding)
                + ", which the scene layout does not declare");
        }
        const bool wantsImage = isImageBinding(type);
        if (wantsImage != (source.image != nullptr)
            || wantsImage == (source.buffer != nullptr)) {
            throw std::runtime_error(
                "Scene descriptor write for binding "
                + std::to_string(source.binding)
                + " supplies the wrong kind of descriptor info");
        }
        writes[index] = VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = source.binding,
            .descriptorCount = 1,
            .descriptorType = type,
            .pImageInfo = source.image,
            .pBufferInfo = source.buffer,
        };
    }

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VulkanSceneDescriptors::updateTextureSet(
    uint32_t setIndex,
    const Resources& resources) const
{
    const VkDescriptorSet descriptorSet = setIndex < textureSets_.size()
        ? textureSets_[setIndex]
        : VK_NULL_HANDLE;
    if (!descriptorSet) {
        throw std::runtime_error("Texture heap descriptors have not been created");
    }
    if (resources.modelTextures.size() != modelTextureCount_) {
        throw std::runtime_error(
            "Texture heap resources do not match its stable capacity");
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
    const VkWriteDescriptorSet write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = static_cast<uint32_t>(modelImages.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = modelImages.data(),
    };
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void VulkanSceneDescriptors::destroy()
{
    if (device_) {
        for (GpuMappedBuffer& buffer : frameBuffers_) {
            buffer.destroy(allocator_);
        }
        if (pool_) {
            vkDestroyDescriptorPool(device_, pool_, nullptr);
        }
        if (layout_) {
            vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
        }
        if (textureLayout_) {
            vkDestroyDescriptorSetLayout(device_, textureLayout_, nullptr);
        }
    }
    sets_.clear();
    textureSets_.clear();
    frameBuffers_.clear();
    frameSetCount_ = 0;
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    textureLayout_ = VK_NULL_HANDLE;
    modelTextureCount_ = 0;
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
}

GpuMappedBuffer VulkanSceneDescriptors::createFrameBuffer() const
{
    GpuMappedBuffer result;
    try {
        result.create(
            *allocator_,
            sizeof(SceneFrameUniform),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            "Scene frame uniform buffer");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_BUFFER, result.handle(),
            "Scene frame uniform buffer");
        std::memset(result.mapped(), 0, sizeof(SceneFrameUniform));
    } catch (...) {
        result.destroy(allocator_);
        throw;
    }
    return result;
}

void VulkanSceneDescriptors::updateFrame(
    uint32_t setIndex,
    const RenderFrameData::Lighting& lighting,
    const SceneCamera& camera,
    bool preview) const
{
    const uint32_t internalSetIndex = setIndex * 2 + (preview ? 1U : 0U);
    if (internalSetIndex >= frameBuffers_.size() ||
        !frameBuffers_[internalSetIndex].mapped()) {
        throw std::runtime_error("Scene frame uniform buffer is unavailable");
    }
    SceneFrameUniform uniform {
        .clipFromWorld = camera.clipFromWorld,
        .shadowFromWorld = camera.shadowFromWorld,
        .cameraPositionAndNearPlane = {
            camera.position.x,
            camera.position.y,
            camera.position.z,
            camera.nearPlane,
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
        frameBuffers_[internalSetIndex].mapped(),
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

const VkDescriptorSet& VulkanSceneDescriptors::textureSet(
    uint32_t setIndex) const
{
    if (setIndex >= textureSets_.size()) {
        static const VkDescriptorSet nullSet = VK_NULL_HANDLE;
        return nullSet;
    }
    return textureSets_[setIndex];
}

} // namespace sokoban
