#include "engine/render/VulkanPipelineFactory.hpp"

#include "engine/render/GltfMesh.hpp"
#include "engine/render/GroundChunkGeometry.hpp"
#include "engine/render/GpuSkinning.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/ShaderCatalog.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanResourceUtils.hpp"
#include "engine/render/WaterCellCachePlan.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

std::vector<char> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<char> data(size);
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
}

// Every vertex layout the factory binds, in one table.
//
// The scene and the shadow pipelines each hand-rolled their own bindings and
// attribute lists, and the two agreed on the location numbering only by
// inspection: position 0, joints 5, weights 6 and attachment node 7 appear in
// both, spelled out twice. That is exactly the trap the comment below warns
// about, one function further along. A location exists once now.
//
// File scope rather than locals of the helper because a
// VkPipelineVertexInputStateCreateInfo holds pointers into these, and it
// outlives the call that builds it.
constexpr VkVertexInputBindingDescription meshBinding {
    .binding = 0,
    .stride = sizeof(MeshVertex),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
};

constexpr VkVertexInputBindingDescription skinnedBinding {
    .binding = 0,
    .stride = sizeof(GpuSkinnedVertex),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
};

constexpr VkVertexInputBindingDescription groundChunkBinding {
    .binding = 0,
    .stride = sizeof(GroundChunkVertex),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
};

constexpr std::array<VkVertexInputAttributeDescription, 5> groundChunkAttributes {
    VkVertexInputAttributeDescription { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GroundChunkVertex, position) },
    VkVertexInputAttributeDescription { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GroundChunkVertex, normal) },
    VkVertexInputAttributeDescription { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GroundChunkVertex, faceCoord) },
    VkVertexInputAttributeDescription { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GroundChunkVertex, wallCoverage) },
    VkVertexInputAttributeDescription { 4, 0, VK_FORMAT_R32_UINT, offsetof(GroundChunkVertex, tileSlot) },
};

// Locations 8 and 9 rather than 5 and 6 so that a static mesh and a
// skinned one name the same thing the same way: 5 to 7 are the skinning
// attributes, and a tangent that moved depending on the pipeline would be
// a trap in two vertex shaders instead of a number in one table.
constexpr std::array<VkVertexInputAttributeDescription, 6> meshAttributes {
    VkVertexInputAttributeDescription {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(MeshVertex, position),
    },
    VkVertexInputAttributeDescription {
        .location = 1,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(MeshVertex, normal),
    },
    VkVertexInputAttributeDescription {
        .location = 2,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(MeshVertex, uv),
    },
    // Locations 3 and 4 are retired: they carried a texture index and a
    // material flag word per vertex until F3b put both in the material
    // buffer. The rest keep their numbers because the shadow variants of
    // these shaders read the same layout and only some of its slots.
    VkVertexInputAttributeDescription {
        .location = 8,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = offsetof(MeshVertex, tangent),
    },
    VkVertexInputAttributeDescription {
        .location = 9,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(MeshVertex, uv1),
    },
    VkVertexInputAttributeDescription {
        .location = 10,
        .binding = 0,
        .format = VK_FORMAT_R32_UINT,
        .offset = offsetof(MeshVertex, materialIndex),
    },
};

constexpr std::array<VkVertexInputAttributeDescription, 9> skinnedAttributes {
    VkVertexInputAttributeDescription { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuSkinnedVertex, position) },
    VkVertexInputAttributeDescription { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuSkinnedVertex, normal) },
    VkVertexInputAttributeDescription { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuSkinnedVertex, uv) },
    VkVertexInputAttributeDescription { 5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(GpuSkinnedVertex, joints) },
    VkVertexInputAttributeDescription { 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuSkinnedVertex, weights) },
    VkVertexInputAttributeDescription { 7, 0, VK_FORMAT_R32_UINT, offsetof(GpuSkinnedVertex, attachmentNodeIndex) },
    VkVertexInputAttributeDescription { 8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuSkinnedVertex, tangent) },
    VkVertexInputAttributeDescription { 9, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuSkinnedVertex, uv1) },
    VkVertexInputAttributeDescription { 10, 0, VK_FORMAT_R32_UINT, offsetof(GpuSkinnedVertex, materialIndex) },
};

