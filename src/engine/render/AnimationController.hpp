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
        AnimationPlaybackMode fromPlaybackMode = AnimationPlaybackMode::Loop;
        const GltfAnimationClip* toClip = nullptr;
        float toTimeSeconds = 0.0f;
        AnimationPlaybackMode toPlaybackMode = AnimationPlaybackMode::Loop;
        float blend = 1.0f;

        [[nodiscard]] bool blended() const { return fromClip != nullptr; }
    };

    struct InstanceSkinningRequest {
        uint64_t instanceId = 0;
        RenderModel model = cubeModel;
        SkinningRequest skinning;
    };

    explicit AnimationController(float fadeDurationSeconds = config::playerAnimationFadeSeconds);

    // Sets the fallback clip for tiles without an animation and for clip()
    // lookups whose requested clip is unavailable. Ids come from the manifest.
    void configure(RenderAnimation fallbackClip);

    void clear();
    void setClip(RenderAnimation animation, GltfAnimationClip clip);
    [[nodiscard]] bool hasClip(RenderAnimation animation) const;
    [[nodiscard]] const GltfAnimationClip& clip(RenderAnimation animation) const;

    void setPreview(
        RenderModel model,
        const GltfAnimationClip* clip,
        float timeSeconds);
    // Available instance poses emit on every call, including paused animations
    // and previews, to populate each frame's separate GPU buffer.
    // Tiles with animationInstanceId == 0 do not produce requests.
    [[nodiscard]] std::vector<InstanceSkinningRequest> updateInstances(
        const RenderFrameData& frameData);
    [[nodiscard]] FrameArray<InstanceSkinningRequest> updateInstances(
        const RenderFrameData& frameData, FrameArena& arena);

private:
    struct PlaybackState {
        RenderAnimation activeAnimation = noAnimation;
        float activeAnimationTime = -1.0f;
        AnimationPlaybackMode activePlaybackMode = AnimationPlaybackMode::Loop;
        RenderAnimation fadeFromAnimation = noAnimation;
        float fadeFromTime = 0.0f;
        AnimationPlaybackMode fadeFromPlaybackMode = AnimationPlaybackMode::Loop;
        float fadeElapsed = 0.0f;
        float lastTransitionTimeSeconds = -1.0f;
    };

    [[nodiscard]] std::optional<SkinningRequest> updateTile(
        const RenderFrameData::Tile& tile,
        PlaybackState& playback,
        float transitionTimeSeconds);
    void updateInstancesInto(
        const RenderFrameData& frameData,
        FrameArray<InstanceSkinningRequest>& requests);

    // Indexed by RenderAnimation::value - 1; grown on demand.
    std::vector<GltfAnimationClip> clips_;
    RenderAnimation fallbackClip_ {};
    float fadeDurationSeconds_ = config::playerAnimationFadeSeconds;
    std::unordered_map<uint64_t, PlaybackState> instancePlayback_;
    const GltfAnimationClip* previewClip_ = nullptr;
    RenderModel previewModel_ {};
    float previewTimeSeconds_ = 0.0f;
};

} // namespace sokoban
