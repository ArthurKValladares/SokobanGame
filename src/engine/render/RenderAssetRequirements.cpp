#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/CliffWallGeometry.hpp"
#include "engine/ControlVisuals.hpp"
#include "engine/GroundTileGeometry.hpp"
#include "engine/render/SelectorRenderConfig.hpp"

#include "engine/ActionFeedbackConfig.hpp"
#include "engine/AnimationCatalog.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/CampaignSession.hpp"
#include "engine/Level.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/RenderFrameParts.hpp"
#include "engine/RotatorVisuals.hpp"
#include "engine/RobotVisuals.hpp"
#include "engine/TileTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace sokoban {

std::vector<LevelLocation> puzzleAssetPrefetchTargets(
    const CampaignSession& campaign,
    const PlayerProfile& profile,
    const OverworldMap* overworldMap)
{
    std::vector<LevelLocation> targets;
    if (!campaign.inOverworld()) {
        // The current screen is already requested at visible priority. Its
        // successor becomes selectable when the current puzzle is completed.
        if (campaign.currentScreen() < campaign.screenCount(
                campaign.currentLevel()) - 1) {
            targets.push_back({
                .level = campaign.currentLevel(),
                .screen = campaign.currentScreen() + 1,
            });
        }
        return targets;
    }

    const auto includeSelectable = [&](LevelLocation target) {
        if (campaign.selectorViewState(profile, target).status !=
                ScreenSelectorStatus::Unavailable &&
            std::ranges::find(targets, target) == targets.end()) {
            targets.push_back(target);
        }
    };
    if (overworldMap) {
        for (const OverworldSelectorRuntime& selector :
             overworldMap->selectors()) {
            if (selector.screen == campaign.activeOverworldScreen() &&
                selector.target) {
                includeSelectable(*selector.target);
            }
        }
    } else {
        for (LevelLocation target : campaign.overworldTargets()) {
            includeSelectable(target);
        }
    }
    return targets;
}

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