// The shadow pipelines bind position only - and, for a skinned mesh, the three
// attributes the skinning needs to compute it.
constexpr VkVertexInputAttributeDescription meshPositionAttribute {
    .location = 0,
    .binding = 0,
    .format = VK_FORMAT_R32G32B32_SFLOAT,
    .offset = offsetof(MeshVertex, position),
};

constexpr std::array<VkVertexInputAttributeDescription, 4> skinnedPositionAttributes {
    VkVertexInputAttributeDescription { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuSkinnedVertex, position) },
    VkVertexInputAttributeDescription { 5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(GpuSkinnedVertex, joints) },
    VkVertexInputAttributeDescription { 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuSkinnedVertex, weights) },
    VkVertexInputAttributeDescription { 7, 0, VK_FORMAT_R32_UINT, offsetof(GpuSkinnedVertex, attachmentNodeIndex) },
};

#if SOKOBAN_ENABLE_DEBUG_UI
constexpr std::array meshOutlineAttributes { meshAttributes[0], meshAttributes[1] };
constexpr std::array skinnedOutlineAttributes {
    skinnedAttributes[0], skinnedAttributes[1], skinnedAttributes[3],
    skinnedAttributes[4], skinnedAttributes[5],
};
#endif

} // namespace

VulkanPipelineFactory::~VulkanPipelineFactory()
{
    destroy();
}

void VulkanPipelineFactory::create(CreateInfo createInfo)
{
    destroy();
    device_ = createInfo.device;
    pipelineCache_ = createInfo.pipelineCache;
    shadowFormat_ = createInfo.shadowFormat;

    VkPushConstantRange pushConstantRange {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        // Sized for the shadow pipelines, which still take a whole
        // GpuDrawInstance this way. Scene pipelines push only a
        // DrawInstanceIndexPushConstants into the front of the same range.
        .size = sizeof(GpuDrawInstance),
    };
    const std::array<VkDescriptorSetLayout, 2> descriptorSetLayouts {
        createInfo.descriptorSetLayout,
        createInfo.textureDescriptorSetLayout,
    };
    if (!descriptorSetLayouts[0] || !descriptorSetLayouts[1]) {
        throw std::runtime_error(
            "Scene pipelines require scene and texture descriptor layouts");
    }
    VkPipelineLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_),
        "vkCreatePipelineLayout failed");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout_, "Scene pipeline layout");
    const VkPushConstantRange cachePushConstants {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(WaterCellCachePlan),
    };
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pPushConstantRanges = &cachePushConstants;
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &waterCellsLayout_),
        "Water cell cache pipeline layout creation failed");

    // Which attachment each pipeline draws into. Scene geometry and the
    // post-process passes that run before the tonemap write the scene target;
    // the tonemap and the UI write a display image.
    const VkFormat sceneFormat = createInfo.sceneColorFormat;
    const VkFormat displayFormat = createInfo.colorFormat;

    // Named through the catalog rather than spelled again here. A path typed
    // out at this call site is a file-not-found on the first launch after the
    // change, with nothing between the typo and the user; a wrong catalog
    // name does not compile.
    const auto shaderModule = [&](std::string_view name) {
        return createShaderModule(
            createInfo.assetRoot / "shaders" /
            shaderCatalog::compiledName(name));
    };

    // Indices are positional only so that the cleanup loop below has one
    // array to walk; nothing else depends on the order.
    std::array<VkShaderModule, shaderCatalog::sources.size()> shaders {};
    // New chunk modules follow the existing optional debug slots; compute
    // remains last in both developer and shipping catalogs.
    constexpr std::size_t groundChunkShaderFirst = shaderCatalog::sources.size() - 4;
    try {
        shaders[0] = shaderModule(shaderCatalog::triangleVert);
        shaders[1] = shaderModule(shaderCatalog::triangleFrag);
        shaders[2] = shaderModule(shaderCatalog::shadowVert);
        shaders[3] = shaderModule(shaderCatalog::modelVert);
        shaders[4] = shaderModule(shaderCatalog::modelShadowVert);
        shaders[5] = shaderModule(shaderCatalog::fullscreenVert);
        shaders[6] = shaderModule(shaderCatalog::ssaoFrag);
        shaders[7] = shaderModule(shaderCatalog::ssaoCompositeFrag);
        shaders[8] = shaderModule(shaderCatalog::waterFrag);
        shaders[9] = shaderModule(shaderCatalog::mirrorEnergyFrag);
        shaders[10] = shaderModule(shaderCatalog::groundSplatFrag);
        shaders[11] = shaderModule(shaderCatalog::worldTransitionFrag);
        shaders[12] = shaderModule(shaderCatalog::skinnedModelVert);
        shaders[13] = shaderModule(shaderCatalog::skinnedModelShadowVert);
        shaders[14] = shaderModule(shaderCatalog::tonemapFrag);
        shaders[15] = shaderModule(shaderCatalog::uiFrag);
        shaders[16] = shaderModule(shaderCatalog::atmosphereFrag);
        shaders[17] = shaderModule(shaderCatalog::atmosphereCompositeFrag);
        shaders[18] = shaderModule(shaderCatalog::bloomExtractFrag);
        shaders[19] = shaderModule(shaderCatalog::bloomBlurFrag);
        shaders[groundChunkShaderFirst] = shaderModule(shaderCatalog::groundChunkVert);
        shaders[groundChunkShaderFirst + 1] = shaderModule(shaderCatalog::groundChunkShadowVert);
        shaders[groundChunkShaderFirst + 2] = shaderModule(shaderCatalog::groundChunkFrag);
        shaders.back() = shaderModule(shaderCatalog::waterCellsComp);
        const VkComputePipelineCreateInfo cachePipelineInfo {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shaders.back(),
                .pName = "main",
            },
            .layout = waterCellsLayout_,
        };
        vkCheck(vkCreateComputePipelines(device_, pipelineCache_, 1,
            &cachePipelineInfo, nullptr, &waterCells_), "Water cell cache pipeline creation failed");
