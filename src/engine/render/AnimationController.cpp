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
    previewModel_ = cubeModel;
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

void AnimationController::setPreview(
    RenderModel model,
    const GltfAnimationClip* clip,
    float timeSeconds)
{
    previewModel_ = clip != nullptr ? model : cubeModel;
    previewClip_ = clip;
    previewTimeSeconds_ = timeSeconds;
}

std::optional<AnimationController::SkinningRequest> AnimationController::update(const RenderFrameData& frameData)
{
    if (previewClip_ != nullptr &&
        std::ranges::any_of(frameData.tiles, [&](const RenderFrameData::Tile& tile) {
            return tile.model == previewModel_;
        })) {
        constexpr float timeEpsilon = 0.0001f;
        if (previewClip_ == activePreviewClip_ &&
            std::abs(previewTimeSeconds_ - activePreviewTime_) < timeEpsilon) {
            return std::nullopt;
        }

        activePreviewClip_ = previewClip_;
        activePreviewTime_ = previewTimeSeconds_;
        legacyPlayback_ = {};
        return SkinningRequest {
            .toClip = previewClip_,
            .toTimeSeconds = previewTimeSeconds_,
        };
    }
    activePreviewClip_ = nullptr;

    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.model == playerModel_ && !tile.animation.isNone()) {
            return updateTile(tile, legacyPlayback_);
        }
    }
    return std::nullopt;
}

std::vector<AnimationController::InstanceSkinningRequest>
AnimationController::updateInstances(const RenderFrameData& frameData)
{
    std::vector<InstanceSkinningRequest> requests;
    requests.reserve(frameData.tiles.size());
    activePreviewClip_ = previewClip_;
    activePreviewTime_ = previewClip_ != nullptr
        ? previewTimeSeconds_
        : -1.0f;
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.animationInstanceId == 0) {
            continue;
        }
        PlaybackState& playback = instancePlayback_[tile.animationInstanceId];
        if (previewClip_ != nullptr && tile.model == previewModel_) {
            playback = {};
            requests.push_back({
                .instanceId = tile.animationInstanceId,
                .model = tile.model,
                .skinning = {
                    .toClip = previewClip_,
                    .toTimeSeconds = previewTimeSeconds_,
                },
            });
            continue;
        }
        if (std::optional<SkinningRequest> request = updateTile(tile, playback, true)) {
            requests.push_back({
                .instanceId = tile.animationInstanceId,
                .model = tile.model,
                .skinning = *request,
            });
        }
    }
    return requests;
}

std::optional<AnimationController::SkinningRequest> AnimationController::updateTile(
    const RenderFrameData::Tile& tile,
    PlaybackState& playback,
    bool forceSample)
{
    constexpr float timeEpsilon = 0.0001f;
    RenderAnimation requestedAnimation = tile.animation.isNone()
        ? fallbackClip_
        : tile.animation;
    float requestedTime = tile.animationTimeSeconds;
    bool resolvedNonLoopingFallback = false;
    if (!tile.animationLoops &&
        !tile.animationFallback.isNone() &&
        hasClip(requestedAnimation) &&
        hasClip(tile.animationFallback) &&
        requestedTime >= clip(requestedAnimation).durationSeconds) {
        requestedAnimation = tile.animationFallback;
        requestedTime = tile.animationFallbackTimeSeconds;
        resolvedNonLoopingFallback = true;
    }
    if (requestedAnimation.isNone() || !hasClip(requestedAnimation)) {
        return std::nullopt;
    }

    const float timeDelta = playback.activeAnimation.isNone()
        ? 0.0f
        : requestedTime - playback.activeAnimationTime;
    if (resolvedNonLoopingFallback) {
        // The fallback is authored as the terminal pose of the one-shot clip.
        // A generic crossfade would sample the completed source at its exact
        // duration, which looping samplers wrap back to the starting pose.
        playback.fadeFromAnimation = noAnimation;
        playback.fadeElapsed = 0.0f;
    } else if (!(requestedAnimation == playback.activeAnimation) &&
        !playback.activeAnimation.isNone()) {
        playback.fadeFromAnimation = playback.activeAnimation;
        playback.fadeFromTime = playback.activeAnimationTime;
        playback.fadeElapsed = 0.0f;
    }

    if (!forceSample && playback.fadeFromAnimation.isNone() &&
        requestedAnimation == playback.activeAnimation &&
        std::abs(requestedTime - playback.activeAnimationTime) < timeEpsilon) {
        return std::nullopt;
    }

    SkinningRequest request {
        .toClip = &clip(requestedAnimation),
        .toTimeSeconds = requestedTime,
    };
    if (!playback.fadeFromAnimation.isNone()) {
        playback.fadeFromTime += timeDelta;
        playback.fadeElapsed += std::abs(timeDelta);
        if (fadeDurationSeconds_ <= 0.0f ||
            playback.fadeElapsed >= fadeDurationSeconds_) {
            playback.fadeFromAnimation = noAnimation;
        } else {
            float blend = playback.fadeElapsed / fadeDurationSeconds_;
            blend = blend * blend * (3.0f - 2.0f * blend);
            request.fromClip = &clip(playback.fadeFromAnimation);
            request.fromTimeSeconds = playback.fadeFromTime;
            request.blend = blend;
        }
    }

    playback.activeAnimation = requestedAnimation;
    playback.activeAnimationTime = requestedTime;
    return request;
}

void AnimationController::resetPlayback()
{
    legacyPlayback_ = {};
    instancePlayback_.clear();
    activePreviewClip_ = nullptr;
    activePreviewTime_ = -1.0f;
}

} // namespace sokoban
