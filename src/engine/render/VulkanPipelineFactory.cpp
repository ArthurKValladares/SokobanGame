#include "engine/render/VulkanPipelineFactory.hpp"

#include "engine/render/GltfMesh.hpp"
#include "engine/render/GpuSkinning.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

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
    colorFormat_ = createInfo.colorFormat;
    shadowFormat_ = createInfo.shadowFormat;

    VkPushConstantRange pushConstantRange {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TilePushConstants),
    };
    VkPipelineLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = createInfo.descriptorSetLayout ? 1U : 0U,
        .pSetLayouts = createInfo.descriptorSetLayout ? &createInfo.descriptorSetLayout : nullptr,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_),
        "vkCreatePipelineLayout failed");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout_, "Scene pipeline layout");

    std::array<VkShaderModule, 14> shaders {};
    try {
        shaders[0] = createShaderModule(createInfo.assetRoot / "shaders/triangle.vert.glsl.spv");
        shaders[1] = createShaderModule(createInfo.assetRoot / "shaders/triangle.frag.glsl.spv");
        shaders[2] = createShaderModule(createInfo.assetRoot / "shaders/shadow.vert.glsl.spv");
        shaders[3] = createShaderModule(createInfo.assetRoot / "shaders/model.vert.glsl.spv");
        shaders[4] = createShaderModule(createInfo.assetRoot / "shaders/model_shadow.vert.glsl.spv");
        shaders[5] = createShaderModule(createInfo.assetRoot / "shaders/fullscreen.vert.glsl.spv");
        shaders[6] = createShaderModule(createInfo.assetRoot / "shaders/ssao.frag.glsl.spv");
        shaders[7] = createShaderModule(createInfo.assetRoot / "shaders/ssao_composite.frag.glsl.spv");
        shaders[8] = createShaderModule(createInfo.assetRoot / "shaders/water.frag.glsl.spv");
        shaders[9] = createShaderModule(
            createInfo.assetRoot / "shaders/mirror_energy.frag.glsl.spv");
        shaders[10] = createShaderModule(
            createInfo.assetRoot / "shaders/ground_splat.frag.glsl.spv");
        shaders[11] = createShaderModule(
            createInfo.assetRoot / "shaders/world_transition.frag.glsl.spv");
        shaders[12] = createShaderModule(
            createInfo.assetRoot / "shaders/skinned_model.vert.glsl.spv");
        shaders[13] = createShaderModule(
            createInfo.assetRoot / "shaders/skinned_model_shadow.vert.glsl.spv");

        scene_ = createScenePipeline(
            shaders[0], shaders[1], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe);
        sceneOpaque_ = createScenePipeline(
            shaders[0], shaders[1], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe,
            BlendMode::Opaque);
        water_ = createScenePipeline(
            shaders[0], shaders[8], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe);
        mirrorEnergy_ = createScenePipeline(
            shaders[0], shaders[9], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe);
        groundSplat_ = createScenePipeline(
            shaders[0], shaders[10], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe);
        groundSplatOpaque_ = createScenePipeline(
            shaders[0], shaders[10], VertexLayout::None,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe,
            BlendMode::Opaque);
        ui_ = createScenePipeline(
            shaders[0], shaders[1], VertexLayout::None,
            VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_UNDEFINED, createInfo.wireframe);
        model_ = createScenePipeline(
            shaders[3], shaders[1], VertexLayout::Mesh,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe);
        modelOpaque_ = createScenePipeline(
            shaders[3], shaders[1], VertexLayout::Mesh,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe,
            BlendMode::Opaque);
        mirrorEnergyModel_ = createScenePipeline(
            shaders[3], shaders[9], VertexLayout::Mesh,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe);
        skinnedModel_ = createScenePipeline(
            shaders[12], shaders[1], VertexLayout::SkinnedMesh,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe);
        skinnedModelOpaque_ = createScenePipeline(
            shaders[12], shaders[1], VertexLayout::SkinnedMesh,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe,
            BlendMode::Opaque);
        skinnedMirrorEnergyModel_ = createScenePipeline(
            shaders[12], shaders[9], VertexLayout::SkinnedMesh,
            createInfo.sampleCount, createInfo.depthFormat, createInfo.wireframe);
        shadow_ = createShadowPipeline(shaders[2], VertexLayout::None);
        modelShadow_ = createShadowPipeline(shaders[4], VertexLayout::MeshPosition);
        skinnedModelShadow_ = createShadowPipeline(
            shaders[13], VertexLayout::SkinnedMeshPosition);
        ssao_ = createPostProcessPipeline(
            shaders[5], shaders[6], VK_FORMAT_R8_UNORM, false);
        ssaoComposite_ = createPostProcessPipeline(
            shaders[5], shaders[7], createInfo.colorFormat, true);
        ssaoVisualize_ = createPostProcessPipeline(
            shaders[5], shaders[7], createInfo.colorFormat, false);
        worldTransition_ = createPostProcessPipeline(
            shaders[5], shaders[11], createInfo.colorFormat, false);
        const std::array namedPipelines {
            std::pair { scene_, "Scene pipeline" },
            std::pair { sceneOpaque_, "Scene pipeline (opaque)" },
            std::pair { groundSplatOpaque_, "Ground splat pipeline (opaque)" },
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
            std::pair { skinnedModelShadow_, "Skinned model shadow pipeline" },
            std::pair { ssao_, "SSAO pipeline" },
            std::pair { ssaoComposite_, "SSAO composite pipeline" },
            std::pair { ssaoVisualize_, "SSAO visualize pipeline" },
            std::pair { worldTransition_, "World transition pipeline" },
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
            scene_, sceneOpaque_, water_, mirrorEnergy_, groundSplat_,
            groundSplatOpaque_, ui_, model_, modelOpaque_,
            mirrorEnergyModel_, skinnedModel_, skinnedModelOpaque_,
            skinnedMirrorEnergyModel_,
            shadow_, modelShadow_, skinnedModelShadow_,
            ssao_, ssaoComposite_, ssaoVisualize_, worldTransition_,
        };
        for (VkPipeline pipeline : pipelines) {
            if (pipeline) {
                vkDestroyPipeline(device_, pipeline, nullptr);
            }
        }
        if (layout_) {
            vkDestroyPipelineLayout(device_, layout_, nullptr);
        }
    }
    scene_ = VK_NULL_HANDLE;
    sceneOpaque_ = VK_NULL_HANDLE;
    groundSplatOpaque_ = VK_NULL_HANDLE;
    modelOpaque_ = VK_NULL_HANDLE;
    skinnedModelOpaque_ = VK_NULL_HANDLE;
    water_ = VK_NULL_HANDLE;
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
    ssaoVisualize_ = VK_NULL_HANDLE;
    worldTransition_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    shadowFormat_ = VK_FORMAT_UNDEFINED;
    colorFormat_ = VK_FORMAT_UNDEFINED;
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

