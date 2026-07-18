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

void AnimationController::configure(RenderModel playerModel, RenderAnimation fallbackClip)
{
    playerModel_ = playerModel;
    fallbackClip_ = fallbackClip;
}

void AnimationController::clear()
{
    clips_.clear();
    previewClip_ = nullptr;
    previewTimeSeconds_ = 0.0f;
    resetPlayback();
}

void AnimationController::setClip(RenderAnimation animation, GltfAnimationClip clip)
{
    if (animation.isNone()) {
        throw std::invalid_argument("Animation clips require a concrete animation id");
    }
    if (clips_.size() <= animation.index()) {
        clips_.resize(animation.index() + 1);
    }
    clips_[animation.index()] = std::move(clip);
}

bool AnimationController::hasClip(RenderAnimation animation) const
{
    return !animation.isNone() &&
        animation.index() < clips_.size() &&
        !clips_[animation.index()].channels.empty();
}

const GltfAnimationClip& AnimationController::clip(RenderAnimation animation) const
{
    if (!hasClip(animation)) {
        animation = fallbackClip_;
    }
    if (!hasClip(animation)) {
        throw std::out_of_range("Animation clip requested before it was loaded");
    }
    return clips_[animation.index()];
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
        activeAnimation_ = noAnimation;
        fadeFromAnimation_ = noAnimation;
        return SkinningRequest {
            .toClip = previewClip_,
            .toTimeSeconds = previewTimeSeconds_,
        };
    }
    activePreviewClip_ = nullptr;

    RenderAnimation requestedAnimation = noAnimation;
    float requestedTime = 0.0f;
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.model == playerModel_ && !tile.animation.isNone()) {
            requestedAnimation = tile.animation;
            requestedTime = tile.animationTimeSeconds;
            break;
        }
    }
    if (requestedAnimation.isNone() || !hasClip(requestedAnimation)) {
        return std::nullopt;
    }

    const float timeDelta = activeAnimation_.isNone()
        ? 0.0f
        : requestedTime - activeAnimationTime_;
    if (!(requestedAnimation == activeAnimation_) && !activeAnimation_.isNone()) {
        fadeFromAnimation_ = activeAnimation_;
        fadeFromTime_ = activeAnimationTime_;
        fadeElapsed_ = 0.0f;
    }

    if (fadeFromAnimation_.isNone() &&
        requestedAnimation == activeAnimation_ &&
        std::abs(requestedTime - activeAnimationTime_) < timeEpsilon) {
        return std::nullopt;
    }

    SkinningRequest request {
        .toClip = &clip(requestedAnimation),
        .toTimeSeconds = requestedTime,
    };
    if (!fadeFromAnimation_.isNone()) {
        fadeFromTime_ += timeDelta;
        fadeElapsed_ += std::abs(timeDelta);
        if (fadeDurationSeconds_ <= 0.0f || fadeElapsed_ >= fadeDurationSeconds_) {
            fadeFromAnimation_ = noAnimation;
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

void AnimationController::resetPlayback()
{
    activeAnimation_ = noAnimation;
    activeAnimationTime_ = -1.0f;
    fadeFromAnimation_ = noAnimation;
    fadeFromTime_ = 0.0f;
    fadeElapsed_ = 0.0f;
    activePreviewClip_ = nullptr;
    activePreviewTime_ = -1.0f;
}

} // namespace sokoban
