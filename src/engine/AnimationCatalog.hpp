#pragma once

#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class AssetManifest;

// Stable semantic call sites owned by code. The JSON catalog must contain
// exactly one binding for every value in this enum, so adding a new animation
// use cannot silently bypass authoring and validation.
enum class AnimationUse : uint8_t {
    PlayerIdle,
    PlayerMove,
    PlayerPush,
    PlayerDeath,
    PlayerDeadIdle,
    EnemyIdle,
    EnemyAttack,
    MirrorPreviewPlayerIdle,
    MirrorPreviewPlayerDeadIdle,
    EditorPlayerIdle,
    EditorEnemyIdle,
    ThumbnailPlayerIdle,
    ThumbnailEnemyIdle,
    Count,
};

struct AnimationUseDefinition {
    AnimationUse use;
    std::string_view id;
    std::string_view label;
};

[[nodiscard]] std::span<const AnimationUseDefinition>
animationUseDefinitions();
[[nodiscard]] std::string_view animationUseId(AnimationUse use);

class AnimationCatalog {
public:
    struct TimelineEvent {
        std::string id;
        float normalizedTime = 0.0f;

        friend bool operator==(const TimelineEvent&, const TimelineEvent&) =
            default;
    };

    struct EventGate {
        AnimationUse sourceUse = AnimationUse::PlayerIdle;
        std::string eventId;

        friend bool operator==(const EventGate&, const EventGate&) = default;
    };

    struct ClipTuning {
        float speed = 1.0f;
        float durationSeconds = 0.0f;
    };

    struct UseBinding {
        RenderAnimation animation = noAnimation;
        float speed = 1.0f;
        std::vector<TimelineEvent> events;
        std::optional<EventGate> startAfter;
    };

    [[nodiscard]] static AnimationCatalog parse(
        std::string_view text,
        const AssetManifest& manifest);
    [[nodiscard]] static AnimationCatalog loadFromFile(
        const std::filesystem::path& file,
        const AssetManifest& manifest);

    [[nodiscard]] RenderAnimation animation(AnimationUse use) const;
    [[nodiscard]] float globalSpeed(RenderAnimation animation) const;
    [[nodiscard]] float clipDuration(RenderAnimation animation) const;
    [[nodiscard]] float useSpeed(AnimationUse use) const;
    [[nodiscard]] float effectiveSpeed(AnimationUse use) const;
    [[nodiscard]] std::span<const TimelineEvent> events(AnimationUse use) const;
    [[nodiscard]] const std::optional<EventGate>& startGate(
        AnimationUse use) const;
    [[nodiscard]] float eventSourceTime(
        AnimationUse use,
        std::string_view eventId) const;

    void setGlobalSpeed(RenderAnimation animation, float speed);
    void setClipDuration(RenderAnimation animation, float durationSeconds);
    void setUseAnimation(AnimationUse use, RenderAnimation animation);
    void setUseSpeed(AnimationUse use, float speed);
    void setTimelineEvent(
        AnimationUse use,
        std::string eventId,
        float normalizedTime);
    void removeTimelineEvent(AnimationUse use, std::string_view eventId);
    void setStartGate(AnimationUse use, std::optional<EventGate> gate);

    [[nodiscard]] std::string serialize(const AssetManifest& manifest) const;
    void save(
        const std::filesystem::path& file,
        const AssetManifest& manifest) const;

private:
    static constexpr std::size_t useCount =
        static_cast<std::size_t>(AnimationUse::Count);

    void validateRelations() const;

    std::vector<ClipTuning> clips_;
    std::array<UseBinding, useCount> uses_ {};
};

} // namespace sokoban
