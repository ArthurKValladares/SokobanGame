#include "engine/render/VulkanSceneRecorder.hpp"

#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanGpuProfiler.hpp"
#include "engine/render/MirrorConfig.hpp"
#include "engine/render/LightingConfig.hpp"
#include "engine/render/OpaqueDrawSorter.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/SceneDrawLanes.hpp"
#include "engine/render/WaterConfig.hpp"
#include "engine/render/VulkanModelResources.hpp"
#include "engine/render/VulkanPipelineFactory.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanResourceUtils.hpp"
#include "engine/render/VulkanSceneDescriptors.hpp"
#include "engine/render/VulkanShadowPass.hpp"
#include "engine/render/VulkanSsaoPass.hpp"
#include "engine/render/VulkanSwapchainResources.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
// Deliberately fatal rather than defaulting to 0. This flag decides whether
// Application and DebugUi declare some of their members, so a translation unit
// that quietly assumed a value would disagree with the rest of the program
// about those class layouts - and link anyway. CMake defines it PUBLIC on
// sokoban_core, so anything linking a Sokoban library already has it.
#error "SOKOBAN_ENABLE_DEBUG_UI must be defined by the build (see CMakeLists.txt)"
#endif

namespace sokoban {
namespace {

// A model draw passes its authored material mode straight through, so the
// first three DrawMaterialMode values have to keep agreeing with the manifest
// enum. Nothing else forces that: they are declared in different headers for
// different reasons.
static_assert(static_cast<uint32_t>(DrawMaterialMode::Untextured)
    == static_cast<uint32_t>(ModelMaterialMode::Untextured));
static_assert(static_cast<uint32_t>(DrawMaterialMode::ManifestTexture)
    == static_cast<uint32_t>(ModelMaterialMode::SingleTexture));
static_assert(static_cast<uint32_t>(DrawMaterialMode::GltfMaterial)
    == static_cast<uint32_t>(ModelMaterialMode::PrimitiveMaterials));

// Which shading path a UI command takes. A switch rather than the nested
// ternary this replaced, so adding a UiDrawKind is a compiler error here
// instead of silently falling through to the title background.
[[nodiscard]] constexpr DrawMaterialMode uiDrawMaterialMode(UiDrawKind kind)
{
    switch (kind) {
    case UiDrawKind::FontGlyph:
        return DrawMaterialMode::FontGlyph;
    case UiDrawKind::SceneImage:
        return DrawMaterialMode::SceneImage;
    case UiDrawKind::TextureImage:
        return DrawMaterialMode::TextureImage;
    case UiDrawKind::Image:
        return DrawMaterialMode::TitleBackground;
    case UiDrawKind::Solid:
        break;
    }
    // Solid never reaches here: it returns through drawFace above, which uses
    // no texture at all.
    return DrawMaterialMode::Untextured;
}

double elapsedMilliseconds(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

RenderPhaseTiming renderPhaseTiming(const FrameTimeSummary& summary)
{
    return {
        .available = summary.available(),
        .samples = summary.sampleCount,
        .latestMilliseconds = summary.latestMilliseconds,
        .averageMilliseconds = summary.averageMilliseconds,
        .p95Milliseconds = summary.p95Milliseconds,
        .maximumMilliseconds = summary.maximumMilliseconds,
    };
}

std::array<Vec4, 4> affineTransformColumns(
    Vec4 origin,
    Vec4 xPoint,
    Vec4 yPoint,
    Vec4 zPoint)
{
    return {
        subtract(xPoint, origin),
        subtract(yPoint, origin),
        subtract(zPoint, origin),
        origin,
    };
}

Vec4 projectPointShadow(
    const RenderFrameData::PointLight& light,
    uint32_t cubeFace,
    Vec3 point)
{
    static constexpr std::array<Vec3, 6> forward {
        Vec3 { 1.0f, 0.0f, 0.0f }, Vec3 { -1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f }, Vec3 { 0.0f, -1.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f }, Vec3 { 0.0f, 0.0f, -1.0f },
    };
    static constexpr std::array<Vec3, 6> right {
        Vec3 { 0.0f, 0.0f, -1.0f }, Vec3 { 0.0f, 0.0f, 1.0f },
        Vec3 { 1.0f, 0.0f, 0.0f }, Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 1.0f, 0.0f, 0.0f }, Vec3 { -1.0f, 0.0f, 0.0f },
    };
    static constexpr std::array<Vec3, 6> up {
        Vec3 { 0.0f, -1.0f, 0.0f }, Vec3 { 0.0f, -1.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f }, Vec3 { 0.0f, 0.0f, -1.0f },
        Vec3 { 0.0f, -1.0f, 0.0f }, Vec3 { 0.0f, -1.0f, 0.0f },
    };
    const Vec3 relative {
        point.x - light.position.x,
        point.y - light.position.y,
        point.z - light.position.z,
    };
    const auto dot3 = [](Vec3 a, Vec3 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    };
    const float depth = dot3(relative, forward[cubeFace]);
    const float nearPlane = config::pointShadowNearPlane;
    const float farPlane = std::max(light.range, nearPlane + 0.001f);
    const float denominator = farPlane - nearPlane;
    return {
        dot3(relative, right[cubeFace]),
        dot3(relative, up[cubeFace]),
        farPlane / denominator * depth -
            farPlane * nearPlane / denominator,
        depth,
    };
}

Vec3 transformModelPoint(
    const ModelTransformPoints& transform,
    Vec3 point)
{
    const Vec3 xAxis = subtract(transform.xPoint, transform.origin);
    const Vec3 yAxis = subtract(transform.yPoint, transform.origin);
    const Vec3 zAxis = subtract(transform.zPoint, transform.origin);
    return add(
        transform.origin,
        add(
            multiply(xAxis, point.x),
            add(
                multiply(yAxis, point.y),
                multiply(zAxis, point.z))));
}

Aabb modelWorldBounds(
    const RenderFrameData::Tile& tile,
    const VulkanModelResources::ModelBounds& bounds)
{
    if (!bounds.valid) {
        return {};
    }
    const ModelTransformPoints transform =
        IsoScenePreparer::modelTransformPoints(tile);
    Aabb result;
    for (float x : { bounds.minimum.x, bounds.maximum.x }) {
        for (float y : { bounds.minimum.y, bounds.maximum.y }) {
            for (float z : { bounds.minimum.z, bounds.maximum.z }) {
                result = expand(
                    result,
                    transformModelPoint(transform, { x, y, z }));
            }
        }
    }
    return result;
}

struct RecorderModelCandidate {
    std::size_t tileIndex = 0;
    MaterialAlphaSelection alphaSelection = MaterialAlphaSelection::All;
    float depth = 0.0f;
    std::size_t sourceOrder = 0;
};

struct RecorderModelDraw {
    const RenderFrameData::Tile* tile = nullptr;
    VulkanModelResources::MeshView mesh {};
    GpuDrawInstance constants {};
    ModelMaterialPolicy materialPolicy {};
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint32_t pipelineRank = 0;
    std::array<uint32_t, 29> batchState {};
    bool mirrorGhost = false;
    bool skinned = false;
};

} // namespace

struct VulkanSceneRecorder::Scratch {
    std::vector<RecorderModelCandidate> modelCandidates;
    std::vector<RecorderModelDraw> modelDraws;
    std::vector<OpaqueDrawSortItem> orderedModelDraws;
    std::vector<OpaqueDrawBatch> modelBatches;

    [[nodiscard]] uint64_t capacityBytes() const
    {
        return modelCandidates.capacity() * sizeof(RecorderModelCandidate) +
            modelDraws.capacity() * sizeof(RecorderModelDraw) +
            orderedModelDraws.capacity() * sizeof(OpaqueDrawSortItem) +
            modelBatches.capacity() * sizeof(OpaqueDrawBatch);
    }
};

VulkanSceneRecorder::VulkanSceneRecorder()
    : scratch_(std::make_unique<Scratch>())
{
}

VulkanSceneRecorder::~VulkanSceneRecorder() = default;

namespace {

void renderDebugUi(VkCommandBuffer commandBuffer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui_ImplVulkan_RenderDrawData(
        ImGui::GetDrawData(), commandBuffer);
#else
    (void)commandBuffer;
#endif
}

} // namespace

class SceneRecordingSession {
public:
    SceneRecordingSession(
        VulkanSceneRecorder::Resources resources,
        const VulkanSceneRecorder::FrameConfiguration& configuration,
        PointShadowFaceCache& pointShadowFaceCache,
        std::array<std::vector<PointShadowModelState>,
            RenderFrameData::pointLightCapacity>& pointShadowModelStateScratch,
        bool pointShadowCacheEnabled,
        VulkanSceneRecorder::Scratch& scratch,
        bool scratchReuseEnabled,
        FrameTimeTelemetry& setupTimeTelemetry,
        FrameTimeTelemetry& gameTimeTelemetry,
        FrameTimeTelemetry& shadowTimeTelemetry,
        FrameTimeTelemetry& sceneTimeTelemetry,
        FrameTimeTelemetry& ssaoTimeTelemetry,
        FrameTimeTelemetry& previewTimeTelemetry,
        FrameTimeTelemetry& outputTimeTelemetry)
        : device_(resources.device)
        , gpuProfiler_(resources.gpuProfiler)
        , swapchain_(resources.swapchain)
        , shadowPass_(resources.shadowPass)
        , ssaoPass_(resources.ssaoPass)
        , descriptors_(resources.sceneDescriptors)
        , pipelines_(resources.pipelines)
        , models_(resources.modelResources)
        , configuration_(configuration)
        , pointShadowFaceCache_(pointShadowFaceCache)
        , pointShadowModelStateScratch_(pointShadowModelStateScratch)
        , pointShadowCacheEnabled_(pointShadowCacheEnabled)
        , scratch_(scratch)
        , scratchReuseEnabled_(scratchReuseEnabled)
        , setupTimeTelemetry_(setupTimeTelemetry)
        , gameTimeTelemetry_(gameTimeTelemetry)
        , shadowTimeTelemetry_(shadowTimeTelemetry)
        , sceneTimeTelemetry_(sceneTimeTelemetry)
        , ssaoTimeTelemetry_(ssaoTimeTelemetry)
        , previewTimeTelemetry_(previewTimeTelemetry)
        , outputTimeTelemetry_(outputTimeTelemetry)
    {
    }

