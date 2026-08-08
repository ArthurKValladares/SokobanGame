#pragma once

#include "engine/render/RenderTypes.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace sokoban {

class AssetManifest;
class AnimationCatalog;
class Level;

class RenderAssetRequirements {
public:
    void requireModel(RenderModel model);
    void requireAnimation(RenderAnimation animation);
    void requireTexture(RenderTexture texture);
    void merge(const RenderAssetRequirements& other);
    void clear();

    [[nodiscard]] bool contains(RenderModel model) const;
    [[nodiscard]] bool contains(RenderAnimation animation) const;
    [[nodiscard]] bool contains(RenderTexture texture) const;
    [[nodiscard]] std::size_t modelCount() const;
    [[nodiscard]] std::size_t animationCount() const;
    [[nodiscard]] std::size_t textureCount() const;
    [[nodiscard]] bool empty() const;

private:
    // Indexed by id index (value - 1); grown on demand.
    std::vector<bool> models_;
    std::vector<bool> animations_;
    std::vector<bool> textures_;
};

// `location` picks that screen's ground splat map; leave it unset to preload
// the shared map instead (a screen with no map of its own uses it anyway).
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForLevel(
    const Level& level,
    const AssetManifest& manifest,
    std::optional<LevelLocation> location = std::nullopt,
    const AnimationCatalog* animations = nullptr);
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForFrame(
    const RenderFrameData& frame);
void renderAssetRequirementsForFrame(
    const RenderFrameData& frame,
    RenderAssetRequirements& requirements);

} // namespace sokoban
