#pragma once

#include "engine/Config.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstddef>
#include <optional>

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

    void clear();
    void setClip(RenderAnimation animation, GltfAnimationClip clip);
    [[nodiscard]] bool hasClip(RenderAnimation animation) const;
    [[nodiscard]] const GltfAnimationClip& clip(RenderAnimation animation) const;

    void setPreview(const GltfAnimationClip* clip, float timeSeconds);
    [[nodiscard]] std::optional<SkinningRequest> update(const RenderFrameData& frameData);

private:
    [[nodiscard]] static std::size_t index(RenderAnimation animation);
    void resetPlayback();

    std::array<GltfAnimationClip, static_cast<std::size_t>(RenderAnimation::Count)> clips_ {};
    float fadeDurationSeconds_ = config::playerAnimationFadeSeconds;
    RenderAnimation activeAnimation_ = RenderAnimation::None;
    float activeAnimationTime_ = -1.0f;
    RenderAnimation fadeFromAnimation_ = RenderAnimation::None;
    float fadeFromTime_ = 0.0f;
    float fadeElapsed_ = 0.0f;
    const GltfAnimationClip* previewClip_ = nullptr;
    float previewTimeSeconds_ = 0.0f;
    const GltfAnimationClip* activePreviewClip_ = nullptr;
    float activePreviewTime_ = -1.0f;
};

} // namespace sokoban
