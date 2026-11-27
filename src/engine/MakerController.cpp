#include "engine/MakerController.hpp"

#include "engine/CampaignSession.hpp"
#include "engine/Log.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"
#include "engine/ui/MakerHelp.hpp"
#include "engine/ui/MakerTileGuide.hpp"
#include "engine/ui/SelectorPrompt.hpp"
#include "engine/ui/UiColorValues.hpp"
#include "engine/ui/UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <ranges>
#include <span>
#include <utility>

namespace sokoban {
namespace {

using AreaMode = MakerAreaViewState::Mode;

UiRect fitSceneViewport(UiRect bounds, Vec2 source)
{
    const Vec2 start { std::ceil(bounds.position.x),
                       std::ceil(bounds.position.y) };
    const Vec2 end {
        std::floor(bounds.position.x + bounds.size.x),
        std::floor(bounds.position.y + bounds.size.y),
    };
    const Vec2 available = end - start;
    if (source.x <= 0.0f || source.y <= 0.0f || available.x <= 0.0f ||
        available.y <= 0.0f) {
        return { start, {} };
    }
    const float scale =
        std::min(available.x / source.x, available.y / source.y);
    const Vec2 size { std::floor(source.x * scale),
                      std::floor(source.y * scale) };
    const Vec2 spare = available - size;
    return {
        start + Vec2 { std::floor(spare.x * 0.5f), std::floor(spare.y * 0.5f) },
        size,
    };
}

bool showsMakerResizePreview(MakerDraftDialogMode mode)
{
    return mode == MakerDraftDialogMode::ResizeBoard ||
        mode == MakerDraftDialogMode::ConfirmCrop;
}

bool isMakerDraftPicker(MakerDraftDialogMode mode)
{
    return mode == MakerDraftDialogMode::Load ||
        mode == MakerDraftDialogMode::SelectorTarget;
}

MakerSession::StrokeMode makerStrokeMode(const InputRouter::EditorInput& input)
{
    if (input.deleting) return MakerSession::StrokeMode::Delete;
    if (input.replaceLayer) return MakerSession::StrokeMode::Replace;
    return MakerSession::StrokeMode::Brush;
}

bool makerStrokeErases(const MakerSession& maker, MakerSession::StrokeMode mode)
{
    return mode == MakerSession::StrokeMode::Delete ||
        (mode == MakerSession::StrokeMode::Brush &&
         maker.tool() == MakerSession::Tool::Erase);
}

std::optional<MakerAreaViewState::Mode> makerAreaMode(MakerSession::Tool tool)
{
    using Mode = MakerAreaViewState::Mode;
    switch (tool) {
    case MakerSession::Tool::SelectArea:
        return Mode::Rectangle;
    case MakerSession::Tool::FloodFill:
        return Mode::FloodFill;
    case MakerSession::Tool::PasteArea:
        return Mode::Paste;
    default:
        return std::nullopt;
    }
}

MakerCommand makerShortcutCommand(
    const InputRouter::Frame& input,
    const MakerSession& maker)
{
    const auto& keys = input.makerEditor;
    if (input.toggleMakerPlaytestPressed) return MakerCommand::TestPlay;
    if (keys.savePressed) return MakerCommand::Save;
    if (keys.undoPressed) return MakerCommand::Undo;
    if (keys.redoPressed) return MakerCommand::Redo;
    if (input.makerBoard.togglePressed) return MakerCommand::ToggleBoardCursor;
    if (input.makerTopDownPressed) return MakerCommand::ToggleTopDown;
    if (input.makerFrameSelectionPressed) return MakerCommand::FrameSelection;
    if (keys.rotateTilePressed) return MakerCommand::Rotate;
    if (keys.layerDownPressed) return MakerCommand::LayerDown;
    if (keys.layerUpPressed) return MakerCommand::LayerUp;
    if (input.makerAreaAction) {
        switch (*input.makerAreaAction) {
        case InputAction::MakerSelectArea:
            return MakerCommand::SelectArea;
        case InputAction::MakerFloodFill:
            return MakerCommand::FloodFill;
        case InputAction::MakerCopySelection:
            return MakerCommand::CopySelection;
        case InputAction::MakerCutSelection:
            return MakerCommand::CutSelection;
        case InputAction::MakerPasteArea:
            return MakerCommand::PasteArea;
        case InputAction::MakerEraseSelection:
            return MakerCommand::EraseSelection;
        default:
            break;
        }
    }
    if (keys.cycleToolPressed) {
        switch (maker.tool()) {
        case MakerSession::Tool::Paint:
            return MakerCommand::Erase;
        case MakerSession::Tool::Erase:
            return MakerCommand::Connect;
        case MakerSession::Tool::Connect:
            return maker.overworld() ? MakerCommand::ConnectSelectors
                                     : MakerCommand::SelectArea;
        case MakerSession::Tool::ConnectSelectors:
            return MakerCommand::SelectArea;
        case MakerSession::Tool::SelectArea:
            return MakerCommand::FloodFill;
        case MakerSession::Tool::FloodFill:
        case MakerSession::Tool::PasteArea:
            return MakerCommand::Paint;
        }
    }
    return MakerCommand::None;
}

using LinkEndpoint = LevelEditor::LinkEndpoint;
using LinkKind = LevelEditor::LinkEndpointKind;

Level::LinkGroupKind makerGroupKind(LinkKind kind)
{
    return kind == LinkKind::Object ? Level::LinkGroupKind::Object
                                    : Level::LinkGroupKind::Device;
}

TileType makerTileAt(const LevelEditor& editor, GridPosition3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        std::cmp_greater_equal(cell.x, editor.documentWidth()) ||
        std::cmp_greater_equal(cell.y, editor.documentHeight()) ||
        std::cmp_greater_equal(cell.z, editor.documentDepth())) {
        return TileType::Air;
    }
    const auto& row = editor.documentLayers()[static_cast<std::size_t>(cell.z)]
                                             [static_cast<std::size_t>(cell.y)];
    const auto tile = charToTileType(row[static_cast<std::size_t>(cell.x)])
                          .value_or(TileType::Air);
    return tile == TileType::Air &&
            editor.waterLayer() == static_cast<uint32_t>(cell.z)
        ? TileType::Water
        : tile;
}

bool makerScreenContains(const LevelEditor& editor, GridPosition3 cell)
{
    return cell.x >= 0 && cell.y >= 0 &&
        std::cmp_less(cell.x, editor.documentWidth()) &&
        std::cmp_less(cell.y, editor.documentHeight());
}

struct MakerOverworldCell {
    const Level::Definition& definition;
    GridPosition3 local;
    OverworldScreenId screen;
};

std::optional<MakerOverworldCell> makerOverworldCellAt(
    const MakerSession& maker,
    GridPosition3 cell)
{
    const auto& layout = maker.overworldLayout();
    const OverworldSlot current = maker.currentScreenSlot();
    const auto width = static_cast<int>(layout.screenWidth);
    const auto height = static_cast<int>(layout.screenHeight);
    for (const auto& screen : layout.screens) {
        const GridPosition offset { (screen.slot.x - current.x) * width,
                                    (screen.slot.y - current.y) * height };
        const GridPosition3 local { cell.x - offset.x,
                                    cell.y - offset.y,
                                    cell.z };
        if (local.x < 0 || local.y < 0 || local.x >= width || local.y >= height)
            continue;
        if (const auto* definition = maker.overworldDefinition(screen.id)) {
            return MakerOverworldCell { *definition, local, screen.id };
        }
    }
    return std::nullopt;
}

const MakerDraftStore::SavedLevel* makerLevelFromCatalog(
    std::span<const MakerDraftStore::SavedLevel> levels,
    const std::optional<std::string>& id,
    const std::optional<std::string>& legacyName,
    std::span<const std::string> retiredNames = {})
{
    using SavedLevel = MakerDraftStore::SavedLevel;
    if (id) {
        const auto found = std::ranges::find(levels, *id, &SavedLevel::id);
        return found == levels.end() ? nullptr : &*found;
    }
    if (!legacyName) return nullptr;
    const auto alias =
        std::ranges::find(levels, legacyName, &SavedLevel::legacyName);
    if (alias != levels.end()) return &*alias;
    if (std::ranges::binary_search(retiredNames, *legacyName)) return nullptr;
    const auto current =
        std::ranges::find(levels, *legacyName, &SavedLevel::name);
    return current == levels.end() ? nullptr : &*current;
}

constexpr std::array makerRecoveryKinds { MakerRecoveryKind::Puzzle,
                                          MakerRecoveryKind::Overworld };

std::size_t makerRecoveryIndex(MakerRecoveryKind kind)
{
    return kind == MakerRecoveryKind::Puzzle ? 0U : 1U;
}

std::string makerLecternField(std::string_view text)
{
    std::string field;
    for (const char character : text) {
        switch (character) {
        case '\\':
            field += "\\\\";
            break;
        case '\n':
            field += "\\n";
            break;
        default:
            field += character;
            break;
        }
    }
    return field;
}

std::string makerLecternText(std::string_view field)
{
    std::string text;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 1 < field.size()) {
            if (field[i + 1] == 'n') {
                text += '\n';
                ++i;
                continue;
            }
            if (field[i + 1] == '\\') ++i;
        }
        text += field[i];
    }
    return text;
}

TileType makerLinkTile(const LevelEditor& editor, LinkEndpoint endpoint)
{
    const auto tile = makerTileAt(editor, endpoint.cell);
    const auto plate =
        editor.documentPlateAt(endpoint.cell).value_or(TileType::Air);
    if (endpoint.kind == LinkKind::Source || endpoint.kind == LinkKind::Portal)
        return plate;
    if (endpoint.kind == LinkKind::Device &&
        (tileTypeIsRotator(plate) || tileTypeIsLockPlate(plate)))
        return plate;
    return tile;
}

TileType makerLinkTile(const MakerSession& maker, LinkEndpoint endpoint)
{
    if (maker.overworld() && endpoint.kind == LinkKind::Portal) {
        if (const auto location = makerOverworldCellAt(maker, endpoint.cell)) {
            const auto& definition = location->definition;
            const auto cell = location->local;
            const auto covered =
                std::ranges::find(definition.plates, cell, &Level::Plate::cell);
            if (covered != definition.plates.end() &&
                tileTypeIsPortal(covered->tile))
                return covered->tile;
            if (cell.z >= 0 &&
                std::cmp_less(cell.z, definition.layers.size())) {
                return charToTileType(definition.layers[cell.z][cell.y][cell.x])
                    .value_or(TileType::Air);
            }
        }
    }
    return makerLinkTile(maker.editor(), endpoint);
}

Vec3 makerLinkAnchor(
    const MakerSession& maker,
    LinkEndpoint endpoint,
    const AssetManifest& manifest,
    const PresentationSettings& settings)
{
    const auto tile = makerLinkTile(maker, endpoint);
    const auto visual = tileVisual(tile, endpoint.cell, manifest, settings);
    Vec3 anchor {
        visual.position.x + visual.size.x * 0.5f,
        visual.position.y + visual.size.y * 0.5f,
        visual.baseElevation +
            (tileTypeIsGate(tile) ? 0.55f : visual.height * 0.65f),
    };
    if (tileTypeIsPortal(tile)) {
        const auto edge = portalEdgeOffset(tile);
        anchor.x += static_cast<float>(edge.x) * 0.5f;
        anchor.y += static_cast<float>(edge.y) * 0.5f;
        anchor.z = visual.baseElevation + 0.8f;
    }
    return anchor;
}

std::optional<LinkEndpoint> otherMakerLinkEndpoint(
    const LevelEditor& editor,
    LinkEndpoint current)
{
    for (const auto kind : { LinkKind::Object,
                             LinkKind::Source,
                             LinkKind::Device,
                             LinkKind::Portal }) {
        LinkEndpoint endpoint { current.cell, kind };
        if (endpoint != current && editor.hasLinkEndpoint(endpoint))
            return endpoint;
    }
    return std::nullopt;
}

struct MakerLinkPresentation {
    MakerLinkInspector inspector;
    std::string title;
    std::string connectionLabel;
    std::vector<std::string> groupNames;
    std::vector<Vec3> groupColors;
    std::vector<std::string> members;
    std::vector<LinkEndpoint> peers;
    std::vector<MakerLinkLine> lines;
    std::vector<MakerLinkMarker> markers;
};

void populateMakerLinkInspector(
    MakerLinkPresentation& view,
    const LevelEditor& editor,
    LinkEndpoint endpoint)
{
    using Kind = MakerLinkInspector::Kind;
    const auto color = editor.linkEndpointColor(endpoint);
    const auto& groups = editor.linkGroupsView();
    view.title = makerTileGuide::name(makerLinkTile(editor, endpoint));
    view.inspector.canClear = true;
    view.inspector.canSelectOther =
        otherMakerLinkEndpoint(editor, endpoint).has_value();
    const auto addPeers = [&](const auto& cells, LinkKind kind) {
        for (const auto cell : cells) {
            if (LinkEndpoint peer { cell, kind }; peer != endpoint)
                view.peers.push_back(peer);
        }
    };
    if (endpoint.kind == LinkKind::Portal) {
        view.inspector.kind = Kind::Portal;
        std::size_t pairNumber = 0;
        for (const auto& group : groups) {
            if (group.portals.size() == 2) ++pairNumber;
            if (!color || !LevelEditor::sameLinkColor(*color, group.color))
                continue;
            if (group.portals.size() == 2) {
                addPeers(group.portals, LinkKind::Portal);
                view.connectionLabel =
                    "Portal pair " + std::to_string(pairNumber);
                view.title = view.connectionLabel;
                view.inspector.groupColor = color;
            }
            view.inspector.canDisconnect = group.portals.size() > 1;
            break;
        }
        view.inspector.detail = !view.peers.empty()
            ? "These two portals lead to each other."
            : view.inspector.canDisconnect
            ? "More than two portals share this pair. Unpair this portal to "
              "change its exit."
            : "Click another unpaired portal to set its exit.";
        return;
    }

    const bool object = endpoint.kind == LinkKind::Object;
    const bool source = endpoint.kind == LinkKind::Source;
    const auto linkedCells = object ? std::vector<GridPosition3> {}
        : source                    ? editor.devicesDrivenBy(endpoint.cell)
                 : editor.linkSourcesForDevice(endpoint.cell);
    view.inspector.kind = object ? Kind::Object : Kind::Device;
    view.inspector.canCreate = true;
    const auto groupKind = makerGroupKind(endpoint.kind);
    const auto addGroup = [&](Vec3 groupColor, std::string_view name) {
        view.groupColors.push_back(groupColor);
        view.groupNames.push_back(
            name.empty()
                ? (object ? "Existing sync group " : "Existing device group ") +
                    std::to_string(view.groupColors.size())
                : std::string(name));
    };
    for (const auto& group : groups) {
        const auto name = editor.linkGroupName(groupKind, group.color);
        const auto deviceCount = group.gates.size() + group.rotators.size() +
            group.lockPlates.size() + group.elevators.size() +
            group.minecarts.size();
        const bool active =
            color && LevelEditor::sameLinkColor(*color, group.color);
        const bool legacyGroup = object
            ? !group.objects.empty()
            : group.pressurePlates.size() + deviceCount > 1;
        const bool explicitlyWired = active && !linkedCells.empty();
        if (name.empty() && !legacyGroup && !explicitlyWired) continue;
        if (active) {
            view.inspector.activeGroup = view.groupColors.size();
            if (object) addPeers(group.objects, LinkKind::Object);
        }
        addGroup(group.color, name);
    }
    for (const auto& named : editor.linkGroupNames()) {
        if (named.kind == groupKind &&
            std::ranges::none_of(view.groupColors, [&](Vec3 existing) {
                return LevelEditor::sameLinkColor(existing, named.color);
            })) {
            addGroup(named.color, named.name);
        }
    }
    if (!object) {
        addPeers(linkedCells, source ? LinkKind::Device : LinkKind::Source);
    }
    view.inspector.canRename = view.inspector.activeGroup.has_value();
    view.inspector.canDisconnect =
        !view.peers.empty() || view.inspector.activeGroup.has_value();
    view.inspector.detail = object
        ? "Click another movable object or choose a group. Group members copy "
          "successful moves."
        : source
        ? "Click a device to connect it to this signal source."
        : "Click a pressure plate, button, or lever to drive this device.";
    if (endpoint.kind == LinkKind::Device &&
        makerLinkTile(editor, endpoint) == TileType::Elevator) {
        const auto& elevators = editor.elevators();
        const auto elevator =
            std::ranges::find(elevators, endpoint.cell, &Level::Elevator::cell);
        const auto layer = static_cast<int>(editor.activeLayer());
        if (elevator != elevators.end() && layer != elevator->cell.z) {
            view.inspector.elevatorStopOnLayer =
                std::ranges::find(elevator->levels, layer) !=
                elevator->levels.end();
        }
        if (elevator != elevators.end() && elevator->levels.size() < 2) {
            view.inspector.detail = "Connect an activator, then choose another "
                                    "layer and add a stop.";
        }
    }
    if (view.inspector.activeGroup) {
        view.connectionLabel = view.groupNames[*view.inspector.activeGroup];
        view.inspector.groupColor = color;
    } else if (!view.peers.empty()) {
        view.connectionLabel = "Signal connection";
    }
}