#if SOKOBAN_ENABLE_DEBUG_UI
        shaders[20] = shaderModule(shaderCatalog::debugOutlineVert);
        shaders[21] = shaderModule(shaderCatalog::debugOutlineSkinnedVert);
        shaders[22] = shaderModule(shaderCatalog::debugOutlineBoxVert);
        shaders[23] = shaderModule(shaderCatalog::debugOutlineFrag);
        shaders[24] = shaderModule(shaderCatalog::debugLinkFrag);
        shaders[25] = shaderModule(shaderCatalog::debugLinkVert);
        debugOutline_ = createScenePipeline(
            shaders[20], shaders[23], VertexLayout::MeshOutline,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat, false);
        debugOutlineSkinned_ = createScenePipeline(
            shaders[21], shaders[23], VertexLayout::SkinnedMeshOutline,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat, false);
        debugOutlineBox_ = createScenePipeline(
            shaders[22], shaders[23], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat, false);
        debugLink_ = createScenePipeline(
            shaders[25], shaders[24], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat, false);
#endif

        scene_ = createScenePipeline(
            shaders[0], shaders[1], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe);
        sceneOpaque_ = createScenePipeline(
            shaders[0], shaders[1], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe, Target::SceneOpaque);
        water_ = createScenePipeline(
            shaders[0], shaders[8], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe, Target::SceneBlended, createInfo.waterCellCacheEnabled);
        mirrorEnergy_ = createScenePipeline(
            shaders[0], shaders[9], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe);
        groundSplat_ = createScenePipeline(
            shaders[0], shaders[10], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe);
        groundSplatOpaque_ = createScenePipeline(
            shaders[0], shaders[10], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe, Target::SceneOpaque);
        groundChunkOpaque_ = createScenePipeline(
            shaders[groundChunkShaderFirst], shaders[groundChunkShaderFirst + 2],
            VertexLayout::GroundChunk,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe, Target::SceneOpaque);
        // The game's UI has its own fragment path and composites after the
        // tonemap, onto the swapchain or onto the display image the developer
        // workspace publishes. It retains the shared instanced-quad vertex
        // transport without pulling scene lighting into the fragment stage.
        ui_ = createScenePipeline(
            shaders[0], shaders[15], VertexLayout::None,
            VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_UNDEFINED, displayFormat,
            createInfo.wireframe, Target::Display);
        model_ = createScenePipeline(
            shaders[3], shaders[1], VertexLayout::Mesh,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe);
        modelOpaque_ = createScenePipeline(
            shaders[3], shaders[1], VertexLayout::Mesh,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe, Target::SceneOpaque);
        mirrorEnergyModel_ = createScenePipeline(
            shaders[3], shaders[9], VertexLayout::Mesh,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe);
        skinnedModel_ = createScenePipeline(
            shaders[12], shaders[1], VertexLayout::SkinnedMesh,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe);
        skinnedModelOpaque_ = createScenePipeline(
            shaders[12], shaders[1], VertexLayout::SkinnedMesh,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe, Target::SceneOpaque);
        skinnedMirrorEnergyModel_ = createScenePipeline(
            shaders[12], shaders[9], VertexLayout::SkinnedMesh,
            createInfo.sampleCount, createInfo.depthFormat, sceneFormat,
            createInfo.wireframe);
        shadow_ = createShadowPipeline(shaders[2], VertexLayout::None);
        modelShadow_ = createShadowPipeline(shaders[4], VertexLayout::MeshPosition);
        groundChunkShadow_ = createShadowPipeline(
            shaders[groundChunkShaderFirst + 1], VertexLayout::GroundChunkPosition);
        skinnedModelShadow_ = createShadowPipeline(
            shaders[13], VertexLayout::SkinnedMeshPosition);
        ssao_ = createPostProcessPipeline(
            shaders[5], shaders[6], VK_FORMAT_R8_UNORM);
        ssaoComposite_ = createPostProcessPipeline(
            shaders[5], shaders[7], sceneFormat);
        // Expensive volumetric integration is always single-sample and writes
        // a reduced-resolution scattering/transmittance target. The composite
        // blends that result over the scene in place; its MSAA twin exists
        // only for the overworld mirror continuation.
        atmosphere_ = createPostProcessPipeline(
            shaders[5], shaders[16], sceneFormat);
        atmosphereComposite_ = createPostProcessPipeline(
            shaders[5], shaders[17], sceneFormat,
            VK_SAMPLE_COUNT_1_BIT, PostProcessBlend::Atmosphere);
        atmosphereCompositeMultisample_ = createPostProcessPipeline(
            shaders[5], shaders[17], sceneFormat, createInfo.sampleCount);
        bloomExtract_ = createPostProcessPipeline(
            shaders[5], shaders[18], sceneFormat);
        bloomBlur_ = createPostProcessPipeline(
            shaders[5], shaders[19], sceneFormat);
        worldTransition_ = createPostProcessPipeline(
            shaders[5], shaders[11], sceneFormat);
        tonemap_ = createPostProcessPipeline(
            shaders[5], shaders[14], displayFormat);
        const std::array namedPipelines {
#if SOKOBAN_ENABLE_DEBUG_UI
            std::pair { debugOutline_, "Linked item outline pipeline" },
            std::pair { debugOutlineSkinned_, "Linked item skinned outline pipeline" },
            std::pair { debugOutlineBox_, "Linked item box outline pipeline" },
            std::pair { debugLink_, "Dotted item link pipeline" },
#endif
            std::pair { scene_, "Scene pipeline" },
            std::pair { sceneOpaque_, "Scene pipeline (opaque)" },
            std::pair { groundSplatOpaque_, "Ground splat pipeline (opaque)" },
            std::pair { groundChunkOpaque_, "Ground chunk pipeline (opaque)" },
            std::pair { modelOpaque_, "Model pipeline (opaque)" },
            std::pair {
                skinnedModelOpaque_, "Skinned model pipeline (opaque)" },
            std::pair { water_, "Water pipeline" },
            std::pair { mirrorEnergy_, "Mirror energy pipeline" },
            std::pair { groundSplat_, "Ground splat pipeline" },
            std::pair { ui_, "UI pipeline" },
            std::pair { model_, "Model pipeline" },
            std::pair { mirrorEnergyModel_, "Mirror energy model pipeline" },
            std::pair { skinnedModel_, "Skinned model pipeline" },
            std::pair { skinnedMirrorEnergyModel_, "Skinned mirror energy model pipeline" },
            std::pair { shadow_, "Directional shadow pipeline" },
            std::pair { modelShadow_, "Model shadow pipeline" },
            std::pair { groundChunkShadow_, "Ground chunk sun shadow pipeline" },
            std::pair { skinnedModelShadow_, "Skinned model shadow pipeline" },
            std::pair { ssao_, "SSAO pipeline" },
            std::pair { ssaoComposite_, "SSAO composite pipeline" },
            std::pair { atmosphere_, "Volumetric atmosphere pipeline" },
            std::pair {
                atmosphereComposite_,
                "Volumetric atmosphere composite pipeline" },
            std::pair {
                atmosphereCompositeMultisample_,
                "Volumetric atmosphere composite pipeline (multisample)" },
            std::pair { bloomExtract_, "Bloom extract pipeline" },
            std::pair { bloomBlur_, "Bloom blur pipeline" },
            std::pair { worldTransition_, "World transition pipeline" },
            std::pair { tonemap_, "Tonemap pipeline" },
        };
        for (const auto& [pipeline, name] : namedPipelines) {
            vulkanDebug::setObjectName(
                device_, VK_OBJECT_TYPE_PIPELINE, pipeline, name);
        }
    } catch (...) {
        for (VkShaderModule shader : shaders) {
            if (shader) {
                vkDestroyShaderModule(device_, shader, nullptr);
            }
        }
        destroy();
        throw;
    }
    for (VkShaderModule shader : shaders) {
        vkDestroyShaderModule(device_, shader, nullptr);
    }
}

