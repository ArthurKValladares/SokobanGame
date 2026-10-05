#include "engine/RenderFrameBuilder.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/ElevatorVisuals.hpp"
#include "engine/GateEffect.hpp"
#include "engine/MinecartGateVisuals.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/RenderFrameParts.hpp"
#include "engine/RotatorVisuals.hpp"
#include "engine/Rules.hpp"
#include "engine/TileTypes.hpp"
#include "engine/TurretRayTrace.hpp"
#include "engine/render/MirrorConfig.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/SelectorRenderConfig.hpp"
#include "engine/render/WaterGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

// The editor frame, and `tileVisual`.
//
// Split out of RenderFrameBuilder.cpp, where it sat in a second anonymous
// namespace below the gameplay builder. It shares 22 helpers with gameplay;
// those are in RenderFrameParts.hpp. What is here is the part that is only the
// editor's: previews of the tile being placed, pick targets, hover and
// selection highlighting, and the neighbouring overworld screens.
//
// `tileVisual` is public and lives here because the editor is its only caller
// inside this pair - the other callers are the thumbnail bake and the palette.

namespace sokoban {

using namespace renderFrameParts;

namespace {

CharacterType characterForStartTile(
    TileType tile,
    CharacterType legacyCharacter = CharacterType::Rogue)
{
    if (tile == TileType::Knight) {
        return CharacterType::Knight;
    }
    if (tile == TileType::Rogue) {
        return CharacterType::Rogue;
    }
    if (tile == TileType::Druid) {
        return CharacterType::Druid;
    }
    if (tile == TileType::Witch) {
        return CharacterType::Witch;
    }
    if (tile == TileType::Bard) {
        return CharacterType::Bard;
    }
    return legacyCharacter;
}

class EditorFrameBuild {
public:
    explicit EditorFrameBuild(
        const RenderFrameBuilder::EditorInput& input,
        FrameArena* arena = nullptr)
        : input_(input)
        , arena_(arena)
        , layers_(input.editor.documentLayers())
        , activeLayer_(input.editor.activeLayer())
        , layerCount_(static_cast<uint32_t>(layers_.size()))
        , waterLayer_(input.editor.waterLayer())
        , layerLocked_(input.editor.layerLocked())
    {
    }

