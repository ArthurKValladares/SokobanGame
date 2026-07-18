#include "engine/render/RenderAssetRequirements.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/Level.hpp"
#include "engine/TileTypes.hpp"

#include <algorithm>

namespace sokoban {
namespace {

void require(std::vector<bool>& set, std::size_t index)
{
    if (set.size() <= index) {
        set.resize(index + 1, false);
    }
    set[index] = true;
}

[[nodiscard]] bool containsIndex(const std::vector<bool>& set, std::size_t index)
{
    return index < set.size() && set[index];
}

} // namespace

void RenderAssetRequirements::requireModel(RenderModel model)
{
    if (model.isCube()) {
        return;
    }
    require(models_, model.index());
}

void RenderAssetRequirements::requireAnimation(RenderAnimation animation)
{
    if (animation.isNone()) {
        return;
    }
    require(animations_, animation.index());
}

void RenderAssetRequirements::merge(const RenderAssetRequirements& other)
{
    models_.resize(std::max(models_.size(), other.models_.size()), false);
    for (std::size_t i = 0; i < other.models_.size(); ++i) {
        models_[i] = models_[i] || other.models_[i];
    }
    animations_.resize(std::max(animations_.size(), other.animations_.size()), false);
    for (std::size_t i = 0; i < other.animations_.size(); ++i) {
        animations_[i] = animations_[i] || other.animations_[i];
    }
}

bool RenderAssetRequirements::contains(RenderModel model) const
{
    return !model.isCube() && containsIndex(models_, model.index());
}

bool RenderAssetRequirements::contains(RenderAnimation animation) const
{
    return !animation.isNone() && containsIndex(animations_, animation.index());
}

std::size_t RenderAssetRequirements::modelCount() const
{
    return static_cast<std::size_t>(std::count(models_.begin(), models_.end(), true));
}

std::size_t RenderAssetRequirements::animationCount() const
{
    return static_cast<std::size_t>(
        std::count(animations_.begin(), animations_.end(), true));
}

bool RenderAssetRequirements::empty() const
{
    return modelCount() == 0 && animationCount() == 0;
}

RenderAssetRequirements renderAssetRequirementsForLevel(
    const Level& level,
    const AssetManifest& manifest)
{
    RenderAssetRequirements requirements;

    // Every valid level has a player, and gameplay can select any of these
    // clips without the level data changing.
    requirements.requireModel(manifest.playerModel());
    requirements.requireAnimation(manifest.playerIdleAnimation());
    requirements.requireAnimation(manifest.playerMoveAnimation());
    requirements.requireAnimation(manifest.playerPushAnimation());

    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                requirements.requireModel(
                    manifest.modelForTile(level.tileAt(x, y, z)));
            }
        }
    }
    for (const Level::MovableTile& movable : level.movableTiles()) {
        requirements.requireModel(manifest.modelForTile(movable.type));
    }
    return requirements;
}

RenderAssetRequirements renderAssetRequirementsForFrame(const RenderFrameData& frame)
{
    RenderAssetRequirements requirements;
    for (const RenderFrameData::Tile& tile : frame.tiles) {
        requirements.requireModel(tile.model);
        requirements.requireAnimation(tile.animation);
    }
    return requirements;
}
} // namespace sokoban
