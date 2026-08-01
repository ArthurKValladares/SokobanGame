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
        const AssetManifest& manifest,
        std::span<const AnimationCatalog::TimelineEvent> markers = {});
    bool previewCatalogAnimation(
        RenderModel model,
        RenderAnimation animation,
        const AssetManifest& manifest,
        VulkanRenderer& renderer);
    [[nodiscard]] float normalizedTime() const;
    [[nodiscard]] float durationSeconds() const;
    [[nodiscard]] std::optional<RenderFrameData> previewFrame(
        const AssetManifest& manifest,
        const PresentationSettings& settings) const;

private:
    void rescan(VulkanRenderer& renderer);

    std::filesystem::path assetRoot_;
    std::vector<std::filesystem::path> files_;
    std::vector<std::string> fileLabels_;
    int fileIndex_ = -1;
    std::vector<std::string> clipNames_;
    int clipIndex_ = -1;
    std::optional<GltfAnimationClip> clip_;
    std::string error_;
    float time_ = 0.0f;
    float speed_ = 1.0f;
    RenderModel model_ = cubeModel;
    bool playing_ = true;
    bool loop_ = true;
    bool active_ = false;
    bool scanned_ = false;
};

} // namespace sokoban
