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
        VkDescriptorSetLayout textureDescriptorSetLayout = VK_NULL_HANDLE;
        // The surface's format. Only the two pipelines that draw a display
        // image use it: the tonemap pass and the UI.
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        // What everything that draws into the scene targets. Separate from
        // colorFormat since F2a; a pipeline created against the wrong one is
        // a dynamic-rendering format mismatch, not a subtle shading bug.
        VkFormat sceneColorFormat = VK_FORMAT_UNDEFINED;
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
    // Blended variants. The opaque pass uses the *Opaque() accessors below;
    // these remain for the translucent pass, for UI, and for the handful of
    // opaque-pass surfaces that still carry a sub-1.0 alpha (editor previews).
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
    // Colour-write-only twins of scene/groundSplat/model/skinnedModel with
    // blending disabled. Opaque geometry has no business reading the colour
    // attachment back: the blend unit costs read-modify-write bandwidth on
    // every covered sample, which at 4x-8x MSAA is the dominant cost of an
    // otherwise trivial fragment.
    [[nodiscard]] VkPipeline sceneOpaque() const { return sceneOpaque_; }
    [[nodiscard]] VkPipeline groundSplatOpaque() const
    {
        return groundSplatOpaque_;
    }
    [[nodiscard]] VkPipeline modelOpaque() const { return modelOpaque_; }
    [[nodiscard]] VkPipeline skinnedModelOpaque() const
    {
        return skinnedModelOpaque_;
    }

    [[nodiscard]] VkPipeline shadow() const { return shadow_; }
    [[nodiscard]] VkPipeline modelShadow() const { return modelShadow_; }
    [[nodiscard]] VkPipeline skinnedModelShadow() const {
        return skinnedModelShadow_;
    }
    [[nodiscard]] VkPipeline ssao() const { return ssao_; }
    // One pipeline for both the composite and its debug visualization: they
    // are the same shader, the same format and, since the composite stopped
    // blending, the same state. params.w picks which one the draw is.
    [[nodiscard]] VkPipeline ssaoComposite() const { return ssaoComposite_; }
    [[nodiscard]] VkPipeline worldTransition() const { return worldTransition_; }
    // Scene target -> display image. The one place a scene colour becomes a
    // presentable one, which is why the range and encode decisions live in
    // its shader rather than scattered through the scene shaders.
    [[nodiscard]] VkPipeline tonemap() const { return tonemap_; }

private:
    enum class VertexLayout {
        None,
        Mesh,
        MeshPosition,
        SkinnedMesh,
        SkinnedMeshPosition,
    };

    // The bindings and attributes one layout needs. Shared by the scene and
    // shadow pipeline creators, which each described half the layouts.
    [[nodiscard]] static VkPipelineVertexInputStateCreateInfo vertexInputFor(
        VertexLayout layout);

    [[nodiscard]] VkShaderModule createShaderModule(const std::filesystem::path& path) const;

    // What a pipeline draws into, which decides three things together because
    // they are not independent: whether it blends, what its alpha channel
    // means, and whether the fragment shader is specialized to produce that
    // alpha.
    //
    // The scene target's alpha is not an opacity. It carries the share of
    // each pixel's light that came from the ambient term, which is what the
    // SSAO composite scales its effect by, so that occlusion stops darkening
    // direct sunlight. Opaque scene draws write it; blended scene draws must
    // leave it untouched, so a translucent surface inherits the mask of the
    // geometry behind it rather than blending garbage over it. Only a draw
    // targeting a display image still writes a real alpha.
    enum class Target {
        SceneBlended,
        SceneOpaque,
        Display,
    };

    [[nodiscard]] VkPipeline createScenePipeline(
        VkShaderModule vertexShader,
        VkShaderModule fragmentShader,
        VertexLayout vertexLayout,
        VkSampleCountFlagBits sampleCount,
        VkFormat depthFormat,
        VkFormat colorFormat,
        bool wireframe,
        Target target = Target::SceneBlended) const;
    [[nodiscard]] VkPipeline createShadowPipeline(
        VkShaderModule vertexShader,
        VertexLayout vertexLayout) const;
    // Fullscreen passes. All of them write their result outright: the SSAO
    // composite was the last multiply blend and stopped being one when it
    // started scaling the ambient term rather than the finished pixel.
    [[nodiscard]] VkPipeline createPostProcessPipeline(
        VkShaderModule vertexShader,
        VkShaderModule fragmentShader,
        VkFormat colorFormat) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VkFormat shadowFormat_ = VK_FORMAT_UNDEFINED;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline scene_ = VK_NULL_HANDLE;
    VkPipeline sceneOpaque_ = VK_NULL_HANDLE;
    VkPipeline groundSplatOpaque_ = VK_NULL_HANDLE;
    VkPipeline modelOpaque_ = VK_NULL_HANDLE;
    VkPipeline skinnedModelOpaque_ = VK_NULL_HANDLE;
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
    VkPipeline worldTransition_ = VK_NULL_HANDLE;
    VkPipeline tonemap_ = VK_NULL_HANDLE;
};

} // namespace sokoban
