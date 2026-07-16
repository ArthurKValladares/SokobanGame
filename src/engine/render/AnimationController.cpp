#include "engine/render/AnimationController.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sokoban {

AnimationController::AnimationController(float fadeDurationSeconds)
    : fadeDurationSeconds_(std::max(fadeDurationSeconds, 0.0f))
{
}

void AnimationController::clear()
{
    clips_ = {};
    previewClip_ = nullptr;
    previewTimeSeconds_ = 0.0f;
    resetPlayback();
}

void AnimationController::setClip(RenderAnimation animation, GltfAnimationClip clip)
{
    if (animation == RenderAnimation::None || animation == RenderAnimation::Count) {
        throw std::invalid_argument("Animation clips require a concrete RenderAnimation value");
    }
    clips_[index(animation)] = std::move(clip);
}

bool AnimationController::hasClip(RenderAnimation animation) const
{
    if (animation == RenderAnimation::None || animation == RenderAnimation::Count) {
        return false;
    }
    return !clips_[index(animation)].channels.empty();
}

const GltfAnimationClip& AnimationController::clip(RenderAnimation animation) const
{
    if (animation == RenderAnimation::None || animation == RenderAnimation::Count) {
        animation = RenderAnimation::RogueIdle;
    }
    return clips_[index(animation)];
}

void AnimationController::setPreview(const GltfAnimationClip* clip, float timeSeconds)
{
    previewClip_ = clip;
    previewTimeSeconds_ = timeSeconds;
}

std::optional<AnimationController::SkinningRequest> AnimationController::update(const RenderFrameData& frameData)
{
    constexpr float timeEpsilon = 0.0001f;
    if (previewClip_ != nullptr) {
        if (previewClip_ == activePreviewClip_ &&
            std::abs(previewTimeSeconds_ - activePreviewTime_) < timeEpsilon) {
            return std::nullopt;
        }

        activePreviewClip_ = previewClip_;
        activePreviewTime_ = previewTimeSeconds_;
        activeAnimation_ = RenderAnimation::None;
        fadeFromAnimation_ = RenderAnimation::None;
        return SkinningRequest {
            .toClip = previewClip_,
            .toTimeSeconds = previewTimeSeconds_,
        };
    }
    activePreviewClip_ = nullptr;

    RenderAnimation requestedAnimation = RenderAnimation::None;
    float requestedTime = 0.0f;
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.model == RenderModel::Rogue && tile.animation != RenderAnimation::None) {
            requestedAnimation = tile.animation;
            requestedTime = tile.animationTimeSeconds;
            break;
        }
    }
    if (requestedAnimation == RenderAnimation::None || !hasClip(requestedAnimation)) {
        return std::nullopt;
    }

    const float timeDelta = activeAnimation_ == RenderAnimation::None
        ? 0.0f
        : requestedTime - activeAnimationTime_;
    if (requestedAnimation != activeAnimation_ && activeAnimation_ != RenderAnimation::None) {
        fadeFromAnimation_ = activeAnimation_;
        fadeFromTime_ = activeAnimationTime_;
        fadeElapsed_ = 0.0f;
    }

    if (fadeFromAnimation_ == RenderAnimation::None &&
        requestedAnimation == activeAnimation_ &&
        std::abs(requestedTime - activeAnimationTime_) < timeEpsilon) {
        return std::nullopt;
    }

    SkinningRequest request {
        .toClip = &clip(requestedAnimation),
        .toTimeSeconds = requestedTime,
    };
    if (fadeFromAnimation_ != RenderAnimation::None) {
        fadeFromTime_ += timeDelta;
        fadeElapsed_ += std::abs(timeDelta);
        if (fadeDurationSeconds_ <= 0.0f || fadeElapsed_ >= fadeDurationSeconds_) {
            fadeFromAnimation_ = RenderAnimation::None;
        } else {
            float blend = fadeElapsed_ / fadeDurationSeconds_;
            blend = blend * blend * (3.0f - 2.0f * blend);
            request.fromClip = &clip(fadeFromAnimation_);
            request.fromTimeSeconds = fadeFromTime_;
            request.blend = blend;
        }
    }

    activeAnimation_ = requestedAnimation;
    activeAnimationTime_ = requestedTime;
    return request;
}

std::size_t AnimationController::index(RenderAnimation animation)
{
    return static_cast<std::size_t>(animation);
}

void AnimationController::resetPlayback()
{
    activeAnimation_ = RenderAnimation::None;
    activeAnimationTime_ = -1.0f;
    fadeFromAnimation_ = RenderAnimation::None;
    fadeFromTime_ = 0.0f;
    fadeElapsed_ = 0.0f;
    activePreviewClip_ = nullptr;
    activePreviewTime_ = -1.0f;
}

} // namespace sokoban
