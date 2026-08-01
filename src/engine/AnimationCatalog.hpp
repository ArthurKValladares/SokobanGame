#pragma once

#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
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
    struct UseBinding {
        RenderAnimation animation = noAnimation;
        float speed = 1.0f;
    };

    [[nodiscard]] static AnimationCatalog parse(
        std::string_view text,
        const AssetManifest& manifest);
    [[nodiscard]] static AnimationCatalog loadFromFile(
        const std::filesystem::path& file,
        const AssetManifest& manifest);

    [[nodiscard]] RenderAnimation animation(AnimationUse use) const;
    [[nodiscard]] float globalSpeed(RenderAnimation animation) const;
    [[nodiscard]] float useSpeed(AnimationUse use) const;
    [[nodiscard]] float effectiveSpeed(AnimationUse use) const;

    void setGlobalSpeed(RenderAnimation animation, float speed);
    void setUseAnimation(AnimationUse use, RenderAnimation animation);
    void setUseSpeed(AnimationUse use, float speed);

    [[nodiscard]] std::string serialize(const AssetManifest& manifest) const;
    void save(
        const std::filesystem::path& file,
        const AssetManifest& manifest) const;

private:
    static constexpr std::size_t useCount =
        static_cast<std::size_t>(AnimationUse::Count);

    std::vector<float> globalSpeeds_;
    std::array<UseBinding, useCount> uses_ {};
};

} // namespace sokoban
