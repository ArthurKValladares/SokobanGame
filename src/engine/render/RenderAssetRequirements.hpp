#pragma once

#include "engine/render/RenderTypes.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace sokoban {

class AssetManifest;
class AnimationCatalog;
class CampaignSession;
class Level;
class OverworldMap;
struct PlayerProfile;

class RenderAssetRequirements {
public:
    void requireModel(RenderModel model);
    void requireAnimation(RenderAnimation animation);
    void requireTexture(RenderTexture texture);
    void requireGroundSplat(const GroundSplatTextures& textures);
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
//
// Native ground and cliff cells are asked for the bodies they draw, not for
// every body of their variant. The choice depends on which neighbours are
// drawn, so it is made by the functions frames use, on the level's tiles.
// Play changes a body in one way only, which is planned for: ice arriving
// under unsupported rock, or leaving it. An editor, where every edit can
// change a neighbour's body, uses the module-variant set below.
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForLevel(
    const Level& level,
    const AssetManifest& manifest,
    std::optional<LevelLocation> location = std::nullopt,
    const AnimationCatalog* animations = nullptr);
// Warm action clips and brief effects before their first use. This excludes
// models and terrain textures, allowing composed worlds to stream those from
// their visible frames without losing an actor's first pose or effect burst.
[[nodiscard]] RenderAssetRequirements renderActionAssetRequirementsForLevel(
    const Level& level,
    const AssetManifest& manifest,
    const AnimationCatalog* animations = nullptr);
// What a composed overworld draws while `activeScreen` is the settled active
// screen: its visible neighbourhood, with each screen's own splat map and the
// bodies that set of screens selects. This plans the world a title screen is
// about to open before any frame of it exists; an unknown screen yields
// nothing. `activeScreen` is an OverworldScreenId.
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForOverworldNeighborhood(
    const OverworldMap& map,
    uint32_t activeScreen,
    const AssetManifest& manifest,
    const AnimationCatalog* animations = nullptr);
// What one seam crossing from `activeScreen` can bring into view: for every
// screen sharing an edge with it, what is drawn while the view slides there
// and once that screen is the active one. Each of those draws a different
// set of screens, with its own edges, so each contributes its own bodies.
//
// Resolving the bodies of a drawn set runs the frame's own selection over all
// of its tiles, and a crossing has several sets ahead of it, so the plan is
// made a piece at a time: begin() returns the board content of every screen
// in reach, and each step() the bodies of one more drawn set. Resolved sets
// are kept for the map they came from, which makes a screen seen before
// cheap to plan from again. They assume the manifest they were resolved
// with: call reset() when it changes.
class OverworldTransitionAssetPlanner {
public:
    OverworldTransitionAssetPlanner();
    ~OverworldTransitionAssetPlanner();

    // Replaces any plan in progress. Bodies of drawn sets already resolved
    // for this map are part of the result; the rest are left to step().
    [[nodiscard]] RenderAssetRequirements begin(
        const OverworldMap& map,
        uint32_t activeScreen,
        const AssetManifest& manifest,
        const AnimationCatalog* animations = nullptr);
    [[nodiscard]] bool pending() const;
    // Resolves one more drawn set. A map other than the one begin() saw
    // abandons the plan and yields nothing.
    [[nodiscard]] RenderAssetRequirements step(
        const OverworldMap& map, const AssetManifest& manifest);
    // Abandons the plan in progress and keeps what is resolved.
    void cancel();
    // Abandons the plan and forgets what is resolved, for when something
    // other than the map changed which bodies its cells draw.
    void reset();

private:
    struct State;
    std::unique_ptr<State> state_;
};
// The whole of such a plan at once.
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForOverworldTransitions(
    const OverworldMap& map,
    uint32_t activeScreen,
    const AssetManifest& manifest,
    const AnimationCatalog* animations = nullptr);
// For editors, where the level-wide set is not enough: painting one cell can
// change the body its neighbours select, and a body that is not loaded yet
// leaves its cell empty until it is. The first reports which native ground
// and cliff variants a frame draws, as a bit mask; the second names every
// body of the variants in such a mask.
[[nodiscard]] uint32_t moduleBodyVariantsInFrame(const RenderFrameData& frame);
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForModuleVariants(
    uint32_t variants, const AssetManifest& manifest);
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForFrame(
    const RenderFrameData& frame);
void renderAssetRequirementsForFrame(
    const RenderFrameData& frame,
    RenderAssetRequirements& requirements);

// Plans speculative puzzle work without reading any screen files. A loaded
// puzzle warms only its immediate successor. In the overworld, warm selectable
// flags on the active authored screen; crossing to another screen refreshes
// the plan. A legacy single-screen overworld uses its authored target list.
[[nodiscard]] std::vector<LevelLocation> puzzleAssetPrefetchTargets(
    const CampaignSession& campaign,
    const PlayerProfile& profile,
    const OverworldMap* overworldMap = nullptr);

} // namespace sokoban