void RenderAssetRequirements::requireGroundSplat(const GroundSplatTextures& textures)
{
    for (RenderTexture texture : textures.sampledTextures()) {
        requireTexture(texture);
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

void RenderAssetRequirements::clear()
{
    std::fill(models_.begin(), models_.end(), false);
    std::fill(animations_.begin(), animations_.end(), false);
    std::fill(textures_.begin(), textures_.end(), false);
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

RenderAssetRequirements renderActionAssetRequirementsForLevel(
    const Level& level,
    const AssetManifest& manifest,
    const AnimationCatalog* animations)
{
    RenderAssetRequirements requirements;
    auto requireUse = [&](AnimationUse use, RenderAnimation fallback) {
        requirements.requireAnimation(
            animations != nullptr ? animations->animation(use) : fallback);
    };
    requireUse(AnimationUse::PlayerIdle, manifest.playerIdleAnimation());
    requireUse(AnimationUse::PlayerMove, manifest.playerMoveAnimation());
    requireUse(AnimationUse::PlayerPush, manifest.playerPushAnimation());
    requireUse(AnimationUse::PlayerPull, manifest.playerPullAnimation());
    requireUse(AnimationUse::PlayerDeath, manifest.playerDeathAnimation());
    requireUse(AnimationUse::PlayerDeadIdle, manifest.playerDeadIdleAnimation());
    requireUse(
        AnimationUse::MirrorPreviewPlayerIdle,
        manifest.playerIdleAnimation());
    requireUse(
        AnimationUse::MirrorPreviewPlayerDeadIdle,
        manifest.playerDeadIdleAnimation());
    if (!level.enemyStarts().empty()) {
        requireUse(AnimationUse::EnemyIdle, manifest.playerIdleAnimation());
        requireUse(AnimationUse::EnemyAttack, manifest.enemyAttackAnimation());
    }
    if (std::ranges::any_of(level.movableTiles(), [](const auto& movable) {
            return tileTypeIsRobot(movable.type);
        }) || std::ranges::any_of(level.decorations(), [](const auto& decoration) {
            return decoration.model == "Robot";
        })) {
        requirements.requireAnimation(robotDriveAnimation(manifest));
    }
    // Any hero can walk into something, and the dust that raises is over
    // before demand-driven streaming would have brought its textures in. A
    // manifest that declares no such sprite has no dust and nothing to warm.
    if (config::actionFeedbackEnabled) {
        for (std::string_view textureName : config::actionFeedbackDustTextureNames) {
            requirements.requireTexture(manifest.findTextureIdByName(textureName));
        }
    }
    // A valid level has one metadata record for every gate tile, so these
    // collections cover brief effects without traversing the terrain grid.
    if (!level.gates().empty()) {
        requirements.requireTexture(
            manifest.textureIdByName(config::gateParticleTextureName));
    }
    if (std::ranges::any_of(level.movableTiles(), [](const auto& movable) {
            return tileTypeIsMirror(movable.type);
        })) {
        for (std::string_view textureName : config::mirrorSwapSmokeTextureNames) {
            requirements.requireTexture(manifest.textureIdByName(textureName));
        }
    }
    if (!level.portals().empty()) {
        requirements.requireTexture(
            manifest.findTextureIdByName(config::turretGlowTextureName));
    }
    if (std::ranges::any_of(level.movableTiles(), [](const auto& movable) {
            return tileTypeIsTurret(movable.type);
        })) {
        // The flame's first front can arrive before demand-driven streaming.
        requirements.requireTexture(
            manifest.textureIdByName(config::turretGlowTextureName));
    }
    return requirements;
}

namespace {

using CellFilter = std::function<bool(GridPosition3)>;

// Native ground and cliff cells draw one of several authored bodies, chosen
// from which of their neighbours are drawn with them. These are the frame
// tiles such cells produce before that choice: tileVisual's placement, the
// authored rotation, and whether the cell below supports the rock. Legacy
// and custom ground has a single model and is left out.
struct ModuleTiles {
    std::vector<RenderFrameData::Tile> tiles;
    // Parallel to `tiles`: rock over a cell with no fixed support, in a level
    // that has ice. Ice is a movable, so a block can arrive under such a
    // cell or leave it, and the cell swaps between its regular body and the
    // jagged underside of an unsupported one.
    std::vector<uint8_t> iceMaySupport;
};

[[nodiscard]] ModuleTiles moduleTilesForLevel(
    const Level& level, const AssetManifest& manifest)
{
    PresentationSettings settings;
    settings.applyTileScales(manifest);
    const auto isIce = [](const Level::MovableTile& movable) {
        return movable.type == TileType::Ice;
    };
    const bool levelHasIce =
        std::ranges::any_of(level.movableTiles(), isIce);
    ModuleTiles result;
    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                const TileType type = level.tileAt(x, y, z);
                if (!tileTypeIsGround(type) && !tileTypeIsCliffWall(type)) {
                    continue;
                }
                const GridPosition3 cell {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };
                RenderFrameData::Tile tile =
                    tileVisual(type, cell, manifest, settings);
                if (!tile.groundModule && !tile.cliffWall) {
                    continue;
                }
                renderFrameParts::applyAuthoredTileRotation(
                    tile, level.tileRotationQuarterTurns(cell));
                bool iceMaySupport = false;
                if (tile.groundModule && z > 0) {
                    // The same rule the gameplay builder applies, with the
                    // level's starting movables in place of live state.
                    const GridPosition3 below { cell.x, cell.y, cell.z - 1 };
                    const TileType support = level.tileAt(x, y, z - 1);
                    const bool fixedSupport = support != TileType::Ice &&
                        groundTileBottomSupport(support);
                    tile.groundModuleSupported = fixedSupport ||
                        std::ranges::any_of(level.movableTiles(),
                            [&](const Level::MovableTile& movable) {
                                return isIce(movable) &&
                                    movable.position == below;
                            });
                    iceMaySupport = !fixedSupport && levelHasIce;
                }
                result.tiles.push_back(tile);
                result.iceMaySupport.push_back(iceMaySupport ? 1 : 0);
            }
        }
    }
    return result;
}

// Requires the body each of `drawn` selects when those tiles, and only
// those, are drawn together. A cell at the edge of what is drawn has no
// neighbour there, so its body depends on the drawn set and not only on the
// level. The choice is made in place by the functions frames use.
// `iceMaySupport` is parallel to `drawn`.
void requireSelectedModuleBodies(
    RenderAssetRequirements& requirements,
    std::vector<RenderFrameData::Tile>& drawn,
    std::span<const uint8_t> iceMaySupport,
    const AssetManifest& manifest)
{
    if (drawn.empty()) {
        return;
    }
    processCliffWallGeometry(drawn, manifest);
    processGroundTileGeometry(drawn, manifest);
    for (std::size_t index = 0; index < drawn.size(); ++index) {
        const RenderFrameData::Tile& tile = drawn[index];
        requirements.requireModel(tile.model);
        if (!tile.groundModule || iceMaySupport[index] == 0) {
            continue;
        }
        // Whichever of the pair the starting position selects, play can
        // reach the other.
        const std::size_t shape =
            tileModuleForMask(tile.groundModuleSideMask).shapeIndex;
        for (const bool bottom : { false, true }) {
            if (const auto body = manifest.findModelIdByName(
                    groundTileModelName(
                        shape, tile.groundRockVariant,
                        tile.groundModuleSquare, bottom))) {
                requirements.requireModel(*body);
            }
        }
    }
}

// Everything a level needs except the bodies its native ground and cliff
// cells select, which requireSelectedModuleBodies adds for a given drawn
// set. `includeCell`, when set, limits the board, its movable objects and its
// decorations to the cells it accepts. Actors, effects and splat textures are
// level-wide and are always included.
RenderAssetRequirements levelAssetRequirements(
    const Level& level,
    const AssetManifest& manifest,
    std::optional<LevelLocation> location,
    const AnimationCatalog* animations,
    const CellFilter* includeCell)
{
    const auto included = [includeCell](GridPosition3 cell) {
        return includeCell == nullptr || (*includeCell)(cell);
    };
    RenderAssetRequirements requirements =
        renderActionAssetRequirementsForLevel(level, manifest, animations);

    const auto requireCharacter = [&](CharacterType character) {
        requirements.requireModel(manifest.characterModel(character));
        const auto abilities = baseCharacterAbilities(character);
        if (abilities.has(CharacterAbility::ProtectionAura)) {
            requirements.requireTexture(manifest.findTextureIdByName("ParticleGlow"));
        }
        if (abilities.has(CharacterAbility::Crystallize)) {
            requirements.requireModel(manifest.modelForTile(TileType::Crystal));
            requirements.requireModel(manifest.modelForTile(TileType::SkippingCrystal));
            for (std::string_view name : config::mirrorSwapSmokeTextureNames) {
                requirements.requireTexture(manifest.textureIdByName(name));
            }
        }
    };
    for (const Level::PlayerStart& player : level.playerStarts()) {
        requireCharacter(player.character);
    }
    for (const Level::Wardrobe& wardrobe : level.wardrobes()) {
        requireCharacter(wardrobe.character);
    }
    if (!level.enemyStarts().empty()) {
        requirements.requireModel(manifest.enemyModel());
    }

    // Ground splatting samples these directly from the descriptor array, so
    // they must be required like any model texture; unset ids are ignored.
    // Resolved through the same helper the frame builder uses, so preloading
    // cannot fetch a different screen's map than the one that gets drawn.
    const GroundSplatTextures splat = groundSplatTexturesForScreen(
        [&manifest](std::string_view name) {
            return manifest.findTextureIdByName(name);
        },
        location);
    requirements.requireGroundSplat(splat);
    requirements.requireTexture(manifest.findTextureIdByName(groundRockSideTextureName));
    for (const Level::GroundSplat& authored : level.groundSplats()) {
        requirements.requireGroundSplat(groundSplatTexturesForMaterials(
            [&manifest](std::string_view name) { return manifest.findTextureIdByName(name); },
            authored.base, authored.detail, manifest.findTextureIdByName(authored.mask),
            authored.texture2, authored.texture3));
    }

    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                if (!included({
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                    })) {
                    continue;
                }
                if (tileTypeIsWater(level.tileAt(x, y, z))) {
                    continue;
                }
                if (tileTypeIsGate(level.tileAt(x, y, z))) {
                    continue;
                }
                const TileType tile = level.tileAt(x, y, z);
                requirements.requireModel(manifest.modelForTile(tile));
                if (const auto parts = controlModelParts(manifest, tile)) {
                    requirements.requireModel(parts->base);
                    requirements.requireModel(parts->moving);
                }
                if (tileTypeIsLever(tile)) {
                    if (const auto onModel = manifest.findModelIdByName("LeverOn")) {
                        requirements.requireModel(*onModel);
                    }
                }
                // A plate under a mirror is not in the grid; ask for it too.
                const std::optional<TileType> plate = level.plateAt({
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                });
                if (plate && *plate != tile) {
                    requirements.requireModel(manifest.modelForTile(*plate));
                    if (const auto parts = controlModelParts(manifest, *plate)) {
                        requirements.requireModel(parts->base);
                        requirements.requireModel(parts->moving);
                    }
                    if (tileTypeIsLever(*plate)) {
                        if (const auto onModel = manifest.findModelIdByName("LeverOn")) {
                            requirements.requireModel(*onModel);
                        }
                    }
                }
                if (tileTypeIsRotator(plate.value_or(tile))) {
                    if (const std::optional<RotatorModelParts> parts =
                            rotatorModelParts(manifest, *plate)) {
                        requirements.requireModel(parts->gear);
                        requirements.requireModel(parts->icon);
                    }
                }
            }
        }
    }
    for (const Level::MovableTile& movable : level.movableTiles()) {
        if (included(movable.position)) {
            requirements.requireModel(manifest.modelForTile(movable.type));
        }
    }
    for (const Level::Decoration& decoration : level.decorations()) {
        // A decoration may sit above the top layer, so only its column
        // decides which part of the board it belongs to.
        if (included({
                static_cast<int>(std::floor(decoration.position.x)),
                static_cast<int>(std::floor(decoration.position.y)),
                0,
            })) {
            requirements.requireModel(
                manifest.modelIdByName(decoration.model));
        }
    }
    if (!level.selectors().empty()) {
        // Progress can change while the overworld stays resident. Keep every
        // A/B and availability color ready for the return from a puzzle.
        for (std::string_view modelName : selectorRender::modelNames) {
            requirements.requireModel(manifest.modelIdByName(modelName));
        }
    }
    return requirements;
}