    RenderStats record(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene,
        const RenderFrameData* previewFrameData,
        const PreparedRenderScene* previewScene,
        const UiDrawData& uiDrawData)
    {
        const VkExtent2D extent = swapchain_.extent();
        const VkExtent2D renderExtent = swapchain_.renderExtent();
        const VkExtent2D ssaoExtent = ssaoPass_.aoExtent();
        const auto unavailableModelCount = [this](
                                               const RenderFrameData& data,
                                               const PreparedRenderScene& prepared) {
            uint32_t result = 0;
            const auto countUnavailable = [&](std::size_t tileIndex) {
                if (!models_.tileReadyForDraw(
                        data.tiles[tileIndex],
                        configuration_.descriptorFrameIndex)) {
                    ++result;
                }
            };
            for (std::size_t tileIndex : prepared.opaqueModelIndices) {
                countUnavailable(tileIndex);
            }
            for (std::size_t tileIndex : prepared.translucentModelIndices) {
                countUnavailable(tileIndex);
            }
            return result;
        };
        stats_ = {
            .frameIndex = configuration_.statsFrameIndex,
            .totalTiles = static_cast<uint32_t>(
                frameData.tiles.size() + frameData.waterSurfaces.size() +
                (previewFrameData
                    ? previewFrameData->tiles.size() +
                        previewFrameData->waterSurfaces.size()
                    : 0)),
            .scenePreparations = previewFrameData ? 2U : 1U,
            .preparedIsoFaces =
                static_cast<uint32_t>(scene.isoFaces.size() +
                    (previewScene ? previewScene->isoFaces.size() : 0)),
            .preparedShadowFaces =
                static_cast<uint32_t>(scene.shadowFaces.size() +
                    (previewScene ? previewScene->shadowFaces.size() : 0)),
            .pointShadowFaceCandidates = scene.pointShadowFaceCandidates,
            .pointShadowFacesInRange = scene.pointShadowFacesInRange,
            .pointShadowFacesCulled = scene.pointShadowFacesCulled,
            .preparedModels = static_cast<uint32_t>(
                scene.opaqueModelIndices.size() +
                scene.translucentModelIndices.size() +
                (previewScene
                    ? previewScene->opaqueModelIndices.size() +
                        previewScene->translucentModelIndices.size()
                    : 0)),
            .unavailableModels = unavailableModelCount(frameData, scene) +
                (previewFrameData && previewScene
                    ? unavailableModelCount(*previewFrameData, *previewScene)
                    : 0),
            .preparedParticles =
                static_cast<uint32_t>(scene.particles.size() +
                    (previewScene ? previewScene->particles.size() : 0)),
            .persistentRenderables = static_cast<uint32_t>(
                scene.renderables.size() +
                (previewScene ? previewScene->renderables.size() : 0)),
            .reusedRenderableBounds = scene.reusedRenderableBounds +
                (previewScene ? previewScene->reusedRenderableBounds : 0),
            .rebuiltRenderableBounds = scene.rebuiltRenderableBounds +
                (previewScene ? previewScene->rebuiltRenderableBounds : 0),
            .visibleRenderables = scene.visibleRenderables +
                (previewScene ? previewScene->visibleRenderables : 0),
            .culledRenderables = scene.culledRenderables +
                (previewScene ? previewScene->culledRenderables : 0),
            .frustumCullingEnabled = scene.frustumCullingEnabled &&
                (!previewScene || previewScene->frustumCullingEnabled),
            .swapchainWidth = extent.width,
            .swapchainHeight = extent.height,
            .swapchainImages = swapchain_.imageCount(),
            .renderWidth = renderExtent.width,
            .renderHeight = renderExtent.height,
            .ssaoWidth = ssaoExtent.width,
            .ssaoHeight = ssaoExtent.height,
            .renderScalePercent = static_cast<uint32_t>(
                swapchain_.renderScalePercent()),
            .activeSamples = configuration_.activeSamples,
            .wireframeEnabled =
                configuration_.wireframeEnabled,
            .wireframeLineWidth =
                configuration_.wireframeLineWidth,
            .pipelineRebuilds =
                configuration_.pipelineRebuilds,
            .swapchainRecreations =
                configuration_.swapchainRecreations,
            .swapchainRecreationDeferrals =
                configuration_.swapchainRecreationDeferrals,
            .renderResourceReconfigurations =
                configuration_.renderResourceReconfigurations,
            .presentQueueRetirementWaits =
                configuration_.presentQueueRetirementWaits,
            .retiredRenderResourceSets =
                configuration_.retiredRenderResourceSets,
            .rendererReconfigurationPending =
                configuration_.rendererReconfigurationPending,
        };

        const auto setupStart = std::chrono::steady_clock::now();
        // The camera the whole frame renders through. Built here rather than
        // in the descriptor class so that nothing below the recorder has to
        // know what an isometric layout is.
        const auto cameraFor = [](const PreparedRenderScene& source) {
            return SceneCamera {
                .clipFromWorld =
                    isoClipFromWorld(source.isoLayout, source.renderExtent),
                .shadowFromWorld = shadowClipFromWorld(source.shadowLayout),
                .position = source.isoLayout.cameraPosition,
                .nearPlane = std::max(source.isoLayout.nearestDepth, 0.001f),
            };
        };
        descriptors_.updateFrame(
            configuration_.descriptorFrameIndex,
            frameData.lighting,
            cameraFor(scene),
            false);
        descriptors_.updateFrame(
            configuration_.descriptorFrameIndex,
            previewFrameData ? previewFrameData->lighting : frameData.lighting,
            cameraFor(previewScene ? *previewScene : scene),
            true);

        const VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        vkCheck(
            vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer failed");
        vulkanDebug::beginLabel(
            device_, commandBuffer, "Sokoban frame", { 0.1f, 0.4f, 1.0f, 1.0f });
        gpuProfiler_.beginFrame(commandBuffer, configuration_.descriptorFrameIndex);
        vulkanDebug::beginLabel(
            device_, commandBuffer, "Swapchain setup", { 0.3f, 0.7f, 1.0f, 1.0f });
        swapchain_.beginFrame(commandBuffer, imageIndex, stats_);
        vulkanDebug::endLabel(device_, commandBuffer);
        setupTimeTelemetry_.record(elapsedMilliseconds(setupStart));

        const auto gameStart = std::chrono::steady_clock::now();
        const bool mainHasTranslucency =
            scene.hasTranslucentContent ||
            hasAuthoredBlendMaterials(frameData, scene);
        const bool mainDepthPublished =
            VulkanSsaoPass::samplesSceneDepth(
                frameData.lighting.ambientOcclusion) ||
            mainHasTranslucency;
        const bool directSsaoColor =
            VulkanSsaoPass::samplesSceneDepth(
                frameData.lighting.ambientOcclusion) &&
            !mainHasTranslucency &&
            ssaoPass_.valid() && pipelines_.ssao() &&
            pipelines_.ssaoComposite();
        const bool ssaoColorSnapshotCopied =
            !directSsaoColor && VulkanSsaoPass::samplesSceneDepth(
                frameData.lighting.ambientOcclusion);
        stats_.mainSceneHasTranslucency = mainHasTranslucency;
        stats_.ssaoColorSnapshotCopied = ssaoColorSnapshotCopied;
        vulkanDebug::beginLabel(
            device_, commandBuffer, "Game rendering", { 0.2f, 0.9f, 0.4f, 1.0f });
        recordGameRendering(
            commandBuffer,
            swapchain_.renderColorView(directSsaoColor),
            swapchain_.resolveColorView(directSsaoColor),
            frameData,
            scene,
            directSsaoColor);
        vulkanDebug::endLabel(device_, commandBuffer);
        gameTimeTelemetry_.record(elapsedMilliseconds(gameStart));

        const auto ssaoStart = std::chrono::steady_clock::now();
        gpuProfiler_.beginPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::Ssao);
        vulkanDebug::beginLabel(
            device_, commandBuffer, "SSAO", { 0.8f, 0.4f, 1.0f, 1.0f });
        // The composite reads the scene and writes it back, which it cannot
        // do to the attachment it is bound to. With ambient occlusion off,
        // this color snapshot and both SSAO draws stay empty.
        gpuProfiler_.beginPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::SsaoSnapshot);
        if (ssaoColorSnapshotCopied) {
            swapchain_.copyResolvedSceneColor(commandBuffer, stats_);
        }
        gpuProfiler_.endPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::SsaoSnapshot);
        ssaoPass_.record(
            commandBuffer,
            swapchain_.resolvedColorView(),
            frameData.lighting.ambientOcclusion,
            isoClipFromView(scene.isoLayout, scene.renderExtent),
            descriptorSet(),
            pipelines_.layout(),
            {
                .occlusion = pipelines_.ssao(),
                .composite = pipelines_.ssaoComposite(),
            },
            gpuProfiler_,
            configuration_.descriptorFrameIndex,
            stats_);
        vulkanDebug::endLabel(device_, commandBuffer);
        gpuProfiler_.endPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::Ssao);
        if (mainDepthPublished) {
            swapchain_.prepareSceneDepthAttachment(commandBuffer, stats_);
        }
        ssaoTimeTelemetry_.record(elapsedMilliseconds(ssaoStart));
        if (previewFrameData && previewScene) {
            // Preserve the completed main view before the preview replaces
            // the inset. ScreenPreviewOverlay samples this copy to feather
            // the main view back over the preview at its perimeter.
            const auto previewStart = std::chrono::steady_clock::now();
            vulkanDebug::beginLabel(
                device_, commandBuffer, "Preview rendering", { 1.0f, 0.7f, 0.2f, 1.0f });
            swapchain_.copyResolvedSceneColor(commandBuffer, stats_);
            recordPreviewRendering(
                commandBuffer,
                swapchain_.renderColorView(),
                swapchain_.resolveColorView(),
                *previewFrameData,
                *previewScene);
            vulkanDebug::endLabel(device_, commandBuffer);
            previewTimeTelemetry_.record(
                elapsedMilliseconds(previewStart));
        }
        const auto outputStart = std::chrono::steady_clock::now();
        gpuProfiler_.beginPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::Output);
        vulkanDebug::beginLabel(
            device_, commandBuffer, "Level transition", { 1.0f, 0.4f, 0.2f, 1.0f });
        recordLevelTransition(
            commandBuffer, frameData.levelTransitionAmount);
        vulkanDebug::endLabel(device_, commandBuffer);
        vulkanDebug::beginLabel(
            device_, commandBuffer, "Tonemap", { 0.6f, 0.5f, 1.0f, 1.0f });
        recordTonemap(commandBuffer, frameData.outputTransform);
        vulkanDebug::endLabel(device_, commandBuffer);
        vulkanDebug::beginLabel(
            device_, commandBuffer, "UI composition", { 0.9f, 0.9f, 0.2f, 1.0f });
        if (configuration_.developerWorkspaceVisible) {
            // Compose the game's own UI onto the tonemapped display image,
            // then publish that image for ImGui's dockable Game Viewport. The
            // swapchain is reserved for the docking workspace itself.
            //
            // The UI belongs on this side of the tonemap in both paths: it is
            // authored in display colours and would otherwise be graded along
            // with the scene.
            recordOverlayRendering(
                commandBuffer,
                swapchain_.displayColorImage(),
                swapchain_.displayColorView(),
                renderExtent,
                uiDrawData,
                true,
                false,
                false);
            swapchain_.publishDisplayColor(commandBuffer, stats_);
            swapchain_.prepareSwapchainForUi(
                commandBuffer, imageIndex, stats_);
            recordOverlayRendering(
                commandBuffer,
                swapchain_.image(imageIndex),
                swapchain_.imageView(imageIndex),
                extent,
                uiDrawData,
                false,
                true,
                true);
        } else {
            swapchain_.upscaleSceneToSwapchain(
                commandBuffer, imageIndex, stats_);
            recordOverlayRendering(
                commandBuffer,
                swapchain_.image(imageIndex),
                swapchain_.imageView(imageIndex),
                extent,
                uiDrawData,
                true,
                true,
                false);
        }
        vulkanDebug::endLabel(device_, commandBuffer);
        vulkanDebug::beginLabel(
            device_, commandBuffer, "Present transition", { 0.3f, 0.7f, 1.0f, 1.0f });
        swapchain_.endFrame(commandBuffer, imageIndex, stats_);
        vulkanDebug::endLabel(device_, commandBuffer);
        gpuProfiler_.endPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::Output);
        gpuProfiler_.endFrame(commandBuffer, configuration_.descriptorFrameIndex);
        vulkanDebug::endLabel(device_, commandBuffer);
        vkCheck(
            vkEndCommandBuffer(commandBuffer),
            "vkEndCommandBuffer failed");
        outputTimeTelemetry_.record(elapsedMilliseconds(outputStart));
        return stats_;
    }