    [[nodiscard]] RenderFrameData build()
    {
        RenderFrameData frame = initializeEditorFrame();
        appendOverworldNeighbors(frame);
        appendEditorLayers(frame);
#if SOKOBAN_ENABLE_DEBUG_UI
        appendDebugView(frame);
#endif
        appendEditorPreviews(frame);
        appendEditorCamera(frame);
        applyEditorScrollingMaterials(frame);
        return frame;
    }

private:
#if SOKOBAN_ENABLE_DEBUG_UI
    void appendDebugView(RenderFrameData& frame) const
    {
        if (!input_.showDebugView) {
            return;
        }
        if (arena_) {
            frame.debugItemOutlines = FrameArray<RenderFrameData::Tile>(
                *arena_, RenderFrameData::tileCapacity);
            frame.debugItemLinks = FrameArray<RenderFrameData::DebugItemLink>(
                *arena_, RenderFrameData::debugItemLinkCapacity);
        }
        const auto visible = [this](GridPosition3 cell) {
            return (!layerLocked_ || cell.z == static_cast<int>(activeLayer_)) &&
                pendingTileMoveSource() != cell;
        };
        const auto visualFor = [&](GridPosition3 cell, Vec3 color, bool plate) {
            const TileType type = plate
                ? input_.editor.documentPlateAt(cell).value_or(
                      documentTileAt(cell))
                : documentTileAt(cell);
            auto tile = tileVisual(type, cell, input_.manifest, input_.settings);
            // Gates use procedural energy geometry, not tileVisual's flat
            // fallback. Their debug proxy spans the same unit-height cell.
            if (type == TileType::Gate) {
                tile.position = { static_cast<float>(cell.x), static_cast<float>(cell.y) };
                tile.size = { 1.0f, 1.0f };
                tile.baseElevation = static_cast<float>(cell.z);
                tile.height = 1.0f;
                tile.model = cubeModel;
            }
            // Match the cart's authored orientation on its underlying rail.
            if (tileTypeIsMinecart(type)) {
                tile.modelRotationQuarterTurns = railOrientationQuarterTurns(
                    input_.editor.documentPlateAt(cell).value_or(TileType::Air))
                    .value_or(0);
            }
            tile.color = { color.x, color.y, color.z, 1.0f };
            tile.pickable = false;
            tile.showGrid = false;
            tile.affectsCameraFit = false;
            return tile;
        };
        const auto anchor = [](const RenderFrameData::Tile& tile) {
            return Vec3 {
                tile.position.x + tile.size.x * 0.5f,
                tile.position.y + tile.size.y * 0.5f,
                tile.baseElevation + tile.height,
            };
        };
        const auto outline = [&](const RenderFrameData::Tile& tile) {
            if (visible(tile.cell) &&
                frame.debugItemOutlines.size() < RenderFrameData::tileCapacity &&
                !std::ranges::any_of(frame.debugItemOutlines, [&](const auto& existing) {
                    return existing.cell == tile.cell && existing.model == tile.model;
                })) {
                frame.debugItemOutlines.push_back(tile);
            }
        };
        const auto connect = [&](const RenderFrameData::Tile& from,
                                 const RenderFrameData::Tile& to) {
            if (visible(from.cell) && visible(to.cell) &&
                from.cell != to.cell &&
                frame.debugItemLinks.size() <
                    RenderFrameData::debugItemLinkCapacity) {
                frame.debugItemLinks.push_back({
                    .from = anchor(from), .to = anchor(to), .color = from.color,
                });
            }
        };
        // These are the editor's 8-bit color groups, including unsaved edits.
        // Explicit device pressurePlates lists are rebuilt only when saving.
        const auto groups = input_.editor.linkGroups();
        for (const auto& group : groups) {
            if (group.hasDevice() && !group.pressurePlates.empty()) {
                for (GridPosition3 cell : group.pressurePlates) {
                    outline(visualFor(cell, group.color, true));
                }
                const auto devices = [&](const auto& cells, bool coveredPlate) {
                    for (GridPosition3 cell : cells) {
                        const auto device = visualFor(cell, group.color, coveredPlate);
                        outline(device);
                        for (GridPosition3 plate : group.pressurePlates) {
                            connect(visualFor(plate, group.color, true), device);
                        }
                    }
                };
                devices(group.gates, false);
                devices(group.rotators, true);
                devices(group.lockPlates, true);
                devices(group.elevators, false);
                devices(group.minecarts, false);
            }
            // Movable groups synchronize together; a star keeps large groups
            // readable without inventing pressure-plate links to the objects.
            if (group.objects.size() > 1) {
                const auto first = visualFor(group.objects.front(), group.color, false);
                for (GridPosition3 cell : group.objects) {
                    const auto object = visualFor(cell, group.color, false);
                    outline(object);
                    connect(first, object);
                }
            }
            if (group.portals.size() == 2) {
                const auto first = visualFor(group.portals[0], group.color, false);
                const auto second = visualFor(group.portals[1], group.color, false);
                outline(first);
                outline(second);
                connect(first, second);
            }
        }

        using Style = RenderFrameData::DebugItemLink::Style;
        const auto segment = [&](GridPosition3 fromCell, GridPosition3 toCell,
                                 Vec3 from, Vec3 to, Vec3 color, Style style) {
            if (visible(fromCell) && visible(toCell) &&
                frame.debugItemLinks.size() < RenderFrameData::debugItemLinkCapacity) {
                frame.debugItemLinks.push_back({ from, to,
                    { color.x, color.y, color.z, 1.0f }, style });
            }
        };
        const auto stop = [&](GridPosition3 cell, Vec3 position, Vec3 color) {
            segment(cell, cell, position, position, color, Style::Stop);
        };
        for (const auto& elevator : input_.editor.elevators()) {
            if (!visible(elevator.cell) || elevator.levels.size() < 2) {
                continue;
            }
            const auto platform = visualFor(elevator.cell, elevator.color, false);
            outline(platform);
            const auto point = [&](int layer) {
                Vec3 result = anchor(platform);
                result.z += static_cast<float>(layer - elevator.cell.z);
                return result;
            };
            for (std::size_t index = 1; index < elevator.levels.size(); ++index) {
                const int from = elevator.levels[index - 1];
                const int to = elevator.levels[index];
                segment({ elevator.cell.x, elevator.cell.y, from },
                    { elevator.cell.x, elevator.cell.y, to }, point(from), point(to),
                    elevator.color, Style::BidirectionalArrows);
            }
            for (int layer : elevator.levels) {
                stop({ elevator.cell.x, elevator.cell.y, layer }, point(layer), elevator.color);
            }
        }
        const auto railAt = [&](GridPosition3 cell) {
            return input_.editor.documentPlateAt(cell).value_or(documentTileAt(cell));
        };
        for (const auto& cart : input_.editor.minecarts()) {
            if (!visible(cart.cell)) {
                continue;
            }
            const auto platform = visualFor(cart.cell, cart.color, false);
            outline(platform);
            Level::MinecartRoute route;
            try {
                route = Level::buildMinecartRoute(cart, railAt, "level editor debug view");
            } catch (const std::runtime_error&) {
                // An unfinished branching track is not a playable cycle yet.
                // Keep the cart highlighted while its rails are being edited.
                continue;
            }
            const auto point = [&](GridPosition3 cell) {
                return Vec3 { static_cast<float>(cell.x) + 0.5f,
                    static_cast<float>(cell.y) + 0.5f, anchor(platform).z };
            };
            if (route.stops.size() > 1) {
                // A shuttle never travels beyond its last stop, even when
                // connected rail continues farther. Loops use the whole track.
                const std::size_t count = route.loop ? route.cells.size()
                    : route.stopCellIndices.back() + 1;
                const Style style = route.loop ? Style::Arrows : Style::BidirectionalArrows;
                for (std::size_t index = 1; index < count; ++index) {
                    const auto from = route.cells[index - 1];
                    const auto to = route.cells[index];
                    segment(from, to, point(from), point(to), cart.color, style);
                }
                if (route.loop) {
                    const auto from = route.cells.back();
                    const auto to = route.cells.front();
                    segment(from, to, point(from), point(to), cart.color, style);
                }
            }
            for (GridPosition3 cell : route.stops) {
                stop(cell, point(cell), cart.color);
            }
        }

        const auto occupied = [&](GridPosition3 cell) {
            return pendingTileMoveSource() != cell && tileTypeOccupiesLevelCell(documentTileAt(cell));
        };
        const auto rayCell = [&](GridPosition3 cell) {
            if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
                cell.x >= static_cast<int>(input_.editor.documentWidth()) ||
                cell.y >= static_cast<int>(input_.editor.documentHeight()) ||
                cell.z >= static_cast<int>(layerCount_)) {
                return rules::TurretRayCell::Blocked;
            }
            const TileType type = documentTileAt(cell);
            if (occupied(cell)) {
                return rules::TurretRayCell::Occupied;
            }
            if (type == TileType::Gate) {
                const auto gate = std::ranges::find(input_.editor.gates(), cell, &Level::Gate::cell);
                if (gate != input_.editor.gates().end()) {
                    const auto plates = input_.editor.linkedPressurePlates(gate->color);
                    const bool pressed = !plates.empty() && std::ranges::all_of(plates, occupied);
                    if (pressed != gate->startOpen) {
                        return rules::TurretRayCell::Open;
                    }
                }
                return rules::TurretRayCell::Blocked;
            }
            return type == TileType::Minecart || tileTypeAllowsEntity(type)
                ? rules::TurretRayCell::Open : rules::TurretRayCell::Blocked;
        };
        const auto portalCrossing = [&](GridPosition3 cell, MoveDirection direction)
            -> std::optional<Level::PortalCrossing> {
            const TileType entrance = input_.editor.documentPlateAt(cell).value_or(TileType::Air);
            const GridPosition incoming = rules::directionOffset(direction);
            if (!tileTypeIsPortal(entrance) || portalEdgeOffset(entrance) != incoming) {
                return std::nullopt;
            }
            for (const auto& group : groups) {
                if (group.portals.size() != 2 ||
                    std::ranges::find(group.portals, cell) == group.portals.end()) {
                    continue;
                }
                const auto exit = group.portals[group.portals[0] == cell ? 1 : 0];
                const TileType exitTile = input_.editor.documentPlateAt(exit).value_or(TileType::Air);
                if (!tileTypeIsPortal(exitTile) || pendingTileMoveSource() == exit) {
                    return std::nullopt;
                }
                const GridPosition edge = portalEdgeOffset(exitTile);
                const GridPosition outgoing { -edge.x, -edge.y };
                GridPosition rotated = incoming;
                int turns = 0;
                while (rotated != outgoing) {
                    rotated = { -rotated.y, rotated.x };
                    ++turns;
                }
                return Level::PortalCrossing { exit, outgoing, turns };
            }
            return std::nullopt;
        };
        for (uint32_t z = 0; z < layerCount_; ++z) {
            for (uint32_t y = 0; y < input_.editor.documentHeight(); ++y) {
                for (uint32_t x = 0; x < input_.editor.documentWidth(); ++x) {
                    const GridPosition3 cell { static_cast<int>(x), static_cast<int>(y), static_cast<int>(z) };
                    if (!visible(cell)) { continue; }
                    const TileType type = documentTileAt(cell);
                    if (const auto direction = rules::turretDirectionForTile(type)) {
                        const Vec3 color = input_.editor.objectLinkColorAt(cell)
                            .value_or(Vec3 { 1.0f, 0.25f, 0.12f });
                        outline(visualFor(cell, color, false));
                        std::vector<rules::TurretRaySegment> rays;
                        rules::traceTurretRay(cell, *direction, std::nullopt, rayCell, portalCrossing, &rays);
                        const auto point = [](GridPosition3 position, GridPosition edge) {
                            return Vec3 { static_cast<float>(position.x) + 0.5f + static_cast<float>(edge.x) * 0.5f,
                                static_cast<float>(position.y) + 0.5f + static_cast<float>(edge.y) * 0.5f,
                                static_cast<float>(position.z) + config::turretMuzzleElevation };
                        };
                        for (std::size_t index = 0; index < rays.size(); ++index) {
                            const auto& ray = rays[index];
                            Vec3 from = point(ray.from, ray.fromEdge);
                            if (index == 0) {
                                const auto offset = rules::directionOffset(*direction);
                                from.x += static_cast<float>(offset.x) * config::turretMuzzleForwardOffset;
                                from.y += static_cast<float>(offset.y) * config::turretMuzzleForwardOffset;
                            }
                            segment(ray.from, ray.to, from, point(ray.to, ray.toEdge), color, Style::Sightline);
                        }
                    }
                    const TileType plate = input_.editor.documentPlateAt(cell).value_or(type);
                    if (const auto direction = rules::conveyorDirectionForTile(plate)) {
                        constexpr Vec3 color { 0.22f, 0.70f, 1.0f };
                        Vec3 from { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                            static_cast<float>(z) + config::surfaceEntityHeight };
                        const auto offset = rules::directionOffset(*direction);
                        const Vec3 delta { static_cast<float>(offset.x) * 0.38f, static_cast<float>(offset.y) * 0.38f, 0.0f };
                        segment(cell, cell, from - delta, from + delta, color, Style::Arrows);
                    }
                }
            }
        }
    }
#endif