void VulkanPipelineFactory::destroy()
{
    if (device_) {
        const std::array pipelines {
            scene_, sceneOpaque_, water_, waterCells_, mirrorEnergy_, groundSplat_,
            groundSplatOpaque_, ui_, model_, modelOpaque_,
            groundChunkOpaque_, groundChunkShadow_,
            mirrorEnergyModel_, skinnedModel_, skinnedModelOpaque_,
            skinnedMirrorEnergyModel_,
            shadow_, modelShadow_, skinnedModelShadow_,
            ssao_, ssaoComposite_, atmosphere_, atmosphereComposite_,
            atmosphereCompositeMultisample_,
            bloomExtract_, bloomBlur_,
            worldTransition_, tonemap_,
#if SOKOBAN_ENABLE_DEBUG_UI
            debugOutline_, debugOutlineSkinned_, debugOutlineBox_, debugLink_,
#endif
        };
        for (VkPipeline pipeline : pipelines) {
            if (pipeline) {
                vkDestroyPipeline(device_, pipeline, nullptr);
            }
        }
        if (layout_) {
            vkDestroyPipelineLayout(device_, layout_, nullptr);
        }
        if (waterCellsLayout_) {
            vkDestroyPipelineLayout(device_, waterCellsLayout_, nullptr);
        }
    }
    scene_ = VK_NULL_HANDLE;
    sceneOpaque_ = VK_NULL_HANDLE;
    groundSplatOpaque_ = VK_NULL_HANDLE;
    groundChunkOpaque_ = VK_NULL_HANDLE;
    groundChunkShadow_ = VK_NULL_HANDLE;
    modelOpaque_ = VK_NULL_HANDLE;
    skinnedModelOpaque_ = VK_NULL_HANDLE;
    water_ = VK_NULL_HANDLE;
    waterCells_ = VK_NULL_HANDLE;
    waterCellsLayout_ = VK_NULL_HANDLE;
    mirrorEnergy_ = VK_NULL_HANDLE;
    groundSplat_ = VK_NULL_HANDLE;
    ui_ = VK_NULL_HANDLE;
    model_ = VK_NULL_HANDLE;
    mirrorEnergyModel_ = VK_NULL_HANDLE;
    skinnedModel_ = VK_NULL_HANDLE;
    skinnedMirrorEnergyModel_ = VK_NULL_HANDLE;
    shadow_ = VK_NULL_HANDLE;
    modelShadow_ = VK_NULL_HANDLE;
    skinnedModelShadow_ = VK_NULL_HANDLE;
    ssao_ = VK_NULL_HANDLE;
    ssaoComposite_ = VK_NULL_HANDLE;
    atmosphere_ = VK_NULL_HANDLE;
    atmosphereComposite_ = VK_NULL_HANDLE;
    atmosphereCompositeMultisample_ = VK_NULL_HANDLE;
    bloomExtract_ = VK_NULL_HANDLE;
    bloomBlur_ = VK_NULL_HANDLE;
    worldTransition_ = VK_NULL_HANDLE;
    tonemap_ = VK_NULL_HANDLE;
#if SOKOBAN_ENABLE_DEBUG_UI
    debugOutline_ = VK_NULL_HANDLE;
    debugOutlineSkinned_ = VK_NULL_HANDLE;
    debugOutlineBox_ = VK_NULL_HANDLE;
    debugLink_ = VK_NULL_HANDLE;
#endif
    layout_ = VK_NULL_HANDLE;
    shadowFormat_ = VK_FORMAT_UNDEFINED;
    pipelineCache_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

VkShaderModule VulkanPipelineFactory::createShaderModule(
    const std::filesystem::path& path) const
{
    const std::vector<char> code = readFile(path);
    VkShaderModuleCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data()),
    };
    VkShaderModule result = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &createInfo, nullptr, &result),
        "vkCreateShaderModule failed");
    vulkanDebug::setObjectName(
        device_,
        VK_OBJECT_TYPE_SHADER_MODULE,
        result,
        path.filename().string());
    return result;
}

