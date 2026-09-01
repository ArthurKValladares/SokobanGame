#include "engine/RenderFrameBuilder.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/RenderFrameParts.hpp"
#include "engine/Rules.hpp"
#include "engine/TileTypes.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/SelectorRenderConfig.hpp"
#include "engine/render/MirrorConfig.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/WaterConfig.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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
        appendEditorPreviews(frame);
        appendEditorCamera(frame);
        applyEditorScrollingMaterials(frame);
        return frame;
    }

private:
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

        RenderFrameData::Tile renderTile = tileVisual(
            tile, cell, input_.manifest, input_.settings);
        renderTile.pickable = false;
        renderTile.affectsCameraFit = false;
        const bool animatedActor =
            tile == TileType::Player || tile == TileType::Enemy;
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
        frame.tiles.push_back(renderTile);
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

        // Shared with the thumbnail bake, so a palette icon cannot end up
        // looking different from the tile the editor draws.
        RenderFrameData::Tile renderTile = tileVisual(
            tile, { x, y, z }, input_.manifest, input_.settings);
        renderTile.baseElevation += preview ? 0.02f : 0.0f;
        renderTile.pickOnly = pickOnly;
        renderTile.isEditorPreview = preview;
        const bool animatedActor =
            tile == TileType::Player || tile == TileType::Enemy;
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
        frame.tiles.push_back(renderTile);
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
                }
            }
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
    const bool conveyor = tileTypeIsConveyor(tile);
    const float tileSize = surfaceEntity
        ? settings.geometry.surfaceEntityWidthDepth
        : 1.0f;
    const float centeredOffset = (1.0f - tileSize) * 0.5f;

    Vec4 color = tileColor(tile);
    if (tile == TileType::Player || tile == TileType::Enemy) {
        color = { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    if (tile == TileType::Ice) {
        color.w = config::iceTintAlpha;
    }

    RenderFrameData::Tile visual {
        .cell = cell,
        .position = {
            static_cast<float>(cell.x) + centeredOffset,
            static_cast<float>(cell.y) + centeredOffset,
        },
        .size = { tileSize, tileSize },
        .color = color,
        .baseElevation = static_cast<float>(cell.z),
        // Conveyors are the reason this is shared: they are neither a surface
        // entity nor a solid block, so anything that only tests those two ends
        // up drawing them flat.
        .height = surfaceEntity
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
        .showGrid = tile != TileType::Player,
        .affectsCameraFit = tileTypeAffectsCameraFit(tile),
        .model = manifest.modelForTile(tile),
        .animation = tile == TileType::Player || tile == TileType::Enemy
            ? manifest.playerIdleAnimation()
            : noAnimation,
        .animationInstanceId = tile == TileType::Player || tile == TileType::Enemy
            ? authoredAnimationInstance(tile, cell)
            : uint64_t { 0 },
        // Conveyors carry their direction in the tile type; mirrors carry an
        // orientation. Both are rotations of one shared model, so dropping
        // either collapses a whole family into identical-looking tiles.
        .modelRotationQuarterTurns =
            rules::conveyorDirectionForTile(tile)
            ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
            : mirrorOrientationQuarterTurns(tile).value_or(0),
        .modelRotationOffsetRadians = tileTypeIsMirror(tile)
            ? config::mirrorModelRotationOffsetRadians
            : 0.0f,
        .effect = tile == TileType::Ground
            ? RenderSurfaceEffect::GroundSplat
            : RenderSurfaceEffect::Standard,
    };
    applyTileScale(visual, settings.tileScale(tile));
    return visual;
}

} // namespace sokoban
