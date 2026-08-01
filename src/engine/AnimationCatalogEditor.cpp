#include "engine/AnimationCatalogEditor.hpp"

#include "engine/AssetManifest.hpp"

#include <exception>
#include <system_error>
#include <utility>

namespace sokoban {
namespace {

bool samePath(
    const std::filesystem::path& left,
    const std::filesystem::path& right)
{
    if (left.empty() || right.empty()) {
        return false;
    }
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(left, right, error);
    if (!error) {
        return equivalent;
    }
    return left.lexically_normal() == right.lexically_normal();
}

} // namespace

bool AnimationCatalogEditor::initialize(
    std::filesystem::path sourcePath,
    std::filesystem::path runtimePath,
    const AssetManifest& manifest)
{
    sourcePath_ = std::move(sourcePath);
    runtimePath_ = std::move(runtimePath);
    return reload(manifest);
}

bool AnimationCatalogEditor::reload(const AssetManifest& manifest)
{
    try {
        catalog_ = AnimationCatalog::loadFromFile(sourcePath_, manifest);
        loaded_ = true;
        dirty_ = false;
        status_ = "Loaded " + sourcePath_.string();
        return true;
    } catch (const std::exception& error) {
        status_ = "Load failed: " + std::string(error.what());
        return false;
    }
}

bool AnimationCatalogEditor::save(const AssetManifest& manifest)
{
    if (!loaded_) {
        status_ = "Save failed: no animation catalog is loaded.";
        return false;
    }
    try {
        catalog_.save(sourcePath_, manifest);
        if (!runtimePath_.empty() && !samePath(sourcePath_, runtimePath_)) {
            catalog_.save(runtimePath_, manifest);
        }
        dirty_ = false;
        status_ = "Saved source and runtime animation catalogs.";
        return true;
    } catch (const std::exception& error) {
        // The source is authoritative. If its write succeeded but mirroring
        // failed, keep dirty set so Save remains visibly actionable.
        dirty_ = true;
        status_ = "Save failed: " + std::string(error.what());
        return false;
    }
}

void AnimationCatalogEditor::setGlobalSpeed(
    RenderAnimation animation,
    float speed)
{
    catalog_.setGlobalSpeed(animation, speed);
    dirty_ = true;
}

void AnimationCatalogEditor::setClipDuration(
    RenderAnimation animation,
    float durationSeconds)
{
    catalog_.setClipDuration(animation, durationSeconds);
    dirty_ = true;
}

void AnimationCatalogEditor::setUseAnimation(
    AnimationUse use,
    RenderAnimation animation)
{
    catalog_.setUseAnimation(use, animation);
    dirty_ = true;
}

void AnimationCatalogEditor::setUseSpeed(AnimationUse use, float speed)
{
    catalog_.setUseSpeed(use, speed);
    dirty_ = true;
}

void AnimationCatalogEditor::setTimelineEvent(
    AnimationUse use,
    std::string eventId,
    float normalizedTime)
{
    catalog_.setTimelineEvent(use, std::move(eventId), normalizedTime);
    dirty_ = true;
}

void AnimationCatalogEditor::removeTimelineEvent(
    AnimationUse use,
    std::string_view eventId)
{
    catalog_.removeTimelineEvent(use, eventId);
    dirty_ = true;
}

void AnimationCatalogEditor::setStartGate(
    AnimationUse use,
    std::optional<AnimationCatalog::EventGate> gate)
{
    catalog_.setStartGate(use, std::move(gate));
    dirty_ = true;
}

} // namespace sokoban