// The vertex-input state for one layout.
//
// A private static member rather than a file-local function because
// VertexLayout is private, and static because it reads nothing but the tables
// above. Both pipeline creators call it, which is the point: the scene one
// used to answer "nothing" for the two position layouts and the shadow one
// "nothing" for the two full ones, so between them every layout was described
// twice and half-described once.
VkPipelineVertexInputStateCreateInfo VulkanPipelineFactory::vertexInputFor(
    VertexLayout layout)
{
    VkPipelineVertexInputStateCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    switch (layout) {
    case VertexLayout::None:
        return info;
    case VertexLayout::Mesh:
        info.pVertexBindingDescriptions = &meshBinding;
        info.pVertexAttributeDescriptions = meshAttributes.data();
        info.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(meshAttributes.size());
        break;
    case VertexLayout::MeshPosition:
        info.pVertexBindingDescriptions = &meshBinding;
        info.pVertexAttributeDescriptions = &meshPositionAttribute;
        info.vertexAttributeDescriptionCount = 1;
        break;
    case VertexLayout::GroundChunk:
        info.pVertexBindingDescriptions = &groundChunkBinding;
        info.pVertexAttributeDescriptions = groundChunkAttributes.data();
        info.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(groundChunkAttributes.size());
        break;
    case VertexLayout::GroundChunkPosition:
        info.pVertexBindingDescriptions = &groundChunkBinding;
        info.pVertexAttributeDescriptions = groundChunkAttributes.data();
        info.vertexAttributeDescriptionCount = 1;
        break;
    case VertexLayout::SkinnedMesh:
        info.pVertexBindingDescriptions = &skinnedBinding;
        info.pVertexAttributeDescriptions = skinnedAttributes.data();
        info.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(skinnedAttributes.size());
        break;
    case VertexLayout::SkinnedMeshPosition:
        info.pVertexBindingDescriptions = &skinnedBinding;
        info.pVertexAttributeDescriptions = skinnedPositionAttributes.data();
        info.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(skinnedPositionAttributes.size());
        break;
#if SOKOBAN_ENABLE_DEBUG_UI
    case VertexLayout::MeshOutline:
        info.pVertexBindingDescriptions = &meshBinding;
        info.pVertexAttributeDescriptions = meshOutlineAttributes.data();
        info.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(meshOutlineAttributes.size());
        break;
    case VertexLayout::SkinnedMeshOutline:
        info.pVertexBindingDescriptions = &skinnedBinding;
        info.pVertexAttributeDescriptions = skinnedOutlineAttributes.data();
        info.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(skinnedOutlineAttributes.size());
        break;
#endif
    }
    info.vertexBindingDescriptionCount = 1;
    return info;
}