// The screens drawn while `screen` is the active one, as a sorted list.
[[nodiscard]] std::vector<OverworldScreenId> sortedNeighborhood(
    const OverworldMap& map, OverworldScreenId screen)
{
    std::vector<OverworldScreenId> screens = map.visibleNeighborhood(screen);
    std::ranges::sort(screens);
    return screens;
}

[[nodiscard]] std::vector<OverworldScreenId> mergedScreens(
    std::span<const OverworldScreenId> left,
    std::span<const OverworldScreenId> right)
{
    std::vector<OverworldScreenId> merged;
    merged.reserve(left.size() + right.size());
    std::ranges::set_union(left, right, std::back_inserter(merged));
    return merged;
}

// The screens a single step can make the active one: those sharing an edge
// with `screen`. A diagonal neighbour is drawn, but takes two crossings.
[[nodiscard]] std::vector<OverworldScreenId> crossingDestinations(
    const OverworldMap& map, OverworldScreenId screen)
{
    std::vector<OverworldScreenId> destinations;
    const OverworldScreenRuntime* from = map.screen(screen);
    if (from == nullptr) {
        return destinations;
    }
    for (const OverworldScreenRuntime& candidate : map.screens()) {
        if (std::abs(candidate.slot.x - from->slot.x) +
                std::abs(candidate.slot.y - from->slot.y) == 1) {
            destinations.push_back(candidate.id);
        }
    }
    std::ranges::sort(destinations);
    return destinations;
}