void populateMakerOverworldPortalInspector(
    MakerLinkPresentation& view,
    const MakerSession& maker,
    LinkEndpoint endpoint)
{
    view.inspector.kind = MakerLinkInspector::Kind::Portal;
    view.title = makerTileGuide::name(makerLinkTile(maker, endpoint));
    view.inspector.canClear = true;
    view.inspector.canSelectOther =
        otherMakerLinkEndpoint(maker.editor(), endpoint).has_value();
    for (const auto peer : maker.overworldPortalPeers(endpoint.cell)) {
        view.peers.push_back({ peer, LinkKind::Portal });
    }
    view.inspector.canDisconnect = !view.peers.empty();
    if (view.peers.size() == 1) {
        view.connectionLabel = "Portal pair";
        view.title = view.connectionLabel;
        if (const auto* portal = maker.overworldPortalAt(endpoint.cell))
            view.inspector.groupColor = portal->color;
        view.inspector.detail = "These two portals lead to each other.";
    } else {
        view.inspector.detail = view.inspector.canDisconnect
            ? "More than two portals share this pair. Unpair this portal to "
              "change its exit."
            : "Click another unpaired portal to set its exit.";
    }
}

} // namespace

OverworldScreenId makerPlaytestScreen(
    const OverworldMap& map,
    const GameState& state)
{
    return CampaignSession::sharedPlayerScreen(map, state)
        .value_or(map.startScreen());
}

CameraAngles makerPlaytestAngles(
    const Level& level,
    const OverworldMap* world,
    const GameState& state)
{
    if (world) {
        if (const auto* screen =
                world->screen(makerPlaytestScreen(*world, state))) {
            return screen->definition.cameraAngles.value_or(CameraAngles {});
        }
    }
    return level.cameraAngles().value_or(CameraAngles {});
}

// What one workspace frame works out before it draws and acts on the result.
//
// The steps of drawWorkspace fill this in order. The view state borrows the
// strings and spans kept here, so it has to live for the whole frame.
struct MakerController::WorkspaceFrame {
    const InputRouter::Frame& input;
    Vec2 viewport;
    float scale = 1.0f;
    std::optional<Vec2> wheelPosition;

    MakerLayout layout {};
    UiRect scene {};
    // The frame on screen was drawn with this layout, so picks and
    // projections against it are valid.
    bool projectionReady = false;
    bool modalOpen = false;
    bool toolbarOverflowOpen = false;
    bool boardAvailable = false;
    bool showingProblems = false;
    bool navigating = false;
    bool canPick = false;
    bool inspecting = false;

    std::optional<GridPosition3> cursorCell;
    std::optional<GridPosition3> pointerCell;
    std::optional<GridPosition3> hovered;
    std::optional<Vec2> problemMarker;
    std::optional<MakerSession::TileProperties> properties;

    std::optional<MakerSession::Region> areaSelection;
    MakerAreaViewState area {};
    std::optional<GridPosition3> areaHovered;
    std::optional<MakerSession::Region> validPaste;
    std::optional<MakerAreaViewState::Outline> cursorOutline;

    bool moving = false;
    MakerSession::StrokeMode strokeMode = MakerSession::StrokeMode::Brush;
    TileType boardTile = TileType::Air;
    bool painting = false;
    bool connecting = false;
    bool ordering = false;
    std::optional<LinkEndpoint> selectedLink;

    MakerLinkPresentation wiring;
    std::string coveredLinkHint;
    const Level::ScreenSelector* hoveredSelector = nullptr;

    TileType brush = TileType::Air;
    bool showPlacementHint = false;
    bool newOverworldScreen = false;
    std::string_view placementError;
    std::string selectorHint;

    MakerPropertyPanel propertyPanel {};
    std::array<uint8_t, 4> minecartDirections {};
    std::string minecartRoute;
    std::string elevatorStops;

    std::optional<MakerSession::Region> frameRegion;
    MakerViewInput viewInput {};
    MakerViewAction action {};
};

MakerController::MakerController(
    MakerHost& host,
    UiContext& ui,
    InputState& input,
    const AssetManifest& assetManifest,
    const PresentationSettings& presentationSettings,
    const std::filesystem::path& saveDirectory)
    : host_(host)
    , ui_(ui)
    , input_(input)
    , assetManifest_(assetManifest)
    , presentationSettings_(presentationSettings)
    , draftStore_(saveDirectory / "maker-drafts")
    , recoveryStore_(saveDirectory / "maker-recovery")
{
}

void MakerController::open()
{
    if (!puzzleSession_) puzzleSession_.emplace();
    if (!session_) session_ = &*puzzleSession_;
    open_ = true;
    refreshSavedLevels();
    checkRecovery();
}

void MakerController::close()
{
    helpOpen_ = false;
    if (overworldMapOpen_) closeOverworldMap();
    if (cameraDialog_) closeCameraDialog();
    if (playtest_) endPlaytest();
    if (draftDialog_) closeDraftDialog();
    if (colorDialog_) closeColorDialog();
    (void)view_.closeToolbarOverflow();
    clearBoardCursor();
    host_.blurTextInput();
    session_->endStroke();
    updateAutosave(*session_, true);
    session_->cancelMove();
    session_->clearTileInspection();
    panel_ = MakerPanel::Palette;
    open_ = false;
    viewport_.reset();
    panPosition_.reset();
    hover_.reset();
    areaDragAnchor_.reset();
}

void MakerController::shutdown()
{
    detachedCamera_.attach(host_.window());
    if (puzzleSession_) updateAutosave(*puzzleSession_, true);
    if (overworldSession_) updateAutosave(*overworldSession_, true);
}

bool MakerController::dialogOpen() const
{
    return draftDialog_.has_value() || colorDialog_.has_value() || helpOpen_ ||
        cameraDialog_.has_value() || overworldMapOpen_ ||
        view_.toolbarOverflowOpen();
}

void MakerController::updateAutosave()
{
    if (open_ && session_ && !playtest_) updateAutosave(*session_);
}

void MakerController::updateDetachedCamera(float dt)
{
    detachedCamera_.update(dt, input_);
}

void MakerController::windowFocusLost()
{
    panPosition_.reset();
    if (open_) {
        session_->endStroke();
        session_->cancelMove();
    }
}

bool MakerController::handleBack()
{
    // While the options menu is open the press is its own, apart from a
    // focused text field, which always gives up focus first.
    const bool own = !host_.optionsOpen();
    // Help and the camera dialog read the press themselves while they draw.
    if ((helpOpen_ || cameraDialog_) && own) return true;
    if (overworldMapOpen_ && own) {
        if (!view_.cancelOverworldMapAction()) closeOverworldMap();
    } else if (panPosition_ && own) {
        panPosition_.reset();
    } else if (colorDialog_ && own) {
        colorDialog_->cancelRequested = true;
    } else if (draftDialog_ && own) {
        draftDialog_->inputAction = {
            .command = MakerDraftDialogAction::Command::Cancel
        };
    } else if (host_.textInputFocused()) {
        host_.blurTextInput();
    } else if (playtest_ && own) {
        exitPlaytestBoard();
    } else if (open_ && own && view_.closeToolbarOverflow()) {
        ui_.cancelPointerInteraction();
    } else if (open_ && own && boardCursor_) {
        clearBoardCursor();
    } else if (open_ && own && panel_ != MakerPanel::Palette) {
        setPanel(MakerPanel::Palette);
    } else if (open_ && own && session_->selectedLinkEndpoint()) {
        session_->clearLinkSelection();
    } else if (open_ && own && session_->selectedLevelSelectorSource()) {
        session_->clearLevelSelectorSource();
    } else if (
        open_ && own &&
        (makerAreaMode(session_->tool()) || session_->selection())) {
        if (session_->tool() == MakerSession::Tool::PasteArea) {
            session_->setTool(MakerSession::Tool::SelectArea);
        } else if (session_->selection()) {
            session_->clearSelection();
        } else {
            session_->setTool(MakerSession::Tool::Paint);
        }
        areaDragAnchor_.reset();
        ui_.cancelPointerInteraction();
    } else {
        return false;
    }
    return true;
}

void MakerController::releaseCameraMouse()
{
    detachedCamera_.releaseMouse(host_.window());
}

void MakerController::prepareForQuitConfirmation()
{
    detachedCamera_.releaseMouse(host_.window());
    if (draftDialog_) draftDialog_->inputAction = {};
    if (colorDialog_) {
        colorDialog_->tabNavigation = {};
        colorDialog_->cancelRequested = false;
        colorPicker_.endAdjustment();
    }
}

bool MakerController::textFocused() const
{
    if (helpOpen_ || cameraDialog_) return false;
    if (colorDialog_) return colorPicker_.textFocused();
    return draftDialog_ ? view_.draftNameFocused() : view_.tileSearchFocused();
}

void MakerController::blurTextFields()
{
    colorPicker_.blurText();
    view_.blurTileSearch();
    view_.blurDraftName();
}

UiTextEdit* MakerController::activeTextEdit()
{
    if (colorDialog_) return colorPicker_.activeTextEdit();
    return draftDialog_ ? view_.activeDraftNameEdit() : view_.activeTextEdit();
}

UiRect MakerController::textCaret() const
{
    if (colorDialog_) return colorPicker_.textCaret();
    return draftDialog_ ? view_.draftNameCaret() : view_.textCaret();
}

bool MakerController::handleTextPress(Vec2 position, bool extendSelection)
{
    if (colorDialog_)
        return colorPicker_.handleTextPress(ui_, position, extendSelection);
    return draftDialog_
        ? view_.handleDraftNamePress(ui_, position, extendSelection)
        : view_.handleTileSearchPress(ui_, position, extendSelection);
}

bool MakerController::searchShortcutAvailable() const
{
    return (!draftDialog_ || isMakerDraftPicker(draftDialog_->mode)) &&
        !colorDialog_;
}

UiTextEdit* MakerController::searchTextEdit()
{
    const bool draftPicker =
        draftDialog_ && isMakerDraftPicker(draftDialog_->mode);
    return draftPicker ? view_.activeDraftNameEdit() : view_.activeTextEdit();
}

bool MakerController::focusSearch()
{
    const bool draftPicker =
        draftDialog_ && isMakerDraftPicker(draftDialog_->mode);
    return draftPicker ? view_.focusDraftSearch() : view_.focusTileSearch();
}

bool MakerController::handlePickerKey(const SDL_Event& event)
{
    if (!draftDialog_ || !isMakerDraftPicker(draftDialog_->mode) ||
        event.type != SDL_EVENT_KEY_DOWN ||
        (event.key.key != SDLK_UP && event.key.key != SDLK_DOWN)) {
        return false;
    }
    const auto* edit = view_.activeDraftNameEdit();
    if (!edit || !edit->composition().text.empty()) return false;
    draftDialog_->pickerNavigation.previous |= event.key.key == SDLK_UP;
    draftDialog_->pickerNavigation.next |= event.key.key == SDLK_DOWN;
    return true;
}

std::optional<bool> MakerController::handleTextResult(
    const SDL_Event& event,
    SdlTextInputResult result)
{
    const bool consumed = result != SdlTextInputResult::Ignored;
    if (colorDialog_) {
        const bool keyDown = event.type == SDL_EVENT_KEY_DOWN &&
            !event.key.repeat &&
            event.key.windowID == SDL_GetWindowID(host_.window());
        if (keyDown && event.key.key == SDLK_TAB &&
            result != SdlTextInputResult::Consumed) {
            if (colorPicker_.textFocused()) colorPicker_.submitText();
            colorPicker_.endAdjustment();
            if (!colorPicker_.textFocused()) {
                const bool previous = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                colorDialog_->tabNavigation = { .previous = previous,
                                                .next = !previous };
            }
            host_.refreshTextInput();
            return true;
        }
        if (result == SdlTextInputResult::Submit) {
            colorPicker_.submitText();
            host_.refreshTextInput();
        } else if (
            keyDown && event.key.key == SDLK_ESCAPE &&
            result == SdlTextInputResult::Blur) {
            colorDialog_->cancelRequested = true;
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            host_.blurTextInput();
            colorPicker_.endAdjustment();
        }
        return consumed;
    }
    if (draftDialog_ && result == SdlTextInputResult::Submit) {
        if (isMakerDraftPicker(draftDialog_->mode)) {
            draftDialog_->pickerNavigation.activate = true;
        } else {
            draftDialog_->inputAction = view_.submitDraftName();
        }
        return consumed;
    }
    if (draftDialog_ && result == SdlTextInputResult::Blur &&
        event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        draftDialog_->inputAction = {
            .command = MakerDraftDialogAction::Command::Cancel
        };
        return consumed;
    }
    return std::nullopt;
}

void MakerController::drawWorkspace(
    Vec2 viewport,
    const InputRouter::Frame& input,
    std::optional<Vec2> wheelPosition)
{
    navigationKeys_ = input.navigationKeys;
    const float scale = std::clamp(
        std::max(host_.displayScale(), viewport.y / 1080.0f), 1.0f, 3.0f);
    if (playtest_) {
        drawPlaytestWorkspace(viewport, scale, input);
        return;
    }
    WorkspaceFrame frame { .input = input,
                           .viewport = viewport,
                           .scale = scale,
                           .wheelPosition = wheelPosition };
    if (!beginWorkspaceFrame(frame)) return;
    // What the pointer and the board cursor are over.
    resolveBoardCells(frame);
    resolveAreaState(frame);
    resolveHoverState(frame);
    // What the view shows on top of the board.
    buildLinkOverlay(frame);
    buildSelectorOverlay(frame);
    buildInspectionMarkers(frame);
    resolvePlacementHint(frame);
    buildPropertyPanel(frame);
    resolveFrameRegion(frame);
    drawWorkspaceView(frame);
    // A modal is drawn after the view so this frame's pointer edges never
    // reach the board, and it takes the rest of the frame.
    if (drawModal(frame)) return;
    if (applyCommand(frame)) return;
    handleBoardPointer(frame);
}