VkPipeline VulkanPipelineFactory::createScenePipeline(
    VkShaderModule vertexShader,
    VkShaderModule fragmentShader,
    VertexLayout vertexLayout,
    VkSampleCountFlagBits sampleCount,
    VkFormat depthFormat,
    VkFormat colorFormat,
    bool wireframe,
    Target target,
    bool waterCellCacheEnabled) const
{
    std::array<VkPipelineShaderStageCreateInfo, 2> stages {
        VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShader,
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShader,
            .pName = "main",
        },
    };
    // Tells triangle.frag / ground_splat.frag to put the ambient mask in
    // alpha. Attached to every scene pipeline rather than only the opaque
    // ones so that each states its answer instead of leaning on the default;
    // a shader that does not declare the constant is unaffected by it.
    // Constant 1 selects the water cache; specializing it off eliminates the
    // lookup branch and buffer reads from the procedural control pipeline.
    const std::array<VkBool32, 2> specializationValues {
        target == Target::SceneOpaque ? VK_TRUE : VK_FALSE,
        waterCellCacheEnabled ? VK_TRUE : VK_FALSE,
    };
    const std::array<VkSpecializationMapEntry, 2> specializationEntries {
        VkSpecializationMapEntry { 0, 0, sizeof(VkBool32) },
        VkSpecializationMapEntry { 1, sizeof(VkBool32), sizeof(VkBool32) },
    };
    const VkSpecializationInfo specialization {
        .mapEntryCount = static_cast<uint32_t>(specializationEntries.size()),
        .pMapEntries = specializationEntries.data(),
        .dataSize = sizeof(specializationValues),
        .pData = specializationValues.data(),
    };
    stages[1].pSpecializationInfo = &specialization;

    const VkPipelineVertexInputStateCreateInfo vertexInput =
        vertexInputFor(vertexLayout);
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = sampleCount,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depthFormat != VK_FORMAT_UNDEFINED,
        .depthWriteEnable = depthFormat != VK_FORMAT_UNDEFINED,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    // A blended scene draw uses its alpha and writes none: the destination
    // alpha is the ambient mask of whatever opaque surface it sits in front
    // of, and blending over it would destroy the one channel the SSAO
    // composite reads.
    VkColorComponentFlags colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT;
    if (target != Target::SceneBlended) {
        colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendAttachmentState blendAttachment {
        .blendEnable = target == Target::SceneOpaque ? VK_FALSE : VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = colorWriteMask,
    };
    VkPipelineColorBlendStateCreateInfo colorBlending {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };
    const std::array dynamicStates {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat,
    };
    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = layout_,
    };
    VkPipeline result = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(
        device_, pipelineCache_, 1, &pipelineInfo, nullptr, &result),
        "vkCreateGraphicsPipelines scene pipeline failed");
    return result;
}