// Everything on `screens` of a composed overworld except selected module
// bodies, with each screen's own splat map. `screens` must be sorted. The
// composed level is the only one that loads: a single screen has no hero
// start, and its portals and links may pair with another screen's.
[[nodiscard]] RenderAssetRequirements overworldContent(
    const OverworldMap& map,
    const AssetManifest& manifest,
    std::span<const OverworldScreenId> screens,
    const AnimationCatalog* animations)
{
    const CellFilter onScreens = [&map, screens](GridPosition3 cell) {
        const std::optional<OverworldScreenId> owner = map.screenAt(cell);
        return owner && std::ranges::binary_search(screens, *owner);
    };
    RenderAssetRequirements requirements = levelAssetRequirements(
        map.level(), manifest, std::nullopt, animations, &onScreens);
    // Composed frames sample one map per visible screen rather than only the
    // shared one the level-wide set names.
    for (const OverworldScreenId screen : screens) {
        requirements.requireGroundSplat(
            groundSplatTexturesForOverworldScreen(
                [&manifest](std::string_view name) {
                    return manifest.findTextureIdByName(name);
                },
                screen));
    }
    return requirements;
}

// A composed overworld's module tiles with the screen each belongs to, found
// once because a plan resolves the bodies of several drawn sets.
struct OverworldModules {
    ModuleTiles modules;
    std::vector<std::optional<OverworldScreenId>> owners;
};