RenderFrameData MakerController::buildEditFrame(
    const InputRouter::Frame& input,
    FrameArena& arena,
    const MakerEditFrameEnvironment& environment)
{
    const auto& maker = *session_;
    const auto& editor = maker.editor();
    const auto& keys = input.makerEditor;
    const auto mode =
        maker.strokeActive() ? maker.strokeMode() : makerStrokeMode(keys);
    const bool inspecting = panel_ == MakerPanel::Properties;
    const bool moving = !inspecting && keys.moving && !keys.pickModifier;
    const bool selectingMoveSource = moving && !editor.pendingMove();
    const bool erasing = !inspecting &&
        (selectingMoveSource || (!moving && makerStrokeErases(maker, mode)));
    const bool connecting = maker.tool() == MakerSession::Tool::Connect &&
        !moving && mode == MakerSession::StrokeMode::Brush;
    const bool ordering =
        maker.tool() == MakerSession::Tool::ConnectSelectors && !moving &&
        mode == MakerSession::StrokeMode::Brush;
    auto hoverCell = inspecting || view_.tileSearchFocused() || connecting ||
            ordering || (!moving && makerAreaMode(maker.tool()))
        ? std::nullopt
        : hover_;
    if (hoverCell && !erasing && !moving && !maker.placingSelector() &&
        makerScreenContains(editor, *hoverCell) &&
        !editor.tilePlacementError(*hoverCell, editor.selectedTile()).empty()) {
        hoverCell.reset();
    }
    // At most the eight screens around the active one.
    ArenaArray<RenderFrameBuilder::EditorInput::OverworldNeighbor> neighbors(
        arena, 8);
    int left = 0;
    int top = 0;
    int right = static_cast<int>(editor.documentWidth());
    int bottom = static_cast<int>(editor.documentHeight());
    uint32_t depth = editor.documentDepth();
    if (maker.overworld() && maker.showNeighbors()) {
        const auto& world = maker.overworldLayout();
        const OverworldSlot active = maker.currentScreenSlot();
        for (const auto& screen : world.screens) {
            const int x = screen.slot.x - active.x;
            const int y = screen.slot.y - active.y;
            if ((x == 0 && y == 0) || std::abs(x) > 1 || std::abs(y) > 1)
                continue;
            if (const auto* definition = maker.overworldDefinition(screen.id)) {
                const GridPosition origin {
                    x * static_cast<int>(world.screenWidth),
                    y * static_cast<int>(world.screenHeight)
                };
                neighbors.push_back({
                    .screen = screen.id,
                    .origin = origin,
                    .width = world.screenWidth,
                    .height = world.screenHeight,
                    .definition = definition,
                });
                left = std::min(left, origin.x);
                top = std::min(top, origin.y);
                right = std::max(
                    right, origin.x + static_cast<int>(world.screenWidth));
                bottom = std::max(
                    bottom, origin.y + static_cast<int>(world.screenHeight));
                depth = std::max(
                    depth, static_cast<uint32_t>(definition->layers.size()));
            }
        }
    }
    const auto resizePreviewSize =
        draftDialog_ && showsMakerResizePreview(draftDialog_->mode)
        ? std::optional { view_.resizePreviewSize() }
        : std::nullopt;
    using LinkColorPreview = RenderFrameBuilder::EditorInput::LinkColorPreview;
    using ColorKind = LinkColorPreview::Kind;
    std::optional<LinkColorPreview> linkColorPreview;
    // UI input is processed before this frame is built, so a drag or
    // valid text edit reaches the scene on the same frame as the picker.
    if (const auto& dialog = colorDialog_; dialog &&
        !LevelEditor::sameLinkColor(dialog->initial, colorPicker_.color())) {
        const auto endpoint = dialog->endpoint;
        const auto color = colorPicker_.color();
        if (endpoint.kind == LinkKind::Portal && maker.overworld()) {
            const auto location = makerOverworldCellAt(maker, endpoint.cell);
            if (const auto* portal = maker.overworldPortalAt(endpoint.cell);
                portal && location) {
                linkColorPreview = LinkColorPreview {
                    .kind = ColorKind::Portal,
                    .original = dialog->initial,
                    .color = color,
                    .portalScope = portal->scope,
                    .overworldPortalPair = portal->overworldPair,
                    .overworldPortalScreen = location->screen,
                };
            }
        } else if (endpoint.kind == LinkKind::Portal) {
            const auto& portals = editor.portals();
            const auto portal =
                std::ranges::find(portals, endpoint.cell, &Level::Portal::cell);
            if (portal != portals.end()) {
                linkColorPreview = LinkColorPreview {
                    ColorKind::Portal, dialog->initial, color, portal->scope
                };
            }
        } else {
            linkColorPreview = LinkColorPreview {
                endpoint.kind == LinkKind::Object ? ColorKind::Object
                                                  : ColorKind::Device,
                dialog->initial,
                color,
            };
        }
    }
    RenderFrameData frame = RenderFrameBuilder::buildEditor(
        {
            .manifest = assetManifest_,
            .editor = editor,
            .settings = presentationSettings_,
            .ambientLife = true,
            .animations = environment.animations,
            .hoverCell = hoverCell,
            .deleting = erasing,
            .selectingMoveSource = selectingMoveSource,
            .allowBoardExpansion = false,
            .worldAnimationTimeSeconds = environment.worldAnimationTimeSeconds,
            .animationTransitionTimeSeconds =
                environment.animationTransitionTimeSeconds,
            .overworldScreen = maker.overworld()
                ? std::optional { maker.currentScreenId() }
                : std::nullopt,
            .overworldNeighbors = neighbors,
            .selectorState = environment.selectorState,
            .groundGeometryCache = environment.groundGeometryCache,
            .resizePreviewSize = resizePreviewSize,
            .isolateActiveLayer = !inspecting &&
                maker.tool() == MakerSession::Tool::Connect &&
                maker.linkLayerIsolated(),
            .linkColorPreview = linkColorPreview,
            .allowOverworldScreenExpansion =
                maker.overworld() && maker.showNeighbors(),
            .previewOverworldSelector =
                !inspecting && maker.placingSelector() && !moving && !erasing,
            .pickOverworldPortals = maker.overworld(),
            .pickOverworldSelectors = maker.overworld(),
        },
        arena);
    if (cameraDialog_) {
        const CameraAngles preview =
            cameraDialog_->angles.value_or(CameraAngles {});
        frame.cameraPitchDegrees = preview.pitchDegrees;
        frame.cameraYawDegrees = preview.yawDegrees;
    }
    frame.gridOverlay = { { 0.10f, 0.12f, 0.14f, 0.78f }, 2.0f };
    if (maker.overworld() &&
        (!maker.strokeActive() || !overworldCameraExtent_)) {
        // Keep the projection stable under the cursor until the brush is
        // released.
        overworldCameraExtent_ = RenderFrameData::CameraExtent {
            .originX = left - 1,
            .originY = top - 1,
            .originZ = -1,
            .width = static_cast<uint32_t>(right - left + 2),
            .height = static_cast<uint32_t>(bottom - top + 2),
            .depth = depth + 1,
        };
    }
    if (maker.overworld())
        frame.cameraExtent = overworldCameraExtent_;
    else if (!resizePreviewSize)
        frame.cameraExtent = RenderFrameData::CameraExtent {
            .originX = -1,
            .originY = -1,
            .originZ = -1,
            .width = frame.levelWidth + 2,
            .height = frame.levelHeight + 2,
            .depth = frame.levelDepth + 1,
        };
    boardFootprint_ = BoardFootprint { left, top, right, bottom };
    if (!cameraDialog_ && !resizePreviewSize) viewportCamera_.apply(frame);
    detachedCamera_.apply(frame);
    return frame;
}

void MakerController::openColorDialog(
    LinkEndpoint endpoint,
    Vec3 color,
    std::string_view groupName)
{
    session_->endStroke();
    session_->cancelMove();
    host_.blurTextInput();
    ui_.cancelPointerInteraction();
    hover_.reset();
    const std::string title =
        groupName.empty() ? "Group Color" : std::string(groupName) + " Color";
    colorPicker_.open(color, title);
    colorDialog_ = ColorDialog { .endpoint = endpoint, .initial = color };
}

void MakerController::closeColorDialog()
{
    host_.blurTextInput();
    ui_.cancelPointerInteraction();
    colorDialog_.reset();
}

void MakerController::drawColorDialog(
    Vec2 viewport,
    float scale,
    const MakerViewInput& input)
{
    auto navigation = input.navigation;
    const auto tab = std::exchange(colorDialog_->tabNavigation, {});
    if (tab.previous || tab.next) navigation = tab;
    const bool gamepad = input_.activeDevice() == ActiveInputDevice::Gamepad;
    const auto pressed = [&](InputAction action,
                             GameplayLoop::ButtonState key) {
        return static_cast<int>(
            gamepad ? input_.actionPressed(action) : key.pressed);
    };
    const UiColorPickerAction action = colorPicker_.draw(
        ui_,
        viewport,
        scale,
        {
            .navigation = navigation,
            .extendTextSelection = input.extendTextSelection,
            .horizontalStep =
                pressed(InputAction::MoveRight, navigationKeys_.right) -
                pressed(InputAction::MoveLeft, navigationKeys_.left),
            .verticalStep = pressed(InputAction::MoveUp, navigationKeys_.up) -
                pressed(InputAction::MoveDown, navigationKeys_.down),
        });
    using Command = UiColorPickerAction::Command;
    if (colorDialog_->cancelRequested || action.command == Command::Cancel) {
        closeColorDialog();
    } else if (action.command == Command::Apply) {
        const bool sameGroup =
            session_->selectedLinkEndpoint() == colorDialog_->endpoint;
        if (sameGroup &&
            (LevelEditor::sameLinkColor(action.color, colorDialog_->initial) ||
             session_->recolorSelectedLinkGroup(action.color))) {
            closeColorDialog();
        } else {
            const auto status = session_->status();
            colorPicker_.setError(
                !sameGroup || status.empty()
                    ? "The selected group is unavailable."
                    : status);
        }
    } else if (
        action.command == Command::CopyHex &&
        !SDL_SetClipboardText(action.hex.c_str())) {
        colorPicker_.setError("Could not copy the color to the clipboard.");
    }
}

bool MakerController::handleCameraEvent(const SDL_Event& event)
{
    if (!detachedCamera_.active()) return false;

    if (event.type == SDL_EVENT_WINDOW_RESIZED ||
        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        viewport_.reset();
    }
    SDL_Window* const window = host_.window();
    const bool canCapture = cameraDialog_ && !host_.optionsOpen() &&
        viewport_ && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.windowID == SDL_GetWindowID(window) &&
        ui_.contains(
            *viewport_,
            scaleUiPosition(
                { event.button.x, event.button.y },
                host_.windowSize(),
                host_.windowPixelSize()));
    const bool wasCaptured = detachedCamera_.capturingMouse();
    if (detachedCamera_.handleEvent(event, window, canCapture)) {
        if (wasCaptured != detachedCamera_.capturingMouse())
            ui_.cancelPointerInteraction();
        return true;
    }
    if (detachedCamera_.capturingMouse()) {
        if (event.type == SDL_EVENT_KEY_DOWN ||
            event.type == SDL_EVENT_KEY_UP) {
            input_.handleEvent(event, InputState::PressPolicy::Suppress);
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL) return true;
    }
    return false;
}

void MakerController::closeCameraDialog()
{
    detachedCamera_.attach(host_.window());
    cameraDialog_.reset();
    viewport_.reset();
    hover_.reset();
    ui_.cancelPointerInteraction();
}

void MakerController::drawCameraDialog(
    const MakerLayout& layout,
    MakerViewInput input,
    bool canDetach)
{
    const bool captured = detachedCamera_.capturingMouse();
    if (captured) input.navigation = {};
    const auto action = view_.drawCameraDialog(
        ui_,
        layout,
        {
            .angles = cameraDialog_->angles,
            .defaults = CameraAngles {},
            .detached = detachedCamera_.active(),
            .canDetach = canDetach,
            .mouseCaptured = captured,
        },
        input);
    using Command = MakerCameraDialogAction::Command;
    if (input_.actionPressed(InputAction::MenuBack)) {
        if (captured)
            detachedCamera_.releaseMouse(host_.window());
        else if (detachedCamera_.active())
            detachedCamera_.attach(host_.window());
        else
            closeCameraDialog();
        return;
    }
    cameraDialog_->angles = action.angles;
    switch (action.command) {
    case Command::None:
        break;
    case Command::Cancel:
        closeCameraDialog();
        break;
    case Command::Apply:
        (void)session_->setCameraAngles(action.angles);
        closeCameraDialog();
        break;
    case Command::ToggleDetached:
        if (detachedCamera_.active())
            detachedCamera_.attach(host_.window());
        else if (canDetach) {
            const MakerSceneCamera camera = *host_.sceneCamera();
            detachedCamera_.detach(
                camera.position,
                camera.forward,
                camera.verticalFovDegrees,
                camera.right);
        }
        ui_.cancelPointerInteraction();
        break;
    case Command::ResetPose:
        detachedCamera_.resetPose();
        break;
    }
}

void MakerController::openDraftDialog(
    MakerDraftDialogMode mode,
    std::string_view name)
{
    DraftDialog dialog { .mode = mode, .pendingName = std::string(name) };
    if (mode == MakerDraftDialogMode::Load) {
        try {
            if (session_->overworld()) {
                dialog.names = draftStore_.listOverworlds();
            } else {
                dialog.levelRows = draftStore_.listLevels();
                dialog.names.reserve(dialog.levelRows.size());
                for (const auto& level : dialog.levelRows)
                    dialog.names.push_back(level.name);
            }
        } catch (const std::exception& error) {
            dialog.error = error.what();
        }
    }
    session_->endStroke();
    session_->cancelMove();
    host_.blurTextInput();
    if (mode == MakerDraftDialogMode::ResizeBoard) {
        const auto& editor = session_->editor();
        view_.beginResizeBoardDialog(
            editor.documentWidth(), editor.documentHeight());
    } else {
        view_.beginDraftDialog(mode, dialog.pendingName);
    }
    ui_.cancelPointerInteraction();
    hover_.reset();
    draftDialog_ = std::move(dialog);
}

void MakerController::refreshSavedLevels()
{
    try {
        std::vector<std::string> retired;
        savedLevels_ = draftStore_.listLevels(&retired);
        retiredLevelNames_ = std::move(retired);
    } catch (const std::exception& error) {
        log::warning(log::Category::Persistence)
            << "Could not list saved Maker levels: " << error.what();
    }
    problemsCheckedAt_ = {};
    for (auto& target : problemTargets_)
        target.second.checkedAt = {};
}

std::string_view MakerController::selectorLabel(
    const Level::ScreenSelector* selector) const
{
    if (selector) {
        if (const auto* saved = makerLevelFromCatalog(
                savedLevels_,
                selector->draftId,
                selector->draftName,
                retiredLevelNames_)) {
            return saved->name;
        }
        if (selector->draftId || selector->draftName)
            return "Missing saved level";
        if (selector->target) return "Legacy puzzle";
    }
    return "Unassigned Level Selector";
}

bool MakerController::recoveryMatchesSaved(
    const MakerRecoverySnapshot& snapshot) const
{
    if (snapshot.draftName.empty()) return false;
    try {
        if (const auto* puzzle =
                std::get_if<Level::Definition>(&snapshot.document)) {
            const auto saved = snapshot.draftId
                ? draftStore_.loadById(*snapshot.draftId)
                : draftStore_.load(snapshot.draftName);
            return saved == *puzzle;
        }
        const auto& world = std::get<OverworldDraftOverride>(snapshot.document);
        const auto saved = draftStore_.loadOverworld(snapshot.draftName);
        return world.layout == saved.layout &&
            std::ranges::equal(
                   world.definitions,
                   saved.definitions,
                   [](const auto& left, const auto& right) {
                       return left.screen == right.screen &&
                           left.definition == right.definition;
                   });
    } catch (const std::exception&) {
        return false;
    }
}

void MakerController::checkRecovery()
{
    if (!recoveryChecked_) {
        for (const auto kind : makerRecoveryKinds) {
            auto& slot = autosaves_[makerRecoveryIndex(kind)];
            try {
                slot.pending = recoveryStore_.load(kind);
                slot.journalPresent = slot.pending.has_value();
                if (slot.pending && recoveryMatchesSaved(*slot.pending)) {
                    recoveryStore_.discard(kind);
                    slot = {};
                }
            } catch (const std::exception& error) {
                slot.loadError = error.what();
            }
        }
        recoveryChecked_ = true;
    }
    const auto current = session_->overworld() ? MakerRecoveryKind::Overworld
                                               : MakerRecoveryKind::Puzzle;
    if (autosaves_[makerRecoveryIndex(current)].unresolved()) {
        openRecovery(current);
    } else {
        const auto other = makerRecoveryKinds[1 - makerRecoveryIndex(current)];
        if (autosaves_[makerRecoveryIndex(other)].unresolved())
            openRecovery(other);
    }
}

void MakerController::openRecovery(MakerRecoveryKind kind, bool confirmDiscard)
{
    const auto& slot = autosaves_[makerRecoveryIndex(kind)];
    if (!slot.unresolved()) return;
    const auto mode = confirmDiscard ? MakerDraftDialogMode::DiscardAutosave
                                     : MakerDraftDialogMode::RecoverAutosave;
    std::string name = kind == MakerRecoveryKind::Puzzle ? "Untitled Puzzle"
                                                         : "Untitled Overworld";
    if (slot.pending) {
        const auto* saved = makerLevelFromCatalog(
            savedLevels_, slot.pending->draftId, std::nullopt);
        if (saved)
            name = saved->name;
        else if (!slot.pending->draftName.empty())
            name = slot.pending->draftName;
    }
    openDraftDialog(mode, name);
    draftDialog_->recoveryKind = kind;
    if (!confirmDiscard) draftDialog_->error = slot.loadError;
}

void MakerController::updateAutosave(MakerSession& maker, bool force)
{
    if (!recoveryChecked_) return;
    applyAutosaveResults();
    const auto kind = maker.overworld() ? MakerRecoveryKind::Overworld
                                        : MakerRecoveryKind::Puzzle;
    auto& slot = autosaves_[makerRecoveryIndex(kind)];
    if (slot.unresolved()) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && (maker.strokeActive() || now < slot.nextAttempt)) return;
    if (force) maker.endStroke();
    const bool dirty = maker.hasUnsavedChanges();
    const uint64_t revision = maker.editRevision();
    if (dirty) {
        if (slot.writtenRevision == revision || slot.queuedRevision == revision)
            return;
    } else if (
        slot.discardQueued || (!slot.journalPresent && !slot.queuedRevision)) {
        return;
    }
    slot.nextAttempt = now + std::chrono::seconds(1);
    // Only the snapshot copy is taken here. Serializing it and replacing the
    // journal durably flushes the disk, so that runs on the writer's thread
    // and applyAutosaveResults() records the outcome on a later frame.
    try {
        if (dirty) {
            autosaveWriter_.save(revision, maker.recoverySnapshot());
            slot.queuedRevision = revision;
            slot.discardQueued = false;
        } else {
            autosaveWriter_.discard(kind, revision);
            slot.queuedRevision.reset();
            slot.discardQueued = true;
        }
    } catch (const std::exception& error) {
        if (!slot.writeError)
            log::warning(log::Category::Persistence)
                << "Could not update Maker autosave: " << error.what();
        slot.writeError = true;
    }
}

void MakerController::applyAutosaveResults()
{
    for (const auto& result : autosaveWriter_.takeResults()) {
        auto& slot = autosaves_[makerRecoveryIndex(result.kind)];
        // Clearing the marker lets a failed request be retried at the next
        // attempt.
        if (!result.journalPresent) {
            slot.discardQueued = false;
        } else if (slot.queuedRevision == result.revision) {
            slot.queuedRevision.reset();
        }
        if (!result.error.empty()) {
            if (!slot.writeError)
                log::warning(log::Category::Persistence)
                    << "Could not update Maker autosave: " << result.error;
            slot.writeError = true;
            continue;
        }
        slot.journalPresent = result.journalPresent;
        slot.writtenRevision = result.journalPresent
            ? std::optional { result.revision }
            : std::nullopt;
        slot.writeError = false;
    }
}