    [[nodiscard]] std::optional<GridPosition3> pendingTileMoveSource() const
    {
        const std::optional<LevelEditor::MoveObject>& move =
            input_.editor.pendingMove();
        return move && move->kind == LevelEditor::MoveObject::Kind::Tile
            ? std::optional<GridPosition3> { move->source }
            : std::nullopt;
    }

    [[nodiscard]] std::optional<uint32_t> pendingSelectorMoveId() const
    {
        const std::optional<LevelEditor::MoveObject>& move =
            input_.editor.pendingMove();
        return move &&
                move->kind == LevelEditor::MoveObject::Kind::ScreenSelector
            ? std::optional<uint32_t> { move->selectorId }
            : std::nullopt;
    }

    [[nodiscard]] RenderFrameData initializeEditorFrame() const
    {
        RenderFrameData frame = arena_ != nullptr
            ? RenderFrameData(*arena_)
            : RenderFrameData {};
        frame.viewMode = RenderViewMode::Isometric3D;
        const CameraAngles angles = input_.editor.cameraAngles().value_or(CameraAngles {});
        frame.cameraPitchDegrees = angles.pitchDegrees;
        frame.cameraYawDegrees = angles.yawDegrees;
        frame.lighting = input_.settings.renderLighting();
        frame.gridOverlay = input_.settings.renderGridOverlay();
        frame.outputTransform = input_.settings.renderOutputTransform();
        frame.waterRendering = input_.settings.water;
        frame.levelWidth = input_.editor.documentWidth();
        frame.levelHeight = input_.editor.documentHeight();
        frame.levelDepth = std::max(layerCount_, 1U);
        // Ordinary levels can be expanded by painting the one-cell border.
        // Overworld component dimensions are fixed by layout.json, and a
        // border would make a displayed neighbor look editable.
        frame.gridPickBorder = input_.editor.editingOverworld() ? 0U : 1U;
        // Previews the edited screen's own map, so what the brush paints is
        // what is on screen. A scratch document belongs to no screen and falls
        // back to the shared map.
        frame.groundSplat = input_.overworldScreen
            ? groundSplatTexturesForOverworldScreen(
                  [&manifest = input_.manifest](std::string_view name) {
                      return manifest.findTextureIdByName(name);
                  },
                  *input_.overworldScreen)
            : groundSplatTextures(input_.manifest, input_.levelLocation);
        frame.waterAnimationTimeSeconds = input_.worldAnimationTimeSeconds;
        frame.effectAnimationTimeSeconds = input_.worldAnimationTimeSeconds;
        frame.animationTransitionTimeSeconds =
            input_.animationTransitionTimeSeconds;
        frame.tiles.reserve(
            static_cast<std::size_t>(frame.levelWidth) *
                frame.levelHeight *
                layerCount_ *
                2 +
            input_.editor.decorations().size() + 2);

        return frame;
    }

    void appendEditorCamera(RenderFrameData& frame) const
    {
        const auto gameplayExtent = gameplayExtentForTiles(
            frame.levelWidth,
            frame.levelHeight,
            layerCount_,
            [this](uint32_t x, uint32_t y, uint32_t z) {
                return documentTileAt(x, y, z);
            });
        frame.cameraExtent =
            gameplayExtent.value_or(RenderFrameData::CameraExtent {});
        frame.waterGridBounds = waterGridBoundsFor(gameplayExtent);
    }

    [[nodiscard]] TileType documentTileAt(
        uint32_t x,
        uint32_t y,
        uint32_t z) const
    {
        if (z >= layers_.size() ||
            y >= layers_[z].size() ||
            x >= layers_[z][y].size()) {
            return TileType::Air;
        }
        const TileType authored =
            charToTileType(layers_[z][y][x]).value_or(TileType::Air);
        if (authored == TileType::Air && waterLayer_ == z) {
            return TileType::Water;
        }
        return authored;
    }

