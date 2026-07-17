#include "engine/render/RenderAssetRequirements.hpp"

#include "engine/Level.hpp"
#include "engine/TileTypes.hpp"

#include <algorithm>
#include <stdexcept>

namespace sokoban {
namespace {

template <typename Enum>
std::size_t enumIndex(Enum value)
{
    return static_cast<std::size_t>(value);
}

} // namespace

void RenderAssetRequirements::requireModel(RenderModel model)
{
    if (model == RenderModel::Cube) {
        return;
    }
    if (model == RenderModel::Count) {
        throw std::invalid_argument("Cannot require the RenderModel::Count sentinel");
    }
    models_[enumIndex(model)] = true;
}

void RenderAssetRequirements::requireAnimation(RenderAnimation animation)
{
    if (animation == RenderAnimation::None) {
        return;
    }
    if (animation == RenderAnimation::Count) {
        throw std::invalid_argument("Cannot require the RenderAnimation::Count sentinel");
    }
    animations_[enumIndex(animation)] = true;
}

void RenderAssetRequirements::merge(const RenderAssetRequirements& other)
{
    for (std::size_t i = 0; i < models_.size(); ++i) {
        models_[i] = models_[i] || other.models_[i];
    }
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        animations_[i] = animations_[i] || other.animations_[i];
    }
}

bool RenderAssetRequirements::contains(RenderModel model) const
{
    return model != RenderModel::Cube &&
        model != RenderModel::Count &&
        models_[enumIndex(model)];
}

bool RenderAssetRequirements::contains(RenderAnimation animation) const
{
    return animation != RenderAnimation::None &&
        animation != RenderAnimation::Count &&
        animations_[enumIndex(animation)];
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

RenderModel renderModelForTile(TileType tile)
{
    switch (tile) {
    case TileType::Wall:
        return RenderModel::BricksA;
    case TileType::Rock:
        return RenderModel::Stone;
    case TileType::Water:
        return RenderModel::Water;
    case TileType::Ice:
        return RenderModel::Glass;
    case TileType::ConveyorUp:
    case TileType::ConveyorDown:
    case TileType::ConveyorRight:
    case TileType::ConveyorLeft:
        return RenderModel::Conveyor;
    case TileType::Player:
        return RenderModel::Rogue;
    default:
        return RenderModel::Cube;
    }
}

RenderAssetRequirements renderAssetRequirementsForLevel(const Level& level)
{
    RenderAssetRequirements requirements;

    // Every valid level has a player, and gameplay can select any of these
    // clips without the level data changing.
    requirements.requireModel(RenderModel::Rogue);
    requirements.requireAnimation(RenderAnimation::RogueIdle);
    requirements.requireAnimation(RenderAnimation::RogueMovement);
    requirements.requireAnimation(RenderAnimation::RoguePush);

    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                requirements.requireModel(renderModelForTile(level.tileAt(x, y, z)));
            }
        }
    }
    for (const Level::MovableTile& movable : level.movableTiles()) {
        requirements.requireModel(renderModelForTile(movable.type));
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