VkPipeline VulkanPipelineFactory::createShadowPipeline(
    VkShaderModule vertexShader,
    VertexLayout vertexLayout) const
{
    VkPipelineShaderStageCreateInfo stage {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertexShader,
        .pName = "main",
    };
    const VkPipelineVertexInputStateCreateInfo vertexInput =
        vertexInputFor(vertexLayout);
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    const std::array dynamicStates {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .depthAttachmentFormat = shadowFormat_,
    };
    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 1,
        .pStages = &stage,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pDynamicState = &dynamicState,
        .layout = layout_,
    };
    VkPipeline result = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(
        device_, pipelineCache_, 1, &pipelineInfo, nullptr, &result),
        "vkCreateGraphicsPipelines shadow pipeline failed");
    return result;
}

VkPipeline VulkanPipelineFactory::createPostProcessPipeline(
    VkShaderModule vertexShader,
    VkShaderModule fragmentShader,
    VkFormat colorFormat,
    VkSampleCountFlagBits sampleCount,
    PostProcessBlend blend) const
{
    std::array<VkPipelineShaderStageCreateInfo, 2> stages {
        VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShader,
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShader,
            .pName = "main",
        },
    };
    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = sampleCount,
    };
    VkPipelineColorBlendAttachmentState blendAttachment {
        .blendEnable = blend == PostProcessBlend::Atmosphere,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        // Scene alpha carries the ambient-light share used by SSAO. Fogging
        // the scene must not replace or attenuate that semantic channel.
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo colorBlending {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };
    const std::array dynamicStates {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
    };
    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = layout_,
    };
    VkPipeline result = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(
        device_, pipelineCache_, 1, &pipelineInfo, nullptr, &result),
        "vkCreateGraphicsPipelines post-process pipeline failed");
    return result;
}

} // namespace sokoban