    [[nodiscard]] TileType documentTileAt(GridPosition3 position) const
    {
        if (position.x < 0 || position.y < 0 || position.z < 0) {
            return TileType::Air;
        }
        return documentTileAt(
            static_cast<uint32_t>(position.x),
            static_cast<uint32_t>(position.y),
            static_cast<uint32_t>(position.z));
    }

    [[nodiscard]] static TileType definitionTileAt(
        const Level::Definition& definition,
        GridPosition3 position)
    {
        if (position.x < 0 || position.y < 0 || position.z < 0 ||
            position.z >= static_cast<int>(definition.layers.size())) {
            return TileType::Air;
        }
        const std::vector<std::string>& layer =
            definition.layers[static_cast<std::size_t>(position.z)];
        if (position.y >= static_cast<int>(layer.size()) ||
            position.x >= static_cast<int>(
                layer[static_cast<std::size_t>(position.y)].size())) {
            return TileType::Air;
        }
        const TileType authored = charToTileType(
            layer[static_cast<std::size_t>(position.y)]
                 [static_cast<std::size_t>(position.x)])
                                      .value_or(TileType::Air);
        if (authored == TileType::Air && definition.waterLayer &&
            *definition.waterLayer == static_cast<uint32_t>(position.z)) {
            return TileType::Water;
        }
        return authored;
    }

    void appendOverworldNeighborTile(
        RenderFrameData& frame,
        const Level::Definition& definition,
        GridPosition origin,
        GridPosition3 localCell,
        TileType tile) const
    {
        if (tile == TileType::Air) {
            return;
        }
        const GridPosition3 cell {
            localCell.x + origin.x,
            localCell.y + origin.y,
            localCell.z,
        };
        const auto neighborTileAt = [&](GridPosition3 globalCell) {
            return definitionTileAt(definition, {
                globalCell.x - origin.x,
                globalCell.y - origin.y,
                globalCell.z,
            });
        };
        if (tile == TileType::Water) {
            appendWaterCellSurface(
                frame,
                cell,
                false,
                shorelineMaskForWaterCell(
                    cell,
                    [&](GridPosition3 adjacent) {
                        return tileTypeIsSolidBlock(
                            neighborTileAt(adjacent));
                    }),
                false);
            return;
        }
        if (tile == TileType::Ladder) {
            const std::size_t firstTile = frame.tiles.size();
            appendLadderRungsForCell(
                frame, cell, neighborTileAt, false);
            for (std::size_t index = firstTile;
                 index < frame.tiles.size();
                 ++index) {
                frame.tiles[index].pickable = false;
                frame.tiles[index].affectsCameraFit = false;
            }
            return;
        }
        if (tile == TileType::MinecartGate) {
            const auto covered = std::ranges::find(
                definition.plates, localCell, &Level::Plate::cell);
            appendMinecartGateVisual(frame, cell,
                covered != definition.plates.end() ? covered->tile : TileType::Air);
            return;
        }
        if (tile == TileType::Gate) {
            const auto found = std::ranges::find(
                definition.gates, localCell, &Level::Gate::cell);
            Level::Gate gate = found != definition.gates.end()
                ? *found
                : Level::Gate { .cell = localCell };
            gate.cell = cell;
            appendGateEffect(
                frame,
                gate,
                input_.manifest,
                gate.startOpen ? config::gateStartOpenEditorOpacity : 1.0f,
                input_.worldAnimationTimeSeconds);
            return;
        }

        RenderFrameData::Tile renderTile = tileVisual(
            tile, cell, input_.manifest, input_.settings);
        if (tileTypeIsPlayerStart(tile)) {
            renderTile.model = input_.manifest.characterModel(
                characterForStartTile(
                    tile,
                    definition.character.value_or(CharacterType::Rogue)));
        }
        if (tileTypeIsRotator(tile)) {
            const auto found = std::ranges::find(
                definition.rotators, localCell, &Level::Rotator::cell);
            const Vec3 color = found != definition.rotators.end()
                ? found->color
                : Level::Rotator {}.color;
            renderTile.color = { color.x, color.y, color.z, 1.0f };
        }
        if (tileTypeIsLockPlate(tile)) {
            const auto found = std::ranges::find(
                definition.lockPlates, localCell, &Level::LockPlate::cell);
            const Vec3 color = found != definition.lockPlates.end()
                ? found->color
                : Level::LockPlate {}.color;
            renderTile.color = { color.x, color.y, color.z, 1.0f };
        }
        if (tileTypeIsElevator(tile)) {
            const auto found = std::ranges::find(
                definition.elevators, localCell, &Level::Elevator::cell);
            renderTile.color = elevatorPlatformColor(
                found != definition.elevators.end()
                    ? found->color
                    : Level::Elevator {}.color,
                1.0f);
        }
        if (tileTypeIsMinecart(tile)) {
            const auto found = std::ranges::find(
                definition.minecarts, localCell, &Level::Minecart::cell);
            const Vec3 color = found != definition.minecarts.end()
                ? found->color
                : Level::Minecart {}.color;
            renderTile.color = { color.x, color.y, color.z, 1.0f };
            const auto stop = std::ranges::find(
                definition.plates, localCell, &Level::Plate::cell);
            renderTile.modelRotationQuarterTurns = railOrientationQuarterTurns(
                stop != definition.plates.end()
                    ? stop->tile
                    : TileType::Air)
                .value_or(0);
        }
        if (tileTypeIsPortal(tile)) {
            const auto portal = std::ranges::find(
                definition.portals, localCell, &Level::Portal::cell);
            if (portal != definition.portals.end()) {
                renderTile.color = {
                    portal->color.x, portal->color.y, portal->color.z, 1.0f
                };
            }
        }
        renderTile.pickable = false;
        renderTile.affectsCameraFit = false;
        const bool animatedActor =
            tileTypeIsPlayerStart(tile) || tile == TileType::Enemy;
        const AnimationUse editorUse = tile == TileType::Enemy
            ? AnimationUse::EditorEnemyIdle
            : AnimationUse::EditorPlayerIdle;
        renderTile.animation = animatedActor
            ? animationFor(
                  input_.animations,
                  editorUse,
                  input_.manifest.playerIdleAnimation())
            : noAnimation;
        renderTile.animationTimeSeconds = animatedActor
            ? animationTimeFor(
                  input_.animations,
                  editorUse,
                  input_.worldAnimationTimeSeconds)
            : 0.0f;
        if (tileTypeIsMovableObject(tile)) {
            const auto link = std::ranges::find(
                definition.objectLinks,
                localCell,
                &Level::ObjectLink::cell);
            if (link != definition.objectLinks.end()) {
                appendLinkedObjectAura(frame, renderTile, link->color);
            }
        }
        if (tileTypeIsPortal(tile)) {
            appendPortalVisual(
                frame,
                renderTile,
                tile,
                input_.worldAnimationTimeSeconds,
                input_.manifest.findTextureIdByName(
                    config::turretGlowTextureName));
        } else {
            frame.tiles.push_back(renderTile);
        }
    }

