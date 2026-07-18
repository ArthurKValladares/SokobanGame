#pragma once

#include "engine/Config.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/RenderTypes.hpp"

#include <cstddef>
#include <optional>
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

    explicit AnimationController(float fadeDurationSeconds = config::playerAnimationFadeSeconds);

    // Identifies the model whose tiles drive clip selection and the clip used
    // when an invalid animation id is dereferenced. Ids come from the asset
    // manifest at runtime.
    void configure(RenderModel playerModel, RenderAnimation fallbackClip);

    void clear();
    void setClip(RenderAnimation animation, GltfAnimationClip clip);
    [[nodiscard]] bool hasClip(RenderAnimation animation) const;
    [[nodiscard]] const GltfAnimationClip& clip(RenderAnimation animation) const;

    void setPreview(const GltfAnimationClip* clip, float timeSeconds);
    [[nodiscard]] std::optional<SkinningRequest> update(const RenderFrameData& frameData);

private:
    void resetPlayback();

    // Indexed by RenderAnimation::value - 1; grown on demand.
    std::vector<GltfAnimationClip> clips_;
    RenderModel playerModel_ {};
    RenderAnimation fallbackClip_ {};
    float fadeDurationSeconds_ = config::playerAnimationFadeSeconds;
    RenderAnimation activeAnimation_ = noAnimation;
    float activeAnimationTime_ = -1.0f;
    RenderAnimation fadeFromAnimation_ = noAnimation;
    float fadeFromTime_ = 0.0f;
    float fadeElapsed_ = 0.0f;
    const GltfAnimationClip* previewClip_ = nullptr;
    float previewTimeSeconds_ = 0.0f;
    const GltfAnimationClip* activePreviewClip_ = nullptr;
    float activePreviewTime_ = -1.0f;
};

} // namespace sokoban
