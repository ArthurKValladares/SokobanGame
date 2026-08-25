#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

namespace sokoban {

class VulkanPipelineFactory {
public:
    struct CreateInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        std::filesystem::path assetRoot;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkFormat shadowFormat = VK_FORMAT_UNDEFINED;
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
        bool wireframe = false;
    };

    VulkanPipelineFactory() = default;
    ~VulkanPipelineFactory();

    VulkanPipelineFactory(const VulkanPipelineFactory&) = delete;
    VulkanPipelineFactory& operator=(const VulkanPipelineFactory&) = delete;

    void create(CreateInfo createInfo);
    void destroy();

    [[nodiscard]] VkPipelineLayout layout() const { return layout_; }
    [[nodiscard]] VkPipeline scene() const { return scene_; }
    [[nodiscard]] VkPipeline water() const { return water_; }
    [[nodiscard]] VkPipeline mirrorEnergy() const { return mirrorEnergy_; }
    [[nodiscard]] VkPipeline groundSplat() const { return groundSplat_; }
    [[nodiscard]] VkPipeline ui() const { return ui_; }
    [[nodiscard]] VkPipeline model() const { return model_; }
    [[nodiscard]] VkPipeline mirrorEnergyModel() const {
        return mirrorEnergyModel_;
    }
    [[nodiscard]] VkPipeline skinnedModel() const { return skinnedModel_; }
    [[nodiscard]] VkPipeline skinnedMirrorEnergyModel() const {
        return skinnedMirrorEnergyModel_;
    }
    [[nodiscard]] VkPipeline shadow() const { return shadow_; }
    [[nodiscard]] VkPipeline modelShadow() const { return modelShadow_; }
    [[nodiscard]] VkPipeline skinnedModelShadow() const {
        return skinnedModelShadow_;
    }
    [[nodiscard]] VkPipeline ssao() const { return ssao_; }
    [[nodiscard]] VkPipeline ssaoComposite() const { return ssaoComposite_; }
    [[nodiscard]] VkPipeline ssaoVisualize() const { return ssaoVisualize_; }
    [[nodiscard]] VkPipeline worldTransition() const { return worldTransition_; }

private:
    enum class VertexLayout {
        None,
        Mesh,
        MeshPosition,
        SkinnedMesh,
        SkinnedMeshPosition,
    };

    [[nodiscard]] VkShaderModule createShaderModule(const std::filesystem::path& path) const;
    [[nodiscard]] VkPipeline createScenePipeline(
        VkShaderModule vertexShader,
        VkShaderModule fragmentShader,
        VertexLayout vertexLayout,
        VkSampleCountFlagBits sampleCount,
        VkFormat depthFormat,
        bool wireframe) const;
    [[nodiscard]] VkPipeline createShadowPipeline(
        VkShaderModule vertexShader,
        VertexLayout vertexLayout) const;
    [[nodiscard]] VkPipeline createPostProcessPipeline(
        VkShaderModule vertexShader,
        VkShaderModule fragmentShader,
        VkFormat colorFormat,
        bool multiplyBlend) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat shadowFormat_ = VK_FORMAT_UNDEFINED;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline scene_ = VK_NULL_HANDLE;
    VkPipeline water_ = VK_NULL_HANDLE;
    VkPipeline mirrorEnergy_ = VK_NULL_HANDLE;
    VkPipeline groundSplat_ = VK_NULL_HANDLE;
    VkPipeline ui_ = VK_NULL_HANDLE;
    VkPipeline model_ = VK_NULL_HANDLE;
    VkPipeline mirrorEnergyModel_ = VK_NULL_HANDLE;
    VkPipeline skinnedModel_ = VK_NULL_HANDLE;
    VkPipeline skinnedMirrorEnergyModel_ = VK_NULL_HANDLE;
    VkPipeline shadow_ = VK_NULL_HANDLE;
    VkPipeline modelShadow_ = VK_NULL_HANDLE;
    VkPipeline skinnedModelShadow_ = VK_NULL_HANDLE;
    VkPipeline ssao_ = VK_NULL_HANDLE;
    VkPipeline ssaoComposite_ = VK_NULL_HANDLE;
    VkPipeline ssaoVisualize_ = VK_NULL_HANDLE;
    VkPipeline worldTransition_ = VK_NULL_HANDLE;
};

} // namespace sokoban