    void appendOverworldNeighbors(RenderFrameData& frame) const
    {
        for (const RenderFrameBuilder::EditorInput::OverworldNeighbor& neighbor :
             input_.overworldNeighbors) {
            if (neighbor.definition == nullptr) {
                continue;
            }
            const Level::Definition& definition = *neighbor.definition;
            if (frame.groundSplatRegionCount <
                RenderFrameData::groundSplatRegionCapacity) {
                frame.groundSplatRegions[frame.groundSplatRegionCount++] = {
                    .origin = neighbor.origin,
                    .width = neighbor.width,
                    .height = neighbor.height,
                    .textures = groundSplatTexturesForOverworldScreen(
                        [&manifest = input_.manifest](std::string_view name) {
                            return manifest.findTextureIdByName(name);
                        },
                        neighbor.screen),
                };
            }

            for (std::size_t z = 0; z < definition.layers.size(); ++z) {
                if (layerLocked_ && z != activeLayer_) {
                    continue;
                }
                const std::vector<std::string>& layer = definition.layers[z];
                for (std::size_t y = 0; y < layer.size(); ++y) {
                    for (std::size_t x = 0; x < layer[y].size(); ++x) {
                        const GridPosition3 localCell {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        };
                        appendOverworldNeighborTile(
                            frame,
                            definition,
                            neighbor.origin,
                            localCell,
                            definitionTileAt(definition, localCell));
                        const auto covered = std::ranges::find(
                            definition.plates,
                            localCell,
                            &Level::Plate::cell);
                        if (covered != definition.plates.end()) {
                            appendOverworldNeighborTile(
                                frame,
                                definition,
                                neighbor.origin,
                                localCell,
                                covered->tile);
                        }
                    }
                }
            }

            std::vector<Level::Decoration> decorations =
                definition.decorations;
            for (Level::Decoration& decoration : decorations) {
                decoration.position.x +=
                    static_cast<float>(neighbor.origin.x);
                decoration.position.y +=
                    static_cast<float>(neighbor.origin.y);
            }
            appendDecorations(
                frame,
                decorations,
                input_.manifest,
                std::nullopt,
                std::nullopt,
                false);

            for (Level::ScreenSelector selector : definition.selectors) {
                if (layerLocked_ &&
                    selector.cell.z != static_cast<int>(activeLayer_)) {
                    continue;
                }
                selector.cell.x += neighbor.origin.x;
                selector.cell.y += neighbor.origin.y;
                appendSelector(
                    frame,
                    selector,
                    input_.manifest,
                    input_.selectorState,
                    false,
                    false);
            }
        }
    }

