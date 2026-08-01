#pragma once

#include "engine/render/AnimationConfig.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/RenderTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sokoban {

// Vulkan-free animation selection and crossfade state. The controller owns
// gameplay clips and emits sampling requests for a separate mesh updater.
class AnimationController {
public:
    struct SkinningRequest {
        const GltfAnimationClip* fromClip = nullptr;
        float fromTimeSeconds = 0.0f;
        const GltfAnimationClip* toClip = nullptr;
        float toTimeSeconds = 0.0f;
        float blend = 1.0f;

        [[nodiscard]] bool blended() const { return fromClip != nullptr; }
    };

    struct InstanceSkinningRequest {
        uint64_t instanceId = 0;
        RenderModel model = cubeModel;
        SkinningRequest skinning;
    };

    explicit AnimationController(float fadeDurationSeconds = config::playerAnimationFadeSeconds);

    // Identifies the model whose tiles drive clip selection and the clip used
    // when an invalid animation id is dereferenced. Ids come from the asset
    // manifest at runtime.
    void configure(RenderModel playerModel, RenderAnimation fallbackClip);

    void clear();
    void setClip(RenderAnimation animation, GltfAnimationClip clip);
    [[nodiscard]] bool hasClip(RenderAnimation animation) const;
    [[nodiscard]] const GltfAnimationClip& clip(RenderAnimation animation) const;

    void setPreview(
        RenderModel model,
        const GltfAnimationClip* clip,
        float timeSeconds);
    [[nodiscard]] std::optional<SkinningRequest> update(const RenderFrameData& frameData);
    [[nodiscard]] std::vector<InstanceSkinningRequest> updateInstances(
        const RenderFrameData& frameData);

private:
    struct PlaybackState {
        RenderAnimation activeAnimation = noAnimation;
        float activeAnimationTime = -1.0f;
        RenderAnimation fadeFromAnimation = noAnimation;
        float fadeFromTime = 0.0f;
        float fadeElapsed = 0.0f;
    };

    [[nodiscard]] std::optional<SkinningRequest> updateTile(
        const RenderFrameData::Tile& tile,
        PlaybackState& playback,
        bool forceSample = false);
    void resetPlayback();

    // Indexed by RenderAnimation::value - 1; grown on demand.
    std::vector<GltfAnimationClip> clips_;
    RenderModel playerModel_ {};
    RenderAnimation fallbackClip_ {};
    float fadeDurationSeconds_ = config::playerAnimationFadeSeconds;
    PlaybackState legacyPlayback_;
    std::unordered_map<uint64_t, PlaybackState> instancePlayback_;
    const GltfAnimationClip* previewClip_ = nullptr;
    RenderModel previewModel_ {};
    float previewTimeSeconds_ = 0.0f;
    const GltfAnimationClip* activePreviewClip_ = nullptr;
    float activePreviewTime_ = -1.0f;
};

} // namespace sokoban