bool MakerController::openOverworldItem(GridPosition3 cell, bool selectorsOnly)
{
    if (const auto* selector = session_->levelSelectorAt(cell)) {
        refreshSavedLevels();
        const auto* saved = makerLevelFromCatalog(
            savedLevels_,
            selector->draftId,
            selector->draftName,
            retiredLevelNames_);
        const bool missingTarget =
            !saved && (selector->draftId || selector->draftName);
        const auto previousName = selector->draftName;
        const bool legacyTarget = selector->target.has_value();
        openDraftDialog(
            MakerDraftDialogMode::SelectorTarget,
            saved ? std::string_view(saved->name) : std::string_view {});
        auto& dialog = *draftDialog_;
        dialog.overworldItem = cell;
        dialog.legacySelectorTarget = legacyTarget;
        dialog.missingSelectorTarget = missingTarget;
        if (missingTarget && previousName) dialog.pendingName = *previousName;
        try {
            dialog.names = draftStore_.list();
        } catch (const std::exception& error) {
            dialog.error = error.what();
        }
        return true;
    }
    if (selectorsOnly) return false;
    const auto located = makerOverworldCellAt(*session_, cell);
    if (!located) return false;
    const auto& definition = located->definition;
    const auto local = located->local;
    const auto atCell = [&](const auto& item) { return item.cell == local; };
    const auto lectern = std::ranges::find_if(definition.lecterns, atCell);
    if (lectern == definition.lecterns.end()) return false;
    openDraftDialog(
        MakerDraftDialogMode::LecternText, makerLecternField(lectern->text));
    draftDialog_->overworldItem = cell;
    return true;
}

void MakerController::closeDraftDialog()
{
    if (showsMakerResizePreview(draftDialog_->mode)) viewport_.reset();
    host_.blurTextInput();
    ui_.cancelPointerInteraction();
    draftDialog_.reset();
}