    void appendEditorTile(
        RenderFrameData& frame,
        int x,
        int y,
        int z,
        TileType tile,
        bool preview,
        bool pickOnly = false) const
    {
        if (tile == TileType::Air) {
            return;
        }
        if (tile == TileType::Water) {
            if (pickOnly) {
                frame.tiles.push_back({
                    .cell = { x, y, z },
                    .position = {
                        static_cast<float>(x),
                        static_cast<float>(y),
                    },
                    .baseElevation = static_cast<float>(z) + 1.0f -
                        config::waterDepthBelowGround,
                    .pickOnly = true,
                    .showGrid = false,
                    .affectsCameraFit = false,
                });
                return;
            }
            const GridPosition3 waterCell { x, y, z };
            appendWaterCellSurface(
                frame,
                waterCell,
                preview,
                shorelineMaskForWaterCell(
                    waterCell,
                    [this](GridPosition3 position) {
                        return tileTypeIsSolidBlock(
                            documentTileAt(position));
                    }));
            return;
        }
        if (tile == TileType::Ladder) {
            if (pickOnly) {
                frame.tiles.push_back({
                    .cell = { x, y, z },
                    .position = {
                        static_cast<float>(x),
                        static_cast<float>(y),
                    },
                    .baseElevation = static_cast<float>(z) + 1.0f,
                    .pickOnly = true,
                    .showGrid = false,
                    .affectsCameraFit = false,
                });
                return;
            }
            const auto tileAtForLadder =
                [this, preview, x, y, z](GridPosition3 position) {
                    if (preview &&
                        position.x == x &&
                        position.y == y &&
                        position.z == z) {
                        return TileType::Ladder;
                    }
                    return documentTileAt(position);
                };
            appendLadderRungsForCell(
                frame,
                { x, y, z },
                tileAtForLadder,
                preview);
            return;
        }
        if (tile == TileType::MinecartGate) {
            const GridPosition3 cell { x, y, z };
            if (!pickOnly) {
                TileType rail = input_.editor.documentPlateAt(cell).value_or(TileType::Air);
                if (!tileTypeIsRail(rail)) {
                    rail = documentTileAt(cell);
                }
                appendMinecartGateVisual(frame, cell, rail, 0.0f,
                    preview ? 0.68f : 1.0f, true);
                frame.tiles.back().isEditorPreview = preview;
            } else {
                frame.tiles.push_back({
                    .cell = cell,
                    .position = { static_cast<float>(x), static_cast<float>(y) },
                    .baseElevation = static_cast<float>(z),
                    .height = 1.0f,
                    .pickOnly = true,
                    .showGrid = false,
                });
            }
            return;
        }
        if (tile == TileType::Gate) {
            const GridPosition3 cell { x, y, z };
            const auto found = std::ranges::find(
                input_.editor.gates(), cell, &Level::Gate::cell);
            const Level::Gate gate = found != input_.editor.gates().end()
                ? *found
                : Level::Gate { .cell = cell };
            if (!pickOnly) {
                // A start-open gate is drawn faded: it is empty space
                // until its plates are pressed.
                appendGateEffect(
                    frame,
                    gate,
                    input_.manifest,
                    (preview ? 0.68f : 1.0f) *
                        (gate.startOpen
                                ? config::gateStartOpenEditorOpacity
                                : 1.0f),
                    input_.worldAnimationTimeSeconds);
            }
            frame.tiles.push_back({
                .cell = cell,
                .position = {
                    static_cast<float>(x),
                    static_cast<float>(y),
                },
                .color = { 0.0f, 0.0f, 0.0f, 0.0f },
                .baseElevation = static_cast<float>(z),
                .height = 1.0f,
                .pickOnly = true,
                .showGrid = false,
                .isEditorPreview = preview,
            });
            return;
        }

        // Shared with the thumbnail bake, so a palette icon cannot end up
        // looking different from the tile the editor draws.
        RenderFrameData::Tile renderTile = tileVisual(
            tile, { x, y, z }, input_.manifest, input_.settings);
        if (tileTypeIsPortal(tile)) {
            const auto portal = std::ranges::find(
                input_.editor.portals(),
                GridPosition3 { x, y, z },
                &Level::Portal::cell);
            if (portal != input_.editor.portals().end()) {
                renderTile.color = {
                    portal->color.x, portal->color.y, portal->color.z, 1.0f
                };
            }
        }
        if (tileTypeIsSignalSource(tile)) {
            // In the editor a plate shows its own link color: the color is
            // the link (see LevelEditor::linkGroups).
            const auto color = std::ranges::find(
                input_.editor.pressurePlateColors(),
                GridPosition3 { x, y, z },
                &Level::LinkColor::cell);
            if (color != input_.editor.pressurePlateColors().end()) {
                renderTile.color = {
                    color->color.x,
                    color->color.y,
                    color->color.z,
                    1.0f,
                };
            }
        }
        if (tileTypeIsElevator(tile)) {
            const GridPosition3 cell { x, y, z };
            const auto found = std::ranges::find(
                input_.editor.elevators(), cell, &Level::Elevator::cell);
            renderTile.color = elevatorPlatformColor(
                found != input_.editor.elevators().end()
                    ? found->color
                    : Level::Elevator {}.color,
                1.0f);
        }
        if (tileTypeIsMinecart(tile)) {
            const GridPosition3 cell { x, y, z };
            const auto found = std::ranges::find(
                input_.editor.minecarts(), cell, &Level::Minecart::cell);
            const Vec3 color = found != input_.editor.minecarts().end()
                ? found->color
                : Level::Minecart {}.color;
            renderTile.color = { color.x, color.y, color.z, 1.0f };
            renderTile.modelRotationQuarterTurns = railOrientationQuarterTurns(
                input_.editor.documentPlateAt(cell).value_or(TileType::Air))
                .value_or(0);
        }
        if (tileTypeIsRotator(tile)) {
            const GridPosition3 cell { x, y, z };
            const auto found = std::ranges::find(
                input_.editor.rotators(), cell, &Level::Rotator::cell);
            const Vec3 color = found != input_.editor.rotators().end()
                ? found->color
                : Level::Rotator {}.color;
            renderTile.color = { color.x, color.y, color.z, 1.0f };
        }
        if (tileTypeIsLockPlate(tile)) {
            const GridPosition3 cell { x, y, z };
            const auto found = std::ranges::find(
                input_.editor.lockPlates(), cell, &Level::LockPlate::cell);
            const Vec3 color = found != input_.editor.lockPlates().end()
                ? found->color
                : Level::LockPlate {}.color;
            renderTile.color = { color.x, color.y, color.z, 1.0f };
        }
        if (tileTypeIsPlayerStart(tile)) {
            renderTile.model = input_.manifest.characterModel(
                characterForStartTile(tile, input_.editor.character()));
        }
        renderTile.baseElevation += preview ? 0.02f : 0.0f;
        renderTile.pickOnly = pickOnly;
        renderTile.isEditorPreview = preview;
        const bool animatedActor =
            tileTypeIsPlayerStart(tile) || tile == TileType::Enemy;
        const AnimationUse editorUse = tile == TileType::Enemy
            ? AnimationUse::EditorEnemyIdle
            : AnimationUse::EditorPlayerIdle;
        renderTile.animation = animatedActor
            ? animationFor(
                  input_.animations,
                  editorUse,
                  input_.manifest.playerIdleAnimation())
            : noAnimation;
        renderTile.animationTimeSeconds = animatedActor
            ? animationTimeFor(
                  input_.animations,
                  editorUse,
                  input_.worldAnimationTimeSeconds)
            : 0.0f;
        if (!preview && !pickOnly && tileTypeIsMovableObject(tile)) {
            if (const std::optional<Vec3> linkColor =
                    input_.editor.objectLinkColorAt({ x, y, z })) {
                appendLinkedObjectAura(frame, renderTile, *linkColor);
            }
        }
        if (tileTypeIsPortal(tile)) {
            appendPortalVisual(
                frame,
                renderTile,
                tile,
                input_.worldAnimationTimeSeconds,
                input_.manifest.findTextureIdByName(
                    config::turretGlowTextureName));
        } else {
            frame.tiles.push_back(renderTile);
        }
    }

    static void appendEditorPickCell(
        RenderFrameData& frame,
        GridPosition3 cell,
        bool affectsCameraFit = false)
    {
        frame.tiles.push_back({
            .cell = cell,
            .position = {
                static_cast<float>(cell.x),
                static_cast<float>(cell.y),
            },
            // Pick the visible top of the edited cell. Picking its lower plane
            // introduces perspective parallax against a block preview.
            .baseElevation = static_cast<float>(cell.z) + 1.0f,
            .pickOnly = true,
            .showGrid = false,
            .affectsCameraFit = affectsCameraFit,
        });
    }

    void appendExpansionPickCells(RenderFrameData& frame) const
    {
        if (input_.editor.editingOverworld()) {
            return;
        }
        const int expansionPickLayer = layerLocked_
            ? static_cast<int>(activeLayer_)
            : 0;
        const int editorWidth = static_cast<int>(frame.levelWidth);
        const int editorHeight = static_cast<int>(frame.levelHeight);
        for (int x = -1; x <= editorWidth; ++x) {
            appendEditorPickCell(
                frame, { x, -1, expansionPickLayer }, true);
            appendEditorPickCell(
                frame, { x, editorHeight, expansionPickLayer }, true);
        }
        for (int y = 0; y < editorHeight; ++y) {
            appendEditorPickCell(
                frame, { -1, y, expansionPickLayer }, true);
            appendEditorPickCell(
                frame, { editorWidth, y, expansionPickLayer }, true);
        }
    }

