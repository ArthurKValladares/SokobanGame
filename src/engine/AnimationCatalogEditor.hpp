#pragma once

#include "engine/AnimationCatalog.hpp"

#include <filesystem>
#include <string>

namespace sokoban {

class AssetManifest;

// Headless editable animation-catalog document. It owns dirty/status state and
// all filesystem behavior; Debug ImGui is only an adapter over these commands.
class AnimationCatalogEditor {
public:
    bool initialize(
        std::filesystem::path sourcePath,
        std::filesystem::path runtimePath,
        const AssetManifest& manifest);
    bool reload(const AssetManifest& manifest);
    bool save(const AssetManifest& manifest);

    [[nodiscard]] const AnimationCatalog& catalog() const { return catalog_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] const std::filesystem::path& sourcePath() const
    {
        return sourcePath_;
    }
    [[nodiscard]] const std::filesystem::path& runtimePath() const
    {
        return runtimePath_;
    }

    void setGlobalSpeed(RenderAnimation animation, float speed);
    void setClipDuration(RenderAnimation animation, float durationSeconds);
    void setUseAnimation(AnimationUse use, RenderAnimation animation);
    void setUseSpeed(AnimationUse use, float speed);
    void setTimelineEvent(
        AnimationUse use,
        std::string eventId,
        float normalizedTime);
    void removeTimelineEvent(AnimationUse use, std::string_view eventId);
    void setStartGate(
        AnimationUse use,
        std::optional<AnimationCatalog::EventGate> gate);

private:
    std::filesystem::path sourcePath_;
    std::filesystem::path runtimePath_;
    AnimationCatalog catalog_;
    std::string status_;
    bool loaded_ = false;
    bool dirty_ = false;
};

} // namespace sokoban