void MakerController::handleDraftDialogAction(
    const MakerDraftDialogAction& action)
{
    using Command = MakerDraftDialogAction::Command;
    if (action.command == Command::None) return;

    auto& dialog = *draftDialog_;
    auto& maker = *session_;
    if (dialog.recoveryKind) {
        const auto kind = *dialog.recoveryKind;
        auto& slot = autosaves_[makerRecoveryIndex(kind)];
        const bool confirmingDiscard =
            dialog.mode == MakerDraftDialogMode::DiscardAutosave;
        if (action.command == Command::Cancel) {
            if (confirmingDiscard)
                openRecovery(kind);
            else
                host_.requestBack();
        } else if (action.command == Command::Discard) {
            openRecovery(kind, true);
        } else if (confirmingDiscard) {
            try {
                // Let any journal write still in flight land before the
                // store is used from this thread, then drop its result with
                // the slot below.
                autosaveWriter_.flush();
                applyAutosaveResults();
                recoveryStore_.discard(kind);
            } catch (const std::exception& error) {
                dialog.error = error.what();
                return;
            }
            slot = {};
            closeDraftDialog();
            const auto activeKind = maker.overworld()
                ? MakerRecoveryKind::Overworld
                : MakerRecoveryKind::Puzzle;
            if (autosaves_[makerRecoveryIndex(activeKind)].unresolved())
                checkRecovery();
        } else if (slot.pending) {
            if (kind == MakerRecoveryKind::Overworld) {
                (void)ensureOverworldSession();
            } else if (!puzzleSession_) {
                puzzleSession_.emplace();
            }
            auto& session = kind == MakerRecoveryKind::Puzzle
                ? puzzleSession_
                : overworldSession_;
            auto snapshot = *slot.pending;
            try {
                if (snapshot.draftId) {
                    if (const auto saved =
                            draftStore_.findLevelById(*snapshot.draftId)) {
                        snapshot.draftName = saved->name;
                    } else {
                        snapshot.draftId.reset();
                        snapshot.draftName.clear();
                    }
                } else if (
                    kind == MakerRecoveryKind::Overworld &&
                    !snapshot.draftName.empty()) {
                    const auto names = draftStore_.listOverworlds();
                    if (!std::ranges::binary_search(names, snapshot.draftName))
                        snapshot.draftName.clear();
                }
            } catch (const std::exception& error) {
                dialog.error = error.what();
                return;
            }
            if (session->restoreRecovery(std::move(snapshot))) {
                session_ = &*session;
                slot.pending.reset();
                slot.loadError.clear();
                slot.writtenRevision = session->editRevision();
                resetViewportFraming();
                closeDraftDialog();
            } else {
                dialog.error = session->status();
            }
        }
        return;
    }
    if (dialog.mode == MakerDraftDialogMode::Load &&
        (action.command == Command::Rename ||
         action.command == Command::Delete)) {
        const auto mode = action.command == Command::Rename
            ? MakerDraftDialogMode::RenameDraft
            : MakerDraftDialogMode::DeleteDraft;
        try {
            if (maker.overworld()) {
                openDraftDialog(mode, action.name);
            } else {
                const auto row = std::ranges::find(
                    dialog.levelRows,
                    action.name,
                    &MakerDraftStore::SavedLevel::name);
                const auto saved = row == dialog.levelRows.end()
                    ? std::nullopt
                    : draftStore_.findLevelById(row->id);
                if (saved) {
                    openDraftDialog(mode, saved->name);
                    draftDialog_->savedLevelId = saved->id;
                } else {
                    dialog.error = "This saved level is no longer available.";
                }
            }
        } catch (const std::exception& error) {
            dialog.error = error.what();
        }
        return;
    }
    if (action.command == Command::Cancel) {
        if (dialog.mode == MakerDraftDialogMode::Overwrite) {
            openDraftDialog(MakerDraftDialogMode::Save, dialog.pendingName);
        } else if (
            dialog.mode == MakerDraftDialogMode::Discard ||
            dialog.mode == MakerDraftDialogMode::RenameDraft ||
            dialog.mode == MakerDraftDialogMode::DeleteDraft) {
            openDraftDialog(MakerDraftDialogMode::Load, dialog.pendingName);
        } else if (
            dialog.mode == MakerDraftDialogMode::DiscardNew ||
            dialog.mode == MakerDraftDialogMode::ConfirmCrop) {
            dialog.mode = dialog.mode == MakerDraftDialogMode::DiscardNew
                ? MakerDraftDialogMode::NewDraft
                : MakerDraftDialogMode::ResizeBoard;
            dialog.error.clear();
            view_.resumeBoardSizeDialog();
            ui_.cancelPointerInteraction();
        } else {
            closeDraftDialog();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::RenameDraft ||
        dialog.mode == MakerDraftDialogMode::DeleteDraft) {
        const bool renaming = dialog.mode == MakerDraftDialogMode::RenameDraft;
        std::string renamed;
        try {
            if (dialog.savedLevelId) {
                if (renaming) {
                    auto saved = draftStore_.renameLevel(
                        *dialog.savedLevelId, action.name);
                    maker.updateSavedDraftName(saved.id, saved.name);
                    renamed = std::move(saved.name);
                } else {
                    draftStore_.deleteLevel(*dialog.savedLevelId);
                }
            } else if (maker.overworld()) {
                if (renaming) {
                    draftStore_.renameOverworld(
                        dialog.pendingName, action.name);
                    maker.updateSavedOverworldName(
                        dialog.pendingName, action.name);
                    renamed = action.name;
                } else {
                    draftStore_.deleteOverworld(dialog.pendingName);
                }
            } else {
                dialog.error = "This saved level is no longer available.";
                return;
            }
        } catch (const std::exception& error) {
            dialog.error = error.what();
            return;
        }
        if (!renaming)
            maker.detachDeletedDraft(dialog.pendingName, dialog.savedLevelId);
        if (!maker.overworld()) refreshSavedLevels();
        updateAutosave(maker, true);
        openDraftDialog(MakerDraftDialogMode::Load, renamed);
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::ConfirmDeleteLayer) {
        if (dialog.layer == maker.editor().activeLayer() &&
            maker.deleteLayer(true)) {
            viewport_.reset();
            hover_.reset();
            areaDragAnchor_.reset();
            closeDraftDialog();
        } else {
            dialog.error = maker.status();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::LecternText ||
        dialog.mode == MakerDraftDialogMode::SelectorTarget) {
        bool changed = dialog.mode == MakerDraftDialogMode::LecternText &&
            action.name == dialog.pendingName;
        if (!changed && dialog.overworldItem) {
            if (dialog.mode == MakerDraftDialogMode::LecternText) {
                changed = maker.editOverworldLectern(
                    *dialog.overworldItem, makerLecternText(action.name));
            } else {
                try {
                    const auto saved = action.name.empty()
                        ? std::nullopt
                        : draftStore_.findLevelByName(action.name);
                    if (!saved && !action.name.empty()) {
                        dialog.error =
                            "This saved level is no longer available.";
                        return;
                    }
                    const auto name =
                        saved ? std::optional { saved->name } : std::nullopt;
                    const auto id =
                        saved ? std::optional { saved->id } : std::nullopt;
                    const auto* selector =
                        maker.levelSelectorAt(*dialog.overworldItem);
                    changed = selector && !selector->target &&
                        selector->draftId == id && selector->draftName == name;
                    if (!changed)
                        changed = maker.attachSelectorLevel(
                            *dialog.overworldItem, name, id);
                } catch (const std::exception& error) {
                    dialog.error = error.what();
                    return;
                }
            }
        }
        if (changed)
            closeDraftDialog();
        else
            dialog.error = maker.status();
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::ResizeBoard ||
        dialog.mode == MakerDraftDialogMode::ConfirmCrop) {
        const bool cropConfirmed =
            dialog.mode == MakerDraftDialogMode::ConfirmCrop;
        const std::size_t removedTiles = cropConfirmed
            ? 0
            : maker.resizeTileLossCount(action.width, action.height);
        if (removedTiles != 0) {
            openDraftDialog(MakerDraftDialogMode::ConfirmCrop);
            draftDialog_->removedTileCount = removedTiles;
        } else if (maker.resizeDraft(
                       action.width, action.height, cropConfirmed)) {
            resetViewportFraming();
            closeDraftDialog();
        } else {
            dialog.error = maker.status();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::NewDraft ||
        dialog.mode == MakerDraftDialogMode::DiscardNew) {
        if (dialog.mode == MakerDraftDialogMode::NewDraft &&
            maker.hasUnsavedChanges()) {
            openDraftDialog(MakerDraftDialogMode::DiscardNew);
        } else if (maker.newDraft(action.width, action.height)) {
            updateAutosave(maker, true);
            resetViewportFraming();
            closeDraftDialog();
        } else {
            dialog.error = maker.status();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::NameLinkGroup ||
        dialog.mode == MakerDraftDialogMode::RenameLinkGroup) {
        const auto endpoint = maker.selectedLinkEndpoint();
        const auto color = endpoint
            ? maker.editor().linkEndpointColor(*endpoint)
            : std::nullopt;
        bool saved = false;
        if (dialog.mode == MakerDraftDialogMode::NameLinkGroup) {
            saved = maker.createSelectedLinkGroup(action.name);
        } else if (endpoint && color) {
            const auto kind = makerGroupKind(endpoint->kind);
            saved = maker.editor().linkGroupName(kind, *color) == action.name ||
                maker.renameLinkGroup(kind, *color, action.name);
        }
        if (saved)
            closeDraftDialog();
        else
            dialog.error = maker.status().empty()
                ? "Select a link endpoint first."
                : maker.status();
        return;
    }

    const std::string_view name =
        dialog.mode == MakerDraftDialogMode::Overwrite ||
            dialog.mode == MakerDraftDialogMode::Discard
        ? dialog.pendingName
        : action.name;
    if (dialog.mode == MakerDraftDialogMode::Save ||
        dialog.mode == MakerDraftDialogMode::Overwrite) {
        const bool overwrite = dialog.mode == MakerDraftDialogMode::Overwrite ||
            name == maker.draftName();
        switch (maker.saveDraft(draftStore_, name, overwrite)) {
        case MakerSession::SaveDraftResult::Saved:
            if (!maker.overworld()) refreshSavedLevels();
            updateAutosave(maker, true);
            closeDraftDialog();
            return;
        case MakerSession::SaveDraftResult::NameExists:
            openDraftDialog(MakerDraftDialogMode::Overwrite, name);
            return;
        case MakerSession::SaveDraftResult::Failed:
            break;
        }
    } else if (
        dialog.mode == MakerDraftDialogMode::Load &&
        maker.hasUnsavedChanges()) {
        openDraftDialog(MakerDraftDialogMode::Discard, name);
        return;
    } else if (maker.loadDraft(draftStore_, name)) {
        updateAutosave(maker, true);
        resetViewportFraming();
        closeDraftDialog();
        return;
    }
    dialog.error = maker.status();
}

void MakerController::startPlaytest()
{
    session_->cancelMove();
    updateAutosave(*session_, true);
    std::optional<OverworldMap> world;
    std::optional<Level> level;
    if (session_->overworld()) {
        world = session_->buildOverworldPlaytestMap();
        if (world) level = world->level();
    } else {
        level = session_->buildPlaytestLevel();
    }
    if (!level) return;

    auto& playtest = playtest_.emplace();
    playtest.overworld = std::move(world);
    if (playtest.overworld) {
        try {
            const auto levels = draftStore_.listLevels();
            std::unordered_map<
                std::string,
                std::optional<MakerDraftStore::SavedLevel>>
                legacyTargets;
            for (const auto& selector : playtest.overworld->selectors()) {
                if (selector.draftId) {
                    if (const auto* saved = makerLevelFromCatalog(
                            levels, selector.draftId, std::nullopt)) {
                        playtest.selectorTargets.emplace(
                            selector.runtimeId, *saved);
                    }
                } else if (selector.draftName) {
                    auto [entry, inserted] =
                        legacyTargets.try_emplace(*selector.draftName);
                    if (inserted)
                        entry->second = draftStore_.resolveLevel(
                            std::nullopt, entry->first);
                    if (entry->second)
                        playtest.selectorTargets.emplace(
                            selector.runtimeId, *entry->second);
                }
            }
        } catch (const std::exception& error) {
            log::warning(log::Category::Gameplay)
                << "Could not list saved Maker levels: " << error.what();
        }
    }
    initializePlaytestBoard(
        playtest.board,
        std::move(*level),
        playtest.overworld ? &*playtest.overworld : nullptr);
    host_.blurTextInput();
    ui_.cancelPointerInteraction();
    viewport_.reset();
    hover_.reset();
}

void MakerController::initializePlaytestBoard(
    PlaytestBoard& board,
    Level level,
    const OverworldMap* world)
{
    board.level = std::move(level);
    host_.configurePlaytest(board.session, board.presentation);
    board.session.reset(board.level);
    board.presentation.resetEntities(board.session.state());
    const CameraAngles angles =
        makerPlaytestAngles(board.level, world, board.session.state());
    board.presentation.updateCameraPitch(angles.pitchDegrees, 0.0f, 0.0f);
    board.presentation.updateCameraYaw(angles.yawDegrees, 0.0f, 0.0f);
    board.solved = rules::isAtUnlockedEnd(board.level, board.session.state());
    host_.ensureLevelAssets(board.level);
    host_.setAtmosphere(board.level);
}

void MakerController::endPlaytest()
{
    playtest_.reset();
    host_.restoreAtmosphere();
    ui_.cancelPointerInteraction();
    viewport_.reset();
    hover_.reset();
}

void MakerController::exitPlaytestBoard()
{
    auto& playtest = *playtest_;
    if (!playtest.puzzle) {
        endPlaytest();
        return;
    }
    playtest.puzzle.reset();
    host_.setAtmosphere(playtest.board.level);
    ui_.cancelPointerInteraction();
}

const OverworldSelectorRuntime* MakerController::currentPlaytestSelector() const
{
    const auto& playtest = *playtest_;
    if (!playtest.overworld || playtest.puzzle ||
        playtest.board.session.readingLectern())
        return nullptr;
    const auto* selector = CampaignSession::selectorForInteraction(
        playtest.board.level, playtest.board.session);
    return selector ? MakerLevelProgress::findSelector(
                          playtest.overworld->selectors(), selector->id)
                    : nullptr;
}

const MakerDraftStore::SavedLevel* MakerController::playtestTarget(
    const OverworldSelectorRuntime& selector) const
{
    const auto& targets = playtest_->selectorTargets;
    const auto found = targets.find(selector.runtimeId);
    return found == targets.end() ? nullptr : &found->second;
}

std::optional<uint32_t> MakerController::playtestPrerequisite(
    const OverworldSelectorRuntime& selector) const
{
    const auto& playtest = *playtest_;
    return playtest.progress.unsolvedPrerequisite(
        selector,
        playtest.overworld->selectors(),
        [this](const OverworldSelectorRuntime& prerequisite)
            -> std::optional<std::string_view> {
            if (const auto* target = playtestTarget(prerequisite))
                return target->id;
            return std::nullopt;
        });
}

ScreenSelectorStatus MakerController::playtestSelectorStatus(
    const OverworldSelectorRuntime& selector) const
{
    const auto& playtest = *playtest_;
    const auto* target = playtestTarget(selector);
    if (!target || playtest.invalidLevels.contains(target->id) ||
        playtestPrerequisite(selector)) {
        return ScreenSelectorStatus::Unavailable;
    }
    return playtest.progress.solved(target->id)
        ? ScreenSelectorStatus::Solved
        : ScreenSelectorStatus::Playable;
}

bool MakerController::tryEnterPlaytestSelector()
{
    const auto* selector = currentPlaytestSelector();
    if (!selector ||
        playtestSelectorStatus(*selector) == ScreenSelectorStatus::Unavailable)
        return false;

    auto& playtest = *playtest_;
    const auto& target = *playtestTarget(*selector);
    try {
        auto level = Level::loadFromDefinition(
            draftStore_.loadById(target.id), target.name);
        auto& puzzle = playtest.puzzle.emplace();
        puzzle.id = target.id;
        initializePlaytestBoard(puzzle.board, std::move(level), nullptr);
        if (puzzle.board.solved) playtest.progress.recordCompletion(target.id);
    } catch (const std::exception& error) {
        playtest.puzzle.reset();
        playtest.invalidLevels.emplace(target.id, error.what());
        return false;
    }
    ui_.cancelPointerInteraction();
    return true;
}

std::string MakerController::playtestSelectorHint(
    const OverworldSelectorRuntime& selector) const
{
    const auto& playtest = *playtest_;
    if (!selector.draftName && !selector.draftId)
        return "Attach a saved level to this selector in Maker.";
    const auto* target = playtestTarget(selector);
    if (!target)
        return "Saved level missing: " +
            selector.draftName.value_or("Level Selector target");
    if (const auto error = playtest.invalidLevels.find(target->id);
        error != playtest.invalidLevels.end()) {
        return error->second;
    }
    const auto& selectors = playtest.overworld->selectors();
    if (const auto pending = playtestPrerequisite(selector)) {
        const auto* prerequisite =
            MakerLevelProgress::findSelector(selectors, *pending);
        const auto* required =
            prerequisite ? playtestTarget(*prerequisite) : nullptr;
        return required
            ? "Complete " + required->name + " to unlock " + target->name + "."
            : "The prerequisite selector needs a saved level attached.";
    }
    return target->name;
}

void MakerController::drawPlaytestSelectorPrompt(UiRect scene, Vec2 viewport)
{
    if (!host_.sceneCamera()) return;
    const auto bounds = host_.primaryPlayerBounds();
    if (!bounds) return;
    const auto device = input_.activeDevice() == ActiveInputDevice::Gamepad
        ? BindingDeviceClass::Gamepad
        : BindingDeviceClass::Keyboard;
    const auto label = SelectorPrompt::bindingLabel(
        input_.bindings(), InputAction::MenuConfirm, device, ui_.frameArena());
    const Vec2 playerTop = scene.position +
        scaleUiPosition(bounds->position + Vec2 { bounds->size.x * 0.5f, 0.0f },
                        viewport,
                        scene.size);
    ui_.pushClip(scene);
    SelectorPrompt::draw(
        ui_,
        { playerTop.x,
          std::max(playerTop.y - 10.0f, scene.position.y + 56.0f) },
        label);
    ui_.popClip();
}

void MakerController::toggleOverworld()
{
    session_->endStroke();
    updateAutosave(*session_, true);
    session_->cancelMove();
    session_->clearTileInspection();
    if (session_->overworld()) {
        session_ = &*puzzleSession_;
    } else {
        session_ = &ensureOverworldSession();
    }
    host_.blurTextInput();
    resetViewportFraming();
    ui_.cancelPointerInteraction();
    checkRecovery();
}

MakerSession& MakerController::ensureOverworldSession()
{
    if (!overworldSession_) {
        // A new session edits a puzzle until it is given an overworld.
        overworldSession_.emplace();
        (void)overworldSession_->newOverworldDraft(9, 7);
    }
    return *overworldSession_;
}

void MakerController::resetViewportFraming()
{
    viewportCamera_.resetFraming();
    viewport_.reset();
    panPosition_.reset();
    hover_.reset();
    areaDragAnchor_.reset();
    boardAreaAnchor_.reset();
    boardReveal_ = boardCursor_.has_value();
    overworldCameraExtent_.reset();
    boardFootprint_.reset();
    problemFocus_.reset();
    problemsCheckedAt_ = {};
}

bool MakerController::updateViewportNavigation(
    const InputRouter::PointerInput& pointer,
    UiRect scene,
    std::optional<Vec2> wheelPosition,
    bool enabled)
{
    if (!enabled || scene.size.x <= 0.0f || scene.size.y <= 0.0f) {
        panPosition_.reset();
        return false;
    }

    const auto toPixels = [&](Vec2 point) {
        return scaleUiPosition(
            point, host_.windowSize(), host_.windowPixelSize());
    };
    const bool editing =
        pointer.primaryDown || session_->strokeActive() || areaDragAnchor_;
    if (!panPosition_ && !editing && pointer.middlePressedAt) {
        const Vec2 press = toPixels(*pointer.middlePressedAt);
        if (ui_.contains(scene, press)) {
            panPosition_ = press;
            session_->cancelMove();
            ui_.cancelPointerInteraction();
        }
    }
    if (panPosition_) {
        if (pointer.middleDown || pointer.middleReleasedAt) {
            const Vec2 current = pointer.middleReleasedAt
                ? toPixels(*pointer.middleReleasedAt)
                : ui_.mousePosition();
            Vec2 delta = current - *panPosition_;
            if (const auto board = projectedBoardFootprint(scene)) {
                delta = MakerViewportCamera::limitPan(delta, *board, scene);
            }
            viewportCamera_.pan(delta, scene.size);
            panPosition_ = current;
        }
        if (!pointer.middleDown) panPosition_.reset();
        return true;
    }
    if (!editing && pointer.wheel.y != 0.0f && wheelPosition &&
        ui_.contains(scene, *wheelPosition)) {
        auto board = projectedBoardFootprint(scene);
        const float factor =
            viewportCamera_.zoom(pointer.wheel.y, *wheelPosition, scene);
        if (board && factor != 1.0f) {
            // Zooming in beside the board pushes it away from the pointer,
            // and far enough in, off the screen. Scale last frame's
            // footprint the way the zoom just did and pan back whatever is
            // missing.
            for (Vec2& corner : *board) {
                corner = *wheelPosition + (corner - *wheelPosition) * factor;
            }
            const Vec2 correction =
                MakerViewportCamera::limitPan({}, *board, scene);
            if (correction != Vec2 {}) {
                viewportCamera_.pan(correction, scene.size);
            }
        }
        return true;
    }
    return false;
}

std::optional<MakerViewportCamera::Footprint> MakerController::
    projectedBoardFootprint(UiRect scene)
{
    if (!boardFootprint_) return std::nullopt;
    const auto& board = *boardFootprint_;
    // The top of the ground layer, where the board cursor is projected too.
    constexpr float surface = 1.0f;
    const std::array<Vec3, 4> corners {
        Vec3 { static_cast<float>(board.left),
               static_cast<float>(board.top),
               surface },
        Vec3 { static_cast<float>(board.right),
               static_cast<float>(board.top),
               surface },
        Vec3 { static_cast<float>(board.right),
               static_cast<float>(board.bottom),
               surface },
        Vec3 { static_cast<float>(board.left),
               static_cast<float>(board.bottom),
               surface },
    };
    MakerViewportCamera::Footprint projected {};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto pixel = host_.projectToScene(corners[i], scene);
        // A corner the camera cannot project leaves nothing to measure
        // against, so the gesture is not limited.
        if (!pixel) return std::nullopt;
        projected[i] = *pixel;
    }
    return projected;
}

void MakerController::clearBoardCursor()
{
    boardCursor_.reset();
    boardAreaAnchor_.reset();
    boardDirection_ = {};
    boardReveal_ = false;
}

std::string_view MakerController::boardCursorHint()
{
    constexpr std::array directions {
        InputAction::MakerBoardUp,
        InputAction::MakerBoardDown,
        InputAction::MakerBoardLeft,
        InputAction::MakerBoardRight,
    };
    const bool gamepad = input_.activeDevice() == ActiveInputDevice::Gamepad;
    const auto device =
        gamepad ? BindingDeviceClass::Gamepad : BindingDeviceClass::Keyboard;
    const InputBindings& bindings = input_.bindings();
    bool current = boardCursorHint_.device == device;
    for (std::size_t i = 0; i < directions.size(); ++i) {
        const auto& saved = bindings.forAction(directions[i]);
        if (boardCursorHint_.bindings[i] == saved) continue;
        boardCursorHint_.bindings[i] = saved;
        current = false;
    }
    if (!current) {
        boardCursorHint_.device = device;
        boardCursorHint_.text =
            makerHelp::boardCursorDirectionsLabel(bindings, device);
        boardCursorHint_.text += gamepad ? " to move. Confirm uses the tool."
                                         : " to move. Enter uses the tool.";
    }
    return boardCursorHint_.text;
}

void MakerController::toggleBoardCursor(std::optional<GridPosition3> hovered)
{
    if (boardCursor_) {
        clearBoardCursor();
        return;
    }
    auto& maker = *session_;
    const auto& editor = maker.editor();
    if (editor.documentWidth() == 0 || editor.documentHeight() == 0) return;
    maker.endStroke();
    maker.cancelMove();
    areaDragAnchor_.reset();
    ui_.cancelPointerInteraction();
    boardCursor_ = hovered && makerScreenContains(editor, *hovered)
        ? xy(*hovered)
        : GridPosition {
              static_cast<int>(editor.documentWidth() / 2),
              static_cast<int>(editor.documentHeight() / 2),
          };
    boardReveal_ = true;
}

std::optional<GridPosition3> MakerController::updateBoardCursor(
    GridPosition direction,
    UiRect scene,
    bool projectionReady)
{
    if (!boardCursor_) return std::nullopt;
    auto& maker = *session_;
    const auto& editor = maker.editor();
    if (editor.documentWidth() == 0 || editor.documentHeight() == 0)
        return std::nullopt;
    auto& cursor = *boardCursor_;
    const auto now = std::chrono::steady_clock::now();
    GridPosition step;
    if (direction != GridPosition {} &&
        (direction != boardDirection_ || now >= boardNextStep_)) {
        step = direction;
        boardNextStep_ = now +
            (direction == boardDirection_ ? std::chrono::milliseconds(85)
                                          : std::chrono::milliseconds(300));
    }
    boardDirection_ = direction;
    if (step != GridPosition {} && projectionReady) {
        const Vec3 right = host_.sceneCamera()->right;
        const GridPosition along = std::abs(right.x) >= std::abs(right.y)
            ? GridPosition { right.x < 0.0f ? -1 : 1, 0 }
            : GridPosition { 0, right.y < 0.0f ? -1 : 1 };
        step = { along.x * step.x - along.y * step.y,
                 along.y * step.x + along.x * step.y };
    }
    const GridPosition previous = cursor;
    cursor.x = std::clamp(
        cursor.x + step.x, 0, static_cast<int>(editor.documentWidth()) - 1);
    cursor.y = std::clamp(
        cursor.y + step.y, 0, static_cast<int>(editor.documentHeight()) - 1);
    const GridPosition3 cell { cursor.x,
                               cursor.y,
                               static_cast<int>(editor.activeLayer()) };
    if (boardAreaAnchor_ &&
        (maker.tool() != MakerSession::Tool::SelectArea ||
         boardAreaAnchor_->z != cell.z || panel_ == MakerPanel::Properties)) {
        boardAreaAnchor_.reset();
    }
    if (cursor != previous) {
        boardReveal_ = true;
        if (boardAreaAnchor_) (void)maker.selectArea(*boardAreaAnchor_, cell);
    }
    if (boardReveal_ && projectionReady) {
        const auto projected = host_.projectToScene(
            {
                static_cast<float>(cell.x) + 0.5f,
                static_cast<float>(cell.y) + 0.5f,
                static_cast<float>(std::max(1, cell.z)) + 0.03f,
            },
            scene);
        if (projected) {
            const Vec2 inset =
                scene.size * MakerViewportCamera::keepInViewFraction;
            const Vec2 low = scene.position + inset;
            const Vec2 high = scene.position + scene.size - inset;
            const Vec2 visible { std::clamp(projected->x, low.x, high.x),
                                 std::clamp(projected->y, low.y, high.y) };
            if (visible != *projected)
                viewportCamera_.pan(visible - *projected, scene.size);
        } else {
            viewportCamera_.frameSelection(cell, cell);
        }
        boardReveal_ = false;
    }
    return cell;
}

void MakerController::activateBoardCursor(
    GridPosition3 cell,
    const InputRouter::Frame& input)
{
    auto& maker = *session_;
    const auto& keys = input.makerEditor;
    if (input.makerBoard.contextPressed) {
        if (maker.overworld()) (void)openOverworldItem(cell);
        return;
    }
    if (panel_ == MakerPanel::Properties) {
        (void)maker.inspectTile(cell);
        return;
    }
    if (keys.pickModifier) {
        if (maker.pickTile(cell) && maker.linkLayerIsolated())
            viewport_.reset();
        return;
    }
    if (keys.moving) {
        (void)maker.moveCell(cell);
        return;
    }
    const auto mode = makerStrokeMode(keys);
    if (mode == MakerSession::StrokeMode::Brush) {
        switch (maker.tool()) {
        case MakerSession::Tool::SelectArea:
            if (boardAreaAnchor_) {
                (void)maker.selectArea(*boardAreaAnchor_, cell);
                boardAreaAnchor_.reset();
            } else if (maker.selectArea(cell, cell)) {
                boardAreaAnchor_ = cell;
            }
            return;
        case MakerSession::Tool::FloodFill:
            (void)maker.floodFill(cell);
            return;
        case MakerSession::Tool::PasteArea:
            (void)maker.pasteSelection(cell);
            return;
        case MakerSession::Tool::Connect:
            (void)maker.connectCell(cell);
            return;
        case MakerSession::Tool::ConnectSelectors:
            (void)maker.clickLevelSelectorConnection(cell);
            return;
        case MakerSession::Tool::Paint:
            if (maker.overworld() && openOverworldItem(cell, true)) return;
            break;
        case MakerSession::Tool::Erase:
            break;
        }
    }
    (void)maker.beginStroke(cell, mode);
    maker.endStroke();
}

void MakerController::closeOverworldMap()
{
    overworldMapOpen_ = false;
    ui_.cancelPointerInteraction();
}

void MakerController::drawOverworldMap(
    Vec2 viewport,
    float scale,
    const MakerViewInput& input)
{
    auto& maker = *session_;
    const auto action = view_.drawOverworldMap(
        ui_,
        viewport,
        scale,
        {
            .screens = maker.overworldLayout().screens,
            .activeScreen = maker.currentScreenId(),
            .canUndo = maker.canUndo(),
            .canRedo = maker.canRedo(),
            .status = maker.status(),
        },
        input);
    using Command = MakerOverworldMapAction::Command;
    bool changed = false;
    switch (action.command) {
    case Command::None:
        break;
    case Command::Close:
        closeOverworldMap();
        return;
    case Command::Jump:
        changed = maker.currentScreenId() != action.screen &&
            maker.selectOverworldScreen(action.screen);
        break;
    case Command::Move:
        changed = maker.moveOverworldScreen(action.screen, action.slot);
        break;
    case Command::Delete:
        changed = maker.deleteOverworldScreen(action.screen);
        break;
    case Command::Undo:
        changed = maker.undo();
        break;
    case Command::Redo:
        changed = maker.redo();
        break;
    }
    if (changed) resetViewportFraming();
}

void MakerController::setPanel(MakerPanel panel)
{
    if (panel_ == panel) return;
    session_->endStroke();
    session_->cancelMove();
    if (panel_ == MakerPanel::Properties) session_->clearTileInspection();
    panel_ = panel;
    problemFocus_.reset();
    problemsCheckedAt_ = {};
    host_.blurTextInput();
    hover_.reset();
    areaDragAnchor_.reset();
    ui_.cancelPointerInteraction();
}

bool MakerController::problemTargetPlayable(std::string_view id)
{
    const std::string key(id);
    const auto now = std::chrono::steady_clock::now();
    const auto cached = problemTargets_.find(key);
    // The Problems panel asks four times a second. Look at the file itself
    // once a second; refreshSavedLevels() clears the timestamps, so the
    // panel still follows a save or rename made here at once.
    if (cached != problemTargets_.end() &&
        now - cached->second.checkedAt < std::chrono::seconds(1)) {
        return cached->second.playable;
    }
    std::optional<MakerDraftStore::DraftVersion> version;
    try {
        version = draftStore_.draftVersionById(id);
    } catch (const std::exception&) {
        return false;
    }
    if (!version) {
        problemTargets_.insert_or_assign(
            key, ProblemTarget { std::nullopt, false, now });
        return false;
    }
    if (cached != problemTargets_.end() && cached->second.version == version) {
        cached->second.checkedAt = now;
        return cached->second.playable;
    }

    bool playable = false;
    try {
        (void)Level::loadFromDefinition(draftStore_.loadById(id), key);
        playable = true;
    } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
        // Incomplete drafts can be saved but cannot be entered through a
        // selector.
    }
    problemTargets_.insert_or_assign(
        key, ProblemTarget { version, playable, now });
    return playable;
}

void MakerController::refreshProblems(bool force)
{
    const auto now = std::chrono::steady_clock::now();
    // Scan loaded boards and saved-level metadata only while the panel is
    // visible.
    if (!force && now - problemsCheckedAt_ < std::chrono::milliseconds(250))
        return;
    problems_ = session_->problems(
        [this](std::string_view name) {
            // The catalog is refreshed when the Maker opens and after every
            // save or rename, and resolves legacy names the way the selector
            // labels and Test Play do. Asking the store instead scanned the
            // drafts directory on every refresh.
            const auto* saved = makerLevelFromCatalog(
                savedLevels_,
                std::nullopt,
                std::string(name),
                retiredLevelNames_);
            return saved && problemTargetPlayable(saved->id);
        },
        [this](std::string_view id) { return problemTargetPlayable(id); });
    problemsCheckedAt_ = now;
    if (problemFocus_ &&
        std::ranges::find(problems_, *problemFocus_) == problems_.end()) {
        problemFocus_.reset();
    }
}

void MakerController::jumpToProblem(const MakerSession::Problem& problem)
{
    auto& maker = *session_;
    maker.endStroke();
    if (problem.screen && maker.currentScreenId() != *problem.screen &&
        !maker.selectOverworldScreen(*problem.screen)) {
        return;
    }
    maker.cancelMove();
    maker.clearSelection();
    maker.clearTileInspection();
    maker.clearLinkSelection();
    maker.clearLevelSelectorSource();
    resetViewportFraming();
    problemFocus_ = problem;
    if (problem.cell && makerScreenContains(maker.editor(), *problem.cell)) {
        const auto cell = *problem.cell;
        (void)maker.selectLayer(static_cast<uint32_t>(cell.z));
        viewportCamera_.frameSelection(
            { std::max(0, cell.x - 1), std::max(0, cell.y - 1), cell.z },
            { std::min(
                  static_cast<int>(maker.editor().documentWidth()) - 1,
                  cell.x + 1),
              std::min(
                  static_cast<int>(maker.editor().documentHeight()) - 1,
                  cell.y + 1),
              cell.z });
    }
    ui_.cancelPointerInteraction();
}

void MakerController::drawPlaytestWorkspace(
    Vec2 viewport,
    float scale,
    const InputRouter::Frame& input)
{
    const MakerLayout layout = MakerView::playtestLayout(viewport, scale);
    const UiRect scene = fitSceneViewport(layout.board, viewport);
    ui_.setSceneViewport(scene);
    if (!host_.optionsOpen()) {
        auto& playtest = *playtest_;
        auto& board = playtest.activeBoard();
        const auto* selector = currentPlaytestSelector();
        const std::string hint =
            selector ? playtestSelectorHint(*selector) : std::string {};
        if (input.toggleMakerPlaytestPressed ||
            view_.drawPlaytest(
                ui_,
                layout,
                board.solved,
                playtest.puzzle ? "Return to Overworld" : "Return to Maker",
                hint) == MakerCommand::ReturnToMaker) {
            exitPlaytestBoard();
        } else if (const auto cell = board.session.readingLectern()) {
            host_.drawLectern(
                board.lecternDialog, board.level, *cell, viewport);
        } else if (
            selector &&
            playtestSelectorStatus(*selector) !=
                ScreenSelectorStatus::Unavailable) {
            drawPlaytestSelectorPrompt(scene, viewport);
        }
    }
}

bool MakerController::beginWorkspaceFrame(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    frame.layout = MakerView::layout(frame.viewport, frame.scale);
    frame.scene = fitSceneViewport(frame.layout.board, frame.viewport);
    ui_.setSceneViewport(frame.scene);
    // The previous prepared frame has the projection used by the scene the
    // pointer actually sees. After a resize, wait for the new layout to draw.
    frame.projectionReady =
        viewport_ == frame.scene && host_.sceneCamera().has_value();
    viewport_ = frame.scene;

    if (host_.optionsOpen()) {
        maker.endStroke();
        maker.cancelMove();
        detachedCamera_.releaseMouse(host_.window());
        hover_.reset();
        areaDragAnchor_.reset();
        panPosition_.reset();
        return false;
    }
    frame.modalOpen = draftDialog_ || colorDialog_ || helpOpen_ ||
        cameraDialog_ || overworldMapOpen_;
    frame.toolbarOverflowOpen = view_.toolbarOverflowOpen();
    frame.boardAvailable = !frame.modalOpen && !frame.toolbarOverflowOpen;
    frame.showingProblems = panel_ == MakerPanel::Problems;
    if (frame.showingProblems && !frame.modalOpen) refreshProblems();
    frame.navigating = updateViewportNavigation(
        frame.input.pointer,
        frame.scene,
        frame.wheelPosition,
        frame.boardAvailable);
    frame.canPick = frame.projectionReady && !frame.navigating;
    if (frame.navigating) hover_.reset();
    frame.inspecting = panel_ == MakerPanel::Properties;
    return true;
}

std::optional<GridPosition3> MakerController::pickBoardCell(
    const WorkspaceFrame& frame,
    Vec2 position)
{
    MakerSession& maker = *session_;
    const auto& keys = frame.input.makerEditor;
    if (!frame.canPick) return std::nullopt;
    if (!frame.inspecting && maker.overworld() &&
        maker.tool() == MakerSession::Tool::Connect && !keys.moving &&
        !keys.pickModifier && !keys.deleting && !keys.replaceLayer) {
        if (const auto portal = host_.pickPortalCell(position, frame.scene)) {
            return portal;
        }
    }
    if (const auto cell = host_.pickCell(position, frame.scene)) {
        return cell;
    }
    if (!frame.inspecting && maker.overworld() && maker.showNeighbors()) {
        return host_.pickCellOnPlane(
            position,
            frame.scene,
            static_cast<int>(maker.editor().activeLayer()));
    }
    return std::nullopt;
}

void MakerController::resolveBoardCells(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const auto& keys = frame.input.makerEditor;
    frame.cursorCell =
        frame.boardAvailable && !frame.navigating && !textFocused()
        ? updateBoardCursor(
              frame.input.makerBoard.direction,
              frame.scene,
              frame.projectionReady)
        : std::nullopt;
    frame.pointerCell = frame.boardAvailable
        ? pickBoardCell(frame, ui_.mousePosition())
        : std::nullopt;
    frame.hovered =
        frame.cursorCell && !keys.primaryPressed && !keys.secondaryPressed
        ? frame.cursorCell
        : frame.pointerCell;
    if (frame.showingProblems && frame.canPick && !frame.modalOpen &&
        problemFocus_ && problemFocus_->cell &&
        (!problemFocus_->screen ||
         problemFocus_->screen == maker.currentScreenId())) {
        const auto cell = *problemFocus_->cell;
        Vec3 anchor { static_cast<float>(cell.x) + 0.5f,
                      static_cast<float>(cell.y) + 0.5f,
                      static_cast<float>(cell.z) + 0.12f };
        using ProblemKind = MakerSession::ProblemKind;
        std::optional<LinkKind> linkKind;
        switch (problemFocus_->kind) {
        case ProblemKind::UnpairedPortal:
            linkKind = LinkKind::Portal;
            break;
        case ProblemKind::UnwiredDevice:
            linkKind = LinkKind::Device;
            break;
        case ProblemKind::UnusedSignalSource:
            linkKind = LinkKind::Source;
            break;
        case ProblemKind::MissingPlayerStart:
        case ProblemKind::BrokenLevelSelector:
            break;
        }
        if (linkKind)
            anchor = makerLinkAnchor(
                maker,
                { cell, *linkKind },
                assetManifest_,
                presentationSettings_);
        frame.problemMarker = host_.projectToScene(anchor, frame.scene);
    }
    frame.properties = frame.inspecting ? maker.tileProperties() : std::nullopt;
}

std::optional<GridPosition3> MakerController::pickAreaCell(
    const WorkspaceFrame& frame,
    Vec2 position)
{
    const MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    if (!frame.canPick) return std::nullopt;
    const int layer = static_cast<int>(editor.activeLayer());
    auto cell = host_.pickCell(position, frame.scene);
    if (!cell) {
        cell = host_.pickCellOnPlane(
            position, frame.scene, std::max(0, layer - 1));
    }
    if (cell) cell->z = layer;
    return cell;
}

std::optional<MakerAreaViewState::Outline> MakerController::projectOutline(
    const WorkspaceFrame& frame,
    MakerSession::Region region,
    Vec4 color,
    float lift)
{
    if (!frame.canPick) return std::nullopt;
    const auto& [first, last] = region;
    const float left = static_cast<float>(first.x);
    const float top = static_cast<float>(first.y);
    const float right = static_cast<float>(last.x) + 1.0f;
    const float bottom = static_cast<float>(last.y) + 1.0f;
    const float z = static_cast<float>(std::max(first.z, 1)) + lift;
    const auto project = [&](float x, float y) {
        return host_.projectToScene({ x, y, z }, frame.scene);
    };
    const auto a = project(left, top);
    const auto b = project(right, top);
    const auto c = project(right, bottom);
    const auto d = project(left, bottom);
    if (a && b && c && d)
        return MakerAreaViewState::Outline { { *a, *b, *c, *d }, color };
    return std::nullopt;
}

void MakerController::resolveAreaState(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    frame.areaSelection = maker.selection();
    frame.area = MakerAreaViewState {
        .mode = frame.inspecting ? std::nullopt : makerAreaMode(maker.tool()),
        .hasSelection = !frame.inspecting && frame.areaSelection.has_value(),
        .hasClipboard = maker.hasClipboard(),
    };
    frame.areaHovered = frame.boardAvailable && frame.area.mode
        ? (frame.cursorCell ? frame.cursorCell
                            : pickAreaCell(frame, ui_.mousePosition()))
        : std::nullopt;
    frame.validPaste = frame.area.mode == AreaMode::Paste && frame.areaHovered
        ? maker.pastePreview(*frame.areaHovered)
        : std::nullopt;
    auto outlinedRegion = frame.areaSelection;
    Vec4 outlineColor { 1.0f, 0.78f, 0.35f, 1.0f };
    if (frame.area.mode == AreaMode::Paste) {
        outlinedRegion = frame.validPaste;
        outlineColor = { 0.42f, 0.76f, 1.0f, 1.0f };
        if (!frame.validPaste && frame.areaHovered && frame.area.hasClipboard) {
            const auto size = maker.clipboardSize();
            if (frame.areaHovered->x <=
                    std::numeric_limits<int>::max() - size.x &&
                frame.areaHovered->y <=
                    std::numeric_limits<int>::max() - size.y) {
                outlinedRegion =
                    MakerSession::Region { *frame.areaHovered,
                                           { frame.areaHovered->x + size.x - 1,
                                             frame.areaHovered->y + size.y - 1,
                                             frame.areaHovered->z } };
                outlineColor = { 1.0f, 0.36f, 0.28f, 1.0f };
            }
        }
    } else if (frame.area.mode == AreaMode::FloodFill) {
        outlinedRegion.reset();
        if (frame.areaHovered &&
            makerScreenContains(editor, *frame.areaHovered)) {
            outlinedRegion =
                MakerSession::Region { *frame.areaHovered, *frame.areaHovered };
            outlineColor = { 0.54f, 0.86f, 0.77f, 1.0f };
        }
    }
    if (outlinedRegion && frame.boardAvailable && !frame.inspecting)
        frame.area.outline =
            projectOutline(frame, *outlinedRegion, outlineColor, 0.0f);
    frame.cursorOutline = frame.cursorCell
        ? projectOutline(
              frame,
              { *frame.cursorCell, *frame.cursorCell },
              { 0.54f, 0.86f, 0.77f, 1.0f },
              0.03f)
        : std::nullopt;
}

std::optional<GridPosition3> MakerController::resolveHover(
    const WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const auto& keys = frame.input.makerEditor;
    if (!frame.hovered || keys.pickModifier || frame.inspecting)
        return std::nullopt;
    if (frame.moving) return maker.moveTarget(*frame.hovered);
    if (makerAreaMode(maker.tool())) return std::nullopt;
    return maker.previewCell(
        *frame.hovered,
        maker.strokeActive() ? maker.strokeMode() : frame.strokeMode);
}

void MakerController::resolveHoverState(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    const auto& keys = frame.input.makerEditor;
    if (frame.modalOpen) areaDragAnchor_.reset();
    frame.moving = !frame.inspecting && keys.moving && !keys.pickModifier;
    if (!frame.moving) maker.cancelMove();
    frame.strokeMode = makerStrokeMode(keys);
    hover_ = resolveHover(frame);
    frame.boardTile =
        frame.hovered ? makerTileAt(editor, *frame.hovered) : TileType::Air;
    if (frame.hovered && maker.overworld() &&
        !makerScreenContains(editor, *frame.hovered)) {
        if (const auto location = makerOverworldCellAt(maker, *frame.hovered)) {
            const auto& definition = location->definition;
            const auto cell = location->local;
            if (cell.z >= 0 &&
                std::cmp_less(cell.z, definition.layers.size())) {
                frame.boardTile =
                    charToTileType(definition.layers[cell.z][cell.y][cell.x])
                        .value_or(TileType::Air);
            }
        }
    }
    frame.painting =
        !frame.inspecting && maker.tool() == MakerSession::Tool::Paint;
    frame.connecting =
        !frame.inspecting && maker.tool() == MakerSession::Tool::Connect;
    frame.ordering = !frame.inspecting &&
        maker.tool() == MakerSession::Tool::ConnectSelectors;
    frame.selectedLink =
        frame.connecting ? maker.selectedLinkEndpoint() : std::nullopt;
}

void MakerController::buildLinkOverlay(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    if (frame.connecting) {
        frame.wiring.inspector.detail =
            "Select a device, signal source, portal, or movable object on the "
            "board.";
        if (frame.selectedLink) {
            if (maker.overworld() &&
                frame.selectedLink->kind == LinkKind::Portal) {
                populateMakerOverworldPortalInspector(
                    frame.wiring, maker, *frame.selectedLink);
            } else {
                populateMakerLinkInspector(
                    frame.wiring, editor, *frame.selectedLink);
            }
        }
        const auto project = [&](LinkEndpoint endpoint) -> std::optional<Vec2> {
            if (!frame.canPick ||
                ((maker.linkLayerIsolated() || editor.layerLocked()) &&
                 std::cmp_not_equal(endpoint.cell.z, editor.activeLayer())))
                return std::nullopt;
            if (maker.overworld() &&
                !makerScreenContains(editor, endpoint.cell)) {
                const auto width = static_cast<int>(editor.documentWidth());
                const auto height = static_cast<int>(editor.documentHeight());
                if (!maker.showNeighbors() || endpoint.cell.x < -width ||
                    endpoint.cell.x >= width * 2 || endpoint.cell.y < -height ||
                    endpoint.cell.y >= height * 2)
                    return std::nullopt;
            }
            return host_.projectToScene(
                makerLinkAnchor(
                    maker, endpoint, assetManifest_, presentationSettings_),
                frame.scene);
        };
        const auto highlight = [](LinkKind kind) -> Vec4 {
            if (kind == LinkKind::Portal) return { 0.68f, 0.79f, 1.0f, 1.0f };
            if (kind == LinkKind::Object) return { 1.0f, 0.76f, 0.45f, 1.0f };
            return { 0.54f, 0.86f, 0.77f, 1.0f };
        };
        if (frame.selectedLink) {
            const auto from = project(*frame.selectedLink);
            auto color = highlight(frame.selectedLink->kind);
            if (frame.wiring.inspector.groupColor) {
                const auto linear = ui::color::displayLinear(
                    *frame.wiring.inspector.groupColor);
                color = { linear.x, linear.y, linear.z, 1.0f };
            }
            if (from) frame.wiring.markers.push_back({ *from, color, true });
            for (const auto peer : frame.wiring.peers) {
                const auto cell = peer.cell;
                std::string member(
                    makerTileGuide::name(makerLinkTile(maker, peer)));
                if (maker.overworld() && peer.kind == LinkKind::Portal) {
                    if (const auto location =
                            makerOverworldCellAt(maker, cell)) {
                        member += "  \xc2\xb7  Screen " +
                            std::to_string(location->screen);
                    }
                } else {
                    member += "  " + std::to_string(cell.x) + ", " +
                        std::to_string(cell.y) + "  \xc2\xb7  Layer " +
                        std::to_string(cell.z + 1);
                }
                frame.wiring.members.push_back(std::move(member));
                const auto to = project(peer);
                if (!to) continue;
                frame.wiring.markers.push_back({ *to, color, false });
                if (!from) continue;
                frame.wiring.lines.push_back(
                    { *from,
                      *to,
                      color,
                      frame.wiring.lines.empty()
                          ? std::string_view(frame.wiring.connectionLabel)
                          : std::string_view {} });
            }
        }
        if (const auto endpoint = frame.hovered
                ? maker.linkTargetAt(*frame.hovered)
                : std::nullopt;
            endpoint && endpoint != frame.selectedLink &&
            std::ranges::find(frame.wiring.peers, *endpoint) ==
                frame.wiring.peers.end()) {
            if (const auto marker = project(*endpoint))
                frame.wiring.markers.push_back(
                    { *marker, highlight(endpoint->kind), false });
            if (const auto target = makerLinkTile(maker, *endpoint);
                target != frame.boardTile &&
                makerScreenContains(editor, endpoint->cell)) {
                frame.coveredLinkHint = "Connect to " +
                    std::string(makerTileGuide::name(target)) +
                    " under this tile";
            }
        }
        frame.wiring.inspector.title = frame.wiring.title;
        frame.wiring.inspector.members = frame.wiring.members;
        frame.wiring.inspector.groups = frame.wiring.groupNames;
        frame.wiring.inspector.groupColors = frame.wiring.groupColors;
    }
}

void MakerController::buildSelectorOverlay(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    frame.hoveredSelector = frame.hovered && maker.overworld()
        ? maker.levelSelectorAt(*frame.hovered)
        : nullptr;
    if (frame.ordering) {
        const auto source = maker.selectedLevelSelectorSource();
        frame.wiring.inspector.kind = MakerLinkInspector::Kind::Selector;
        frame.wiring.inspector.canClear = source.has_value();
        frame.wiring.inspector.detail = source
            ? "Click the level this prerequisite unlocks. Click the same "
              "destination again to remove the connection."
            : "Click a Level Selector to choose the prerequisite.";
        if (source) {
            if (const auto* selector = maker.levelSelectorAt(*source)) {
                frame.wiring.title = std::string(selectorLabel(selector));
            } else {
                frame.wiring.title = "Prerequisite selected";
            }
        }
        const auto projectSelector =
            [&](GridPosition3 cell) -> std::optional<Vec2> {
            if (!frame.canPick || !maker.levelSelectorAt(cell) ||
                (editor.layerLocked() &&
                 std::cmp_not_equal(cell.z, editor.activeLayer())))
                return std::nullopt;
            return host_.projectToScene(
                { static_cast<float>(cell.x) + 0.5f,
                  static_cast<float>(cell.y) + 0.5f,
                  static_cast<float>(cell.z) + 0.12f },
                frame.scene);
        };
        constexpr Vec4 connected { 0.54f, 0.86f, 0.77f, 1.0f };
        constexpr Vec4 selected { 1.0f, 0.77f, 0.43f, 1.0f };
        for (const auto& connection : maker.levelSelectorConnections()) {
            const auto from = projectSelector(connection.source);
            const auto to = projectSelector(connection.destination);
            const bool highlight =
                connection.source == source || connection.destination == source;
            const Vec4 color = highlight ? selected : connected;
            if (from && to)
                frame.wiring.lines.push_back(
                    { *from, *to, color, highlight ? "Unlocks" : "", true });
            if (source == connection.source) {
                if (to) frame.wiring.markers.push_back({ *to, color, false });
                frame.wiring.members.push_back(
                    "Unlocks " +
                    std::string(selectorLabel(
                        maker.levelSelectorAt(connection.destination))));
            } else if (source == connection.destination) {
                if (from)
                    frame.wiring.markers.push_back({ *from, color, false });
                frame.wiring.members.push_back(
                    "Requires " +
                    std::string(selectorLabel(
                        maker.levelSelectorAt(connection.source))));
            }
        }
        if (source) {
            if (const auto from = projectSelector(*source))
                frame.wiring.markers.push_back({ *from, selected, true });
        }
        if (frame.hoveredSelector && frame.hovered != source) {
            if (const auto to = projectSelector(*frame.hovered))
                frame.wiring.markers.push_back({ *to, connected, false });
            frame.coveredLinkHint = selectorLabel(frame.hoveredSelector);
        }
        frame.wiring.inspector.title = frame.wiring.title;
        frame.wiring.inspector.members = frame.wiring.members;
    }
}

void MakerController::buildInspectionMarkers(WorkspaceFrame& frame)
{
    const MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    if (frame.inspecting && frame.canPick && !frame.modalOpen) {
        const auto mark = [&](GridPosition3 cell,
                              TileType tile,
                              bool selected) {
            const auto visual =
                tileVisual(tile, cell, assetManifest_, presentationSettings_);
            const auto position = host_.projectToScene(
                {
                    visual.position.x + visual.size.x * 0.5f,
                    visual.position.y + visual.size.y * 0.5f,
                    visual.baseElevation + visual.height * 0.65f,
                },
                frame.scene);
            if (position)
                frame.wiring.markers.push_back(
                    { *position, { 1.0f, 0.78f, 0.35f, 1.0f }, selected });
        };
        if (frame.properties &&
            (!editor.layerLocked() ||
             std::cmp_equal(frame.properties->cell.z, editor.activeLayer()))) {
            mark(frame.properties->cell, frame.properties->tile, true);
        }
        if (frame.hovered && makerScreenContains(editor, *frame.hovered) &&
            frame.boardTile != TileType::Air &&
            (!frame.properties || *frame.hovered != frame.properties->cell)) {
            mark(*frame.hovered, frame.boardTile, false);
        }
    }
}

void MakerController::resolvePlacementHint(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    const auto& keys = frame.input.makerEditor;
    frame.brush = editor.selectedTile();
    const auto hoverStrokeMode =
        maker.strokeActive() ? maker.strokeMode() : frame.strokeMode;
    frame.showPlacementHint =
        (frame.painting ||
         hoverStrokeMode == MakerSession::StrokeMode::Replace) &&
        !frame.moving && !makerStrokeErases(maker, hoverStrokeMode) && hover_ &&
        !view_.tileSearchFocused();
    frame.newOverworldScreen = frame.showPlacementHint && maker.overworld() &&
        !makerScreenContains(editor, *hover_) &&
        !makerOverworldCellAt(maker, *hover_);
    frame.placementError = frame.showPlacementHint &&
            !maker.placingSelector() && makerScreenContains(editor, *hover_)
        ? editor.tilePlacementError(*hover_, frame.brush)
        : std::string_view {};
    if (frame.hoveredSelector && frame.painting && !frame.moving &&
        !keys.pickModifier && !maker.strokeActive() &&
        hoverStrokeMode == MakerSession::StrokeMode::Brush) {
        frame.selectorHint = std::string(selectorLabel(frame.hoveredSelector)) +
            "  \xc2\xb7  Click to attach a level";
    }
}

std::string_view MakerController::placementHint(
    const WorkspaceFrame& frame) const
{
    const MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    if (frame.inspecting && maker.overworld() && frame.hovered &&
        !makerScreenContains(editor, *frame.hovered)) {
        return "Switch to this screen to edit its tile properties.";
    }
    if (frame.area.mode == AreaMode::Paste && frame.areaHovered &&
        !frame.validPaste) {
        return maker.overworld() ? "The copied area must fit on this screen."
                                 : "The copied area must fit on the board.";
    }
    if (!frame.selectorHint.empty()) return frame.selectorHint;
    if (!frame.showPlacementHint) return frame.coveredLinkHint;
    if (!frame.placementError.empty()) return frame.placementError;
    if (frame.newOverworldScreen)
        return "Paint here to create an overworld screen.";
    if (maker.placingSelector())
        return makerTileGuide::levelSelector.placementHint;
    return makerTileGuide::forTile(frame.brush, maker.overworld())
        .placementHint;
}

void MakerController::buildPropertyPanel(WorkspaceFrame& frame)
{
    if (frame.properties) {
        std::size_t directionCount = 0;
        for (std::size_t direction = 0;
             direction < frame.minecartDirections.size();
             ++direction) {
            if ((frame.properties->minecartDirectionsMask &
                 (1U << direction)) != 0) {
                frame.minecartDirections[directionCount++] =
                    static_cast<uint8_t>(direction);
            }
        }
        if (const auto& route = frame.properties->minecartRoute) {
            frame.minecartRoute = route->loop ? "Loop" : "Out and back";
            frame.minecartRoute += " | " + std::to_string(route->cells) +
                " cells, " + std::to_string(route->stops) +
                (route->stops == 1 ? " stop" : " stops");
        }
        for (const int layer : frame.properties->elevatorStops) {
            if (!frame.elevatorStops.empty()) frame.elevatorStops += ", ";
            frame.elevatorStops += std::to_string(layer + 1);
        }
        frame.propertyPanel = {
            .cell = frame.properties->cell,
            .tile = frame.properties->tile,
            .gateStartOpen = frame.properties->gateStartOpen,
            .lockPlateStartEnabled = frame.properties->lockPlateStartEnabled,
            .minecartDirection = frame.properties->minecartDirection,
            .minecartDirections =
                std::span(frame.minecartDirections).first(directionCount),
            .minecartRoute = frame.minecartRoute,
            .elevatorStopOnLayer = frame.properties->elevatorStopOnLayer,
            .elevatorStops = frame.elevatorStops,
        };
    }
}

void MakerController::resolveFrameRegion(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    frame.frameRegion = frame.areaSelection;
    const auto frameCell = [&]() -> std::optional<GridPosition3> {
        if (frame.properties) return frame.properties->cell;
        if (frame.selectedLink) return frame.selectedLink->cell;
        if (frame.ordering && maker.selectedLevelSelectorSource())
            return maker.selectedLevelSelectorSource();
        return frame.areaSelection ? std::nullopt : frame.cursorCell;
    }();
    if (frameCell &&
        (makerScreenContains(editor, *frameCell) ||
         (maker.overworld() && maker.showNeighbors() &&
          frameCell->x >= -static_cast<int>(editor.documentWidth()) &&
          frameCell->x < 2 * static_cast<int>(editor.documentWidth()) &&
          frameCell->y >= -static_cast<int>(editor.documentHeight()) &&
          frameCell->y < 2 * static_cast<int>(editor.documentHeight())))) {
        frame.frameRegion = MakerSession::Region { *frameCell, *frameCell };
    }
}

void MakerController::drawWorkspaceView(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    frame.viewInput = MakerViewInput {
        .wheelY = frame.input.pointer.wheel.y,
        .wheelPosition = frame.wheelPosition,
        .navigation = frame.input.maker,
        .extendTextSelection = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0,
        .interactive = !frame.modalOpen && !frame.navigating,
        .tileThumbnail =
            [this](TileType tile) { return host_.tileThumbnail(tile); },
    };
    frame.action = view_.draw(ui_, frame.layout, {
        .brush = frame.brush,
        .quarterTurns = editor.selectedTileQuarterTurns(),
        .layer = editor.activeLayer(),
        .layerCount = editor.documentDepth(),
        .erasing = maker.tool() == MakerSession::Tool::Erase,
        .canUndo = maker.canUndo(),
        .canRedo = maker.canRedo(),
        .hovered = frame.area.mode ? frame.areaHovered : frame.connecting || frame.ordering || frame.inspecting ? frame.hovered : hover_,
        .status = maker.status().empty() && host_.boardAssetsLoading()
            ? "Loading board assets" : maker.status(),
        .draftName = maker.draftName(),
        .unsavedChanges = maker.hasUnsavedChanges(),
        .placementHint = placementHint(frame),
        .placementBlocked = !frame.placementError.empty() || (frame.area.mode == AreaMode::Paste && frame.areaHovered && !frame.validPaste),
        .boardTile = frame.boardTile == TileType::Air ? std::nullopt : std::optional { frame.boardTile },
        .linkInspector = frame.connecting || frame.ordering ? std::optional { frame.wiring.inspector } : std::nullopt,
        .linkLines = frame.wiring.lines,
        .linkMarkers = frame.wiring.markers,
        .linkViewport = frame.scene,
        .linkLayerIsolated = maker.linkLayerIsolated(),
        .layerLocked = editor.layerLocked(),
        .overworld = maker.overworld(),
        .showNeighbors = maker.showNeighbors(),
        .overworldScreen = maker.overworld() ? GridPosition { maker.currentScreenSlot().x, maker.currentScreenSlot().y }
                                            : GridPosition {},
        .placingSelector = maker.placingSelector(),
        .hasMultipleOverworldScreens = maker.overworld() && maker.overworldLayout().screens.size() > 1,
        .area = frame.area,
        .panel = panel_,
        .layerPanel = {
            .waterLayer = editor.waterLayer(),
            .canAdd = panel_ == MakerPanel::Layers && maker.canAddLayer(),
            .canDelete = panel_ == MakerPanel::Layers && maker.canDeleteLayer(),
            .canSetWater = panel_ == MakerPanel::Layers,
        },
        .propertyPanel = frame.propertyPanel,
        .topDown = viewportCamera_.topDown(),
        .canFrameSelection = frame.frameRegion.has_value(),
        .showViewportControls = !cameraDialog_ &&
            !(draftDialog_ && showsMakerResizePreview(draftDialog_->mode)),
        .problems = frame.showingProblems ? std::span<const MakerSession::Problem>(problems_) :
            std::span<const MakerSession::Problem> {},
        .problemMarker = frame.problemMarker,
        .boardCursorFocused = boardCursor_.has_value(),
        .boardCursorOutline = frame.cursorOutline,
        .boardCursorHint = frame.cursorCell ? boardCursorHint() : std::string_view {},
    }, frame.viewInput);
}

bool MakerController::drawModal(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    if (overworldMapOpen_) {
        MakerViewInput mapInput = frame.viewInput;
        mapInput.interactive = true;
        drawOverworldMap(frame.viewport, frame.scale, mapInput);
        return true;
    }
    if (helpOpen_) {
        MakerViewInput helpInput = frame.viewInput;
        helpInput.navigation.previous |= navigationKeys_.up.pressed;
        helpInput.navigation.next |= navigationKeys_.down.pressed;
        helpInput.navigation.activate |= navigationKeys_.activatePressed;
        if (view_.drawHelp(
                ui_,
                frame.viewport,
                frame.scale,
                input_.bindings(),
                helpInput) ||
            input_.actionPressed(InputAction::MenuBack)) {
            helpOpen_ = false;
            ui_.cancelPointerInteraction();
        }
        return true;
    }
    if (colorDialog_) {
        drawColorDialog(frame.viewport, frame.scale, frame.viewInput);
        return true;
    }
    if (cameraDialog_) {
        drawCameraDialog(
            frame.layout,
            frame.viewInput,
            frame.canPick && host_.sceneCamera()->valid);
        return true;
    }
    if (auto& dialog = draftDialog_) {
        MakerViewInput dialogInput = frame.viewInput;
        dialogInput.interactive = true;
        const bool resizePreview = showsMakerResizePreview(dialog->mode);
        if (isMakerDraftPicker(dialog->mode)) {
            const auto pickerNavigation =
                std::exchange(dialog->pickerNavigation, {});
            if (dialog->mode == MakerDraftDialogMode::Load) {
                if (input_.activeDevice() == ActiveInputDevice::Gamepad) {
                    dialogInput.pickerRowActionStep =
                        static_cast<int>(
                            input_.actionPressed(InputAction::MoveRight)) -
                        static_cast<int>(
                            input_.actionPressed(InputAction::MoveLeft));
                    dialogInput.navigation.previous =
                        input_.actionPressed(InputAction::MoveUp);
                    dialogInput.navigation.next =
                        input_.actionPressed(InputAction::MoveDown);
                } else if (!view_.draftNameFocused()) {
                    dialogInput.pickerRowActionStep =
                        static_cast<int>(navigationKeys_.right.pressed) -
                        static_cast<int>(navigationKeys_.left.pressed);
                }
            }
            dialogInput.navigation.previous |=
                pickerNavigation.previous || navigationKeys_.up.pressed;
            dialogInput.navigation.next |=
                pickerNavigation.next || navigationKeys_.down.pressed;
            dialogInput.navigation.activate |=
                pickerNavigation.activate || navigationKeys_.activatePressed;
        }
        if (dialog->mode == MakerDraftDialogMode::NewDraft ||
            dialog->mode == MakerDraftDialogMode::DiscardNew ||
            dialog->mode == MakerDraftDialogMode::ConfirmDeleteLayer ||
            dialog->mode == MakerDraftDialogMode::DeleteDraft ||
            dialog->recoveryKind || resizePreview) {
            dialogInput.navigation.activate |= navigationKeys_.activatePressed;
        }
        const auto pointerAction = view_.drawDraftDialog(
            ui_,
            frame.viewport,
            frame.scale,
            {
                .mode = dialog->mode,
                .names = dialog->names,
                .error = dialog->error,
                .pendingName = dialog->pendingName,
                .removedTileCount = dialog->removedTileCount,
                .resizePreviewPanel = resizePreview
                    ? std::optional { frame.layout.palette }
                    : std::nullopt,
                .legacySelectorTarget = dialog->legacySelectorTarget,
                .layer = dialog->layer,
                .removedSelectorCount = dialog->layerLoss.selectors,
                .removedDecorationCount = dialog->layerLoss.decorations,
                .removedElevatorStopCount = dialog->layerLoss.elevatorStops,
                .removesWaterLayer = dialog->layerLoss.water,
                .overworld = dialog->recoveryKind
                    ? *dialog->recoveryKind == MakerRecoveryKind::Overworld
                    : maker.overworld(),
                .canRecoverAutosave = !dialog->recoveryKind ||
                    autosaves_[makerRecoveryIndex(*dialog->recoveryKind)]
                        .pending.has_value(),
                .otherAutosaveAvailable = dialog->recoveryKind &&
                    autosaves_[1 - makerRecoveryIndex(*dialog->recoveryKind)]
                        .unresolved(),
                .missingSelectorTarget = dialog->missingSelectorTarget,
            },
            dialogInput);
        // Dismiss after drawing the modal so the frame's pointer edges never
        // reach the board.
        const auto inputAction = std::exchange(dialog->inputAction, {});
        handleDraftDialogAction(
            inputAction.command != MakerDraftDialogAction::Command::None
                ? inputAction
                : pointerAction);
        return true;
    }
    return false;
}

void MakerController::refreshLayerView()
{
    viewport_.reset();
    hover_.reset();
    areaDragAnchor_.reset();
    boardReveal_ = boardCursor_.has_value();
    ui_.cancelPointerInteraction();
}

bool MakerController::applyCommand(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const LevelEditor& editor = maker.editor();
    const auto& keys = frame.input.makerEditor;
    if (frame.navigating || view_.toolbarOverflowOpen() ||
        (frame.toolbarOverflowOpen &&
         frame.action.command == MakerCommand::None))
        return true;

    const MakerCommand command = frame.action.command == MakerCommand::None
        ? makerShortcutCommand(frame.input, maker)
        : frame.action.command;
    if (command == MakerCommand::None && !frame.action.tile) {
        if (keys.toggleLayerLockPressed) {
            maker.toggleLayerLock();
            viewport_.reset();
        } else if (!frame.inspecting && keys.recentTileSlot) {
            if (maker.selectRecentTile(*keys.recentTileSlot) &&
                frame.connecting && maker.linkLayerIsolated()) {
                viewport_.reset();
            }
        }
    }
    switch (command) {
    case MakerCommand::None:
        break;
    case MakerCommand::ToggleToolbarOverflow:
        return true;
    case MakerCommand::ToggleBoardCursor:
        toggleBoardCursor(frame.pointerCell);
        return true;
    case MakerCommand::ShowPalette:
        if (maker.tool() == MakerSession::Tool::Connect ||
            maker.tool() == MakerSession::Tool::ConnectSelectors) {
            maker.setTool(MakerSession::Tool::Paint);
        }
        setPanel(MakerPanel::Palette);
        return true;
    case MakerCommand::ShowLayers:
        setPanel(MakerPanel::Layers);
        return true;
    case MakerCommand::ShowProperties:
        setPanel(MakerPanel::Properties);
        return true;
    case MakerCommand::ShowProblems:
        setPanel(MakerPanel::Problems);
        return true;
    case MakerCommand::JumpToProblem:
        if (frame.action.problem && *frame.action.problem < problems_.size()) {
            const auto problem = problems_[*frame.action.problem];
            refreshProblems(true);
            if (std::ranges::find(problems_, problem) != problems_.end())
                jumpToProblem(problem);
        }
        return true;
    case MakerCommand::Back:
        host_.requestBack();
        return true;
    case MakerCommand::ToggleOverworldMode:
        toggleOverworld();
        return true;
    case MakerCommand::OverworldMap:
        if (!maker.overworld()) break;
        maker.endStroke();
        maker.cancelMove();
        host_.blurTextInput();
        hover_.reset();
        areaDragAnchor_.reset();
        ui_.cancelPointerInteraction();
        view_.beginOverworldMap();
        overworldMapOpen_ = true;
        return true;
    case MakerCommand::ToggleTopDown:
    case MakerCommand::FrameSelection:
        maker.endStroke();
        maker.cancelMove();
        areaDragAnchor_.reset();
        hover_.reset();
        ui_.cancelPointerInteraction();
        if (command == MakerCommand::ToggleTopDown)
            viewportCamera_.toggleTopDown();
        else if (frame.frameRegion)
            viewportCamera_.frameSelection(
                frame.frameRegion->min, frame.frameRegion->max);
        else
            viewportCamera_.resetFraming();
        return true;
    case MakerCommand::ToggleOverworldNeighbors:
        maker.endStroke();
        maker.cancelMove();
        maker.toggleNeighbors();
        resetViewportFraming();
        break;
    case MakerCommand::PreviousOverworldScreen:
    case MakerCommand::NextOverworldScreen: {
        const auto& screens = maker.overworldLayout().screens;
        const auto current = std::ranges::find(
            screens, maker.currentScreenId(), &OverworldScreenSpec::id);
        if (current != screens.end()) {
            const auto count = static_cast<std::size_t>(screens.size());
            const auto index =
                static_cast<std::size_t>(current - screens.begin());
            const auto offset =
                command == MakerCommand::NextOverworldScreen ? 1 : count - 1;
            if (maker.selectOverworldScreen(
                    screens[(index + offset) % count].id)) {
                maker.clearTileInspection();
                resetViewportFraming();
            }
        }
        break;
    }
    case MakerCommand::PaintSelector:
        maker.selectLevelSelectorBrush();
        break;
    case MakerCommand::Help:
        maker.endStroke();
        maker.cancelMove();
        host_.blurTextInput();
        ui_.cancelPointerInteraction();
        hover_.reset();
        view_.beginHelp();
        helpOpen_ = true;
        return true;
    case MakerCommand::Camera:
        maker.endStroke();
        maker.cancelMove();
        host_.blurTextInput();
        ui_.cancelPointerInteraction();
        hover_.reset();
        view_.beginCameraDialog();
        cameraDialog_ = CameraDialog { editor.cameraAngles() };
        return true;
    case MakerCommand::Paint:
    case MakerCommand::Erase:
    case MakerCommand::Connect:
    case MakerCommand::ConnectSelectors:
    case MakerCommand::SelectArea:
    case MakerCommand::FloodFill:
    case MakerCommand::PasteArea: {
        const auto tool = command == MakerCommand::Paint
            ? MakerSession::Tool::Paint
            : command == MakerCommand::Erase   ? MakerSession::Tool::Erase
            : command == MakerCommand::Connect ? MakerSession::Tool::Connect
            : command == MakerCommand::ConnectSelectors
            ? MakerSession::Tool::ConnectSelectors
            : command == MakerCommand::SelectArea
            ? MakerSession::Tool::SelectArea
            : command == MakerCommand::FloodFill
            ? MakerSession::Tool::FloodFill
            : MakerSession::Tool::PasteArea;
        if (tool == MakerSession::Tool::PasteArea && !maker.hasClipboard())
            break;
        if (frame.inspecting ||
            (!frame.showingProblems && tool != MakerSession::Tool::Paint &&
             tool != MakerSession::Tool::Erase)) {
            setPanel(MakerPanel::Palette);
        }
        if (tool != maker.tool()) {
            if (maker.linkLayerIsolated()) viewport_.reset();
            if (makerAreaMode(tool) || makerAreaMode(maker.tool())) {
                areaDragAnchor_.reset();
                ui_.cancelPointerInteraction();
            }
        }
        maker.setTool(tool);
        break;
    }
    case MakerCommand::FillSelection:
        if (!frame.inspecting) (void)maker.fillSelection();
        break;
    case MakerCommand::EraseSelection:
        if (!frame.inspecting) (void)maker.clearSelectionTiles();
        break;
    case MakerCommand::CopySelection:
        if (!frame.inspecting) (void)maker.copySelection();
        break;
    case MakerCommand::CutSelection:
        if (!frame.inspecting) (void)maker.cutSelection();
        break;
    case MakerCommand::Disconnect:
        (void)maker.disconnectSelectedLink();
        break;
    case MakerCommand::ClearLinkSelection:
        if (frame.ordering)
            maker.clearLevelSelectorSource();
        else
            maker.clearLinkSelection();
        break;
    case MakerCommand::NewLinkGroup:
        openDraftDialog(MakerDraftDialogMode::NameLinkGroup);
        return true;
    case MakerCommand::RenameLinkGroup:
        if (frame.wiring.inspector.activeGroup) {
            openDraftDialog(
                MakerDraftDialogMode::RenameLinkGroup,
                frame.wiring.groupNames[*frame.wiring.inspector.activeGroup]);
        }
        return true;
    case MakerCommand::EditLinkGroupColor:
        if (frame.selectedLink && frame.wiring.inspector.groupColor) {
            openColorDialog(
                *frame.selectedLink,
                *frame.wiring.inspector.groupColor,
                frame.wiring.connectionLabel);
        }
        return true;
    case MakerCommand::AssignLinkGroup:
        if (frame.action.linkGroup &&
            *frame.action.linkGroup < frame.wiring.groupColors.size()) {
            (void)maker.assignSelectedLinkGroup(
                frame.wiring.groupColors[*frame.action.linkGroup]);
        }
        break;
    case MakerCommand::SelectOtherLinkEndpoint:
        if (frame.selectedLink) {
            if (const auto other =
                    otherMakerLinkEndpoint(editor, *frame.selectedLink)) {
                (void)maker.selectLinkEndpoint(*other);
            }
        }
        break;
    case MakerCommand::Rotate:
        if (!frame.inspecting) maker.rotateTile();
        break;
    case MakerCommand::LayerDown:
    case MakerCommand::LayerUp:
        maker.stepLayer(command == MakerCommand::LayerDown ? -1 : 1);
        areaDragAnchor_.reset();
        boardReveal_ = boardCursor_.has_value();
        if (editor.layerLocked() ||
            (frame.connecting && maker.linkLayerIsolated())) {
            viewport_.reset();
        }
        break;
    case MakerCommand::SelectLayer:
        if (frame.action.layer && maker.selectLayer(*frame.action.layer))
            refreshLayerView();
        break;
    case MakerCommand::AddLayerAbove:
    case MakerCommand::AddLayerBelow:
        if (maker.addLayer(command == MakerCommand::AddLayerAbove))
            refreshLayerView();
        break;
    case MakerCommand::DeleteLayer: {
        if (!maker.canDeleteLayer()) break;
        const auto loss = maker.layerLoss();
        if (loss.any()) {
            openDraftDialog(MakerDraftDialogMode::ConfirmDeleteLayer);
            draftDialog_->layer = editor.activeLayer();
            draftDialog_->layerLoss = loss;
            draftDialog_->removedTileCount = loss.tiles;
        } else if (maker.deleteLayer()) {
            refreshLayerView();
        }
        return true;
    }
    case MakerCommand::ToggleWaterLayer:
        if (maker.setWaterLayer(editor.waterLayer() != editor.activeLayer()))
            refreshLayerView();
        break;
    case MakerCommand::ToggleLayerLock:
        maker.toggleLayerLock();
        refreshLayerView();
        break;
    case MakerCommand::ToggleLinkLayer:
        maker.toggleLinkLayer();
        viewport_.reset();
        break;
    case MakerCommand::ToggleGateStartOpen:
        if (frame.properties && frame.properties->gateStartOpen)
            (void)maker.setInspectedGateStartOpen(
                !*frame.properties->gateStartOpen);
        break;
    case MakerCommand::ToggleLockPlateStartEnabled:
        if (frame.properties && frame.properties->lockPlateStartEnabled) {
            (void)maker.setInspectedLockPlateStartEnabled(
                !*frame.properties->lockPlateStartEnabled);
        }
        break;
    case MakerCommand::SetMinecartDirection:
        if (frame.action.minecartDirection)
            (void)maker.setInspectedMinecartDirection(
                *frame.action.minecartDirection);
        break;
    case MakerCommand::ToggleElevatorStop:
        if (frame.inspecting)
            (void)maker.toggleInspectedElevatorStop();
        else
            (void)maker.toggleSelectedElevatorStop();
        break;
    case MakerCommand::Undo:
    case MakerCommand::Redo: {
        const auto width = editor.documentWidth();
        const auto height = editor.documentHeight();
        const auto depth = editor.documentDepth();
        const auto water = editor.waterLayer();
        const auto angles = editor.cameraAngles();
        const auto screen =
            maker.overworld() ? maker.currentScreenId() : OverworldScreenId {};
        const auto slot =
            maker.overworld() ? maker.currentScreenSlot() : OverworldSlot {};
        const auto screenCount =
            maker.overworld() ? maker.overworldLayout().screens.size() : 0U;
        if (command == MakerCommand::Undo)
            maker.undo();
        else
            maker.redo();
        if (editor.documentWidth() != width ||
            editor.documentHeight() != height ||
            editor.documentDepth() != depth || editor.waterLayer() != water ||
            editor.cameraAngles() != angles ||
            (maker.overworld() &&
             (maker.currentScreenId() != screen ||
              maker.currentScreenSlot() != slot ||
              maker.overworldLayout().screens.size() != screenCount))) {
            resetViewportFraming();
        }
        break;
    }
    case MakerCommand::NewDraft:
        openDraftDialog(MakerDraftDialogMode::NewDraft);
        return true;
    case MakerCommand::ResizeBoard:
        openDraftDialog(MakerDraftDialogMode::ResizeBoard);
        return true;
    case MakerCommand::Save:
        openDraftDialog(MakerDraftDialogMode::Save, maker.draftName());
        return true;
    case MakerCommand::Load:
        openDraftDialog(MakerDraftDialogMode::Load);
        return true;
    case MakerCommand::TestPlay:
        startPlaytest();
        return true;
    case MakerCommand::ReturnToMaker:
        break;
    }
    if (frame.action.tile) {
        maker.selectTile(*frame.action.tile);
        if (frame.connecting && maker.linkLayerIsolated()) viewport_.reset();
    }
    if (!viewport_) {
        hover_.reset();
        return true;
    }
    return false;
}

void MakerController::handleBoardPointer(WorkspaceFrame& frame)
{
    MakerSession& maker = *session_;
    const auto& keys = frame.input.makerEditor;
    if (frame.cursorCell &&
        (frame.input.makerBoard.activatePressed ||
         frame.input.makerBoard.contextPressed)) {
        activateBoardCursor(*frame.cursorCell, frame.input);
        return;
    }
    if (boardCursor_ &&
        (ui_.clicked(frame.scene) ||
         (keys.secondaryPressed && ui_.hovered(frame.scene)))) {
        clearBoardCursor();
    }

    if (panel_ == MakerPanel::Properties) {
        if (ui_.clicked(frame.scene)) {
            if (const auto cell =
                    pickBoardCell(frame, *ui_.mousePressPosition()))
                (void)maker.inspectTile(*cell);
            else
                maker.clearTileInspection();
        }
        return;
    }

    if (maker.overworld() && keys.secondaryPressed && frame.hovered &&
        openOverworldItem(*frame.hovered)) {
        return;
    }

    if (const auto mode = makerAreaMode(maker.tool());
        mode && !keys.pickModifier && !frame.moving) {
        if (mode == AreaMode::Rectangle) {
            const auto drag = view_.tileSearchFocused()
                ? std::nullopt
                : ui_.dragPosition("maker.area", frame.scene);
            if (const auto press = ui_.mousePressPosition(); drag && press) {
                areaDragAnchor_ = pickAreaCell(frame, *press);
            }
            if (drag && areaDragAnchor_ && frame.scene.size.x >= 1.0f &&
                frame.scene.size.y >= 1.0f) {
                const Vec2 corner {
                    std::clamp(
                        drag->x,
                        frame.scene.position.x,
                        frame.scene.position.x + frame.scene.size.x - 1.0f),
                    std::clamp(
                        drag->y,
                        frame.scene.position.y,
                        frame.scene.position.y + frame.scene.size.y - 1.0f),
                };
                if (const auto cell = pickAreaCell(frame, corner);
                    cell && !maker.selectArea(*areaDragAnchor_, *cell)) {
                    areaDragAnchor_.reset();
                }
            }
            if (!drag || !ui_.mouseDown()) areaDragAnchor_.reset();
        } else if (!view_.tileSearchFocused() && ui_.clicked(frame.scene)) {
            if (const auto cell =
                    pickAreaCell(frame, *ui_.mousePressPosition())) {
                if (mode == AreaMode::FloodFill)
                    (void)maker.floodFill(*cell);
                else
                    (void)maker.pasteSelection(*cell);
            }
        }
        hover_.reset();
        return;
    }
    areaDragAnchor_.reset();

    if (keys.pickModifier || frame.moving) {
        maker.endStroke();
        if (ui_.clicked(frame.scene)) {
            if (const auto cell =
                    pickBoardCell(frame, *ui_.mousePressPosition())) {
                if (frame.moving)
                    (void)maker.moveCell(*cell);
                else {
                    const bool isolated =
                        maker.tool() == MakerSession::Tool::Connect &&
                        maker.linkLayerIsolated();
                    if (maker.pickTile(*cell) && isolated) viewport_.reset();
                }
            }
        }
        hover_ = resolveHover(frame);
        return;
    }

    const auto tool = maker.tool();
    if (frame.strokeMode == MakerSession::StrokeMode::Brush &&
        !maker.strokeActive() &&
        (tool == MakerSession::Tool::Connect ||
         tool == MakerSession::Tool::ConnectSelectors)) {
        if (ui_.clicked(frame.scene)) {
            if (const auto cell =
                    pickBoardCell(frame, *ui_.mousePressPosition())) {
                if (tool == MakerSession::Tool::ConnectSelectors)
                    (void)maker.clickLevelSelectorConnection(*cell);
                else
                    (void)maker.connectCell(*cell);
            }
        }
        return;
    }
    if (maker.overworld() && tool == MakerSession::Tool::Paint &&
        frame.strokeMode == MakerSession::StrokeMode::Brush &&
        !maker.strokeActive() && ui_.clicked(frame.scene)) {
        if (const auto cell = pickBoardCell(frame, *ui_.mousePressPosition());
            cell && openOverworldItem(*cell, true))
            return;
    }

    const auto drag = view_.tileSearchFocused()
        ? std::nullopt
        : ui_.dragPosition("maker.board", frame.scene);
    if (drag) {
        if (const auto press = ui_.mousePressPosition()) {
            maker.endStroke();
            if (const auto cell = pickBoardCell(frame, *press))
                maker.beginStroke(*cell, frame.strokeMode);
        }
        maker.continueStroke(pickBoardCell(frame, *drag), keys.lineConstraint);
    }
    if (maker.strokeActive() && (!drag || !ui_.mouseDown())) {
        maker.endStroke();
    }
    hover_ = resolveHover(frame);
}

} // namespace sokoban