    void appendAuthoredCells(RenderFrameData& frame) const
    {
        for (uint32_t z = 0; z < layerCount_; ++z) {
            if (layerLocked_ && z != activeLayer_) {
                continue;
            }
            for (uint32_t y = 0; y < frame.levelHeight; ++y) {
                for (uint32_t x = 0; x < frame.levelWidth; ++x) {
                    const TileType tile = documentTileAt(x, y, z);
                    if (layerLocked_ && tile == TileType::Air) {
                        appendEditorPickCell(frame, {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        });
                        continue;
                    }
                    if (!layerLocked_ && z == 0 &&
                        tile == TileType::Air &&
                        columnIsEmpty(x, y)) {
                        appendEditorPickCell(frame, {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            0,
                        });
                        continue;
                    }
                    const bool deletePreviewTarget =
                        input_.deleting && input_.hoverCell &&
                        *input_.hoverCell == GridPosition3 {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                    };
                    const bool movePreviewSource =
                        pendingTileMoveSource() ==
                        std::optional<GridPosition3>({
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        });
                    appendEditorTile(
                        frame,
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                        tile,
                        false,
                        deletePreviewTarget || movePreviewSource);
                    appendCoveredPlate(frame, {
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                    });
                    if (tileTypeIsElevator(tile)) {
                        appendElevatorStops(frame, {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        });
                    }
                }
            }
        }
    }

    // The other stops of the elevator authored at `cell`, as dithered
    // platforms that cannot be picked, so the route reads in the editor.
    void appendElevatorStops(RenderFrameData& frame, GridPosition3 cell) const
    {
        const auto found = std::ranges::find(
            input_.editor.elevators(), cell, &Level::Elevator::cell);
        if (found == input_.editor.elevators().end()) {
            return;
        }
        for (const int stop : found->levels) {
            if (stop == cell.z) {
                continue;
            }
            RenderFrameData::Tile ghost = tileVisual(
                TileType::Elevator,
                { cell.x, cell.y, stop },
                input_.manifest,
                input_.settings);
            ghost.color = elevatorPlatformColor(found->color, 1.0f);
            ghost.isEditorPreview = true;
            ghost.pickable = false;
            ghost.showGrid = false;
            ghost.affectsCameraFit = false;
            frame.tiles.push_back(ghost);
        }
    }

    // A plate authored beneath a unit or mirror. It draws under its occupant
    // but never takes picks from it: hover, delete and move address the top.
    void appendCoveredPlate(RenderFrameData& frame, GridPosition3 cell) const
    {
        const auto covered = std::ranges::find(
            input_.editor.coveredPlates(), cell, &Level::Plate::cell);
        if (covered == input_.editor.coveredPlates().end()) {
            return;
        }
        const std::size_t first = frame.tiles.size();
        appendEditorTile(frame, cell.x, cell.y, cell.z, covered->tile, false);
        for (std::size_t index = first; index < frame.tiles.size(); ++index) {
            frame.tiles[index].pickable = false;
        }
    }

    [[nodiscard]] bool columnIsEmpty(uint32_t x, uint32_t y) const
    {
        for (uint32_t layer = 1; layer < layerCount_; ++layer) {
            if (documentTileAt(x, y, layer) != TileType::Air) {
                return false;
            }
        }
        return true;
    }

    void appendEditorWater(RenderFrameData& frame) const
    {
        if (waterLayer_ &&
            (!layerLocked_ || *waterLayer_ == activeLayer_)) {
            appendUnboundedWaterExterior(
                frame,
                frame.levelWidth,
                frame.levelHeight,
                *waterLayer_,
                false,
                [this](GridPosition3 position) {
                    return tileTypeIsSolidBlock(documentTileAt(position));
                });
        }

        for (uint32_t z = 0; z < layerCount_; ++z) {
            if (layerLocked_ && z != activeLayer_) {
                continue;
            }
            appendWaterEdgeFaces(
                frame,
                frame.levelWidth,
                frame.levelHeight,
                static_cast<float>(z) + 1.0f,
                [this, &frame, z](GridPosition position) {
                    if (position.x < 0 ||
                        position.y < 0 ||
                        position.x >=
                            static_cast<int>(frame.levelWidth) ||
                        position.y >=
                            static_cast<int>(frame.levelHeight)) {
                        return waterLayer_ == z;
                    }
                    return documentTileAt(
                               static_cast<uint32_t>(position.x),
                               static_cast<uint32_t>(position.y),
                               z) == TileType::Water;
                },
                [](GridPosition) { return true; });
        }
    }

    void appendEditorLayers(RenderFrameData& frame) const
    {
        appendExpansionPickCells(frame);
        appendAuthoredCells(frame);
        appendEditorWater(frame);
        appendDecorations(
            frame,
            input_.editor.decorations(),
            input_.manifest,
            input_.editor.selectedDecorationIndex(),
            input_.hoverDecoration,
            true);
        const std::optional<uint32_t> movingSelector =
            pendingSelectorMoveId();
        for (const Level::ScreenSelector& selector :
            input_.editor.selectors()) {
            if (layerLocked_ &&
                selector.cell.z != static_cast<int>(activeLayer_)) {
                continue;
            }
            const bool hoveredMoveSource =
                input_.selectingMoveSource && input_.hoverCell &&
                selector.cell == *input_.hoverCell;
            appendSelector(
                frame,
                selector,
                input_.manifest,
                input_.selectorState,
                movingSelector == selector.id || hoveredMoveSource,
                true);
        }
    }

    void appendEditorPreviews(RenderFrameData& frame) const
    {
        const std::optional<uint32_t> movingSelector =
            pendingSelectorMoveId();
        if (const std::optional<GridPosition3> source =
                pendingTileMoveSource();
            source && input_.editorPreviewTile) {
            appendEditorTile(
                frame,
                source->x,
                source->y,
                source->z,
                *input_.editorPreviewTile,
                true);
        }
        if (!movingSelector &&
            (input_.editor.tool() == LevelEditor::Tool::Tiles ||
                input_.deleting || input_.editorPreviewTile) &&
            input_.hoverCell &&
            input_.hoverCell->z >= 0 &&
            input_.hoverCell->x >= -1 &&
            input_.hoverCell->y >= -1 &&
            input_.hoverCell->x <= static_cast<int>(frame.levelWidth) &&
            input_.hoverCell->y <= static_cast<int>(frame.levelHeight)) {
            const TileType selectedTile = input_.deleting
                ? TileType::Air
                : input_.editorPreviewTile.value_or(
                    input_.editor.selectedTile());
            const TileType hoveredTile =
                documentTileAt(*input_.hoverCell);
            const TileType previewTile = selectedTile == TileType::Air
                ? hoveredTile
                : selectedTile;
            if (input_.hoverCell != pendingTileMoveSource()) {
                appendEditorTile(
                    frame,
                    input_.hoverCell->x,
                    input_.hoverCell->y,
                    input_.hoverCell->z,
                    previewTile,
                    true);
            }
        }

        if (input_.hoverCell && movingSelector) {
            const auto source = std::ranges::find(
                input_.editor.selectors(),
                *movingSelector,
                &Level::ScreenSelector::id);
            if (source != input_.editor.selectors().end() &&
                source->cell != *input_.hoverCell) {
                Level::ScreenSelector preview = *source;
                preview.cell = *input_.hoverCell;
                appendSelector(
                    frame,
                    preview,
                    input_.manifest,
                    input_.selectorState,
                    true);
            }
        }

        if (input_.editor.tool() == LevelEditor::Tool::Decorations &&
            !input_.editor.selectedDecorationModel().empty() &&
            !input_.hoverDecoration &&
            input_.hoverCell &&
            input_.hoverCell->x >= 0 &&
            input_.hoverCell->y >= 0 &&
            input_.hoverCell->x < static_cast<int>(frame.levelWidth) &&
            input_.hoverCell->y < static_cast<int>(frame.levelHeight) &&
            input_.hoverCell->z >= 0) {
            frame.tiles.push_back(decorationVisual(
                {
                    .model = input_.editor.selectedDecorationModel(),
                    .position = {
                        static_cast<float>(input_.hoverCell->x) + 0.5f,
                        static_cast<float>(input_.hoverCell->y) + 0.5f,
                        static_cast<float>(input_.hoverCell->z),
                    },
                },
                input_.manifest,
                true));
        }
    }