VkPipeline VulkanPipelineFactory::createScenePipeline(
    VkShaderModule vertexShader,
    VkShaderModule fragmentShader,
    VertexLayout vertexLayout,
    VkSampleCountFlagBits sampleCount,
    VkFormat depthFormat,
    bool wireframe,
    BlendMode blendMode) const
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
    const VkVertexInputBindingDescription meshBinding {
        .binding = 0,
        .stride = sizeof(MeshVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputBindingDescription skinnedBinding {
        .binding = 0,
        .stride = sizeof(GpuSkinnedVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const std::array<VkVertexInputAttributeDescription, 5> attributes {
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
        VkVertexInputAttributeDescription {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(MeshVertex, textureIndex),
        },
        VkVertexInputAttributeDescription {
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(MeshVertex, materialFlags),
        },
    };
    const std::array<VkVertexInputAttributeDescription, 8> skinnedAttributes {
        VkVertexInputAttributeDescription { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuSkinnedVertex, position) },
        VkVertexInputAttributeDescription { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuSkinnedVertex, normal) },
        VkVertexInputAttributeDescription { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuSkinnedVertex, uv) },
        VkVertexInputAttributeDescription { 3, 0, VK_FORMAT_R32_UINT, offsetof(GpuSkinnedVertex, textureIndex) },
        VkVertexInputAttributeDescription { 4, 0, VK_FORMAT_R32_UINT, offsetof(GpuSkinnedVertex, materialFlags) },
        VkVertexInputAttributeDescription { 5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(GpuSkinnedVertex, joints) },
        VkVertexInputAttributeDescription { 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuSkinnedVertex, weights) },
        VkVertexInputAttributeDescription { 7, 0, VK_FORMAT_R32_UINT, offsetof(GpuSkinnedVertex, attachmentNodeIndex) },
    };
    const bool meshLayout = vertexLayout == VertexLayout::Mesh;
    const bool skinnedLayout = vertexLayout == VertexLayout::SkinnedMesh;
    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = (meshLayout || skinnedLayout) ? 1U : 0U,
        .pVertexBindingDescriptions = meshLayout ? &meshBinding : skinnedLayout ? &skinnedBinding : nullptr,
        .vertexAttributeDescriptionCount = meshLayout ? static_cast<uint32_t>(attributes.size()) : skinnedLayout ? static_cast<uint32_t>(skinnedAttributes.size()) : 0U,
        .pVertexAttributeDescriptions = meshLayout ? attributes.data() : skinnedLayout ? skinnedAttributes.data() : nullptr,
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
    VkPipelineColorBlendAttachmentState blendAttachment {
        .blendEnable = blendMode == BlendMode::AlphaBlend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
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
        .pColorAttachmentFormats = &colorFormat_,
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
    const VkVertexInputBindingDescription binding {
        .binding = 0,
        .stride = sizeof(MeshVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputBindingDescription skinnedBinding {
        .binding = 0,
        .stride = sizeof(GpuSkinnedVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attribute {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(MeshVertex, position),
    };
    const std::array<VkVertexInputAttributeDescription, 4> skinnedAttributes {
        VkVertexInputAttributeDescription { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuSkinnedVertex, position) },
        VkVertexInputAttributeDescription { 5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(GpuSkinnedVertex, joints) },
        VkVertexInputAttributeDescription { 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuSkinnedVertex, weights) },
        VkVertexInputAttributeDescription { 7, 0, VK_FORMAT_R32_UINT, offsetof(GpuSkinnedVertex, attachmentNodeIndex) },
    };
    const bool meshPositionLayout = vertexLayout == VertexLayout::MeshPosition;
    const bool skinnedPositionLayout = vertexLayout == VertexLayout::SkinnedMeshPosition;
    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = (meshPositionLayout || skinnedPositionLayout) ? 1U : 0U,
        .pVertexBindingDescriptions = meshPositionLayout ? &binding : skinnedPositionLayout ? &skinnedBinding : nullptr,
        .vertexAttributeDescriptionCount = meshPositionLayout ? 1U : skinnedPositionLayout ? static_cast<uint32_t>(skinnedAttributes.size()) : 0U,
        .pVertexAttributeDescriptions = meshPositionLayout ? &attribute : skinnedPositionLayout ? skinnedAttributes.data() : nullptr,
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
    bool multiplyBlend) const
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blendAttachment {
        .blendEnable = multiplyBlend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
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