private:
    VkDescriptorSet descriptorSet() const
    {
        return descriptors_.set(
            configuration_.descriptorFrameIndex,
            previewDescriptor_);
    }

    // The flat tile shader in either blend mode. Same shaders and same
    // dynamic state, so switching between them costs one pipeline bind.
    [[nodiscard]] VkPipeline flatScenePipeline(bool opaque) const
    {
        return opaque ? pipelines_.sceneOpaque() : pipelines_.scene();
    }

    // Draws a run of quads that were written to consecutive draw-instance
    // entries. This is where T1's throughput actually comes from: the whole
    // point of moving per-draw parameters into a buffer was that neighbouring
    // faces sharing a pipeline stop needing a draw call each.
    void drawQuadRun(
        VkCommandBuffer commandBuffer,
        uint32_t firstInstance,
        uint32_t instanceCount)
    {
        if (instanceCount == 0) {
            return;
        }
        vkCmdDraw(commandBuffer, 6, instanceCount, 0, firstInstance);
        ++stats_.drawCalls;
    }

    // Records one draw's parameters and returns the index a shader reads
    // them back at. Scene pipelines take everything per-draw this way now;
    // only the shadow pipelines still push it.
    [[nodiscard]] uint32_t writeDrawInstance(const GpuDrawInstance& instance)
    {
        return models_.writeDrawInstance(
            configuration_.descriptorFrameIndex, instance);
    }

    // Only the skinned-model pipeline reads this: its gl_InstanceIndex is
    // already spoken for by the skinning palette, so it cannot carry the
    // draw-instance index the way every other pipeline does.
    void pushDrawInstanceIndex(
        VkCommandBuffer commandBuffer, uint32_t drawInstance) const
    {
        const DrawInstanceIndexPushConstants indexConstants {
            .drawInstance = drawInstance,
        };
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(indexConstants),
            &indexConstants);
    }

    void bindDescriptorSet(VkCommandBuffer commandBuffer) const
    {
        const std::array<VkDescriptorSet, 2> sets {
            descriptorSet(),
            descriptors_.textureSet(configuration_.descriptorFrameIndex),
        };
        if (sets[0] && sets[1]) {
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines_.layout(),
                0,
                static_cast<uint32_t>(sets.size()),
                sets.data(),
                0,
                nullptr);
        }
    }

    void recordShadowMapRendering(
        VkCommandBuffer commandBuffer,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene)
    {
        shadowPass_.begin(
            commandBuffer, pipelines_.shadow(), stats_);
        drawShadowFaces(commandBuffer, scene.shadowLayout, scene.shadowFaces);
        VkPipeline boundModelPipeline = VK_NULL_HANDLE;
        for (std::size_t tileIndex : scene.shadowModelIndices) {
            const RenderFrameData::Tile& tile = frameData.tiles[tileIndex];
            if (!models_.tileReadyForDraw(
                    tile, configuration_.descriptorFrameIndex)) {
                continue;
            }
            const VkPipeline modelPipeline =
                models_.modelUsesGpuSkinning(tile.model)
                ? pipelines_.skinnedModelShadow()
                : pipelines_.modelShadow();
            if (boundModelPipeline != modelPipeline) {
                shadowPass_.bindModelPipeline(
                    commandBuffer,
                    modelPipeline,
                    stats_);
                bindDescriptorSet(commandBuffer);
                boundModelPipeline = modelPipeline;
            }
            drawModelShadow(
                commandBuffer,
                scene.shadowLayout,
                tile);
        }
        shadowPass_.end(commandBuffer, stats_);

        const std::size_t pointLightCount = std::min(
            frameData.lighting.pointLightCount,
            RenderFrameData::pointLightCapacity);
        for (std::size_t lightIndex = 0;
             lightIndex < pointLightCount;
             ++lightIndex) {
            const RenderFrameData::PointLight& light =
                frameData.lighting.pointLights[lightIndex];
            if (!light.castsShadows || light.intensity <= 0.0f ||
                light.range <= config::pointShadowNearPlane) {
                continue;
            }
            const PreparedPointShadowCasters& casters =
                scene.pointShadowCasters[lightIndex];
            std::vector<PointShadowModelState>& modelStates =
                pointShadowModelStateScratch_[lightIndex];
            modelStates.clear();
            modelStates.reserve(casters.modelTileIndices.size());
            stats_.pointShadowModelCandidates += static_cast<uint32_t>(
                casters.modelTileIndices.size());
            const Sphere influence { light.position, light.range };
            bool hasSkinnedCaster = false;
            for (std::size_t tileIndex : casters.modelTileIndices) {
                const RenderFrameData::Tile& tile =
                    frameData.tiles[tileIndex];
                const bool ready = models_.tileReadyForDraw(
                    tile, configuration_.descriptorFrameIndex);
                const bool skinned =
                    models_.modelUsesGpuSkinning(tile.model);
                hasSkinnedCaster = hasSkinnedCaster || skinned;
                bool inRange = true;
                // Bind-pose bounds are not conservative for skinned motion.
                // Static loaded meshes have exact local bounds and may be
                // transformed into a safe world-space range test.
                if (pointShadowCacheEnabled_ && ready &&
                    !skinned) {
                    const Aabb bounds = modelWorldBounds(
                        tile, models_.boundsForModel(tile.model));
                    if (bounds.valid()) {
                        inRange = intersects(bounds, influence);
                    }
                }
                if (!inRange) {
                    ++stats_.pointShadowModelsCulled;
                    continue;
                }
                ++stats_.pointShadowModelsInRange;
                modelStates.push_back({
                    .tileIndex = tileIndex,
                    .tile = tile,
                    .ready = ready,
                });
            }
            if (pointShadowCacheEnabled_ && !hasSkinnedCaster &&
                pointShadowFaceCache_.reusable(
                    lightIndex,
                    light,
                    scene.shadowFaces,
                    casters.faceIndices,
                    modelStates)) {
                stats_.pointShadowCubeFacesReused += 6;
                continue;
            }
            for (uint32_t cubeFace = 0; cubeFace < 6; ++cubeFace) {
                shadowPass_.beginPointFace(
                    commandBuffer,
                    static_cast<uint32_t>(lightIndex),
                    cubeFace,
                    pipelines_.shadow(),
                    stats_);
                bindDescriptorSet(commandBuffer);
                for (std::size_t faceIndex : casters.faceIndices) {
                    drawPointShadowFace(
                        commandBuffer,
                        light,
                        cubeFace,
                        scene.shadowFaces[faceIndex]);
                }
                VkPipeline pointModelPipeline = VK_NULL_HANDLE;
                for (const PointShadowModelState& state : modelStates) {
                    if (!state.ready) {
                        continue;
                    }
                    const RenderFrameData::Tile& tile = state.tile;
                    const VkPipeline modelPipeline =
                        models_.modelUsesGpuSkinning(tile.model)
                        ? pipelines_.skinnedModelShadow()
                        : pipelines_.modelShadow();
                    if (pointModelPipeline != modelPipeline) {
                        shadowPass_.bindModelPipeline(
                            commandBuffer,
                            modelPipeline,
                            stats_);
                        bindDescriptorSet(commandBuffer);
                        pointModelPipeline = modelPipeline;
                    }
                    drawPointModelShadow(
                        commandBuffer,
                        light,
                        cubeFace,
                        tile);
                }
                shadowPass_.endPointFace(commandBuffer);
            }
            stats_.pointShadowCubeFacesRendered += 6;
            if (pointShadowCacheEnabled_ && !hasSkinnedCaster) {
                pointShadowFaceCache_.markRendered(
                    lightIndex,
                    light,
                    scene.shadowFaces,
                    casters.faceIndices,
                    modelStates);
            } else {
                pointShadowFaceCache_.invalidate(lightIndex);
            }
        }
        // The descriptor exposes the complete cube array. Even when no point
        // light rendered this frame, every layer must be in its declared
        // shader-read layout before any scene draw can access the descriptor.
        shadowPass_.finishPointShadows(commandBuffer, stats_);
    }

    [[nodiscard]] bool hasAuthoredBlendMaterials(
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene) const
    {
        return std::ranges::any_of(
            scene.opaqueModelIndices,
            [&](std::size_t tileIndex) {
                return models_.materialForModel(
                    frameData.tiles[tileIndex].model).policy.hasBlend;
            });
    }

    void recordGameRendering(
        VkCommandBuffer commandBuffer,
        VkImageView colorView,
        VkImageView resolveView,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene,
        bool directSsaoColor)
    {
        const bool hasTranslucency = scene.hasTranslucentContent ||
            hasAuthoredBlendMaterials(frameData, scene);
        const auto shadowStart = std::chrono::steady_clock::now();
        gpuProfiler_.beginPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::Shadows);
        if (shadowPass_.valid() && pipelines_.shadow()) {
            recordShadowMapRendering(
                commandBuffer, frameData, scene);
        }
        gpuProfiler_.endPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::Shadows);
        shadowTimeTelemetry_.record(elapsedMilliseconds(shadowStart));
        const auto sceneStart = std::chrono::steady_clock::now();
        gpuProfiler_.beginPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::Scene);
        gpuProfiler_.beginPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::SceneRaster);
        if (directSsaoColor) {
            swapchain_.prepareSceneColorAttachment(commandBuffer, stats_);
        } else {
            swapchain_.ensureSceneColorReadable(commandBuffer, stats_);
        }
        recordScenePass(
            commandBuffer,
            colorView,
            resolveView,
            frameData,
            scene,
            false,
            false,
            hasTranslucency || !resolveView,
            false,
            true,
            { .offset = { 0, 0 }, .extent = swapchain_.renderExtent() });
        if (directSsaoColor) {
            swapchain_.publishSceneColor(commandBuffer, stats_);
        }
        gpuProfiler_.endPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::SceneRaster);
        // Publish the single-sample resolve itself. SSAO and translucent
        // water can read it directly; the recorder restores attachment state
        // after those consumers finish.
        gpuProfiler_.beginPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::SceneDepthPublish);
        if (VulkanSsaoPass::samplesSceneDepth(
                frameData.lighting.ambientOcclusion) || hasTranslucency) {
            swapchain_.publishSceneDepth(commandBuffer, stats_);
        }
        gpuProfiler_.endPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::SceneDepthPublish);
        gpuProfiler_.beginPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::SceneTranslucency);
        if (!hasTranslucency) {
            gpuProfiler_.endPhase(
                commandBuffer,
                configuration_.descriptorFrameIndex,
                VulkanGpuPhase::SceneTranslucency);
            gpuProfiler_.endPhase(
                commandBuffer,
                configuration_.descriptorFrameIndex,
                VulkanGpuPhase::Scene);
            sceneTimeTelemetry_.record(elapsedMilliseconds(sceneStart));
            return;
        }
        swapchain_.copyResolvedSceneColor(
            commandBuffer, stats_);
        recordScenePass(
            commandBuffer,
            colorView,
            resolveView,
            frameData,
            scene,
            true,
            true,
            !resolveView,
            true,
            false,
            { .offset = { 0, 0 }, .extent = swapchain_.renderExtent() });
        gpuProfiler_.endPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::SceneTranslucency);
        gpuProfiler_.endPhase(
            commandBuffer,
            configuration_.descriptorFrameIndex,
            VulkanGpuPhase::Scene);
        sceneTimeTelemetry_.record(elapsedMilliseconds(sceneStart));
    }

    void recordPreviewRendering(
        VkCommandBuffer commandBuffer,
        VkImageView colorView,
        VkImageView resolveView,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene)
    {
        previewDescriptor_ = true;
        const bool hasTranslucency = scene.hasTranslucentContent ||
            hasAuthoredBlendMaterials(frameData, scene);
        const VkExtent2D full = swapchain_.renderExtent();
        const VkExtent2D extent {
            std::max(1U, static_cast<uint32_t>(
                static_cast<float>(full.width) * 0.75f)),
            std::max(1U, static_cast<uint32_t>(
                static_cast<float>(full.height) * 0.75f)),
        };
        const VkRect2D inset {
            .offset = {
                static_cast<int32_t>((full.width - extent.width) / 2U),
                static_cast<int32_t>((full.height - extent.height) / 2U),
            },
            .extent = extent,
        };

        if (shadowPass_.valid() && pipelines_.shadow()) {
            recordShadowMapRendering(commandBuffer, frameData, scene);
        }
        recordScenePass(
            commandBuffer,
            colorView,
            resolveView,
            frameData,
            scene,
            false,
            false,
            hasTranslucency || !resolveView,
            false,
            true,
            inset);
        if (!hasTranslucency) {
            return;
        }
        swapchain_.publishSceneDepth(commandBuffer, stats_);
        // Keep sceneColor as the untouched main view for the opacity feather.
        // Preview translucency can also sample it naturally as the world
        // visible behind the portal-like inset.
        recordScenePass(
            commandBuffer,
            colorView,
            resolveView,
            frameData,
            scene,
            true,
            true,
            !resolveView,
            true,
            false,
            inset);
        swapchain_.prepareSceneDepthAttachment(commandBuffer, stats_);
    }

    // Scene target -> display image: linear scene light in, a presentable
    // colour out. Every reader downstream of this point wants the display
    // image rather than the scene target - the upscale blit, the developer
    // workspace's game viewport, and captureRenderedFrame - because the scene
    // target holds range and a format none of them can handle.
    //
    // This is also the frame's only sRGB encode. It happens because the
    // display image is an _SRGB format and the hardware does it on write; see
    // tonemap.frag.glsl before adding any gamma of your own.
    void recordTonemap(
        VkCommandBuffer commandBuffer,
        const RenderFrameData::OutputTransform& outputTransform)
    {
        // Unconditionally, and before the guard below: the blit, the game
        // viewport and the capture all assume the display image is a colour
        // attachment by now. Skipping the transition would make a missing
        // pipeline present as a layout error somewhere else entirely.
        swapchain_.beginTonemap(commandBuffer, stats_);

        const VkPipeline pipeline = pipelines_.tonemap();
        if (!pipeline) {
            return;
        }

        const VkExtent2D extent = swapchain_.renderExtent();
        const VkRenderingAttachmentInfo attachment {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain_.displayColorView(),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            // The pass covers every pixel of the target, so its previous
            // contents are worth nothing.
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };
        const VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { .offset = { 0, 0 }, .extent = extent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment,
        };
        const VkViewport viewport {
            .x = 0.0f,
            .y = static_cast<float>(extent.height),
            .width = static_cast<float>(extent.width),
            .height = -static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const VkRect2D scissor { .offset = { 0, 0 }, .extent = extent };
        GpuDrawInstance pushConstants {};
        // The fragment shader converts EV to a multiplier with exp2. Keeping
        // the persisted value in photographic stops makes the setting
        // symmetric and gives zero an exact no-exposure-change meaning.
        pushConstants.color = {
            normalizedExposureEv(outputTransform.exposureEv),
            static_cast<float>(outputTransform.curve),
            0.0f,
            0.0f,
        };

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ++stats_.renderPasses;
        vkCmdBindPipeline(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        ++stats_.pipelineBinds;
        bindDescriptorSet(commandBuffer);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(GpuDrawInstance),
            &pushConstants);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        ++stats_.drawCalls;
        vkCmdEndRendering(commandBuffer);
    }

    void recordLevelTransition(
        VkCommandBuffer commandBuffer,
        float amount)
    {
        const VkPipeline pipeline = pipelines_.worldTransition();
        if (amount <= 0.0f || !pipeline) {
            return;
        }

        // Sample a copy: Vulkan does not permit the transition shader to read
        // and overwrite resolvedColorImage in the same draw.
        swapchain_.copyResolvedSceneColor(commandBuffer, stats_);

        const VkExtent2D extent = swapchain_.renderExtent();
        const VkRenderingAttachmentInfo attachment {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain_.resolvedColorView(),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };
        const VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { .offset = { 0, 0 }, .extent = extent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment,
        };
        const VkViewport viewport {
            .x = 0.0f,
            .y = static_cast<float>(extent.height),
            .width = static_cast<float>(extent.width),
            .height = -static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const VkRect2D scissor { .offset = { 0, 0 }, .extent = extent };
        GpuDrawInstance pushConstants {};
        pushConstants.color.x = std::clamp(amount, 0.0f, 1.0f);

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ++stats_.renderPasses;
        vkCmdBindPipeline(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        ++stats_.pipelineBinds;
        bindDescriptorSet(commandBuffer);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(GpuDrawInstance),
            &pushConstants);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        ++stats_.drawCalls;
        vkCmdEndRendering(commandBuffer);
    }

    void recordScenePass(
        VkCommandBuffer commandBuffer,
        VkImageView colorView,
        VkImageView resolveView,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene,
        bool translucentPass,
        bool loadColor,
        bool storeColor,
        bool loadDepth,
        bool writeDepth,
        VkRect2D renderArea)
    {
        // Alpha is the ambient mask, not an opacity: a pixel no geometry
        // covered has no ambient term to occlude, so it clears to zero and
        // the SSAO composite leaves it alone. That matters beyond the
        // background - the 2D board's opaque pass runs on the *blended*
        // pipeline (see flatScenePipeline below), which writes no alpha at
        // all, so the clear is the only value those pixels ever get.
        const VkClearValue clearValue {
            .color = { { 0.03f, 0.04f, 0.06f, 0.0f } },
        };
        const VkRenderingAttachmentInfo colorAttachment {
            .sType =
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = colorView,
            .imageLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = resolveView
                ? VK_RESOLVE_MODE_AVERAGE_BIT
                : VK_RESOLVE_MODE_NONE,
            .resolveImageView = resolveView,
            .resolveImageLayout = resolveView
                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = loadColor
                ? VK_ATTACHMENT_LOAD_OP_LOAD
                : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = storeColor
                ? VK_ATTACHMENT_STORE_OP_STORE
                : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = clearValue,
        };
        const VkClearValue depthClear {
            .depthStencil = { .depth = 1.0f, .stencil = 0 },
        };
        const bool samplesDepthAttachment =
            !writeDepth && !swapchain_.resolveDepthView();
        const bool resolvesDepth =
            writeDepth && swapchain_.resolveDepthView();
        const VkRenderingAttachmentInfo depthAttachment {
            .sType =
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain_.depthView(),
            .imageLayout = samplesDepthAttachment
                ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .resolveMode = resolvesDepth
                ? VK_RESOLVE_MODE_SAMPLE_ZERO_BIT
                : VK_RESOLVE_MODE_NONE,
            .resolveImageView = resolvesDepth
                ? swapchain_.resolveDepthView()
                : VK_NULL_HANDLE,
            .resolveImageLayout = resolvesDepth
                ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = loadDepth
                ? VK_ATTACHMENT_LOAD_OP_LOAD
                : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = depthClear,
        };
        const VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = renderArea.offset,
                .extent = renderArea.extent,
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = swapchain_.depthView()
                ? &depthAttachment
                : nullptr,
        };
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ++stats_.renderPasses;

        const VkViewport viewport {
            .x = static_cast<float>(renderArea.offset.x),
            .y = static_cast<float>(
                renderArea.offset.y +
                static_cast<int32_t>(renderArea.extent.height)),
            .width = static_cast<float>(renderArea.extent.width),
            .height = -static_cast<float>(renderArea.extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const VkRect2D scissor = renderArea;
        // The opaque iso pass starts on the non-blended twin. The 2D path and
        // the translucent pass keep the blended one: the former draws a
        // blended grid overlay, the latter needs the blend unit by definition.
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            flatScenePipeline(
                frameData.viewMode == RenderViewMode::Isometric3D &&
                !translucentPass));
        ++stats_.pipelineBinds;
        bindDescriptorSet(commandBuffer);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
        vkCmdSetFrontFace(
            commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetPrimitiveTopology(
            commandBuffer,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdSetLineWidth(
            commandBuffer,
            configuration_.wireframeEnabled
                ? configuration_.wireframeLineWidth
                : 1.0f);
        vkCmdSetDepthTestEnable(
            commandBuffer,
            swapchain_.depthView() ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthWriteEnable(
            commandBuffer,
            swapchain_.depthView() && writeDepth
                ? VK_TRUE
                : VK_FALSE);
        vkCmdSetDepthCompareOp(
            commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);

        if (frameData.viewMode == RenderViewMode::Isometric3D) {
            drawIsoFrame(
                commandBuffer,
                scene,
                frameData,
                translucentPass);
        } else {
            for (const RenderFrameData::Tile& tile :
                 frameData.tiles) {
                drawTile(
                    commandBuffer,
                    scene.tileLayout,
                    tile,
                    frameData.lighting);
            }
            if (!translucentPass) {
                vkCmdSetDepthWriteEnable(
                    commandBuffer, VK_FALSE);
                drawTopDownGridOverlay(
                    commandBuffer,
                    scene.tileLayout,
                    frameData);
            }
        }
        vkCmdEndRendering(commandBuffer);
    }

    void recordOverlayRendering(
        VkCommandBuffer commandBuffer,
        VkImage colorImage,
        VkImageView colorView,
        VkExtent2D targetExtent,
        const UiDrawData& uiDrawData,
        bool renderGameUi,
        bool renderImGui,
        bool clearTarget)
    {
#if !SOKOBAN_ENABLE_DEBUG_UI
        renderImGui = false;
#endif
        const bool hasGameUi =
            renderGameUi &&
            !uiDrawData.commands.empty() &&
            uiDrawData.viewportSize.x > 0.0f &&
            uiDrawData.viewportSize.y > 0.0f;
        if (!hasGameUi && !renderImGui) {
            return;
        }
        if (!clearTarget) {
            vulkanResources::transitionImage(
                commandBuffer,
                colorImage,
                vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                {
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
                {
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT |
                        VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
                VK_DEPENDENCY_BY_REGION_BIT);
            ++stats_.imageBarriers;
        }

        const VkClearValue clearValue {
            .color = { { 0.025f, 0.03f, 0.04f, 1.0f } },
        };

        const VkRenderingAttachmentInfo colorAttachment {
            .sType =
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = colorView,
            .imageLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = clearTarget
                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clearValue,
        };
        const VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = targetExtent,
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        };
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ++stats_.renderPasses;

        if (hasGameUi) {
            const VkViewport viewport {
                .x = 0.0f,
                .y = static_cast<float>(
                    targetExtent.height),
                .width = static_cast<float>(
                    targetExtent.width),
                .height = -static_cast<float>(
                    targetExtent.height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            };
            const VkRect2D scissor {
                .offset = { 0, 0 },
                .extent = targetExtent,
            };
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines_.ui());
            ++stats_.pipelineBinds;
            bindDescriptorSet(commandBuffer);
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
            vkCmdSetFrontFace(
                commandBuffer,
                VK_FRONT_FACE_COUNTER_CLOCKWISE);
            vkCmdSetPrimitiveTopology(
                commandBuffer,
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            vkCmdSetLineWidth(commandBuffer, 1.0f);
            vkCmdSetDepthTestEnable(commandBuffer, VK_FALSE);
            vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);
            vkCmdSetDepthCompareOp(
                commandBuffer, VK_COMPARE_OP_ALWAYS);

            const RenderFrameData::Lighting unlit {};
            for (const UiDrawCommand& command :
                 uiDrawData.commands) {
                drawQuadRun(
                    commandBuffer,
                    drawUiRect(
                        commandBuffer,
                        command,
                        uiDrawData.viewportSize,
                        unlit),
                    1);
            }
        }
        if (renderImGui) {
            renderDebugUi(commandBuffer);
        }
        vkCmdEndRendering(commandBuffer);
    }

    void drawTile(
        VkCommandBuffer commandBuffer,
        const TileRenderLayout& layout,
        const RenderFrameData::Tile& tile,
        const RenderFrameData::Lighting& lighting)
    {
        const Vec2 origin {
            layout.boardBottomLeft.x +
                tile.position.x * layout.tileSize.x,
            layout.boardBottomLeft.y +
                tile.position.y * layout.tileSize.y,
        };
        const Vec2 size {
            layout.tileSize.x * tile.size.x,
            layout.tileSize.y * tile.size.y,
        };
        drawQuadRun(
            commandBuffer,
            drawFace(
            commandBuffer,
            {
                Vec3 { origin.x, origin.y, 0.0f },
                Vec3 { origin.x + size.x, origin.y, 0.0f },
                Vec3 {
                    origin.x + size.x,
                    origin.y + size.y,
                    0.0f },
                Vec3 { origin.x, origin.y + size.y, 0.0f },
            },
            tile.color,
            {},
            lighting,
            false,
            {},
            {},
            0.0f,
            false,
            true),
            1);
    }

    void drawIsoFrame(
        VkCommandBuffer commandBuffer,
        const PreparedRenderScene& scene,
        const RenderFrameData& frameData,
        bool translucentPass)
    {
        const std::vector<std::size_t>& faceIndices =
            translucentPass
            ? scene.translucentFaceIndices
            : scene.opaqueFaceIndices;
        const bool opaquePass = !translucentPass;
        VkPipeline boundFacePipeline = flatScenePipeline(opaquePass);
        uint32_t runFirst = 0;
        uint32_t runCount = 0;
        const auto flushFaceRun = [&] {
            drawQuadRun(commandBuffer, runFirst, runCount);
            runCount = 0;
        };
        // The whole pass stays on LESS_OR_EQUAL, deliberately.
        //
        // LESS is the textbook compare op for a front-to-back opaque pass and
        // it was tried here. It is the wrong trade for this renderer, because
        // projectIsoPoint clamps z to [0, 1] instead of clipping: anything
        // that lands on the far plane arrives at exactly 1.0, and the depth
        // buffer is cleared to exactly 1.0. Under LESS such a surface does
        // not degrade, it vanishes - an entire row of tiles at a time, with
        // nothing on screen to say why. Under LESS_OR_EQUAL it still draws.
        //
        // What made surfaces land on the far plane at all is fixed at the
        // source, in calculateIsoLayout: the depth range now covers every
        // drawn tile rather than only the ones that frame the camera, so
        // nothing is clamped and no two surfaces share z = 1.0. That removes
        // the ties instead of choosing a winner for them, which is why the
        // stricter compare op is no longer buying anything.
        for (std::size_t position = 0; position < faceIndices.size();
             ++position) {
            const std::size_t faceIndex = faceIndices[position];
            const PreparedIsoFace& face =
                scene.isoFaces[faceIndex];
            const RenderFrameData::GroundSplatRegion* splatRegion =
                frameData.groundSplatRegionAt(face.cell);
            const GroundSplatTextures& splatTextures = splatRegion
                ? splatRegion->textures
                : frameData.groundSplat;
            // A face in the opaque list can still carry a sub-1.0 alpha - the
            // editor's ladder-rung preview does - and those must keep the
            // blend unit. Everything else in this pass writes coverage it
            // fully owns. Alpha from a sampled texture is not visible here;
            // there is no alpha mode in the material model yet, and adding one
            // belongs with real materials rather than in this pass.
            const bool opaqueSurface = opaquePass && face.color.w >= 1.0f;
            const VkPipeline desiredPipeline = [&] {
                switch (face.material) {
                case PreparedSurfaceMaterial::Water:
                    return pipelines_.water();
                case PreparedSurfaceMaterial::MirrorEnergy:
                    return pipelines_.mirrorEnergy();
                case PreparedSurfaceMaterial::GroundSplat:
                    // Falls back to the flat tile shader when the manifest
                    // does not provide the ground textures.
                    if (!splatTextures.valid()) {
                        return flatScenePipeline(opaqueSurface);
                    }
                    return opaqueSurface
                        ? pipelines_.groundSplatOpaque()
                        : pipelines_.groundSplat();
                case PreparedSurfaceMaterial::Standard:
                    return flatScenePipeline(opaqueSurface);
                }
                return flatScenePipeline(opaqueSurface);
            }();
            if (desiredPipeline != boundFacePipeline) {
                // The run ends here: a pipeline change is the only thing that
                // can interrupt one, because everything else that used to
                // differ per draw now lives in the instance entry.
                flushFaceRun();
                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    desiredPipeline);
                ++stats_.pipelineBinds;
                boundFacePipeline = desiredPipeline;
            }
            uint32_t faceInstance = 0;
            if (face.material == PreparedSurfaceMaterial::Water) {
                faceInstance = drawWaterFace(
                    commandBuffer,
                    face.worldVertices,
                    face.color,
                    face.worldOrigin,
                    face.gridSize,
                    Vec2 {
                        static_cast<float>(
                            frameData.waterGridBounds.originX),
                        static_cast<float>(
                            frameData.waterGridBounds.originY),
                    },
                    Vec2 {
                        static_cast<float>(
                            frameData.waterGridBounds.width),
                        static_cast<float>(
                            frameData.waterGridBounds.height),
                    },
                    frameData.waterAnimationTimeSeconds,
                    face.shorelineMask,
                    frameData.waterRendering,
                    scene.isoLayout,
                    face.isEditorPreview);
            } else if (
                face.material == PreparedSurfaceMaterial::MirrorEnergy) {
                faceInstance = drawMirrorEnergyFace(
                    commandBuffer,
                    face.worldVertices,
                    face.color,
                    face.normal,
                    frameData.effectAnimationTimeSeconds);
            } else if (
                face.material == PreparedSurfaceMaterial::GroundSplat &&
                splatTextures.valid()) {
                const Vec2 splatOrigin = splatRegion
                    ? Vec2 {
                          face.worldOrigin.x -
                              static_cast<float>(splatRegion->origin.x),
                          face.worldOrigin.y -
                              static_cast<float>(splatRegion->origin.y),
                      }
                    : face.worldOrigin;
                faceInstance = drawGroundSplatFace(
                    commandBuffer,
                    face.worldVertices,
                    face.color,
                    face.normal,
                    frameData.lighting,
                    splatOrigin,
                    face.gridSize,
                    face.showGrid
                        ? frameData.gridOverlay.color
                        : Vec4 {},
                    frameData.gridOverlay.width,
                    face.isEditorPreview,
                    splatTextures);
            } else {
                faceInstance = drawFace(
                    commandBuffer,
                    face.worldVertices,
                    face.color,
                    face.normal,
                    frameData.lighting,
                    face.blurBehind,
                    face.showGrid
                        ? frameData.gridOverlay.color
                        : Vec4 {},
                    face.gridSize,
                    frameData.gridOverlay.width,
                    face.isEditorPreview);
            }
            // Entries are handed out in order, so a run is simply a
            // contiguous span of them. The guard is defensive: if anything
            // ever allocates an entry mid-loop the run breaks cleanly rather
            // than drawing someone else's face.
            if (runCount != 0 && faceInstance != runFirst + runCount) {
                flushFaceRun();
            }
            if (runCount == 0) {
                runFirst = faceInstance;
            }
            ++runCount;
        }
        flushFaceRun();

        const auto batchStateFor = [](const GpuDrawInstance& constants) {
            std::array<uint32_t, 29> result {};
            std::size_t index = 0;
            const auto append = [&result, &index](Vec4 value) {
                result[index++] = std::bit_cast<uint32_t>(value.x);
                result[index++] = std::bit_cast<uint32_t>(value.y);
                result[index++] = std::bit_cast<uint32_t>(value.z);
                result[index++] = std::bit_cast<uint32_t>(value.w);
            };
            append(constants.color);
            result[index++] = std::bit_cast<uint32_t>(
                constants.normalAndAmbientRed.w);
            append(constants.sunDirectionAndAmbientGreen);
            append(constants.sunRadianceAndAmbientBlue);
            append(constants.shadowOptions);
            append(constants.materialOptions);
            append(constants.gridColor);
            append(constants.textureOptions);
            return result;
        };

        const auto modelDepth = [&](const RenderFrameData::Tile& tile) {
            const ModelTransformPoints transform =
                IsoScenePreparer::modelTransformPoints(tile);
            const Vec3 center {
                (transform.xPoint.x + transform.yPoint.x +
                    transform.zPoint.x - transform.origin.x) * 0.5f,
                (transform.xPoint.y + transform.yPoint.y +
                    transform.zPoint.y - transform.origin.y) * 0.5f,
                (transform.xPoint.z + transform.yPoint.z +
                    transform.zPoint.z - transform.origin.z) * 0.5f,
            };
            const Vec3 relative {
                center.x - scene.isoLayout.cameraPosition.x,
                center.y - scene.isoLayout.cameraPosition.y,
                center.z - scene.isoLayout.cameraPosition.z,
            };
            return relative.x * scene.isoLayout.cameraForward.x +
                relative.y * scene.isoLayout.cameraForward.y +
                relative.z * scene.isoLayout.cameraForward.z;
        };
        VulkanSceneRecorder::Scratch transientScratch;
        VulkanSceneRecorder::Scratch& scratch = scratchReuseEnabled_
            ? scratch_
            : transientScratch;
        const auto clearAndReserve = [this](auto& values, std::size_t size) {
            const std::size_t oldCapacity = values.capacity();
            values.clear();
            values.reserve(size);
            if (values.capacity() != oldCapacity) {
                ++stats_.recorderScratchGrowths;
            }
        };
        std::vector<RecorderModelCandidate>& candidates =
            scratch.modelCandidates;
        clearAndReserve(
            candidates,
            scene.opaqueModelIndices.size() +
                scene.translucentModelIndices.size());
        std::size_t sourceOrder = 0;
        if (translucentPass) {
            for (std::size_t tileIndex : scene.translucentModelIndices) {
                candidates.push_back({
                    .tileIndex = tileIndex,
                    .depth = modelDepth(frameData.tiles[tileIndex]),
                    .sourceOrder = sourceOrder++,
                });
            }
            for (std::size_t tileIndex : scene.opaqueModelIndices) {
                const RenderFrameData::Tile& tile = frameData.tiles[tileIndex];
                if (models_.materialForModel(tile.model).policy.hasBlend) {
                    candidates.push_back({
                        .tileIndex = tileIndex,
                        .alphaSelection =
                            MaterialAlphaSelection::BlendOnly,
                        .depth = modelDepth(tile),
                        .sourceOrder = sourceOrder++,
                    });
                }
            }
            // The ordinal preserves stable depth ties without stable_sort's
            // temporary allocation.
            std::sort(
                candidates.begin(),
                candidates.end(),
                [](const RecorderModelCandidate& left,
                    const RecorderModelCandidate& right) {
                    if (left.depth > right.depth) {
                        return true;
                    }
                    if (right.depth > left.depth) {
                        return false;
                    }
                    return left.sourceOrder < right.sourceOrder;
                });
        } else {
            for (std::size_t tileIndex : scene.opaqueModelIndices) {
                const RenderFrameData::Tile& tile = frameData.tiles[tileIndex];
                const ModelMaterialPolicy policy =
                    models_.materialForModel(tile.model).policy;
                if (policy.hasOpaqueOrMask) {
                    candidates.push_back({
                        .tileIndex = tileIndex,
                        .alphaSelection = policy.hasBlend
                            ? MaterialAlphaSelection::OpaqueAndMask
                            : MaterialAlphaSelection::All,
                        .sourceOrder = sourceOrder++,
                    });
                }
            }
        }
        std::vector<RecorderModelDraw>& draws = scratch.modelDraws;
        clearAndReserve(draws, candidates.size());
        for (const RecorderModelCandidate& candidate : candidates) {
            const RenderFrameData::Tile& tile =
                frameData.tiles[candidate.tileIndex];
            if (!models_.tileReadyForDraw(
                    tile, configuration_.descriptorFrameIndex)) {
                continue;
            }
            const bool mirrorGhost =
                tile.effect == RenderSurfaceEffect::MirrorEnergy;
            const bool skinned = models_.modelUsesGpuSkinning(tile.model);
            const VulkanModelResources::MaterialBinding material =
                models_.materialForModel(tile.model);
            const GpuDrawInstance constants = modelPushConstants(
                tile,
                frameData.lighting,
                frameData.effectAnimationTimeSeconds,
                candidate.alphaSelection);
            // Mirror ghosts are translucent by construction. Everything else
            // in the opaque pass with a full-alpha tint skips the blend unit.
            const bool opaqueModel =
                opaquePass && !mirrorGhost && constants.color.w >= 1.0f;
            const uint32_t pipelineRank = (mirrorGhost ? 2U : 0U) +
                (skinned ? 1U : 0U) + (opaqueModel ? 0U : 4U);
            const VkPipeline pipeline = mirrorGhost
                ? (skinned ? pipelines_.skinnedMirrorEnergyModel()
                           : pipelines_.mirrorEnergyModel())
                : opaqueModel
                ? (skinned ? pipelines_.skinnedModelOpaque()
                           : pipelines_.modelOpaque())
                : (skinned ? pipelines_.skinnedModel() : pipelines_.model());
            draws.push_back({
                .tile = &tile,
                .mesh = models_.meshForTile(
                    tile, configuration_.descriptorFrameIndex),
                .constants = constants,
                .materialPolicy = material.policy,
                .pipeline = pipeline,
                .pipelineRank = pipelineRank,
                .batchState = batchStateFor(constants),
                .mirrorGhost = mirrorGhost,
                .skinned = skinned,
            });
        }
        std::vector<OpaqueDrawSortItem>& orderedDraws =
            scratch.orderedModelDraws;
        clearAndReserve(orderedDraws, draws.size());
        for (std::size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex) {
            const RecorderModelDraw& draw = draws[drawIndex];
            orderedDraws.push_back({
                .key = {
                    .pipeline = draw.pipelineRank,
                    .material = std::bit_cast<uint32_t>(
                        draw.constants.textureOptions.x),
                    .mesh = draw.tile->model.value,
                    .fragmentState = draw.batchState,
                },
                .drawIndex = drawIndex,
                .instancable = !draw.skinned,
            });
        }
        std::vector<OpaqueDrawBatch>& batches = scratch.modelBatches;
        const std::size_t oldBatchCapacity = batches.capacity();
        batches.clear();
        if (translucentPass) {
            batches.reserve(orderedDraws.size());
            for (std::size_t itemIndex = 0;
                 itemIndex < orderedDraws.size();
                 ++itemIndex) {
                batches.push_back({ .firstItem = itemIndex, .itemCount = 1 });
            }
        } else {
            sortOpaqueDraws(orderedDraws, batches);
        }
        if (batches.capacity() != oldBatchCapacity) {
            ++stats_.recorderScratchGrowths;
        }
        stats_.recorderScratchCapacityBytes = std::max(
            stats_.recorderScratchCapacityBytes,
            scratch.capacityBytes());

        // Front faces are CLOCKWISE here, not counter-clockwise.
        //
        // glTF authors front faces counter-clockwise, and nothing between
        // object space and clip space reverses that: the camera basis is
        // right-handed (cameraRight x cameraUp == cameraForward),
        // projectIsoPointToClip applies no sign flip to x or y, and decoration
        // scale is clamped positive so no instance carries a mirrored
        // transform. What does reverse it is the scene pass viewport, which
        // has a negative height to put +Y up in Vulkan's Y-down NDC. A
        // negative viewport height flips the winding the rasterizer sees, so
        // a counter-clockwise mesh arrives clockwise.
        //
        // Culling BACK against the pass-level COUNTER_CLOCKWISE front face
        // therefore discards the outside of every mesh and keeps the inside,
        // which is exactly the "hollow model" symptom.
        constexpr VkFrontFace modelFrontFace = VK_FRONT_FACE_CLOCKWISE;

        // Wireframe opts out entirely: seeing through geometry is the whole
        // point of that mode. A model containing a double-sided material also
        // disables whole-draw culling; the fragment shaders then discard back
        // faces only for single-sided primitives in that mixed mesh.
        const bool cullingAllowed = configuration_.modelBackfaceCulling &&
            !configuration_.wireframeEnabled;
        // recordScenePass left the command buffer on CULL_MODE_NONE and
        // COUNTER_CLOCKWISE for the tile quads drawn above. Tile quads are
        // unaffected by either: they are CPU-culled and never cull on the GPU.
        VkCullModeFlags boundCullMode = VK_CULL_MODE_NONE;
        VkFrontFace boundFrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPipeline boundModelPipeline = VK_NULL_HANDLE;
        bool mirrorGhostState = false;
        for (const OpaqueDrawBatch& batch : batches) {
            const RecorderModelDraw& draw =
                draws[orderedDraws[batch.firstItem].drawIndex];
            if (draw.mirrorGhost != mirrorGhostState) {
                vkCmdSetDepthWriteEnable(
                    commandBuffer,
                    draw.mirrorGhost && swapchain_.depthView()
                        ? VK_TRUE
                        : VK_FALSE);
                mirrorGhostState = draw.mirrorGhost;
            }
            const VkCullModeFlags desiredCullMode =
                cullingAllowed && !draw.materialPolicy.hasDoubleSided
                ? VK_CULL_MODE_BACK_BIT
                : VK_CULL_MODE_NONE;
            if (desiredCullMode != boundCullMode) {
                vkCmdSetCullMode(commandBuffer, desiredCullMode);
                boundCullMode = desiredCullMode;
            }
            const VkFrontFace desiredFrontFace =
                desiredCullMode == VK_CULL_MODE_NONE
                ? boundFrontFace
                : modelFrontFace;
            if (desiredFrontFace != boundFrontFace) {
                vkCmdSetFrontFace(commandBuffer, desiredFrontFace);
                boundFrontFace = desiredFrontFace;
            }
            if (boundModelPipeline != draw.pipeline) {
                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    draw.pipeline);
                ++stats_.pipelineBinds;
                boundModelPipeline = draw.pipeline;
            }
            if (draw.skinned) {
                drawModel(
                    commandBuffer,
                    draw.mesh,
                    1,
                    draw.mesh.firstInstance,
                    writeDrawInstance(draw.constants));
            } else {
                uint32_t firstInstance = 0;
                for (std::size_t itemIndex = batch.firstItem;
                     itemIndex < batch.firstItem + batch.itemCount;
                     ++itemIndex) {
                    const uint32_t instance = writeDrawInstance(
                        draws[orderedDraws[itemIndex].drawIndex].constants);
                    if (itemIndex == batch.firstItem) {
                        firstInstance = instance;
                    }
                }
                drawModel(
                    commandBuffer,
                    draw.mesh,
                    static_cast<uint32_t>(batch.itemCount),
                    firstInstance,
                    firstInstance);
            }
        }
        if (mirrorGhostState) {
            vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);
        }
        // Particles and any later quad work assume no culling, and the pass
        // entered on COUNTER_CLOCKWISE. Restore both so nothing downstream
        // inherits model state.
        if (boundCullMode != VK_CULL_MODE_NONE) {
            vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
        }
        if (boundFrontFace != VK_FRONT_FACE_COUNTER_CLOCKWISE) {
            vkCmdSetFrontFace(
                commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        }
        if (translucentPass && !scene.particles.empty()) {
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines_.scene());
            ++stats_.pipelineBinds;
            bool depthTestEnabled = swapchain_.depthView() != VK_NULL_HANDLE;
            for (const PreparedParticle& particle : scene.particles) {
                const bool desiredDepthTest =
                    !particle.drawOnTop &&
                    swapchain_.depthView() != VK_NULL_HANDLE;
                if (desiredDepthTest != depthTestEnabled) {
                    vkCmdSetDepthTestEnable(
                        commandBuffer,
                        desiredDepthTest ? VK_TRUE : VK_FALSE);
                    depthTestEnabled = desiredDepthTest;
                }
                drawQuadRun(
                    commandBuffer, drawParticle(commandBuffer, particle), 1);
            }
            if (!depthTestEnabled && swapchain_.depthView()) {
                vkCmdSetDepthTestEnable(commandBuffer, VK_TRUE);
            }
        }
    }

    void drawTopDownGridOverlay(
        VkCommandBuffer commandBuffer,
        const TileRenderLayout& layout,
        const RenderFrameData& frameData)
    {
        if (frameData.levelWidth == 0 ||
            frameData.levelHeight == 0 ||
            frameData.gridOverlay.color.w <= 0.0f) {
            return;
        }
        const float tileWidthPixels = std::max(
            layout.tileSize.x *
                static_cast<float>(
                    swapchain_.renderExtent().width) *
                0.5f,
            0.001f);
        const float lineWidth = std::clamp(
            frameData.gridOverlay.width / tileWidthPixels,
            0.0f,
            0.5f);
        if (lineWidth <= 0.0f) {
            return;
        }

        const RenderFrameData::Lighting unlit {};
        const float levelWidth =
            static_cast<float>(frameData.levelWidth);
        const float levelHeight =
            static_cast<float>(frameData.levelHeight);
        const float halfLineWidth = lineWidth * 0.5f;
        auto drawRect = [&](float left, float bottom,
                            float right, float top) {
            if (right <= left || top <= bottom) {
                return;
            }
            const Vec2 min {
                layout.boardBottomLeft.x +
                    left * layout.tileSize.x,
                layout.boardBottomLeft.y +
                    bottom * layout.tileSize.y,
            };
            const Vec2 max {
                layout.boardBottomLeft.x +
                    right * layout.tileSize.x,
                layout.boardBottomLeft.y +
                    top * layout.tileSize.y,
            };
            drawQuadRun(
                commandBuffer,
                drawFace(
                commandBuffer,
                {
                    Vec3 { min.x, min.y, 0.0f },
                    Vec3 { max.x, min.y, 0.0f },
                    Vec3 { max.x, max.y, 0.0f },
                    Vec3 { min.x, max.y, 0.0f },
                },
                frameData.gridOverlay.color,
                {},
                unlit,
                false,
                {},
                {},
                0.0f,
                false,
                true),
                1);
        };
        for (uint32_t x = 0; x <= frameData.levelWidth; ++x) {
            const float center = static_cast<float>(x);
            drawRect(
                std::clamp(
                    center - halfLineWidth, 0.0f, levelWidth),
                0.0f,
                std::clamp(
                    center + halfLineWidth, 0.0f, levelWidth),
                levelHeight);
        }
        for (uint32_t y = 0; y <= frameData.levelHeight; ++y) {
            const float center = static_cast<float>(y);
            drawRect(
                0.0f,
                std::clamp(
                    center - halfLineWidth, 0.0f, levelHeight),
                levelWidth,
                std::clamp(
                    center + halfLineWidth, 0.0f, levelHeight));
        }
    }

    // Every quad draw opens the same way: two triangles from the fixed
    // six-index buffer, counted once. Named because five call sites had it
    // written out, and a draw that forgets the topology inherits whichever one
    // the previous draw happened to set.
    void beginQuadDraw(VkCommandBuffer commandBuffer)
    {
        vkCmdSetPrimitiveTopology(
            commandBuffer,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        ++stats_.visibleFaces;
        stats_.vertices += 6;
        stats_.triangles += 2;
    }

    [[nodiscard]] uint32_t drawMirrorEnergyFace(
        VkCommandBuffer commandBuffer,
        const std::array<Vec3, 4>& vertices,
        Vec4 color,
        Vec3 normal,
        float animationTimeSeconds)
    {
        beginQuadDraw(commandBuffer);

        const GpuDrawInstance constants {
            .vertices = quadVertices(vertices, worldSpaceQuad),
            .color = color,
            .normalAndAmbientRed = {
                normal.x, normal.y, normal.z, 0.0f },
            .shadowOptions = {
                config::mirrorEnergyScanlineFrequency,
                config::mirrorEnergyScanlineSpeed,
                config::mirrorEnergyScanlineStrength,
                config::mirrorGhostTextureInfluence,
            },
            .materialOptions = {
                0.0f, 1.0f, 1.0f, animationTimeSeconds },
            .gridColor = {
                config::mirrorGhostRimPower,
                config::mirrorGhostRimStrength,
                config::mirrorEnergyPulseSpeed,
                config::mirrorEnergyPulseStrength,
            },
        };
        return writeDrawInstance(constants);
    }

    [[nodiscard]] uint32_t drawParticle(
        VkCommandBuffer commandBuffer,
        const PreparedParticle& particle)
    {
        beginQuadDraw(commandBuffer);

        const GpuDrawInstance constants {
            .vertices = quadVertices(particle.vertices, worldSpaceQuad),
            .color = particle.color,
            .materialOptions = { 0.0f, 1.0f, 1.0f, 0.0f },
            .textureOptions = {
                shaderValue(DrawMaterialMode::ProceduralTexture),
                static_cast<float>(particle.texture.value),
                0.0f,
                1.0f,
            },
        };
        return writeDrawInstance(constants);
    }

    // `clipSpace` is for the callers whose quads are already in clip space -
    // the UI and the top-down 2D board. They have no world position and must
    // not be projected; see worldSpaceQuad in VulkanRenderConstants.hpp.
    [[nodiscard]] uint32_t drawFace(
        VkCommandBuffer commandBuffer,
        const std::array<Vec3, 4>& vertices,
        Vec4 color,
        Vec3 normal,
        const RenderFrameData::Lighting& lighting,
        bool blurBehind = false,
        Vec4 gridColor = {},
        Vec2 gridSize = {},
        float gridLineWidth = 0.0f,
        bool isEditorPreview = false,
        bool clipSpace = false)
    {
        beginQuadDraw(commandBuffer);

        const SunAmbientLanes lanes = sunAmbientLanes(lighting);
        const GpuDrawInstance constants {
            .vertices = quadVertices(
                vertices, clipSpace ? clipSpaceQuad : worldSpaceQuad),
            .color = color,
            .normalAndAmbientRed = {
                normal.x,
                normal.y,
                normal.z,
                lanes.ambientRed,
            },
            .sunDirectionAndAmbientGreen = lanes.sunDirectionAndAmbientGreen,
            .sunRadianceAndAmbientBlue = lanes.sunRadianceAndAmbientBlue,
            .shadowOptions = faceShadowOptions(
                lighting, gridColor, gridLineWidth, gridSize),
            .materialOptions = {
                blurBehind ? 1.0f : 0.0f,
                gridSize.x,
                gridSize.y,
                isEditorPreview
                    ? -config::iceBlurRadiusPixels
                    : config::iceBlurRadiusPixels,
            },
            .gridColor = gridColor,
            .textureOptions = {
                0.0f,
                0.0f,
                std::max(lighting.specularStrength, 0.0f),
                // w was the Blinn-Phong exponent. Roughness replaced it in
                // F3c and nothing reads this lane on a tile draw now.
                0.0f,
            },
        };
        return writeDrawInstance(constants);
    }

    // Ground tops blended from two textures via a splat map. Lighting,
    // shadowing, grid, and dithering come from the same helpers drawFace uses,
    // so they now agree by construction rather than by being kept in step. Only
    // the free push-constant slots differ: materialOptions.x carries the face's
    // world origin X (opaque ground never blurs) and textureOptions carries the
    // three one-based texture handles plus world origin Y.
    [[nodiscard]] uint32_t drawGroundSplatFace(
        VkCommandBuffer commandBuffer,
        const std::array<Vec3, 4>& vertices,
        Vec4 color,
        Vec3 normal,
        const RenderFrameData::Lighting& lighting,
        Vec2 worldOrigin,
        Vec2 gridSize,
        Vec4 gridColor,
        float gridLineWidth,
        bool isEditorPreview,
        const GroundSplatTextures& textures)
    {
        beginQuadDraw(commandBuffer);

        const SunAmbientLanes lanes = sunAmbientLanes(lighting);
        const GpuDrawInstance constants {
            .vertices = quadVertices(vertices, worldSpaceQuad),
            .color = color,
            .normalAndAmbientRed = {
                normal.x,
                normal.y,
                normal.z,
                lanes.ambientRed,
            },
            .sunDirectionAndAmbientGreen = lanes.sunDirectionAndAmbientGreen,
            .sunRadianceAndAmbientBlue = lanes.sunRadianceAndAmbientBlue,
            .shadowOptions = faceShadowOptions(
                lighting, gridColor, gridLineWidth, gridSize),
            .materialOptions = {
                worldOrigin.x,
                gridSize.x,
                gridSize.y,
                isEditorPreview
                    ? -config::iceBlurRadiusPixels
                    : config::iceBlurRadiusPixels,
            },
            .gridColor = gridColor,
            .textureOptions = {
                static_cast<float>(textures.base.value),
                static_cast<float>(textures.detail.value),
                static_cast<float>(textures.splatMap.value),
                worldOrigin.y,
            },
        };
        return writeDrawInstance(constants);
    }

    [[nodiscard]] uint32_t drawWaterFace(
        VkCommandBuffer commandBuffer,
        const std::array<Vec3, 4>& vertices,
        Vec4 color,
        Vec2 worldOrigin,
        Vec2 size,
        Vec2 boardOrigin,
        Vec2 boardSize,
        float animationTimeSeconds,
        uint32_t shorelineMask,
        const RenderFrameData::WaterRendering& rendering,
        const IsoRenderLayout& layout,
        bool isEditorPreview)
    {
        beginQuadDraw(commandBuffer);

        const float waterRenderingMode =
            rendering.visualizeCausticsOnly ? 2.0f : 1.0f;

        const GpuDrawInstance constants {
            .vertices = quadVertices(vertices, worldSpaceQuad),
            // Water is the pass that claims passData. These are its border
            // and ripple parameters, not geometry.
            .passData = {
                Vec4 {
                    config::waterTileBorderColor.x,
                    config::waterTileBorderColor.y,
                    config::waterTileBorderColor.z,
                    config::waterGridLineOpacity,
                },
                Vec4 {
                    config::waterTileBorderWidth,
                    config::waterTileBorderWarpAmplitude,
                    config::waterTileBorderWarpFrequency,
                    config::waterTileBorderSpeed,
                },
                Vec4 {
                    boardOrigin.x,
                    boardOrigin.y,
                    boardSize.x,
                    boardSize.y,
                },
                Vec4 {
                    rendering.primaryRippleOpacity,
                    rendering.primaryShorelineOpacity,
                    rendering.secondaryShorelineOpacity,
                    rendering.underwaterCausticStrength,
                },
            },
            .color = color,
            .normalAndAmbientRed = {
                layout.nearestDepth,
                layout.farthestDepth,
                rendering.rippleCrestHalfWidth,
                rendering.rippleHaloWidth,
            },
            .sunDirectionAndAmbientGreen = {
                config::waterToneSpatialFrequency,
                config::waterDarkToneMultiplier,
                config::waterLightToneMultiplier,
                config::waterToneTransitionWidth,
            },
            .sunRadianceAndAmbientBlue = {
                config::waterToneSpeed,
                rendering.rippleHaloStrength,
                rendering.rippleCrestStrength,
                rendering.secondaryRippleThicknessScale,
            },
            .shadowOptions = {
                config::waterSecondaryRippleColor.x,
                config::waterSecondaryRippleColor.y,
                config::waterSecondaryRippleColor.z,
                rendering.secondaryRippleOpacity,
            },
            .materialOptions = {
                layout.cameraPosition.x,
                size.x,
                size.y,
                isEditorPreview
                    ? -waterRenderingMode
                    : waterRenderingMode,
            },
            .gridColor = {
                worldOrigin.x,
                worldOrigin.y,
                animationTimeSeconds,
                layout.cameraPosition.y,
            },
            .textureOptions = {
                rendering.rippleSpatialFrequency,
                rendering.rippleSpeed,
                rendering.refractionStrength,
                static_cast<float>(shorelineMask),
            },
        };
        return writeDrawInstance(constants);
    }

    void drawShadowFaces(
        VkCommandBuffer commandBuffer,
        const ShadowRenderLayout& layout,
        const std::vector<std::array<Vec3, 4>>& faces)
    {
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
        for (const std::array<Vec3, 4>& worldVertices : faces) {
            std::array<Vec4, 4> shadowVertices {};
            for (std::size_t i = 0; i < worldVertices.size(); ++i) {
                shadowVertices[i] = IsoScenePreparer::projectShadowPoint(
                    layout, worldVertices[i]);
            }
            const uint32_t instance = writeDrawInstance({
                .vertices = shadowVertices,
            });
            if (instanceCount == 0) {
                firstInstance = instance;
            }
            ++instanceCount;
        }
        if (instanceCount == 0) {
            return;
        }
        const GpuDrawInstance batchConstants {};
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(GpuDrawInstance),
            &batchConstants);
        bindDescriptorSet(commandBuffer);
        vkCmdDraw(commandBuffer, 6, instanceCount, 0, firstInstance);
    }

    void drawPointShadowFace(
        VkCommandBuffer commandBuffer,
        const RenderFrameData::PointLight& light,
        uint32_t cubeFace,
        const std::array<Vec3, 4>& worldVertices)
    {
        std::array<Vec4, 4> shadowVertices {};
        for (std::size_t i = 0; i < worldVertices.size(); ++i) {
            shadowVertices[i] = projectPointShadow(
                light, cubeFace, worldVertices[i]);
        }
        // Point lights can multiply the same caster set by six faces and up
        // to the full light capacity. Keep their proven push-constant path so
        // batching the directional map cannot exhaust the frame instance
        // buffer in a point-light stress scene.
        GpuDrawInstance constants {
            .vertices = shadowVertices,
        };
        constants.passData[0].x = 1.0f;
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(GpuDrawInstance),
            &constants);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

    // Neither camera is a parameter any more. A model's push constants are
    // now entirely about the model: where it is in the world and what it is
    // made of. Where it lands on screen, and where the sun sees it, are the
    // frame's business and live in the uniform buffer.
    [[nodiscard]] GpuDrawInstance modelPushConstants(
        const RenderFrameData::Tile& tile,
        const RenderFrameData::Lighting& lighting,
        float effectAnimationTimeSeconds,
        MaterialAlphaSelection alphaSelection)
    {
        const VulkanModelResources::MaterialBinding material =
            models_.materialForModel(tile.model);
        const SunAmbientLanes lanes = sunAmbientLanes(lighting);
        const bool mirrorEnergy =
            tile.effect == RenderSurfaceEffect::MirrorEnergy;
        const float editorHighlightState =
            tile.editorDecorationHighlight ==
                    RenderFrameData::EditorDecorationHighlight::Selected
                ? 2.0f
                : (tile.editorDecorationHighlight ==
                          RenderFrameData::EditorDecorationHighlight::Hovered
                      ? 1.0f
                      : 0.0f);
        // The authored rotation and scale used to be restated here - the
        // rotation as an Euler triple in normalAndAmbientRed.xyz and the
        // reciprocal of the scale in gridColor.xyz - so that the vertex
        // shaders could rebuild a rotation matrix per vertex. Both are
        // columns of the worldFromModel this same draw already carries in
        // `vertices`, so the shaders read them from there and these two
        // slots are free for a model draw.
        const GpuDrawInstance constants {
            .vertices = IsoScenePreparer::modelWorldTransform(tile),
            // x is the base of the model's material range. y asks the fragment
            // shader to restore single-sided rejection per primitive when a
            // mixed double-sided model has disabled fixed-function culling.
            .passData = {
                Vec4 {
                    static_cast<float>(material.materialBase),
                    material.policy.hasDoubleSided &&
                            configuration_.modelBackfaceCulling &&
                            !configuration_.wireframeEnabled
                        ? 1.0f
                        : 0.0f,
                    0.0f,
                    0.0f,
                },
                Vec4 {},
                Vec4 {},
                Vec4 {},
            },
            .color = tile.color,
            // xyz free; see above. w is the ambient term's red channel.
            .normalAndAmbientRed = { 0.0f, 0.0f, 0.0f, lanes.ambientRed },
            .sunDirectionAndAmbientGreen = lanes.sunDirectionAndAmbientGreen,
            .sunRadianceAndAmbientBlue = lanes.sunRadianceAndAmbientBlue,
            .shadowOptions = {
                mirrorEnergy
                    ? config::mirrorEnergyScanlineFrequency
                    : (lighting.shadows.enabled ? 1.0f : 0.0f),
                mirrorEnergy
                    ? config::mirrorEnergyScanlineSpeed
                    : std::clamp(
                          lighting.shadows.opacity *
                              std::clamp(
                                  lighting.modelShadowReceive,
                                  0.0f,
                                  1.0f),
                          0.0f,
                          1.0f),
                mirrorEnergy
                    ? config::mirrorEnergyScanlineStrength
                    : std::max(lighting.shadows.bias, 0.0f),
                mirrorEnergy
                    ? config::mirrorGhostTextureInfluence
                    : 0.0f,
            },
            .materialOptions = {
                tile.blurBehind ? 1.0f : 0.0f,
                tile.beltScrollOffset,
                static_cast<float>(material.textureIndex),
                mirrorEnergy
                    ? effectAnimationTimeSeconds
                    : (editorHighlightState > 0.0f
                            ? effectAnimationTimeSeconds
                            : (tile.isEditorPreview
                                      ? -config::iceBlurRadiusPixels
                                      : config::iceBlurRadiusPixels)),
            },
            .gridColor = mirrorEnergy
                ? Vec4 {
                      config::mirrorGhostRimPower,
                      config::mirrorGhostRimStrength,
                      config::mirrorEnergyPulseSpeed,
                      config::mirrorEnergyPulseStrength,
                  }
                // xyz free; see above. The marker in w is what the fragment
                // stage reads to decide the editor highlight and to keep the
                // grid overlay off; see modelDrawMarkerAlpha.
                //
                // Mirror energy takes the other branch and so carries no
                // marker. It is safe because it is a different pipeline with a
                // different fragment shader, not because anything here checks.
                : Vec4 { 0.0f, 0.0f, 0.0f, modelDrawMarkerAlpha },
            .textureOptions = {
                shaderValue(material.mode),
                editorHighlightState,
                std::max(lighting.specularStrength, 0.0f),
                // Mixed meshes can share the existing opaque and blended
                // pipelines while each pass keeps its authored modes.
                static_cast<float>(alphaSelection),
            },
        };
        return constants;
    }

    // `firstInstance` is what gl_InstanceIndex starts at; for a static model
    // that is its draw-instance entry, and for a skinned one it is its
    // skinning-palette entry. `drawInstance` is the material and transform
    // entry either way, pushed for the skinned pipeline that cannot reach it
    // through gl_InstanceIndex.
    void drawModel(
        VkCommandBuffer commandBuffer,
        const VulkanModelResources::MeshView& mesh,
        uint32_t instanceCount,
        uint32_t firstInstance,
        uint32_t drawInstance)
    {
        const VkBuffer vertexBuffer = mesh.vertexBuffer;
        const VkDeviceSize offset = mesh.vertexOffset;
        vkCmdBindVertexBuffers(
            commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(
            commandBuffer,
            mesh.indexBuffer,
            mesh.indexOffset,
            VK_INDEX_TYPE_UINT32);
        pushDrawInstanceIndex(commandBuffer, drawInstance);
        vkCmdDrawIndexed(
            commandBuffer, mesh.indexCount, instanceCount, 0, 0, firstInstance);
        stats_.visibleFaces += mesh.indexCount / 3 * instanceCount;
        ++stats_.drawCalls;
        stats_.vertices += mesh.indexCount * instanceCount;
        stats_.triangles += mesh.indexCount / 3 * instanceCount;
    }

    void drawModelShadow(
        VkCommandBuffer commandBuffer,
        const ShadowRenderLayout& layout,
        const RenderFrameData::Tile& tile)
    {
        const VulkanModelResources::MeshView mesh =
            models_.meshForTile(tile, configuration_.descriptorFrameIndex);
        const ModelTransformPoints transform =
            IsoScenePreparer::modelTransformPoints(tile);
        const GpuDrawInstance constants {
            .vertices = affineTransformColumns(
                IsoScenePreparer::projectShadowPoint(
                    layout, transform.origin),
                IsoScenePreparer::projectShadowPoint(
                    layout, transform.xPoint),
                IsoScenePreparer::projectShadowPoint(
                    layout, transform.yPoint),
                IsoScenePreparer::projectShadowPoint(
                    layout, transform.zPoint)),
        };
        const VkBuffer vertexBuffer = mesh.vertexBuffer;
        const VkDeviceSize offset = mesh.vertexOffset;
        vkCmdBindVertexBuffers(
            commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(
            commandBuffer,
            mesh.indexBuffer,
            mesh.indexOffset,
            VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(GpuDrawInstance),
            &constants);
        vkCmdDrawIndexed(
            commandBuffer, mesh.indexCount, 1, 0, 0, mesh.firstInstance);
    }

    void drawPointModelShadow(
        VkCommandBuffer commandBuffer,
        const RenderFrameData::PointLight& light,
        uint32_t cubeFace,
        const RenderFrameData::Tile& tile)
    {
        const VulkanModelResources::MeshView mesh =
            models_.meshForTile(tile, configuration_.descriptorFrameIndex);
        const ModelTransformPoints transform =
            IsoScenePreparer::modelTransformPoints(tile);
        const GpuDrawInstance constants {
            .vertices = affineTransformColumns(
                projectPointShadow(light, cubeFace, transform.origin),
                projectPointShadow(light, cubeFace, transform.xPoint),
                projectPointShadow(light, cubeFace, transform.yPoint),
                projectPointShadow(light, cubeFace, transform.zPoint)),
        };
        const VkBuffer vertexBuffer = mesh.vertexBuffer;
        const VkDeviceSize offset = mesh.vertexOffset;
        vkCmdBindVertexBuffers(
            commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(
            commandBuffer, mesh.indexBuffer, mesh.indexOffset, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(GpuDrawInstance),
            &constants);
        vkCmdDrawIndexed(
            commandBuffer, mesh.indexCount, 1, 0, 0, mesh.firstInstance);
    }

    [[nodiscard]] uint32_t drawUiRect(
        VkCommandBuffer commandBuffer,
        const UiDrawCommand& command,
        Vec2 viewportSize,
        const RenderFrameData::Lighting& lighting)
    {
        const float left =
            -1.0f + 2.0f * command.rect.position.x /
                viewportSize.x;
        const float right =
            -1.0f + 2.0f *
                (command.rect.position.x +
                 command.rect.size.x) /
                viewportSize.x;
        const float top =
            1.0f - 2.0f * command.rect.position.y /
                viewportSize.y;
        const float bottom =
            1.0f - 2.0f *
                (command.rect.position.y +
                 command.rect.size.y) /
                viewportSize.y;
        const std::array vertices {
            Vec3 { left, top, 0.0f },
            Vec3 { right, top, 0.0f },
            Vec3 { right, bottom, 0.0f },
            Vec3 { left, bottom, 0.0f },
        };
        if (command.kind == UiDrawKind::Solid) {
            return drawFace(
                commandBuffer,
                vertices,
                command.color,
                {},
                lighting,
                false,
                {},
                {},
                0.0f,
                false,
                true);
        }

        ++stats_.visibleFaces;
        stats_.vertices += 6;
        stats_.triangles += 2;
        const float materialMode =
            shaderValue(uiDrawMaterialMode(command.kind));
        const GpuDrawInstance constants {
            .vertices = {
                Vec4 { left, top, 0.0f, clipSpaceQuad },
                Vec4 { right, top, 0.0f, clipSpaceQuad },
                Vec4 { right, bottom, 0.0f, clipSpaceQuad },
                Vec4 { left, bottom, 0.0f, clipSpaceQuad },
            },
            .color = command.color,
            .shadowOptions = command.effectOptions,
            .materialOptions = {
                0.0f,
                command.uvRect.size.x,
                command.uvRect.size.y,
                0.0f,
            },
            .gridColor = {
                command.uvRect.position.x,
                command.uvRect.position.y,
                0.0f,
                0.0f,
            },
            .textureOptions = {
                materialMode, static_cast<float>(command.texture.value), 0.0f, 1.0f },
        };
        return writeDrawInstance(constants);
    }

    VkDevice device_ = VK_NULL_HANDLE;
    VulkanGpuProfiler& gpuProfiler_;
    VulkanSwapchainResources& swapchain_;
    VulkanShadowPass& shadowPass_;
    bool previewDescriptor_ = false;
    VulkanSsaoPass& ssaoPass_;
    VulkanSceneDescriptors& descriptors_;
    VulkanPipelineFactory& pipelines_;
    VulkanModelResources& models_;
    const VulkanSceneRecorder::FrameConfiguration& configuration_;
    PointShadowFaceCache& pointShadowFaceCache_;
    std::array<std::vector<PointShadowModelState>,
        RenderFrameData::pointLightCapacity>& pointShadowModelStateScratch_;
    bool pointShadowCacheEnabled_ = true;
    VulkanSceneRecorder::Scratch& scratch_;
    bool scratchReuseEnabled_ = true;
    FrameTimeTelemetry& setupTimeTelemetry_;
    FrameTimeTelemetry& gameTimeTelemetry_;
    FrameTimeTelemetry& shadowTimeTelemetry_;
    FrameTimeTelemetry& sceneTimeTelemetry_;
    FrameTimeTelemetry& ssaoTimeTelemetry_;
    FrameTimeTelemetry& previewTimeTelemetry_;
    FrameTimeTelemetry& outputTimeTelemetry_;
    RenderStats stats_ {};
};

RenderStats VulkanSceneRecorder::record(
    Resources resources,
    const FrameConfiguration& configuration,
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    const RenderFrameData& frameData,
    const PreparedRenderScene& scene,
    const RenderFrameData* previewFrameData,
    const PreparedRenderScene* previewScene,
    const UiDrawData& uiDrawData) const
{
    return SceneRecordingSession(
        resources,
        configuration,
        pointShadowFaceCache_,
        pointShadowModelStateScratch_,
        pointShadowCacheEnabled_,
        *scratch_,
        scratchReuseEnabled_,
        setupTimeTelemetry_,
        gameTimeTelemetry_,
        shadowTimeTelemetry_,
        sceneTimeTelemetry_,
        ssaoTimeTelemetry_,
        previewTimeTelemetry_,
        outputTimeTelemetry_).record(
        commandBuffer,
        imageIndex,
        frameData,
        scene,
        previewFrameData,
        previewScene,
        uiDrawData);
}

void VulkanSceneRecorder::populateTimingStats(RenderStats& stats) const
{
    stats.recorderSetupTiming = renderPhaseTiming(
        setupTimeTelemetry_.summary());
    stats.gameCommandRecordingTiming = renderPhaseTiming(
        gameTimeTelemetry_.summary());
    stats.shadowCommandRecordingTiming = renderPhaseTiming(
        shadowTimeTelemetry_.summary());
    stats.sceneCommandRecordingTiming = renderPhaseTiming(
        sceneTimeTelemetry_.summary());
    stats.ssaoCommandRecordingTiming = renderPhaseTiming(
        ssaoTimeTelemetry_.summary());
    stats.previewCommandRecordingTiming = renderPhaseTiming(
        previewTimeTelemetry_.summary());
    stats.outputCommandRecordingTiming = renderPhaseTiming(
        outputTimeTelemetry_.summary());
}

} // namespace sokoban