    void applyEditorScrollingMaterials(RenderFrameData& frame) const
    {
        for (RenderFrameData::Tile& tile : frame.tiles) {
            if (!tile.model.isCube() &&
                input_.manifest.model(tile.model).hasScrollingMaterial()) {
                tile.beltScrollOffset = input_.conveyorBeltScrollOffset;
            }
        }
    }

    const RenderFrameBuilder::EditorInput& input_;
    FrameArena* arena_ = nullptr;
    const Level::LayerRows& layers_;
    uint32_t activeLayer_ = 0;
    uint32_t layerCount_ = 0;
    std::optional<uint32_t> waterLayer_;
    bool layerLocked_ = false;
};

} // namespace

RenderFrameData RenderFrameBuilder::buildEditor(const EditorInput& input)
{
    return EditorFrameBuild(input).build();
}

RenderFrameData RenderFrameBuilder::buildEditor(
    const EditorInput& input,
    FrameArena& arena)
{
    return EditorFrameBuild(input, &arena).build();
}

RenderFrameData::Tile tileVisual(
    TileType tile,
    GridPosition3 cell,
    const AssetManifest& manifest,
    const PresentationSettings& settings)
{
    const bool surfaceEntity = tileTypeIsSurfaceEntity(tile);
    const bool rail = tileTypeIsRail(tile);
    const bool conveyor = tileTypeIsConveyor(tile);
    const bool rotator = tileTypeIsRotator(tile) || tileTypeIsLockPlate(tile);
    const bool elevator = tileTypeIsElevator(tile);
    const float tileSize = rail
        ? 1.0f
        : rotator
        ? config::rotatorPlateWidthDepth
        : tile == TileType::Button
        ? settings.geometry.surfaceEntityWidthDepth * 0.6f
        : (surfaceEntity ? settings.geometry.surfaceEntityWidthDepth : 1.0f);
    const float centeredOffset = (1.0f - tileSize) * 0.5f;

    Vec4 color = tileColor(tile);
    if (tileTypeIsPlayerStart(tile) || tile == TileType::Enemy ||
        tileTypeIsTurret(tile)) {
        color = { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    if (tile == TileType::Ice) {
        color.w = config::iceTintAlpha;
    }
    if (elevator) {
        color = elevatorPlatformColor(Level::Elevator {}.color, 1.0f);
    }

    RenderFrameData::Tile visual {
        .cell = cell,
        .position = {
            static_cast<float>(cell.x) + centeredOffset,
            static_cast<float>(cell.y) + centeredOffset,
        },
        .size = { tileSize, tileSize },
        .color = color,
        // An elevator platform is a thin slab whose top is flush with the
        // top of its layer (see ElevatorVisuals.hpp).
        .baseElevation = elevator
            ? static_cast<float>(cell.z) + 1.0f -
                config::elevatorPlatformHeight
            : static_cast<float>(cell.z),
        // Conveyors are the reason this is shared: they are neither a surface
        // entity nor a solid block, so anything that only tests those two ends
        // up drawing them flat.
        .height = rotator
            ? config::rotatorPlateHeight
            : elevator
            ? config::elevatorPlatformHeight
            : tile == TileType::Button
            ? settings.geometry.surfaceEntityHeight * 2.0f
            : surfaceEntity
            ? settings.geometry.surfaceEntityHeight
            : (conveyor
                    ? config::conveyorTileHeight
                    : (tileTypeIsSolidBlock(tile) ||
                              tileTypeOccupiesLevelCell(tile) ||
                              tileTypeIsMirror(tile) ||
                              tileTypeIsDecorative(tile)
                            ? 1.0f
                            : 0.0f)),
        .blurBehind = tile == TileType::Ice,
        .showGrid = !tileTypeIsPlayerStart(tile),
        .affectsCameraFit = tileTypeAffectsCameraFit(tile),
        .model = tileTypeIsPlayerStart(tile)
            ? manifest.characterModel(characterForStartTile(tile))
            : manifest.modelForTile(tile),
        .animation = tileTypeIsPlayerStart(tile) || tile == TileType::Enemy
            ? manifest.playerIdleAnimation()
            : noAnimation,
        .animationInstanceId = tileTypeIsPlayerStart(tile) || tile == TileType::Enemy
            ? authoredAnimationInstance(tile, cell)
            : uint64_t { 0 },
        // Conveyors, turrets, mirrors, and rails carry an orientation in their
        // tile type. Each family rotates one shared model.
        .modelRotationQuarterTurns =
            rules::conveyorDirectionForTile(tile)
            ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
            : (rules::turretDirectionForTile(tile)
                    ? facingQuarterTurns(*rules::turretDirectionForTile(tile))
                    : railOrientationQuarterTurns(tile).value_or(
                          mirrorOrientationQuarterTurns(tile).value_or(0))),
        .modelRotationOffsetRadians = tileTypeIsMirror(tile)
            ? config::mirrorModelRotationOffsetRadians
            : 0.0f,
        .effect = tile == TileType::Ground
            ? RenderSurfaceEffect::GroundSplat
            : RenderSurfaceEffect::Standard,
    };
    if (!elevator) {
        applyTileScale(
            visual,
            settings.tileScale(
                tileTypeIsPlayerStart(tile) ? TileType::Player : tile));
    }
    return visual;
}

} // namespace sokoban