[[nodiscard]] OverworldModules overworldModules(
    const OverworldMap& map, const AssetManifest& manifest)
{
    OverworldModules result {
        .modules = moduleTilesForLevel(map.level(), manifest),
    };
    result.owners.reserve(result.modules.tiles.size());
    for (const RenderFrameData::Tile& tile : result.modules.tiles) {
        result.owners.push_back(map.screenAt(tile.cell));
    }
    return result;
}

// The bodies selected while exactly `screens`, sorted, are drawn.
[[nodiscard]] RenderAssetRequirements overworldBodies(
    const OverworldModules& world,
    std::span<const OverworldScreenId> screens,
    const AssetManifest& manifest)
{
    std::vector<RenderFrameData::Tile> drawn;
    std::vector<uint8_t> iceMaySupport;
    for (std::size_t index = 0; index < world.modules.tiles.size(); ++index) {
        if (world.owners[index] &&
            std::ranges::binary_search(screens, *world.owners[index])) {
            drawn.push_back(world.modules.tiles[index]);
            iceMaySupport.push_back(world.modules.iceMaySupport[index]);
        }
    }
    RenderAssetRequirements requirements;
    requireSelectedModuleBodies(requirements, drawn, iceMaySupport, manifest);
    return requirements;
}

} // namespace

RenderAssetRequirements renderAssetRequirementsForLevel(
    const Level& level,
    const AssetManifest& manifest,
    std::optional<LevelLocation> location,
    const AnimationCatalog* animations)
{
    RenderAssetRequirements requirements = levelAssetRequirements(
        level, manifest, location, animations, nullptr);
    // A puzzle draws its whole board, so every module tile is in the set.
    ModuleTiles modules = moduleTilesForLevel(level, manifest);
    requireSelectedModuleBodies(
        requirements, modules.tiles, modules.iceMaySupport, manifest);
    return requirements;
}

