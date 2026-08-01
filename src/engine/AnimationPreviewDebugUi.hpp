#pragma once

#include "engine/AnimationCatalog.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/VulkanRenderer.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sokoban {

class AssetManifest;
class PresentationSettings;

// Debug-only animation browser state and ImGui adapter. Application only
// schedules update/draw calls; the browser owns scanning, selection, playback,
// and renderer preview delegation.
class AnimationPreviewDebugUi {
public:
    void initialize(std::filesystem::path assetRoot);
    void update(float dt, VulkanRenderer& renderer);
    void draw(
        VulkanRenderer& renderer,
        const AssetManifest& manifest);
    void drawCatalogPreview(
        std::span<const AnimationCatalog::TimelineEvent> markers);
    bool previewCatalogAnimation(
        RenderModel model,
        RenderAnimation animation,
        const AssetManifest& manifest,
        VulkanRenderer& renderer);
    void clearCatalogPreview(VulkanRenderer& renderer);
    void setCatalogNormalizedTime(float normalizedTime);
    [[nodiscard]] float catalogNormalizedTime() const;
    [[nodiscard]] float catalogDurationSeconds() const;
    [[nodiscard]] std::optional<RenderFrameData> previewFrame(
        const AssetManifest& manifest,
        const PresentationSettings& settings) const;

private:
    struct PreviewSession {
        std::optional<GltfAnimationClip> clip;
        float time = 0.0f;
        float speed = 1.0f;
        RenderModel model = cubeModel;
        bool playing = true;
        bool loop = true;
        bool active = false;
    };

    void rescan(VulkanRenderer& renderer);
    static void advanceSession(PreviewSession& session, float dt);

    std::filesystem::path assetRoot_;
    std::vector<std::filesystem::path> files_;
    std::vector<std::string> fileLabels_;
    int fileIndex_ = -1;
    std::vector<std::string> clipNames_;
    int clipIndex_ = -1;
    std::string error_;
    std::string catalogError_;
    PreviewSession browser_;
    PreviewSession catalog_;
    bool scanned_ = false;
};

} // namespace sokoban
