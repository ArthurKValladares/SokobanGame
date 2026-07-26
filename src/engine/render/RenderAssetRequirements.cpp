#include "engine/render/RenderAssetRequirements.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/Level.hpp"
#include "engine/ParticleConfig.hpp"
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

void RenderAssetRequirements::requireTexture(RenderTexture texture)
{
    if (!texture.isNone()) {
        require(textures_, texture.index());
    }
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
    textures_.resize(std::max(textures_.size(), other.textures_.size()), false);
    for (std::size_t i = 0; i < other.textures_.size(); ++i) {
        textures_[i] = textures_[i] || other.textures_[i];
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

bool RenderAssetRequirements::contains(RenderTexture texture) const
{
    return !texture.isNone() && containsIndex(textures_, texture.index());
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

std::size_t RenderAssetRequirements::textureCount() const
{
    return static_cast<std::size_t>(
        std::count(textures_.begin(), textures_.end(), true));
}

bool RenderAssetRequirements::empty() const
{
    return modelCount() == 0 && animationCount() == 0 && textureCount() == 0;
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
    requirements.requireAnimation(manifest.playerDeathAnimation());
    requirements.requireAnimation(manifest.playerDeadIdleAnimation());

    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                if (level.tileAt(x, y, z) == TileType::Water) {
                    continue;
                }
                requirements.requireModel(
                    manifest.modelForTile(level.tileAt(x, y, z)));
            }
        }
    }
    bool containsMirror = false;
    for (uint32_t z = 0; z < level.depth() && !containsMirror; ++z) {
        for (uint32_t y = 0; y < level.height() && !containsMirror; ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                if (tileTypeIsMirror(level.tileAt(x, y, z))) {
                    containsMirror = true;
                    break;
                }
            }
        }
    }
    if (containsMirror) {
        for (std::string_view textureName :
             config::mirrorSwapSmokeTextureNames) {
            requirements.requireTexture(
                manifest.textureIdByName(textureName));
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
        requirements.requireAnimation(tile.animationFallback);
    }
    for (const RenderFrameData::Particle& particle : frame.particles) {
        requirements.requireTexture(particle.texture);
    }
    return requirements;
}
} // namespace sokoban