RenderAssetRequirements renderAssetRequirementsForOverworldNeighborhood(
    const OverworldMap& map,
    uint32_t activeScreen,
    const AssetManifest& manifest,
    const AnimationCatalog* animations)
{
    const std::vector<OverworldScreenId> visible =
        sortedNeighborhood(map, activeScreen);
    if (visible.empty()) {
        return {};
    }
    RenderAssetRequirements requirements =
        overworldContent(map, manifest, visible, animations);
    requirements.merge(
        overworldBodies(overworldModules(map, manifest), visible, manifest));
    return requirements;
}

struct OverworldTransitionAssetPlanner::State {
    // The map the tiles and resolved bodies below belong to.
    std::optional<uint64_t> fingerprint;
    OverworldModules world;
    // Bodies already resolved, by the sorted screens drawn. A drawn set
    // recurs: arriving on a screen draws what settling on it later does, and
    // a crossing draws the same screens in both directions.
    std::map<std::vector<OverworldScreenId>, RenderAssetRequirements> resolved;
    // Drawn sets of the current plan still to resolve, last one first.
    std::vector<std::vector<OverworldScreenId>> queued;
};

OverworldTransitionAssetPlanner::OverworldTransitionAssetPlanner()
    : state_(std::make_unique<State>())
{
}

OverworldTransitionAssetPlanner::~OverworldTransitionAssetPlanner() = default;

RenderAssetRequirements OverworldTransitionAssetPlanner::begin(
    const OverworldMap& map,
    uint32_t activeScreen,
    const AssetManifest& manifest,
    const AnimationCatalog* animations)
{
    State& state = *state_;
    state.queued.clear();
    const std::vector<OverworldScreenId> current =
        sortedNeighborhood(map, activeScreen);
    if (current.empty()) {
        return {};
    }
    if (state.fingerprint != map.fingerprint()) {
        state.fingerprint = map.fingerprint();
        state.world = overworldModules(map, manifest);
        state.resolved.clear();
    }

    RenderAssetRequirements requirements;
    std::vector<OverworldScreenId> reachable = current;
    const auto requireBodies = [&](std::vector<OverworldScreenId> drawn) {
        if (const auto known = state.resolved.find(drawn);
            known != state.resolved.end()) {
            requirements.merge(known->second);
        } else if (std::ranges::find(state.queued, drawn) ==
            state.queued.end()) {
            state.queued.push_back(std::move(drawn));
        }
    };
    for (const OverworldScreenId destination :
         crossingDestinations(map, activeScreen)) {
        // While the view slides across the seam both neighbourhoods are
        // drawn; once it settles, only the destination's is. The two drawn
        // sets have different edges, and so select different bodies there.
        std::vector<OverworldScreenId> arrived =
            sortedNeighborhood(map, destination);
        reachable = mergedScreens(reachable, arrived);
        requireBodies(mergedScreens(current, arrived));
        requireBodies(std::move(arrived));
    }
    // step() takes from the back; resolve in the order they were found.
    std::ranges::reverse(state.queued);
    requirements.merge(overworldContent(map, manifest, reachable, animations));
    return requirements;
}

bool OverworldTransitionAssetPlanner::pending() const
{
    return !state_->queued.empty();
}

RenderAssetRequirements OverworldTransitionAssetPlanner::step(
    const OverworldMap& map, const AssetManifest& manifest)
{
    State& state = *state_;
    if (state.queued.empty()) {
        return {};
    }
    if (state.fingerprint != map.fingerprint()) {
        // Not the map the plan was made for; its screens mean nothing here.
        state.queued.clear();
        return {};
    }
    std::vector<OverworldScreenId> drawn = std::move(state.queued.back());
    state.queued.pop_back();
    RenderAssetRequirements bodies =
        overworldBodies(state.world, drawn, manifest);
    state.resolved.emplace(std::move(drawn), bodies);
    return bodies;
}

void OverworldTransitionAssetPlanner::cancel()
{
    state_->queued.clear();
}

void OverworldTransitionAssetPlanner::reset()
{
    *state_ = {};
}

RenderAssetRequirements renderAssetRequirementsForOverworldTransitions(
    const OverworldMap& map,
    uint32_t activeScreen,
    const AssetManifest& manifest,
    const AnimationCatalog* animations)
{
    OverworldTransitionAssetPlanner planner;
    RenderAssetRequirements requirements =
        planner.begin(map, activeScreen, manifest, animations);
    while (planner.pending()) {
        requirements.merge(planner.step(map, manifest));
    }
    return requirements;
}

namespace {

// Ground rock variants take the low bits and cliff wall variants follow.
constexpr uint32_t groundVariantCount =
    static_cast<uint32_t>(groundRockVariantCount);
constexpr uint32_t cliffVariantCount =
    static_cast<uint32_t>(cliffWallModelNames[0].size());
constexpr uint32_t cliffVariantBit = groundVariantCount;
static_assert(cliffVariantBit + cliffVariantCount <= 32,
    "Module variants must fit the mask");

} // namespace

uint32_t moduleBodyVariantsInFrame(const RenderFrameData& frame)
{
    uint32_t variants = 0;
    for (const RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.groundModule) {
            variants |= 1U << (tile.groundRockVariant % groundVariantCount);
        }
        if (tile.cliffWall) {
            variants |= 1U << (cliffVariantBit +
                tile.cliffWallVariant % cliffVariantCount);
        }
    }
    return variants;
}

RenderAssetRequirements renderAssetRequirementsForModuleVariants(
    uint32_t variants, const AssetManifest& manifest)
{
    RenderAssetRequirements requirements;
    for (uint32_t variant = 0; variant < groundVariantCount; ++variant) {
        if ((variants & (1U << variant)) == 0) {
            continue;
        }
        for (std::size_t shape = 0;
             shape < tileModuleCanonicalMasks.size();
             ++shape) {
            for (const bool square : { false, true }) {
                for (const bool bottom : { false, true }) {
                    if (const auto model = manifest.findModelIdByName(
                            groundTileModelName(
                                shape, variant, square, bottom))) {
                        requirements.requireModel(*model);
                    }
                }
            }
        }
    }
    for (uint32_t variant = 0; variant < cliffVariantCount; ++variant) {
        if ((variants & (1U << (cliffVariantBit + variant))) == 0) {
            continue;
        }
        for (const auto& shapes : cliffWallModelNames) {
            if (const auto model = manifest.findModelIdByName(shapes[variant])) {
                requirements.requireModel(*model);
            }
        }
    }
    return requirements;
}

RenderAssetRequirements renderAssetRequirementsForFrame(const RenderFrameData& frame)
{
    RenderAssetRequirements requirements;
    renderAssetRequirementsForFrame(frame, requirements);
    return requirements;
}

void renderAssetRequirementsForFrame(
    const RenderFrameData& frame,
    RenderAssetRequirements& requirements)
{
    requirements.clear();
    for (const RenderFrameData::Tile& tile : frame.tiles) {
        requirements.requireModel(tile.model);
        requirements.requireAnimation(tile.animation);
        requirements.requireAnimation(tile.animationFallback);
        if (tile.groundSplat) {
            requirements.requireGroundSplat(*tile.groundSplat);
        }
    }
    for (const RenderFrameData::Particle& particle : frame.particles) {
        requirements.requireTexture(particle.texture);
    }
    requirements.requireGroundSplat(frame.groundSplat);
    requirements.requireTexture(frame.groundRockSideTexture);
    for (std::size_t index = 0;
         index < frame.groundSplatRegionCount;
         ++index) {
        const GroundSplatTextures& textures =
            frame.groundSplatRegions[index].textures;
        requirements.requireGroundSplat(textures);
    }
}
} // namespace sokoban
