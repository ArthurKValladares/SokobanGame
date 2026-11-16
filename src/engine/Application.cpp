#include "engine/Application.hpp"

#include "engine/Profiler.hpp"
#if SOKOBAN_ENABLE_DEBUG_UI
#include "engine/ApplicationTools.hpp"
#include "engine/AtomicFile.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/DebugUi.hpp"
#include "engine/DevSession.hpp"
#include "engine/LevelProjectStore.hpp"
#include "engine/SolutionStore.hpp"
#include <imgui.h>
#endif

#include "engine/ParticleConfig.hpp"
#include "engine/PerformanceFixtures.hpp"
#include "engine/GroundTileGeometry.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/CompressedTextureArtifact.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/FogOfWarConfig.hpp"
#include "engine/render/WaterGeometry.hpp"

#include "engine/Log.hpp"
#include "engine/PathText.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"
#include "engine/RuntimeContent.hpp"
#include "engine/UserSettingsConfig.hpp"
#include "engine/ui/MakerTileGuide.hpp"
#include "engine/ui/SelectorPrompt.hpp"
#include "engine/ui/UiColorValues.hpp"
#include "engine/ui/UiConfig.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <functional>
#include <limits>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

Vec2 scaleUiPosition(Vec2 position, Vec2 from, Vec2 to)
{
    return {
        from.x > 0.0f ? position.x * to.x / from.x : position.x,
        from.y > 0.0f ? position.y * to.y / from.y : position.y,
    };
}

UiRect fitSceneViewport(UiRect bounds, Vec2 source)
{
    const Vec2 start { std::ceil(bounds.position.x), std::ceil(bounds.position.y) };
    const Vec2 end {
        std::floor(bounds.position.x + bounds.size.x),
        std::floor(bounds.position.y + bounds.size.y),
    };
    const Vec2 available = end - start;
    if (source.x <= 0.0f || source.y <= 0.0f ||
        available.x <= 0.0f || available.y <= 0.0f) {
        return { start, {} };
    }
    const float scale = std::min(available.x / source.x, available.y / source.y);
    const Vec2 size { std::floor(source.x * scale), std::floor(source.y * scale) };
    const Vec2 spare = available - size;
    return {
        start + Vec2 { std::floor(spare.x * 0.5f), std::floor(spare.y * 0.5f) },
        size,
    };
}

bool showsMakerResizePreview(MakerDraftDialogMode mode)
{
    return mode == MakerDraftDialogMode::ResizeBoard || mode == MakerDraftDialogMode::ConfirmCrop;
}

bool isMakerDraftPicker(MakerDraftDialogMode mode)
{
    return mode == MakerDraftDialogMode::Load || mode == MakerDraftDialogMode::SelectorTarget;
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
        (mode == MakerSession::StrokeMode::Brush && maker.tool() == MakerSession::Tool::Erase);
}

std::optional<MakerAreaViewState::Mode> makerAreaMode(MakerSession::Tool tool)
{
    using Mode = MakerAreaViewState::Mode;
    switch (tool) {
    case MakerSession::Tool::SelectArea: return Mode::Rectangle;
    case MakerSession::Tool::FloodFill: return Mode::FloodFill;
    case MakerSession::Tool::PasteArea: return Mode::Paste;
    default: return std::nullopt;
    }
}

MakerCommand makerShortcutCommand(const InputRouter::Frame& input, const MakerSession& maker)
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
        case InputAction::MakerSelectArea: return MakerCommand::SelectArea;
        case InputAction::MakerFloodFill: return MakerCommand::FloodFill;
        case InputAction::MakerCopySelection: return MakerCommand::CopySelection;
        case InputAction::MakerCutSelection: return MakerCommand::CutSelection;
        case InputAction::MakerPasteArea: return MakerCommand::PasteArea;
        case InputAction::MakerEraseSelection: return MakerCommand::EraseSelection;
        default: break;
        }
    }
    if (keys.cycleToolPressed) {
        switch (maker.tool()) {
        case MakerSession::Tool::Paint: return MakerCommand::Erase;
        case MakerSession::Tool::Erase: return MakerCommand::Connect;
        case MakerSession::Tool::Connect: return maker.overworld() ? MakerCommand::ConnectSelectors : MakerCommand::SelectArea;
        case MakerSession::Tool::ConnectSelectors: return MakerCommand::SelectArea;
        case MakerSession::Tool::SelectArea: return MakerCommand::FloodFill;
        case MakerSession::Tool::FloodFill:
        case MakerSession::Tool::PasteArea: return MakerCommand::Paint;
        }
    }
    return MakerCommand::None;
}

using LinkEndpoint = LevelEditor::LinkEndpoint;
using LinkKind = LevelEditor::LinkEndpointKind;

Level::LinkGroupKind makerGroupKind(LinkKind kind)
{
    return kind == LinkKind::Object ? Level::LinkGroupKind::Object : Level::LinkGroupKind::Device;
}

TileType makerTileAt(const LevelEditor& editor, GridPosition3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        std::cmp_greater_equal(cell.x, editor.documentWidth()) ||
        std::cmp_greater_equal(cell.y, editor.documentHeight()) ||
        std::cmp_greater_equal(cell.z, editor.documentDepth())) {
        return TileType::Air;
    }
    const auto& row = editor.documentLayers()[static_cast<std::size_t>(cell.z)][static_cast<std::size_t>(cell.y)];
    const auto tile = charToTileType(row[static_cast<std::size_t>(cell.x)]).value_or(TileType::Air);
    return tile == TileType::Air && editor.waterLayer() == static_cast<uint32_t>(cell.z)
        ? TileType::Water : tile;
}

bool makerScreenContains(const LevelEditor& editor, GridPosition3 cell)
{
    return cell.x >= 0 && cell.y >= 0 &&
        std::cmp_less(cell.x, editor.documentWidth()) && std::cmp_less(cell.y, editor.documentHeight());
}

struct MakerOverworldCell {
    const Level::Definition& definition;
    GridPosition3 local;
    OverworldScreenId screen;
};

std::optional<MakerOverworldCell> makerOverworldCellAt(const MakerSession& maker, GridPosition3 cell)
{
    const auto& layout = maker.overworldLayout();
    const OverworldSlot current = maker.currentScreenSlot();
    const auto width = static_cast<int>(layout.screenWidth);
    const auto height = static_cast<int>(layout.screenHeight);
    for (const auto& screen : layout.screens) {
        const GridPosition offset { (screen.slot.x - current.x) * width, (screen.slot.y - current.y) * height };
        const GridPosition3 local { cell.x - offset.x, cell.y - offset.y, cell.z };
        if (local.x < 0 || local.y < 0 || local.x >= width || local.y >= height) continue;
        if (const auto* definition = maker.overworldDefinition(screen.id)) {
            return MakerOverworldCell { *definition, local, screen.id };
        }
    }
    return std::nullopt;
}

const MakerDraftStore::SavedLevel* makerLevelFromCatalog(std::span<const MakerDraftStore::SavedLevel> levels,
    const std::optional<std::string>& id, const std::optional<std::string>& legacyName)
{
    using SavedLevel = MakerDraftStore::SavedLevel;
    if (id) {
        const auto found = std::ranges::find(levels, *id, &SavedLevel::id);
        return found == levels.end() ? nullptr : &*found;
    }
    if (!legacyName) return nullptr;
    const auto alias = std::ranges::find(levels, legacyName, &SavedLevel::legacyName);
    if (alias != levels.end()) return &*alias;
    const auto current = std::ranges::find(levels, *legacyName, &SavedLevel::name);
    return current == levels.end() ? nullptr : &*current;
}

constexpr std::array makerRecoveryKinds { MakerRecoveryKind::Puzzle, MakerRecoveryKind::Overworld };

std::size_t makerRecoveryIndex(MakerRecoveryKind kind)
{
    return kind == MakerRecoveryKind::Puzzle ? 0U : 1U;
}

std::string makerLecternField(std::string_view text)
{
    std::string field;
    for (const char character : text) {
        switch (character) {
        case '\\': field += "\\\\"; break;
        case '\n': field += "\\n"; break;
        default: field += character; break;
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

ScreenSelectorViewState makerSelectorState(const PuzzleCatalog& catalog, LevelLocation target)
{
    if (!catalog.findScreen(target)) return {};
    return {
        .status = ScreenSelectorStatus::Playable,
        .lastScreenInLevel = static_cast<std::size_t>(target.screen) + 1 ==
            catalog.levels[static_cast<std::size_t>(target.level)].screens.size(),
    };
}

OverworldScreenId makerPlaytestScreen(const OverworldMap& map, const GameState& state)
{
    return CampaignSession::sharedPlayerScreen(map, state).value_or(map.startScreen());
}

CameraAngles makerPlaytestAngles(const Level& level, const OverworldMap* world,
    const GameState& state)
{
    if (world) {
        if (const auto* screen = world->screen(makerPlaytestScreen(*world, state))) {
            return screen->definition.cameraAngles.value_or(CameraAngles {});
        }
    }
    return level.cameraAngles().value_or(CameraAngles {});
}

TileType makerLinkTile(const LevelEditor& editor, LinkEndpoint endpoint)
{
    const auto tile = makerTileAt(editor, endpoint.cell);
    const auto plate = editor.documentPlateAt(endpoint.cell).value_or(TileType::Air);
    if (endpoint.kind == LinkKind::Source || endpoint.kind == LinkKind::Portal) return plate;
    if (endpoint.kind == LinkKind::Device && (tileTypeIsRotator(plate) || tileTypeIsLockPlate(plate))) return plate;
    return tile;
}

TileType makerLinkTile(const MakerSession& maker, LinkEndpoint endpoint)
{
    if (maker.overworld() && endpoint.kind == LinkKind::Portal) {
        if (const auto location = makerOverworldCellAt(maker, endpoint.cell)) {
            const auto& definition = location->definition;
            const auto cell = location->local;
            const auto covered = std::ranges::find(definition.plates, cell, &Level::Plate::cell);
            if (covered != definition.plates.end() && tileTypeIsPortal(covered->tile)) return covered->tile;
            if (cell.z >= 0 && std::cmp_less(cell.z, definition.layers.size())) {
                return charToTileType(definition.layers[cell.z][cell.y][cell.x]).value_or(TileType::Air);
            }
        }
    }
    return makerLinkTile(maker.editor(), endpoint);
}

std::optional<LinkEndpoint> otherMakerLinkEndpoint(const LevelEditor& editor, LinkEndpoint current)
{
    for (const auto kind : { LinkKind::Object, LinkKind::Source, LinkKind::Device, LinkKind::Portal }) {
        LinkEndpoint endpoint { current.cell, kind };
        if (endpoint != current && editor.hasLinkEndpoint(endpoint)) return endpoint;
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

void populateMakerLinkInspector(MakerLinkPresentation& view, const LevelEditor& editor,
    LinkEndpoint endpoint)
{
    using Kind = MakerLinkInspector::Kind;
    const auto color = editor.linkEndpointColor(endpoint);
    const auto& groups = editor.linkGroupsView();
    view.title = makerTileGuide::name(makerLinkTile(editor, endpoint));
    view.inspector.canClear = true;
    view.inspector.canSelectOther = otherMakerLinkEndpoint(editor, endpoint).has_value();
    const auto addPeers = [&](const auto& cells, LinkKind kind) {
        for (const auto cell : cells) {
            if (LinkEndpoint peer { cell, kind }; peer != endpoint) view.peers.push_back(peer);
        }
    };
    if (endpoint.kind == LinkKind::Portal) {
        view.inspector.kind = Kind::Portal;
        std::size_t pairNumber = 0;
        for (const auto& group : groups) {
            if (group.portals.size() == 2) ++pairNumber;
            if (!color || !LevelEditor::sameLinkColor(*color, group.color)) continue;
            if (group.portals.size() == 2) {
                addPeers(group.portals, LinkKind::Portal);
                view.connectionLabel = "Portal pair " + std::to_string(pairNumber);
                view.title = view.connectionLabel;
                view.inspector.groupColor = color;
            }
            view.inspector.canDisconnect = group.portals.size() > 1;
            break;
        }
        view.inspector.detail = !view.peers.empty() ? "These two portals lead to each other." :
            view.inspector.canDisconnect ? "More than two portals share this pair. Unpair this portal to change its exit." :
            "Click another unpaired portal to set its exit.";
        return;
    }

    const bool object = endpoint.kind == LinkKind::Object;
    const bool source = endpoint.kind == LinkKind::Source;
    const auto linkedCells = object ? std::vector<GridPosition3> {} :
        source ? editor.devicesDrivenBy(endpoint.cell) : editor.linkSourcesForDevice(endpoint.cell);
    view.inspector.kind = object ? Kind::Object : Kind::Device;
    view.inspector.canCreate = true;
    const auto groupKind = makerGroupKind(endpoint.kind);
    const auto addGroup = [&](Vec3 groupColor, std::string_view name) {
        view.groupColors.push_back(groupColor);
        view.groupNames.push_back(name.empty()
            ? (object ? "Existing sync group " : "Existing device group ") + std::to_string(view.groupColors.size())
            : std::string(name));
    };
    for (const auto& group : groups) {
        const auto name = editor.linkGroupName(groupKind, group.color);
        const auto deviceCount = group.gates.size() + group.rotators.size() + group.lockPlates.size() +
            group.elevators.size() + group.minecarts.size();
        const bool active = color && LevelEditor::sameLinkColor(*color, group.color);
        const bool legacyGroup = object ? !group.objects.empty() : group.pressurePlates.size() + deviceCount > 1;
        const bool explicitlyWired = active && !linkedCells.empty();
        if (name.empty() && !legacyGroup && !explicitlyWired) continue;
        if (active) {
            view.inspector.activeGroup = view.groupColors.size();
            if (object) addPeers(group.objects, LinkKind::Object);
        }
        addGroup(group.color, name);
    }
    for (const auto& named : editor.linkGroupNames()) {
        if (named.kind == groupKind && std::ranges::none_of(view.groupColors,
                [&](Vec3 existing) { return LevelEditor::sameLinkColor(existing, named.color); })) {
            addGroup(named.color, named.name);
        }
    }
    if (!object) {
        addPeers(linkedCells, source ? LinkKind::Device : LinkKind::Source);
    }
    view.inspector.canRename = view.inspector.activeGroup.has_value();
    view.inspector.canDisconnect = !view.peers.empty() || view.inspector.activeGroup.has_value();
    view.inspector.detail = object ? "Click another movable object or choose a group. Group members copy successful moves." :
        source ? "Click a device to connect it to this signal source." :
        "Click a pressure plate, button, or lever to drive this device.";
    if (endpoint.kind == LinkKind::Device && makerLinkTile(editor, endpoint) == TileType::Elevator) {
        const auto& elevators = editor.elevators();
        const auto elevator = std::ranges::find(elevators, endpoint.cell, &Level::Elevator::cell);
        const auto layer = static_cast<int>(editor.activeLayer());
        if (elevator != elevators.end() && layer != elevator->cell.z) {
            view.inspector.elevatorStopOnLayer = std::ranges::find(elevator->levels, layer) != elevator->levels.end();
        }
        if (elevator != elevators.end() && elevator->levels.size() < 2) {
            view.inspector.detail = "Connect an activator, then choose another layer and add a stop.";
        }
    }
    if (view.inspector.activeGroup) {
        view.connectionLabel = view.groupNames[*view.inspector.activeGroup];
        view.inspector.groupColor = color;
    } else if (!view.peers.empty()) {
        view.connectionLabel = "Signal connection";
    }
}

void populateMakerOverworldPortalInspector(MakerLinkPresentation& view, const MakerSession& maker,
    LinkEndpoint endpoint)
{
    view.inspector.kind = MakerLinkInspector::Kind::Portal;
    view.title = makerTileGuide::name(makerLinkTile(maker, endpoint));
    view.inspector.canClear = true;
    view.inspector.canSelectOther = otherMakerLinkEndpoint(maker.editor(), endpoint).has_value();
    for (const auto peer : maker.overworldPortalPeers(endpoint.cell)) {
        view.peers.push_back({ peer, LinkKind::Portal });
    }
    view.inspector.canDisconnect = !view.peers.empty();
    if (view.peers.size() == 1) {
        view.connectionLabel = "Portal pair";
        view.title = view.connectionLabel;
        if (const auto* portal = maker.overworldPortalAt(endpoint.cell)) view.inspector.groupColor = portal->color;
        view.inspector.detail = "These two portals lead to each other.";
    } else {
        view.inspector.detail = view.inspector.canDisconnect
            ? "More than two portals share this pair. Unpair this portal to change its exit."
            : "Click another unpaired portal to set its exit.";
    }
}

void checkStartupClose(const Window& window)
{
    // There is only one window during startup. Leave the other queued
    // events for the main loop once the application is ready.
    SDL_PumpEvents();
    if (SDL_HasEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED) ||
        SDL_HasEvent(SDL_EVENT_QUIT)) {
        SDL_HideWindow(window.nativeHandle());
        throw ApplicationStartupCancelled {};
    }
}

template<class Load>
auto timedAssetStartup(const Window& window, std::string_view phase, Load&& load)
{
    const auto started = std::chrono::steady_clock::now();
    auto result = std::forward<Load>(load)();
    log::info(log::Category::Application)
        << "Asset startup phase (us): " << phase << '='
        << std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - started).count();
    checkStartupClose(window);
    return result;
}

AntiAliasingMode antiAliasingModeForSamples(int samples)
{
    switch (samples) {
    case 1: return AntiAliasingMode::None;
    case 2: return AntiAliasingMode::Msaa2x;
    case 4: return AntiAliasingMode::Msaa4x;
    default: return AntiAliasingMode::Msaa8x;
    }
}

AssetLoadingBudget assetLoadingBudgetFor(const ApplicationOptions& options)
{
    AssetLoadingBudget budget;
    if (options.textureResidencyBudgetKiB != 0) {
        if (options.textureResidencyBudgetKiB >
            std::numeric_limits<uint64_t>::max() / 1024ULL) {
            throw std::invalid_argument(
                "Texture residency budget is too large");
        }
        budget.textureResidencyBytes =
            options.textureResidencyBudgetKiB * 1024ULL;
    }
    return budget;
}

bool configureStartupWindow(
    Window& window, PlayerProfile& profile, bool smokeRun)
{
    checkStartupClose(window);
    // Apply the persisted mode before Vulkan creates its surface and
    // swapchain. Creating a default window first and changing it after the
    // renderer exists forces an otherwise redundant swapchain generation
    // before frame one.
    profile.normalize();
    // Automated renders need only the window's initial surface and extent.
    // Xvfb has no window manager to acknowledge fullscreen/maximize requests,
    // so keep the constructor's windowed mode for smoke and evidence runs.
    if (smokeRun) {
        return true;
    }
    const UserSettings::Video& video = profile.settings.video;
    if (video.fullscreen) {
        window.setFullscreen(true);
    } else if (video.windowMaximized) {
        window.setWindowedMaximized();
    } else {
        window.setWindowedSize(video.windowWidth, video.windowHeight);
    }
    checkStartupClose(window);
    return true;
}

void appendEvidenceWaterFixture(RenderFrameData& frame)
{
    if (frame.levelWidth == 0 || frame.levelHeight == 0) {
        return;
    }

    // Cross the board edge so one stable draw exercises water over both opaque
    // geometry and the clear background. Lift this evidence-only surface above
    // the ground; authored water sits below the surrounding lip and would be
    // depth-occluded if it were placed directly over an ordinary floor.
    const RenderFrameData::CameraExtent fixtureExtent =
        frame.cameraExtent.value_or(RenderFrameData::CameraExtent {
            .width = frame.levelWidth,
            .height = frame.levelHeight,
            .depth = frame.levelDepth,
        });
    constexpr float stripLeft = -1.0f;
    constexpr float stripWidth = 2.0f;
    const float stripTop = static_cast<float>(fixtureExtent.originY);
    const float stripHeight = std::max(
        static_cast<float>(fixtureExtent.height), 1.0f);
    constexpr uint32_t shorelineMask =
        waterShorelineBit(WaterShorelineEdge::PositiveX) |
        waterShorelineBit(WaterShorelineCorner::PositiveXNegativeY) |
        waterShorelineBit(WaterShorelineCorner::PositiveXPositiveY);
    frame.waterSurfaces.push_back({
        .cell = {
            static_cast<int>(stripLeft),
            fixtureExtent.originY,
            0,
        },
        .position = { stripLeft, stripTop },
        .size = { stripWidth, stripHeight },
        .color = frame.waterRendering.surfaceColor,
        .elevation = 1.0f + config::waterDepthBelowGround,
        .shorelineMask = shorelineMask,
        .pickable = false,
    });
}

bool SDLCALL simulationTimingEventWatch(void* userdata, SDL_Event* event)
{
    const auto& state = *static_cast<ApplicationTimingEventWatchState*>(
        userdata);
    auto& timing = *state.simulation;
    auto& framePacer = *state.framePacer;
    switch (event->type) {
    case SDL_EVENT_WINDOW_MINIMIZED:
        timing.setSuspended(SimulationSuspension::Minimized, true);
        framePacer.setSuspended(FramePacingSuspension::Minimized, true);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        timing.setSuspended(SimulationSuspension::Minimized, false);
        framePacer.setSuspended(FramePacingSuspension::Minimized, false);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        framePacer.setSuspended(FramePacingSuspension::Unfocused, true);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        framePacer.setSuspended(FramePacingSuspension::Unfocused, false);
        break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        timing.setSuspended(SimulationSuspension::Backgrounded, true);
        framePacer.setSuspended(FramePacingSuspension::Backgrounded, true);
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        timing.setSuspended(SimulationSuspension::Backgrounded, false);
        framePacer.setSuspended(FramePacingSuspension::Backgrounded, false);
        break;
    default:
        break;
    }
    return true;
}

} // namespace

Application::Application(ApplicationOptions options)
    : saveSlots_(
          options.saveDirectoryOverride.empty()
              ? SaveStore::preferencePath("Sokoban3D", "Sokoban3D")
              : options.saveDirectoryOverride)
    , playerProfile_(saveSlots_.loadActiveProfile())
    , window_("Sokoban 3D", config::windowWidth, config::windowHeight)
    , startupWindowConfigured_(
          configureStartupWindow(window_, playerProfile_, options.smokeFrames != 0))
    , assetRoot_(timedAssetStartup(window_, "content-validation",
#if SOKOBAN_ENABLE_DEBUG_UI
          [&options] {
              if (options.smokeFrames == 0 && options.evidenceOutputDirectory.empty()) {
                  return runtimeContentRoot(SOKOBAN_SOURCE_LEVEL_DIR);
              }
              return runtimeContentRoot();
          }))
#else
          [] { return runtimeContentRoot(); }))
#endif
    , assetManifest_(timedAssetStartup(window_, "manifest", [this] {
          return AssetManifest::loadFromFile(assetRoot_ / "manifest.json");
      }))
    , audioStartup_(std::async(std::launch::async, [this] {
          return std::make_unique<AudioSystem>(assetRoot_, assetManifest_);
      }))
    , animationCatalog_(timedAssetStartup(window_, "animation-catalog", [this] {
          return AnimationCatalog::loadFromFile(
              assetRoot_ / "animation_catalog.json", assetManifest_);
      }))
    , uiFont_(timedAssetStartup(window_, "font-atlas", [this] {
          return FontAtlas::loadDefault(assetRoot_);
      }))
    , inputPrompts_(timedAssetStartup(window_, "input-prompts", [this] {
          return InputPromptCatalog(assetRoot_, assetManifest_);
      }))
    , renderer_(
          window_.nativeHandle(),
          assetRoot_,
          saveSlots_.directory() / "vulkan_pipeline_cache.bin",
          assetManifest_,
          uiFont_,
          antiAliasingModeForSamples(
              options.evidenceOutputDirectory.empty()
                  ? playerProfile_.settings.video.antiAliasingSamples
                  : options.evidenceAntiAliasingSamples),
          options.evidenceOutputDirectory.empty()
              ? playerProfile_.settings.video.effectiveRenderScalePercent()
              : options.evidenceRenderScalePercent,
          {
              .vsync = options.evidenceOutputDirectory.empty()
                  ? playerProfile_.settings.video.vsync
                  : options.evidenceVsyncEnabled,
              .allowTearing = options.evidenceOutputDirectory.empty()
                  ? playerProfile_.settings.video.allowTearing
                  : !options.evidenceVsyncEnabled,
          },
          assetLoadingBudgetFor(options),
          options.parallelScenePreparationEnabled,
          options.pointShadowOptimizationsEnabled,
          options.recorderScratchReuseEnabled,
          options.showFailureDialogs,
          options.waterCellCacheEnabled)
    , ui_(uiFont_)
    , audioSystem_(audioStartup_.get())
    , mirrorSwapParticleEffect_(
          makeMirrorSwapParticleEffect(assetManifest_))
    , witchSwapParticleEffect_(
          makeWitchSwapParticleEffect(assetManifest_))
    , crystalChangeParticleEffect_(
          makeCrystalChangeParticleEffect(assetManifest_))
    , turretParticleEffects_(
          makeTurretParticleEffects(assetManifest_))
    , settingsCoordinator_(playerProfile_, presentationSettings_)
#if SOKOBAN_ENABLE_DEBUG_UI
    , tools_(std::make_unique<ApplicationTools>())
#endif
    , renderFrameArenas_ {
          FrameArena("render frame A", 2 * renderFrameArenaBytes()),
          FrameArena("render frame B", 2 * renderFrameArenaBytes()),
      }
    , smokeFrames_(options.smokeFrames)
    , launchLevel_(options.startLevel)
    , launchScreen_(options.startScreen)
    , launchEditDocument_(std::move(options.editDocument))
#if SOKOBAN_ENABLE_DEBUG_UI
    , devSessionPath_(saveSlots_.directory() / "dev-session.json")
#endif
    , evidenceOutputDirectory_(
          std::move(options.evidenceOutputDirectory))
    , evidenceLevel_(options.evidenceLevel)
    , evidenceScreen_(options.evidenceScreen)
    , evidenceEffects_(std::move(options.evidenceEffects))
    , launchContinue_(options.continueGame)
    , launchShowTitle_(options.showTitle)
    , evidenceAmbientOcclusionEnabled_(
          options.evidenceAmbientOcclusionEnabled)
    , evidenceWaterEnabled_(options.evidenceWaterEnabled)
    , evidencePointLightEnabled_(options.evidencePointLightEnabled)
    , evidencePointLightStressEnabled_(
          options.evidencePointLightStressEnabled)
    , evidenceDebugUi_(options.evidenceDebugUi)
    , evidenceAnimate_(options.evidenceAnimate)
    , evidenceWaterDisabled_(options.evidenceWaterDisabled)
    , evidenceWaterReflectionsDisabled_(options.evidenceWaterReflectionsDisabled)
{
    checkStartupClose(window_);
    log::info(log::Category::Persistence)
        << saveSlots_.progressStatus();
    const auto levelCatalogStart = std::chrono::steady_clock::now();
    buildLevelCatalog();
    restoreProfileLocation();
    log::info(log::Category::Application)
        << "Asset startup phase (us): level-catalog="
        << std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - levelCatalogStart).count();
    SettingsEffects initialSettings = settingsCoordinator_.initialize();
    // The window was configured before renderer construction so Vulkan saw
    // the final startup extent. Reapplying it here would synchronize the OS
    // window and request a second swapchain for no visual change.
    initialSettings.window.reset();
    applySettingsEffects(initialSettings);
    if (!evidenceOutputDirectory_.empty()) {
        CpuProfiler::instance().setEnabled(options.evidenceProfilerEnabled);
#if !SOKOBAN_ENABLE_DEBUG_UI
        if (evidenceDebugUi_) {
            throw std::invalid_argument("--evidence-debug-ui requires developer tools");
        }
#endif
        // Evidence presentation policy is a process-local diagnostic override,
        // just like scale and MSAA above. Reapply it after user settings are
        // initialized so later swapchain recreation cannot silently return an
        // explicitly non-vsync capture to the persisted/default FIFO policy.
        renderer_.setPresentationPolicy({
            .vsync = options.evidenceVsyncEnabled,
            .allowTearing = !options.evidenceVsyncEnabled,
        });
        presentationSettings_.lighting.ambientOcclusionEnabled =
            evidenceAmbientOcclusionEnabled_;
        if (evidenceWaterEnabled_) {
            presentationSettings_.water =
                RenderFrameData::defaultWaterRendering();
        }
        renderer_.setFrustumCullingEnabled(
            options.evidenceFrustumCullingEnabled);
    }
    presentationSettings_.applyTileScales(assetManifest_);
    presentationSettings_.geometry.processGroundGeometry =
        options.groundGeometryProcessingEnabled;
    presentationSettings_.geometry.smoothGroundRim = options.groundRimEnabled;
    presentationSettings_.geometry.groundChunks = options.groundChunksEnabled;
    presentationSettings_.geometry.groundChunkMeshoptimizer = options.groundChunkMeshoptimizerEnabled;
    evidenceGroundRimFixture_ = options.evidenceGroundRimFixture;
    presentationSettings_.normalize();
    presentation_.setAnimationCatalog(&animationCatalog_);
    screenPreviewPresentation_.setAnimationCatalog(&animationCatalog_);
    checkStartupClose(window_);
    // The title needs no world assets. Speculative work starts after the first
    // draw, using the saved screen or the nearby selectable puzzles only.
    openTitleScreen();

#if SOKOBAN_ENABLE_DEBUG_UI
    const auto toolsStartupStart = std::chrono::steady_clock::now();
    DebugUi::initialize();
    tools_->initialize(
        SOKOBAN_SOURCE_LEVEL_DIR,
        SOKOBAN_SOURCE_ASSET_DIR,
        assetRoot_,
        campaign_.currentLevel(),
        campaign_.currentScreen(),
        assetManifest_,
        animationCatalog_);
    log::info(log::Category::Application)
        << "Asset startup phase (us): developer-tools="
        << std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - toolsStartupStart).count();
    checkStartupClose(window_);

    DebugUi::addTab("Engine", [this] {
        int completedTargets = 0;
        for (LevelLocation target : campaign_.overworldTargets()) {
            completedTargets += playerProfile_.screenCompleted(target) ? 1 : 0;
        }
        tools_->applicationDebugUi.draw({
            .currentLevel = campaign_.currentLevel(),
            .currentScreen = campaign_.currentScreen(),
            .inOverworld = campaign_.inOverworld(),
            .completedSelectorTargets = completedTargets,
            .selectorTargetCount = static_cast<int>(
                campaign_.overworldTargets().size()),
            .level = level_,
            .gameplaySession = gameplaySession_,
            .input = input_,
            .renderer = renderer_,
            .settings = presentationSettings_,
            .audio = *audioSystem_,
            .saveDiagnostics = saveSlots_.progressDiagnostics(),
            .audioSettings = playerProfile_.settings.audio,
            .updateAudioSettings = [this](
                PlayerProfile::AudioSettings settings,
                bool persist) {
                applySettingsEffects(
                    settingsCoordinator_.applyAudioSettings(settings, persist));
            },
        });
    });
    DebugUi::addTab("Profiler", [this] {
        tools_->profilerDebugUi.draw(renderer_);
    });
    if (const std::optional<DevSession> session =
            loadDevSession(devSessionPath_)) {
        tools_->resumeOnLaunch = session->resumeOnLaunch;
    }
    DebugUi::addMenu("Session", [this] {
        const LevelEditor& editor = tools_->levelEditor;
        if ((editor.editingDocument() || editor.playingDraft()) &&
            ImGui::MenuItem("Resume Campaign")) {
            resumeCampaign();
        }
        tools_->drawSessionMenu();
    });
    DebugUi::addMenu("Camera", [this] {
        tools_->drawDetachedCameraMenu(
            window_.nativeHandle(),
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
    });
    DebugUi::addTopBarControl([this] {
        float speed = settingsCoordinator_.userSettings()
                          .gameplay.simulationSpeed;
        ImGui::SetNextItemWidth(180.0f);
        const bool changed = ImGui::SliderFloat(
            "Simulation speed",
            &speed,
            config::simulationSpeedSliderMinimum,
            config::simulationSpeedSliderMaximum,
            "%.3fx");
        const bool committed = ImGui::IsItemDeactivatedAfterEdit();
        if (changed || committed) {
            applySettingsEffects(
                settingsCoordinator_.applySimulationSpeed(
                    speed, committed));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Ctrl-click to type any non-negative value, including above "
                "10x. Rendering and editor input remain at normal speed.");
        }
    });
    tools_->enableShaderHotReload(assetRoot_);
    DebugUi::addTab("Tuning", [this] {
        tools_->tuningDebugUi.draw();
    });
    DebugUi::addTab("Shaders", [this] {
        tools_->drawShaderHotReloadPanel(renderer_);
    });
    DebugUi::addTab("Log", [this] {
        tools_->logDebugUi.draw();
    });
    DebugUi::addTab("Asset Manifest", [this] {
        tools_->assetManifestDebugUi.draw(
            tools_->assetManifestEditor, *audioSystem_, window_.nativeHandle());
        tools_->drawManifestReloadStatus();
    });
    DebugUi::addTab("Level Editor", [this] {
        tools_->levelEditorDebugUi.draw(
            tools_->levelEditor,
            tools_->overworldMapEditor,
            tools_->splatPainter,
            settingsCoordinator_.userSettings().input,
            {
            .playDraft = [this](Level level) {
                startEditorDraft(std::move(level), true);
            },
            .resumeCampaign = [this] { resumeCampaign(); },
            .openGroundPainting = [this] {
                return tools_->openGroundPainting(
                    SOKOBAN_SOURCE_ASSET_DIR,
                    assetRoot_,
                    assetManifest_,
                    renderer_);
            },
            .createGroundSplatMap = [this] {
                return tools_->createGroundSplatMap(
                    SOKOBAN_SOURCE_ASSET_DIR,
                    assetRoot_,
                    assetManifest_,
                    renderer_);
            },
            .createGroundBlendMask = [this] {
                return tools_->createGroundBlendMask(
                    SOKOBAN_SOURCE_ASSET_DIR,
                    assetRoot_,
                    assetManifest_,
                    renderer_);
            },
            .prepareGroundBlendMask = [this](std::string_view textureName) {
                return tools_->prepareGroundBlendMask(textureName,
                    SOKOBAN_SOURCE_ASSET_DIR, assetRoot_, assetManifest_, renderer_);
            },
            .assetManifest = [this]() -> const AssetManifest& { return assetManifest_; },
            .tileThumbnail = [this](TileType tile) {
                // VkDescriptorSet is what ImGui's Vulkan backend uses as a
                // texture id; null means "no thumbnail, draw the swatch".
                return reinterpret_cast<uint64_t>(
                    renderer_.tileThumbnail(tile));
            },
            .bakeTileThumbnails = [this] {
                // Deferred, not run here: this callback executes inside the
                // debug UI's ImGui frame, and the bake begins ImGui and UI
                // frames of its own. Nesting them would trip ImGui's
                // frame-scope assertion.
                tools_->bakeThumbnailsRequested = true;
                return true;
            },
            .decorationMeshes = [this]()
                -> const std::vector<DecorationMeshCatalog::Entry>& {
                return tools_->decorationMeshCatalog.entries();
            },
            .decorationMeshStatus = [this]() -> const std::string& {
                return tools_->decorationMeshCatalog.status();
            },
            .refreshDecorationMeshes = [this] {
                (void)tools_->decorationMeshCatalog.refresh(
                    SOKOBAN_SOURCE_ASSET_DIR, assetManifest_);
            },
            .registerDecorationMesh = [this](
                const std::filesystem::path& relativePath) {
                return tools_->registerDecorationMesh(
                    SOKOBAN_SOURCE_ASSET_DIR,
                    assetRoot_,
                    relativePath,
                    assetManifest_,
                    renderer_);
            },
            });
        tools_->drawSolutionPanel();
    });
    DebugUi::addTab("Animation", [this] {
        if (tools_->animationCatalogDebugUi.draw(
                tools_->animationCatalogEditor,
                assetManifest_,
                tools_->animationPreviewDebugUi,
                renderer_)) {
            animationCatalog_ = tools_->animationCatalogEditor.catalog();
        }
    });
    if (evidenceDebugUi_) {
        DebugUi::requestTabFocus("Level Editor");
    }
#endif

    if (!SDL_AddEventWatch(
            simulationTimingEventWatch, &timingEventWatchState_)) {
        throw std::runtime_error(
            std::string("SDL_AddEventWatch failed: ") + SDL_GetError());
    }
}

Application::~Application()
{
    makerDetachedCamera_.attach(window_.nativeHandle());
    // Playback runs on its own thread. Release it before waiting on saves
    // or graphics work, including exits before the run loop.
    audioSystem_.reset();
    if (makerPuzzleSession_) updateMakerAutosave(*makerPuzzleSession_, true);
    if (makerOverworldSession_) updateMakerAutosave(*makerOverworldSession_, true);
    SDL_RemoveEventWatch(
        simulationTimingEventWatch, &timingEventWatchState_);

    // No longer waits for the world to go quiet.
    //
    // The gate was there because a snapshot taken mid-action could catch an
    // unreplayable half-transition. It does not: a snapshot normally holds the
    // committed state and its undo chain. If a later completed action already
    // used cells an older slide released, the snapshot first folds only those
    // completed slide legs into a copy of the chain. The remaining in-flight
    // action is lost on reload, but its momentum is still on disk, so ambient
    // motion simply plans the rest again.
    //
    // Under concurrency the world is rarely idle, so keeping the gate meant
    // saves quietly became rare exactly when there was most to lose. The one
    // Restored slide momentum recovers the completed action that caused it, so
    // the continuation still folds into the same undo entry.
    if (campaign_.gameLoaded()) {
        checkpointCurrentScreen(false);
    } else if (!playerProfile_.progressEmpty()) {
        persistProfile(true);
    }

    // Give pending presents a visible window while the queues drain. The
    // save worker can write the checkpoint during this wait.
    renderer_.waitIdle();
    log::debug(log::Category::Rendering) << "Renderer idle for shutdown.";
    (void)SDL_HideWindow(window_.nativeHandle());

    const SaveSlotManager::FlushResult persistence = saveSlots_.flush();
    log::debug(log::Category::Persistence) << "Save queues drained for shutdown.";
    if (persistence.progress.outcome ==
        AsyncSaveStore::PersistenceOutcome::RetryableFailure) {
        log::error(log::Category::Persistence)
            << persistence.progress.message;
    }
    if (persistence.settings.outcome ==
        AsyncSaveStore::PersistenceOutcome::RetryableFailure) {
        log::error(log::Category::Persistence)
            << persistence.settings.message;
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    if (developerFilesWritable()) {
        tools_->flushSolutionStore();
        saveDevSession();
    }
    tools_->shutdownDetachedCamera(window_.nativeHandle());
    tools_->shutdownEditorCursors();
    DebugUi::clearTabs();
#endif
}

#if SOKOBAN_ENABLE_DEBUG_UI
void Application::saveDevSession() const
{
    const LevelEditor& editor = tools_->levelEditor;
    DevSession session {
        .resumeOnLaunch = tools_->resumeOnLaunch,
        .editorDocument = editor.loadedDocumentPath(),
        .editingDocument = editor.editingDocument(),
        .activeLayer = static_cast<int>(editor.activeLayer()),
    };
    switch (editor.tool()) {
    case LevelEditor::Tool::Tiles:
        session.tool = "tiles";
        break;
    case LevelEditor::Tool::Decorations:
        session.tool = "decorations";
        break;
    case LevelEditor::Tool::TileDecorations:
        session.tool = "tile-decorations";
        break;
    case LevelEditor::Tool::Selectors:
        session.tool = "selectors";
        break;
    }
    try {
        sokoban::saveDevSession(devSessionPath_, session);
    } catch (const std::exception& error) {
        log::warning(log::Category::Editor)
            << "Could not save the developer session: " << error.what();
    }
}
#endif

void Application::applyLaunchRequest()
{
    bool continueGame = launchContinue_;
    std::filesystem::path editDocument = launchEditDocument_;
    std::optional<LevelLocation> startLocation;
    if (launchLevel_ >= 0) {
        startLocation = LevelLocation {
            .level = launchLevel_,
            .screen = launchScreen_,
        };
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    std::optional<DevSession> session;
    const bool explicitRequest =
        launchShowTitle_ || continueGame || startLocation || !editDocument.empty();
    if (!explicitRequest) {
        session = loadDevSession(devSessionPath_);
        if (session && session->resumeOnLaunch) {
            continueGame = true;
            if (session->editingDocument) {
                editDocument = session->editorDocument;
            }
            log::info(log::Category::Application)
                << "Resuming the last developer session (Session menu, or "
                   "--title, to start at the title instead).";
        }
    }
#else
    if (startLocation || !editDocument.empty()) {
        log::warning(log::Category::Application)
            << "--level and --edit need a Debug build with developer tools; "
               "ignoring them.";
        startLocation.reset();
        editDocument.clear();
    }
#endif
    if (!continueGame && !startLocation && editDocument.empty()) {
        return;
    }

    // Continue exactly as the title's Continue does. A slot with no progress
    // has nothing to continue, so start it the way New Game would.
    if (playerProfile_.progressEmpty()) {
        startNewGame();
    } else {
        handleShellEvent(ShellTitleAction { title::Continue {} });
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    if (startLocation) {
        if (!campaign_.screenExists(startLocation->level, startLocation->screen)) {
            log::warning(log::Category::Application)
                << "--level " << startLocation->level << " --screen "
                << startLocation->screen << " does not exist";
        } else {
            checkpointCurrentScreen(true);
            if (campaign_.startPuzzle(playerProfile_, *startLocation)) {
                loadCurrentScreen();
            }
        }
    }
    if (!editDocument.empty()) {
        LevelEditor& editor = tools_->levelEditor;
        std::filesystem::path path = editDocument;
        if (path.is_relative() && !std::filesystem::exists(path)) {
            // Accept paths relative to the repository as well as the
            // working directory: levels/level3/screen2.scr.
            path = std::filesystem::path(SOKOBAN_SOURCE_LEVEL_DIR).parent_path() /
                editDocument;
        }
        editor.selectDocument(path);
        if (editor.openDocument(path)) {
            if (session && session->editorDocument == editDocument) {
                if (session->tool == "decorations") {
                    editor.setTool(LevelEditor::Tool::Decorations);
                } else if (session->tool == "tile-decorations") {
                    editor.setTool(LevelEditor::Tool::TileDecorations);
                } else if (session->tool == "selectors") {
                    editor.setTool(LevelEditor::Tool::Selectors);
                } else {
                    editor.setTool(LevelEditor::Tool::Tiles);
                }
                editor.setActiveLayer(session->activeLayer);
            }
            editor.setEditingDocument(true);
            tools_->levelEditorDebugUi.initialize(editor);
            log::info(log::Category::Editor)
                << "Editing " << path.string() << " (layer "
                << editor.activeLayer() << ")";
        } else {
            log::warning(log::Category::Editor)
                << "Could not open " << path.string()
                << " for editing: " << editor.status();
        }
    }
#endif
}

#if SOKOBAN_ENABLE_DEBUG_UI
bool Application::bakeTileThumbnails()
{
    // The offline bake requests each icon's assets separately. Startup world
    // prefetches would also drain here and exhaust the shared upload ring.
    renderer_.cancelQueuedAssetPrefetches();
    return tools_->bakeTileThumbnails(
        renderer_,
        ui_,
        assetManifest_,
        presentationSettings_,
        animationCatalog_,
        SOKOBAN_SOURCE_ASSET_DIR,
        assetRoot_,
        window_.sizeInPixels());
}
#endif

bool Application::nativeTextFocused() const
{
    if (makerOpen_ && !makerPlaytest_ && !optionsMenu_.isOpen()) {
        if (makerHelpOpen_ || makerCameraDialog_) return false;
        if (makerColorDialog_) return makerColorPicker_.textFocused();
        return makerDraftDialog_ ? makerView_.draftNameFocused() : makerView_.tileSearchFocused();
    }
    return optionsMenuView_.controlsSearchFocused(optionsMenu_.state());
}

void Application::blurNativeTextInput()
{
    makerColorPicker_.blurText();
    makerView_.blurTileSearch();
    makerView_.blurDraftName();
    optionsMenuView_.blurControlsSearch();
    nativeTextInput_.setTarget(nullptr, nullptr);
}

void Application::updateNativeTextInput()
{
    const bool makerInteractive =
        makerOpen_ && !makerPlaytest_ && !makerHelpOpen_ && !makerCameraDialog_ && !optionsMenu_.isOpen();
    const bool draftNameInteractive = makerInteractive && makerDraftDialog_.has_value();
    const bool colorInteractive = makerInteractive && makerColorDialog_.has_value();
    auto* target = colorInteractive ? makerColorPicker_.activeTextEdit() : makerInteractive
        ? (draftNameInteractive ? makerView_.activeDraftNameEdit() : makerView_.activeTextEdit())
        : optionsMenuView_.activeTextEdit(optionsMenu_.state());
    SDL_Rect area {};
    if (target) {
        const Vec2 size = window_.size();
        const Vec2 pixels = window_.sizeInPixels();
        if (pixels.x > 0.0f && pixels.y > 0.0f) {
            const UiRect caret = colorInteractive ? makerColorPicker_.textCaret() : makerInteractive
                ? (draftNameInteractive ? makerView_.draftNameCaret() : makerView_.textCaret())
                : optionsMenuView_.textCaret();
            const Vec2 position = scaleUiPosition(caret.position, pixels, size);
            const Vec2 extent = scaleUiPosition(caret.size, pixels, size);
            area = {
                static_cast<int>(std::floor(position.x)),
                static_cast<int>(std::floor(position.y)),
                std::max(1, static_cast<int>(std::ceil(extent.x))),
                std::max(1, static_cast<int>(std::ceil(extent.y))),
            };
        }
    }
    nativeTextInput_.setTarget(window_.nativeHandle(), target, area);
}

bool Application::handleNativeTextInputEvent(const SDL_Event& event)
{
    const auto& options = optionsMenu_.state();
    const bool makerInteractive = makerOpen_ && !makerPlaytest_ && !options.open;
    if (makerInteractive && (makerHelpOpen_ || makerCameraDialog_)) return false;
    const bool makerDialogInteractive = makerInteractive && makerDraftDialog_.has_value();
    const bool draftPicker = makerDialogInteractive && isMakerDraftPicker(makerDraftDialog_->mode);
    const bool colorInteractive = makerInteractive && makerColorDialog_.has_value();
    if (makerInteractive && makerView_.toolbarOverflowOpen()) return false;
    if ((options.open || makerInteractive) && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_LEFT &&
        event.button.windowID == SDL_GetWindowID(window_.nativeHandle())) {
        const Vec2 position = scaleUiPosition({ event.button.x, event.button.y },
            window_.size(), window_.sizeInPixels());
        const bool extend = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        const bool focusChanged = colorInteractive ? makerColorPicker_.handleTextPress(ui_, position, extend) : makerInteractive
            ? (makerDialogInteractive ? makerView_.handleDraftNamePress(ui_, position, extend)
                                      : makerView_.handleTileSearchPress(ui_, position, extend))
            : optionsMenuView_.handleControlsSearchPress(ui_, options, position, extend);
        if (focusChanged) {
            updateNativeTextInput();
        }
    }
    if ((!makerDialogInteractive || draftPicker) && !colorInteractive && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.scancode == SDL_SCANCODE_F &&
        (event.key.mod & SDL_KMOD_CTRL) != 0 &&
        (event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0) {
        const auto* target = draftPicker ? makerView_.activeDraftNameEdit() : makerInteractive
            ? makerView_.activeTextEdit() : optionsMenuView_.activeTextEdit(options);
        if (!target || target->composition().text.empty()) {
            const bool focused = draftPicker ? makerView_.focusDraftSearch() : makerInteractive
                ? makerView_.focusTileSearch() : optionsMenuView_.focusControlsSearch(options);
            if (focused) {
                updateNativeTextInput();
                return true;
            }
        }
    }
    if (draftPicker && event.type == SDL_EVENT_KEY_DOWN &&
        (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
        const auto* edit = makerView_.activeDraftNameEdit();
        if (edit && edit->composition().text.empty()) {
            makerDraftDialog_->pickerNavigation.previous |= event.key.key == SDLK_UP;
            makerDraftDialog_->pickerNavigation.next |= event.key.key == SDLK_DOWN;
            return true;
        }
    }
    const auto result = nativeTextInput_.handleEvent(event);
    if (colorInteractive) {
        const bool keyDown = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            event.key.windowID == SDL_GetWindowID(window_.nativeHandle());
        if (keyDown && event.key.key == SDLK_TAB && result != SdlTextInputResult::Consumed) {
            if (makerColorPicker_.textFocused()) makerColorPicker_.submitText();
            makerColorPicker_.endAdjustment();
            if (!makerColorPicker_.textFocused()) {
                const bool previous = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                makerColorDialog_->tabNavigation = { .previous = previous, .next = !previous };
            }
            updateNativeTextInput();
            return true;
        }
        if (result == SdlTextInputResult::Submit) {
            makerColorPicker_.submitText();
            updateNativeTextInput();
        } else if (keyDown && event.key.key == SDLK_ESCAPE && result == SdlTextInputResult::Blur) {
            makerColorDialog_->cancelRequested = true;
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            blurNativeTextInput();
            makerColorPicker_.endAdjustment();
        }
        return result != SdlTextInputResult::Ignored;
    }
    if (makerDialogInteractive && result == SdlTextInputResult::Submit) {
        if (draftPicker) {
            makerDraftDialog_->pickerNavigation.activate = true;
        } else {
            makerDraftDialog_->inputAction = makerView_.submitDraftName();
        }
    } else if (makerDialogInteractive && result == SdlTextInputResult::Blur &&
        event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        makerDraftDialog_->inputAction = { .command = MakerDraftDialogAction::Command::Cancel };
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        result == SdlTextInputResult::Submit || result == SdlTextInputResult::Blur) {
        blurNativeTextInput();
    }
    return result != SdlTextInputResult::Ignored;
}

void Application::openMakerColorDialog(LinkEndpoint endpoint, Vec3 color, std::string_view groupName)
{
    makerSession_->endStroke();
    makerSession_->cancelMove();
    blurNativeTextInput();
    ui_.cancelPointerInteraction();
    makerHover_.reset();
    const std::string title = groupName.empty() ? "Group Color" : std::string(groupName) + " Color";
    makerColorPicker_.open(color, title);
    makerColorDialog_ = MakerColorDialog { .endpoint = endpoint, .initial = color };
}

void Application::closeMakerColorDialog()
{
    blurNativeTextInput();
    ui_.cancelPointerInteraction();
    makerColorDialog_.reset();
}

void Application::drawMakerColorDialog(Vec2 viewport, float scale, const MakerViewInput& input)
{
    auto navigation = input.navigation;
    const auto tab = std::exchange(makerColorDialog_->tabNavigation, {});
    if (tab.previous || tab.next) navigation = tab;
    const bool gamepad = input_.activeDevice() == ActiveInputDevice::Gamepad;
    const auto pressed = [&](InputAction action, SDL_Scancode key) {
        return static_cast<int>(gamepad ? input_.actionPressed(action) : input_.keyPressed(key));
    };
    const UiColorPickerAction action = makerColorPicker_.draw(ui_, viewport, scale, {
        .navigation = navigation,
        .extendTextSelection = input.extendTextSelection,
        .horizontalStep = pressed(InputAction::MoveRight, SDL_SCANCODE_RIGHT) -
            pressed(InputAction::MoveLeft, SDL_SCANCODE_LEFT),
        .verticalStep = pressed(InputAction::MoveUp, SDL_SCANCODE_UP) -
            pressed(InputAction::MoveDown, SDL_SCANCODE_DOWN),
    });
    using Command = UiColorPickerAction::Command;
    if (makerColorDialog_->cancelRequested || action.command == Command::Cancel) {
        closeMakerColorDialog();
    } else if (action.command == Command::Apply) {
        const bool sameGroup = makerSession_->selectedLinkEndpoint() == makerColorDialog_->endpoint;
        if (sameGroup && (LevelEditor::sameLinkColor(action.color, makerColorDialog_->initial) ||
                makerSession_->recolorSelectedLinkGroup(action.color))) {
            closeMakerColorDialog();
        } else {
            const auto status = makerSession_->status();
            makerColorPicker_.setError(!sameGroup || status.empty() ? "The selected group is unavailable." : status);
        }
    } else if (action.command == Command::CopyHex && !SDL_SetClipboardText(action.hex.c_str())) {
        makerColorPicker_.setError("Could not copy the color to the clipboard.");
    }
}

bool Application::handleMakerCameraEvent(const SDL_Event& event)
{
    if (!makerDetachedCamera_.active()) return false;

    if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        makerViewport_.reset();
    }
    SDL_Window* const window = window_.nativeHandle();
    const bool canCapture = makerCameraDialog_ && !optionsMenu_.isOpen() && makerViewport_ &&
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == SDL_GetWindowID(window) &&
        ui_.contains(*makerViewport_, scaleUiPosition({ event.button.x, event.button.y },
            window_.size(), window_.sizeInPixels()));
    const bool wasCaptured = makerDetachedCamera_.capturingMouse();
    if (makerDetachedCamera_.handleEvent(event, window, canCapture)) {
        if (wasCaptured != makerDetachedCamera_.capturingMouse()) ui_.cancelPointerInteraction();
        return true;
    }
    if (makerDetachedCamera_.capturingMouse()) {
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            input_.handleEvent(event, InputState::PressPolicy::Suppress);
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL) return true;
    }
    return false;
}

void Application::closeMakerCameraDialog()
{
    makerDetachedCamera_.attach(window_.nativeHandle());
    makerCameraDialog_.reset();
    makerViewport_.reset();
    makerHover_.reset();
    ui_.cancelPointerInteraction();
}

void Application::drawMakerCameraDialog(const MakerLayout& layout, MakerViewInput input, bool canDetach)
{
    const bool captured = makerDetachedCamera_.capturingMouse();
    if (captured) input.navigation = {};
    const auto action = makerView_.drawCameraDialog(ui_, layout, {
        .angles = makerCameraDialog_->angles,
        .defaults = CameraAngles {},
        .detached = makerDetachedCamera_.active(),
        .canDetach = canDetach,
        .mouseCaptured = captured,
    }, input);
    using Command = MakerCameraDialogAction::Command;
    if (input_.actionPressed(InputAction::MenuBack)) {
        if (captured) makerDetachedCamera_.releaseMouse(window_.nativeHandle());
        else if (makerDetachedCamera_.active()) makerDetachedCamera_.attach(window_.nativeHandle());
        else closeMakerCameraDialog();
        return;
    }
    makerCameraDialog_->angles = action.angles;
    switch (action.command) {
    case Command::None: break;
    case Command::Cancel:
        closeMakerCameraDialog();
        break;
    case Command::Apply:
        (void)makerSession_->setCameraAngles(action.angles);
        closeMakerCameraDialog();
        break;
    case Command::ToggleDetached:
        if (makerDetachedCamera_.active()) makerDetachedCamera_.attach(window_.nativeHandle());
        else if (canDetach) {
            const auto& frame = *preparedRenderFrame_;
            makerDetachedCamera_.detach(
                frame.cameraPosition, frame.cameraForward, frame.cameraVerticalFovDegrees, frame.cameraRight);
        }
        ui_.cancelPointerInteraction();
        break;
    case Command::ResetPose:
        makerDetachedCamera_.resetPose();
        break;
    }
}

void Application::openMakerDraftDialog(MakerDraftDialogMode mode, std::string_view name)
{
    MakerDraftDialog dialog { .mode = mode, .pendingName = std::string(name) };
    if (mode == MakerDraftDialogMode::Load) {
        try {
            dialog.names = makerSession_->overworld() ? makerDraftStore_.listOverworlds() : makerDraftStore_.list();
        } catch (const std::exception& error) {
            dialog.error = error.what();
        }
    }
    makerSession_->endStroke();
    makerSession_->cancelMove();
    blurNativeTextInput();
    if (mode == MakerDraftDialogMode::ResizeBoard) {
        const auto& editor = makerSession_->editor();
        makerView_.beginResizeBoardDialog(editor.documentWidth(), editor.documentHeight());
    } else {
        makerView_.beginDraftDialog(mode, dialog.pendingName);
    }
    ui_.cancelPointerInteraction();
    makerHover_.reset();
    makerDraftDialog_ = std::move(dialog);
}

void Application::refreshMakerSavedLevels()
{
    try {
        makerSavedLevels_ = makerDraftStore_.listLevels();
    } catch (const std::exception& error) {
        log::warning(log::Category::Persistence) << "Could not list saved Maker levels: " << error.what();
    }
    makerProblemsCheckedAt_ = {};
}

std::string_view Application::makerSelectorLabel(const Level::ScreenSelector* selector) const
{
    if (selector) {
        if (const auto* saved = makerLevelFromCatalog(makerSavedLevels_, selector->draftId, selector->draftName)) {
            return saved->name;
        }
        if (selector->draftName) return *selector->draftName;
        if (selector->draftId) return "Missing saved level";
        if (selector->target) return "Legacy puzzle";
    }
    return "Unassigned Level Selector";
}

bool Application::makerRecoveryMatchesSaved(const MakerRecoverySnapshot& snapshot) const
{
    if (snapshot.draftName.empty()) return false;
    try {
        if (const auto* puzzle = std::get_if<Level::Definition>(&snapshot.document)) {
            const auto saved = snapshot.draftId ? makerDraftStore_.loadById(*snapshot.draftId)
                                                : makerDraftStore_.load(snapshot.draftName);
            return saved == *puzzle;
        }
        const auto& world = std::get<OverworldDraftOverride>(snapshot.document);
        const auto saved = makerDraftStore_.loadOverworld(snapshot.draftName);
        return world.layout == saved.layout && std::ranges::equal(world.definitions, saved.definitions,
            [](const auto& left, const auto& right) { return left.screen == right.screen && left.definition == right.definition; });
    } catch (const std::exception&) {
        return false;
    }
}

void Application::checkMakerRecovery()
{
    if (!makerRecoveryChecked_) {
        for (const auto kind : makerRecoveryKinds) {
            auto& slot = makerAutosaves_[makerRecoveryIndex(kind)];
            try {
                slot.pending = makerRecoveryStore_.load(kind);
                slot.journalPresent = slot.pending.has_value();
                if (slot.pending && makerRecoveryMatchesSaved(*slot.pending)) {
                    makerRecoveryStore_.discard(kind);
                    slot = {};
                }
            } catch (const std::exception& error) {
                slot.loadError = error.what();
            }
        }
        makerRecoveryChecked_ = true;
    }
    const auto current = makerSession_->overworld() ? MakerRecoveryKind::Overworld : MakerRecoveryKind::Puzzle;
    if (makerAutosaves_[makerRecoveryIndex(current)].unresolved()) {
        openMakerRecovery(current);
    } else {
        const auto other = makerRecoveryKinds[1 - makerRecoveryIndex(current)];
        if (makerAutosaves_[makerRecoveryIndex(other)].unresolved()) openMakerRecovery(other);
    }
}

void Application::openMakerRecovery(MakerRecoveryKind kind, bool confirmDiscard)
{
    const auto& slot = makerAutosaves_[makerRecoveryIndex(kind)];
    if (!slot.unresolved()) return;
    const auto mode = confirmDiscard ? MakerDraftDialogMode::DiscardAutosave : MakerDraftDialogMode::RecoverAutosave;
    std::string name = kind == MakerRecoveryKind::Puzzle ? "Untitled Puzzle" : "Untitled Overworld";
    if (slot.pending) {
        const auto* saved = makerLevelFromCatalog(makerSavedLevels_, slot.pending->draftId, std::nullopt);
        if (saved) name = saved->name;
        else if (!slot.pending->draftName.empty()) name = slot.pending->draftName;
    }
    openMakerDraftDialog(mode, name);
    makerDraftDialog_->recoveryKind = kind;
    if (!confirmDiscard) makerDraftDialog_->error = slot.loadError;
}

void Application::updateMakerAutosave(MakerSession& maker, bool force)
{
    if (!makerRecoveryChecked_) return;
    const auto kind = maker.overworld() ? MakerRecoveryKind::Overworld : MakerRecoveryKind::Puzzle;
    auto& slot = makerAutosaves_[makerRecoveryIndex(kind)];
    if (slot.unresolved()) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && (maker.strokeActive() || now < slot.nextAttempt)) return;
    if (force) maker.endStroke();
    const bool dirty = maker.hasUnsavedChanges();
    if (dirty ? slot.writtenRevision == maker.editRevision() : !slot.journalPresent) return;
    slot.nextAttempt = now + std::chrono::seconds(1);
    try {
        if (dirty) {
            makerRecoveryStore_.save(maker.recoverySnapshot());
            slot.writtenRevision = maker.editRevision();
        } else {
            makerRecoveryStore_.discard(kind);
            slot.writtenRevision.reset();
        }
        slot.journalPresent = dirty;
        slot.writeError = false;
    } catch (const std::exception& error) {
        if (!slot.writeError) log::warning(log::Category::Persistence) << "Could not update Maker autosave: " << error.what();
        slot.writeError = true;
    }
}

bool Application::openMakerOverworldItem(GridPosition3 cell, bool selectorsOnly)
{
    if (const auto* selector = makerSession_->levelSelectorAt(cell)) {
        refreshMakerSavedLevels();
        const auto* saved = makerLevelFromCatalog(makerSavedLevels_, selector->draftId, selector->draftName);
        const std::string current = saved ? saved->name : selector->draftName.value_or(
            selector->draftId ? "Missing saved level" : "");
        const bool legacyTarget = selector->target.has_value();
        openMakerDraftDialog(MakerDraftDialogMode::SelectorTarget, current);
        auto& dialog = *makerDraftDialog_;
        dialog.overworldItem = cell;
        dialog.legacySelectorTarget = legacyTarget;
        try {
            dialog.names = makerDraftStore_.list();
        } catch (const std::exception& error) {
            dialog.error = error.what();
        }
        return true;
    }
    if (selectorsOnly) return false;
    const auto located = makerOverworldCellAt(*makerSession_, cell);
    if (!located) return false;
    const auto& definition = located->definition;
    const auto local = located->local;
    const auto atCell = [&](const auto& item) { return item.cell == local; };
    const auto lectern = std::ranges::find_if(definition.lecterns, atCell);
    if (lectern == definition.lecterns.end()) return false;
    openMakerDraftDialog(MakerDraftDialogMode::LecternText, makerLecternField(lectern->text));
    makerDraftDialog_->overworldItem = cell;
    return true;
}

void Application::closeMakerDraftDialog()
{
    if (showsMakerResizePreview(makerDraftDialog_->mode)) makerViewport_.reset();
    blurNativeTextInput();
    ui_.cancelPointerInteraction();
    makerDraftDialog_.reset();
}

void Application::handleMakerDraftDialogAction(const MakerDraftDialogAction& action)
{
    using Command = MakerDraftDialogAction::Command;
    if (action.command == Command::None) return;

    auto& dialog = *makerDraftDialog_;
    auto& maker = *makerSession_;
    if (dialog.recoveryKind) {
        const auto kind = *dialog.recoveryKind;
        auto& slot = makerAutosaves_[makerRecoveryIndex(kind)];
        const bool confirmingDiscard = dialog.mode == MakerDraftDialogMode::DiscardAutosave;
        if (action.command == Command::Cancel) {
            if (confirmingDiscard) openMakerRecovery(kind);
            else handleShellEvent(ShellBackPressed {});
        } else if (action.command == Command::Discard) {
            openMakerRecovery(kind, true);
        } else if (confirmingDiscard) {
            try {
                makerRecoveryStore_.discard(kind);
            } catch (const std::exception& error) {
                dialog.error = error.what();
                return;
            }
            slot = {};
            closeMakerDraftDialog();
            const auto activeKind = maker.overworld() ? MakerRecoveryKind::Overworld : MakerRecoveryKind::Puzzle;
            if (makerAutosaves_[makerRecoveryIndex(activeKind)].unresolved()) checkMakerRecovery();
        } else if (slot.pending) {
            auto& session = kind == MakerRecoveryKind::Puzzle ? makerPuzzleSession_ : makerOverworldSession_;
            if (!session) session.emplace();
            auto snapshot = *slot.pending;
            if (const auto* saved = makerLevelFromCatalog(makerSavedLevels_, snapshot.draftId, std::nullopt)) {
                snapshot.draftName = saved->name;
            }
            if (session->restoreRecovery(std::move(snapshot))) {
                makerSession_ = &*session;
                slot.pending.reset();
                slot.loadError.clear();
                slot.writtenRevision = session->editRevision();
                resetMakerViewportFraming();
                closeMakerDraftDialog();
            } else {
                dialog.error = session->status();
            }
        }
        return;
    }
    if (dialog.mode == MakerDraftDialogMode::Load && action.command == Command::Rename && !maker.overworld()) {
        try {
            if (const auto saved = makerDraftStore_.findLevelByName(action.name)) {
                openMakerDraftDialog(MakerDraftDialogMode::RenameDraft, saved->name);
                makerDraftDialog_->savedLevelId = saved->id;
            } else {
                dialog.error = "This saved level is no longer available.";
            }
        } catch (const std::exception& error) {
            dialog.error = error.what();
        }
        return;
    }
    if (action.command == Command::Cancel) {
        if (dialog.mode == MakerDraftDialogMode::Overwrite) {
            openMakerDraftDialog(MakerDraftDialogMode::Save, dialog.pendingName);
        } else if (dialog.mode == MakerDraftDialogMode::Discard || dialog.mode == MakerDraftDialogMode::RenameDraft) {
            openMakerDraftDialog(MakerDraftDialogMode::Load);
        } else if (dialog.mode == MakerDraftDialogMode::DiscardNew ||
                   dialog.mode == MakerDraftDialogMode::ConfirmCrop) {
            dialog.mode = dialog.mode == MakerDraftDialogMode::DiscardNew
                ? MakerDraftDialogMode::NewDraft : MakerDraftDialogMode::ResizeBoard;
            dialog.error.clear();
            makerView_.resumeBoardSizeDialog();
            ui_.cancelPointerInteraction();
        } else {
            closeMakerDraftDialog();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::RenameDraft && dialog.savedLevelId) {
        try {
            const auto saved = makerDraftStore_.renameLevel(*dialog.savedLevelId, action.name);
            maker.updateSavedDraftName(saved.id, saved.name);
            refreshMakerSavedLevels();
            openMakerDraftDialog(MakerDraftDialogMode::Load, saved.name);
        } catch (const std::exception& error) {
            dialog.error = error.what();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::ConfirmDeleteLayer) {
        if (dialog.layer == maker.editor().activeLayer() && maker.deleteLayer(true)) {
            makerViewport_.reset();
            makerHover_.reset();
            makerAreaDragAnchor_.reset();
            closeMakerDraftDialog();
        } else {
            dialog.error = maker.status();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::LecternText || dialog.mode == MakerDraftDialogMode::SelectorTarget) {
        bool changed = dialog.mode == MakerDraftDialogMode::LecternText && action.name == dialog.pendingName;
        if (!changed && dialog.overworldItem) {
            if (dialog.mode == MakerDraftDialogMode::LecternText) {
                changed = maker.editOverworldLectern(*dialog.overworldItem, makerLecternText(action.name));
            } else {
                try {
                    const auto saved = action.name.empty() ? std::nullopt : makerDraftStore_.findLevelByName(action.name);
                    if (!saved && !action.name.empty()) {
                        dialog.error = "This saved level is no longer available.";
                        return;
                    }
                    const auto name = saved ? std::optional { saved->name } : std::nullopt;
                    const auto id = saved ? std::optional { saved->id } : std::nullopt;
                    const auto* selector = maker.levelSelectorAt(*dialog.overworldItem);
                    changed = selector && !selector->target && selector->draftId == id && selector->draftName == name;
                    if (!changed) changed = maker.attachSelectorLevel(*dialog.overworldItem, name, id);
                } catch (const std::exception& error) {
                    dialog.error = error.what();
                    return;
                }
            }
        }
        if (changed) closeMakerDraftDialog();
        else dialog.error = maker.status();
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::ResizeBoard ||
        dialog.mode == MakerDraftDialogMode::ConfirmCrop) {
        const bool cropConfirmed = dialog.mode == MakerDraftDialogMode::ConfirmCrop;
        const std::size_t removedTiles = cropConfirmed
            ? 0 : maker.resizeTileLossCount(action.width, action.height);
        if (removedTiles != 0) {
            openMakerDraftDialog(MakerDraftDialogMode::ConfirmCrop);
            makerDraftDialog_->removedTileCount = removedTiles;
        } else if (maker.resizeDraft(action.width, action.height, cropConfirmed)) {
            resetMakerViewportFraming();
            closeMakerDraftDialog();
        } else {
            dialog.error = maker.status();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::NewDraft ||
        dialog.mode == MakerDraftDialogMode::DiscardNew) {
        if (dialog.mode == MakerDraftDialogMode::NewDraft && maker.hasUnsavedChanges()) {
            openMakerDraftDialog(MakerDraftDialogMode::DiscardNew);
        } else if (maker.newDraft(action.width, action.height)) {
            updateMakerAutosave(maker, true);
            resetMakerViewportFraming();
            closeMakerDraftDialog();
        } else {
            dialog.error = maker.status();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::NameLinkGroup ||
        dialog.mode == MakerDraftDialogMode::RenameLinkGroup) {
        const auto endpoint = maker.selectedLinkEndpoint();
        const auto color = endpoint ? maker.editor().linkEndpointColor(*endpoint) : std::nullopt;
        bool saved = false;
        if (dialog.mode == MakerDraftDialogMode::NameLinkGroup) {
            saved = maker.createSelectedLinkGroup(action.name);
        } else if (endpoint && color) {
            const auto kind = makerGroupKind(endpoint->kind);
            saved = maker.editor().linkGroupName(kind, *color) == action.name ||
                maker.renameLinkGroup(kind, *color, action.name);
        }
        if (saved) closeMakerDraftDialog();
        else dialog.error = maker.status().empty() ? "Select a link endpoint first." : maker.status();
        return;
    }

    const std::string_view name = dialog.mode == MakerDraftDialogMode::Overwrite ||
        dialog.mode == MakerDraftDialogMode::Discard ? dialog.pendingName : action.name;
    if (dialog.mode == MakerDraftDialogMode::Save || dialog.mode == MakerDraftDialogMode::Overwrite) {
        const bool overwrite = dialog.mode == MakerDraftDialogMode::Overwrite ||
            name == maker.draftName();
        switch (maker.saveDraft(makerDraftStore_, name, overwrite)) {
        case MakerSession::SaveDraftResult::Saved:
            if (!maker.overworld()) refreshMakerSavedLevels();
            updateMakerAutosave(maker, true);
            closeMakerDraftDialog();
            return;
        case MakerSession::SaveDraftResult::NameExists:
            openMakerDraftDialog(MakerDraftDialogMode::Overwrite, name);
            return;
        case MakerSession::SaveDraftResult::Failed:
            break;
        }
    } else if (dialog.mode == MakerDraftDialogMode::Load && maker.hasUnsavedChanges()) {
        openMakerDraftDialog(MakerDraftDialogMode::Discard, name);
        return;
    } else if (maker.loadDraft(makerDraftStore_, name)) {
        updateMakerAutosave(maker, true);
        resetMakerViewportFraming();
        closeMakerDraftDialog();
        return;
    }
    dialog.error = maker.status();
}

void Application::startMakerPlaytest()
{
    makerSession_->cancelMove();
    updateMakerAutosave(*makerSession_, true);
    std::optional<OverworldMap> world;
    std::optional<Level> level;
    if (makerSession_->overworld()) {
        world = makerSession_->buildOverworldPlaytestMap();
        if (world) level = world->level();
    } else {
        level = makerSession_->buildPlaytestLevel();
    }
    if (!level) return;

    auto& playtest = makerPlaytest_.emplace();
    playtest.overworld = std::move(world);
    if (playtest.overworld) {
        try {
            const auto levels = makerDraftStore_.listLevels();
            std::unordered_map<std::string, std::optional<MakerDraftStore::SavedLevel>> legacyTargets;
            for (const auto& selector : playtest.overworld->selectors()) {
                if (selector.draftId) {
                    if (const auto* saved = makerLevelFromCatalog(levels, selector.draftId, std::nullopt)) {
                        playtest.selectorTargets.emplace(selector.runtimeId, *saved);
                    }
                } else if (selector.draftName) {
                    auto [entry, inserted] = legacyTargets.try_emplace(*selector.draftName);
                    if (inserted) entry->second = makerDraftStore_.resolveLevel(std::nullopt, entry->first);
                    if (entry->second) playtest.selectorTargets.emplace(selector.runtimeId, *entry->second);
                }
            }
        } catch (const std::exception& error) {
            log::warning(log::Category::Gameplay) << "Could not list saved Maker levels: " << error.what();
        }
    }
    initializeMakerPlaytestBoard(playtest.board, std::move(*level),
        playtest.overworld ? &*playtest.overworld : nullptr);
    blurNativeTextInput();
    ui_.cancelPointerInteraction();
    makerViewport_.reset();
    makerHover_.reset();
}

void Application::initializeMakerPlaytestBoard(
    MakerPlaytestBoard& board, Level level, const OverworldMap* world)
{
    board.level = std::move(level);
    board.session.setStepDurationSeconds(gameplaySession_.stepDurationSeconds());
    board.session.setStepRates(gameplaySession_.stepRates());
    board.session.reset(board.level);
    board.presentation.setAnimationCatalog(&animationCatalog_);
    board.presentation.resetEntities(board.session.state());
    const CameraAngles angles = makerPlaytestAngles(board.level, world, board.session.state());
    board.presentation.updateCameraPitch(angles.pitchDegrees, 0.0f, 0.0f);
    board.presentation.updateCameraYaw(angles.yawDegrees, 0.0f, 0.0f);
    board.solved = rules::isAtUnlockedEnd(board.level, board.session.state());
    renderer_.ensureAssets(renderAssetRequirementsForLevel(
        board.level, assetManifest_, std::nullopt, &animationCatalog_));
    audioSystem_->setAtmosphericLevel(board.level);
}

void Application::returnToMaker()
{
    makerPlaytest_.reset();
    audioSystem_->setAtmosphericLevel(level_);
    ui_.cancelPointerInteraction();
    makerViewport_.reset();
    makerHover_.reset();
}

void Application::exitMakerPlaytestBoard()
{
    auto& playtest = *makerPlaytest_;
    if (!playtest.puzzle) {
        returnToMaker();
        return;
    }
    playtest.puzzle.reset();
    audioSystem_->setAtmosphericLevel(playtest.board.level);
    ui_.cancelPointerInteraction();
}

const OverworldSelectorRuntime* Application::currentMakerPlaytestSelector() const
{
    const auto& playtest = *makerPlaytest_;
    if (!playtest.overworld || playtest.puzzle || playtest.board.session.readingLectern()) return nullptr;
    const auto* selector = CampaignSession::selectorForInteraction(playtest.board.level, playtest.board.session);
    return selector ? MakerLevelProgress::findSelector(playtest.overworld->selectors(), selector->id) : nullptr;
}

const MakerDraftStore::SavedLevel* Application::makerPlaytestTarget(const OverworldSelectorRuntime& selector) const
{
    const auto& targets = makerPlaytest_->selectorTargets;
    const auto found = targets.find(selector.runtimeId);
    return found == targets.end() ? nullptr : &found->second;
}

std::optional<uint32_t> Application::makerPlaytestPrerequisite(const OverworldSelectorRuntime& selector) const
{
    const auto& playtest = *makerPlaytest_;
    return playtest.progress.unsolvedPrerequisite(selector, playtest.overworld->selectors(),
        [this](const OverworldSelectorRuntime& prerequisite) -> std::optional<std::string_view> {
            if (const auto* target = makerPlaytestTarget(prerequisite)) return target->id;
            return std::nullopt;
        });
}

ScreenSelectorStatus Application::makerPlaytestSelectorStatus(const OverworldSelectorRuntime& selector) const
{
    const auto& playtest = *makerPlaytest_;
    const auto* target = makerPlaytestTarget(selector);
    if (!target || playtest.invalidLevels.contains(target->id) || makerPlaytestPrerequisite(selector)) {
        return ScreenSelectorStatus::Unavailable;
    }
    return playtest.progress.solved(target->id) ? ScreenSelectorStatus::Solved : ScreenSelectorStatus::Playable;
}

bool Application::tryEnterMakerPlaytestSelector()
{
    const auto* selector = currentMakerPlaytestSelector();
    if (!selector || makerPlaytestSelectorStatus(*selector) == ScreenSelectorStatus::Unavailable) return false;

    auto& playtest = *makerPlaytest_;
    const auto& target = *makerPlaytestTarget(*selector);
    try {
        auto level = Level::loadFromDefinition(makerDraftStore_.loadById(target.id), target.name);
        auto& puzzle = playtest.puzzle.emplace();
        puzzle.id = target.id;
        initializeMakerPlaytestBoard(puzzle.board, std::move(level), nullptr);
        if (puzzle.board.solved) playtest.progress.recordCompletion(target.id);
    } catch (const std::exception& error) {
        playtest.puzzle.reset();
        playtest.invalidLevels.emplace(target.id, error.what());
        return false;
    }
    ui_.cancelPointerInteraction();
    return true;
}

std::string Application::makerPlaytestSelectorHint(const OverworldSelectorRuntime& selector) const
{
    const auto& playtest = *makerPlaytest_;
    if (!selector.draftName && !selector.draftId) return "Attach a saved level to this selector in Maker.";
    const auto* target = makerPlaytestTarget(selector);
    if (!target) return "Saved level missing: " + selector.draftName.value_or("Level Selector target");
    if (const auto error = playtest.invalidLevels.find(target->id); error != playtest.invalidLevels.end()) {
        return error->second;
    }
    const auto& selectors = playtest.overworld->selectors();
    if (const auto pending = makerPlaytestPrerequisite(selector)) {
        const auto* prerequisite = MakerLevelProgress::findSelector(selectors, *pending);
        const auto* required = prerequisite ? makerPlaytestTarget(*prerequisite) : nullptr;
        return required ? "Complete " + required->name + " to unlock " + target->name + "."
            : "The prerequisite selector needs a saved level attached.";
    }
    return target->name;
}

void Application::drawMakerPlaytestSelectorPrompt(UiRect scene, Vec2 viewport)
{
    if (!preparedRenderFrame_) return;
    const auto bounds = renderer_.primaryPlayerBoundsToPixels(*preparedRenderFrame_);
    if (!bounds) return;
    const auto device = input_.activeDevice() == ActiveInputDevice::Gamepad
        ? BindingDeviceClass::Gamepad : BindingDeviceClass::Keyboard;
    const auto label = SelectorPrompt::bindingLabel(
        input_.bindings(), InputAction::MenuConfirm, device, ui_.frameArena());
    const Vec2 playerTop = scene.position + scaleUiPosition(
        bounds->position + Vec2 { bounds->size.x * 0.5f, 0.0f }, viewport, scene.size);
    ui_.pushClip(scene);
    SelectorPrompt::draw(ui_, { playerTop.x, std::max(playerTop.y - 10.0f, scene.position.y + 56.0f) }, label);
    ui_.popClip();
}

void Application::drawLecternDialog(
    LecternDialog& dialog, const Level& level, GridPosition3 cell, Vec2 viewport)
{
    if (const auto* lectern = level.lecternAt(cell)) {
        dialog.draw(ui_, viewport, lectern->text, {
            .bindings = &input_.bindings(),
            .prompts = &inputPrompts_,
            .deviceClass = input_.activeDevice() == ActiveInputDevice::Gamepad
                ? BindingDeviceClass::Gamepad : BindingDeviceClass::Keyboard,
            .gamepad = input_.activeGamepadPresentation(),
        });
    }
}

void Application::toggleMakerOverworld()
{
    makerSession_->endStroke();
    updateMakerAutosave(*makerSession_, true);
    makerSession_->cancelMove();
    makerSession_->clearTileInspection();
    if (makerSession_->overworld()) {
        makerSession_ = &*makerPuzzleSession_;
    } else {
        if (!makerOverworldSession_) {
            makerOverworldSession_.emplace();
            (void)makerOverworldSession_->newOverworldDraft(9, 7);
        }
        makerSession_ = &*makerOverworldSession_;
    }
    blurNativeTextInput();
    resetMakerViewportFraming();
    ui_.cancelPointerInteraction();
    checkMakerRecovery();
}

void Application::resetMakerViewportFraming()
{
    makerViewportCamera_.resetFraming();
    makerViewport_.reset();
    makerPanPosition_.reset();
    makerHover_.reset();
    makerAreaDragAnchor_.reset();
    makerBoardAreaAnchor_.reset();
    makerBoardReveal_ = makerBoardCursor_.has_value();
    makerOverworldCameraExtent_.reset();
    makerProblemFocus_.reset();
    makerProblemsCheckedAt_ = {};
}

bool Application::updateMakerViewportNavigation(const InputRouter::PointerInput& pointer,
    UiRect scene, std::optional<Vec2> wheelPosition, bool enabled)
{
    if (!enabled || scene.size.x <= 0.0f || scene.size.y <= 0.0f) {
        makerPanPosition_.reset();
        return false;
    }

    const auto toPixels = [&](Vec2 point) {
        return scaleUiPosition(point, window_.size(), window_.sizeInPixels());
    };
    const bool editing = pointer.primaryDown || makerSession_->strokeActive() || makerAreaDragAnchor_;
    if (!makerPanPosition_ && !editing && pointer.middlePressedAt) {
        const Vec2 press = toPixels(*pointer.middlePressedAt);
        if (ui_.contains(scene, press)) {
            makerPanPosition_ = press;
            makerSession_->cancelMove();
            ui_.cancelPointerInteraction();
        }
    }
    if (makerPanPosition_) {
        if (pointer.middleDown || pointer.middleReleasedAt) {
            const Vec2 current = pointer.middleReleasedAt ? toPixels(*pointer.middleReleasedAt) : ui_.mousePosition();
            makerViewportCamera_.pan(current - *makerPanPosition_, scene.size);
            makerPanPosition_ = current;
        }
        if (!pointer.middleDown) makerPanPosition_.reset();
        return true;
    }
    if (!editing && pointer.wheel.y != 0.0f && wheelPosition && ui_.contains(scene, *wheelPosition)) {
        makerViewportCamera_.zoom(pointer.wheel.y, *wheelPosition, scene);
        return true;
    }
    return false;
}

void Application::clearMakerBoardCursor()
{
    makerBoardCursor_.reset();
    makerBoardAreaAnchor_.reset();
    makerBoardDirection_ = {};
    makerBoardReveal_ = false;
}

void Application::toggleMakerBoardCursor(std::optional<GridPosition3> hovered)
{
    if (makerBoardCursor_) {
        clearMakerBoardCursor();
        return;
    }
    auto& maker = *makerSession_;
    const auto& editor = maker.editor();
    if (editor.documentWidth() == 0 || editor.documentHeight() == 0) return;
    maker.endStroke();
    maker.cancelMove();
    makerAreaDragAnchor_.reset();
    ui_.cancelPointerInteraction();
    makerBoardCursor_ = hovered && makerScreenContains(editor, *hovered) ? xy(*hovered) : GridPosition {
        static_cast<int>(editor.documentWidth() / 2), static_cast<int>(editor.documentHeight() / 2),
    };
    makerBoardReveal_ = true;
}

std::optional<GridPosition3> Application::updateMakerBoardCursor(GridPosition direction, UiRect scene, bool projectionReady)
{
    if (!makerBoardCursor_) return std::nullopt;
    auto& maker = *makerSession_;
    const auto& editor = maker.editor();
    if (editor.documentWidth() == 0 || editor.documentHeight() == 0) return std::nullopt;
    auto& cursor = *makerBoardCursor_;
    const auto now = std::chrono::steady_clock::now();
    GridPosition step;
    if (direction != GridPosition {} && (direction != makerBoardDirection_ || now >= makerBoardNextStep_)) {
        step = direction;
        makerBoardNextStep_ = now + (direction == makerBoardDirection_ ? std::chrono::milliseconds(85)
                                                                     : std::chrono::milliseconds(300));
    }
    makerBoardDirection_ = direction;
    if (step != GridPosition {} && projectionReady) {
        const Vec3 right = preparedRenderFrame_->cameraRight;
        const GridPosition along = std::abs(right.x) >= std::abs(right.y)
            ? GridPosition { right.x < 0.0f ? -1 : 1, 0 }
            : GridPosition { 0, right.y < 0.0f ? -1 : 1 };
        step = { along.x * step.x - along.y * step.y, along.y * step.x + along.x * step.y };
    }
    const GridPosition previous = cursor;
    cursor.x = std::clamp(cursor.x + step.x, 0, static_cast<int>(editor.documentWidth()) - 1);
    cursor.y = std::clamp(cursor.y + step.y, 0, static_cast<int>(editor.documentHeight()) - 1);
    const GridPosition3 cell { cursor.x, cursor.y, static_cast<int>(editor.activeLayer()) };
    if (makerBoardAreaAnchor_ && (maker.tool() != MakerSession::Tool::SelectArea ||
            makerBoardAreaAnchor_->z != cell.z || makerPanel_ == MakerPanel::Properties)) {
        makerBoardAreaAnchor_.reset();
    }
    if (cursor != previous) {
        makerBoardReveal_ = true;
        if (makerBoardAreaAnchor_) (void)maker.selectArea(*makerBoardAreaAnchor_, cell);
    }
    if (makerBoardReveal_ && projectionReady) {
        const auto projected = renderer_.projectToPixels(*preparedRenderFrame_, {
            static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f,
            static_cast<float>(std::max(1, cell.z)) + 0.03f,
        }, scene);
        if (projected) {
            const Vec2 inset = scene.size * 0.12f;
            const Vec2 low = scene.position + inset;
            const Vec2 high = scene.position + scene.size - inset;
            const Vec2 visible { std::clamp(projected->x, low.x, high.x), std::clamp(projected->y, low.y, high.y) };
            if (visible != *projected) makerViewportCamera_.pan(visible - *projected, scene.size);
        } else {
            makerViewportCamera_.frameSelection(cell, cell);
        }
        makerBoardReveal_ = false;
    }
    return cell;
}

void Application::activateMakerBoardCursor(GridPosition3 cell, const InputRouter::Frame& input)
{
    auto& maker = *makerSession_;
    const auto& keys = input.makerEditor;
    if (input.makerBoard.contextPressed) {
        if (maker.overworld()) (void)openMakerOverworldItem(cell);
        return;
    }
    if (makerPanel_ == MakerPanel::Properties) {
        (void)maker.inspectTile(cell);
        return;
    }
    if (keys.pickModifier) {
        if (maker.pickTile(cell) && maker.linkLayerIsolated()) makerViewport_.reset();
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
            if (makerBoardAreaAnchor_) {
                (void)maker.selectArea(*makerBoardAreaAnchor_, cell);
                makerBoardAreaAnchor_.reset();
            } else if (maker.selectArea(cell, cell)) {
                makerBoardAreaAnchor_ = cell;
            }
            return;
        case MakerSession::Tool::FloodFill: (void)maker.floodFill(cell); return;
        case MakerSession::Tool::PasteArea: (void)maker.pasteSelection(cell); return;
        case MakerSession::Tool::Connect: (void)maker.connectCell(cell); return;
        case MakerSession::Tool::ConnectSelectors: (void)maker.clickLevelSelectorConnection(cell); return;
        case MakerSession::Tool::Paint:
            if (maker.overworld() && openMakerOverworldItem(cell, true)) return;
            break;
        case MakerSession::Tool::Erase: break;
        }
    }
    (void)maker.beginStroke(cell, mode);
    maker.endStroke();
}

void Application::closeMakerOverworldMap()
{
    makerOverworldMapOpen_ = false;
    ui_.cancelPointerInteraction();
}

void Application::drawMakerOverworldMap(Vec2 viewport, float scale, const MakerViewInput& input)
{
    auto& maker = *makerSession_;
    const auto action = makerView_.drawOverworldMap(ui_, viewport, scale, {
        .screens = maker.overworldLayout().screens,
        .activeScreen = maker.currentScreenId(),
        .canUndo = maker.canUndo(),
        .canRedo = maker.canRedo(),
        .status = maker.status(),
    }, input);
    using Command = MakerOverworldMapAction::Command;
    bool changed = false;
    switch (action.command) {
    case Command::None: break;
    case Command::Close: closeMakerOverworldMap(); return;
    case Command::Jump:
        changed = maker.currentScreenId() != action.screen && maker.selectOverworldScreen(action.screen);
        break;
    case Command::Move: changed = maker.moveOverworldScreen(action.screen, action.slot); break;
    case Command::Delete: changed = maker.deleteOverworldScreen(action.screen); break;
    case Command::Undo: changed = maker.undo(); break;
    case Command::Redo: changed = maker.redo(); break;
    }
    if (changed) resetMakerViewportFraming();
}

void Application::setMakerPanel(MakerPanel panel)
{
    if (makerPanel_ == panel) return;
    makerSession_->endStroke();
    makerSession_->cancelMove();
    if (makerPanel_ == MakerPanel::Properties) makerSession_->clearTileInspection();
    makerPanel_ = panel;
    makerProblemFocus_.reset();
    makerProblemsCheckedAt_ = {};
    blurNativeTextInput();
    makerHover_.reset();
    makerAreaDragAnchor_.reset();
    ui_.cancelPointerInteraction();
}

bool Application::makerProblemTargetPlayable(std::string_view id)
{
    const std::string key(id);
    std::optional<MakerDraftStore::DraftVersion> version;
    try {
        version = makerDraftStore_.draftVersionById(id);
    } catch (const std::exception&) {
        return false;
    }
    if (!version) {
        makerProblemTargets_.erase(key);
        return false;
    }
    if (const auto found = makerProblemTargets_.find(key);
        found != makerProblemTargets_.end() && found->second.version == *version) {
        return found->second.playable;
    }

    bool playable = false;
    try {
        (void)Level::loadFromDefinition(makerDraftStore_.loadById(id), key);
        playable = true;
    } catch (const std::exception&) {
        // Incomplete drafts can be saved but cannot be entered through a selector.
    }
    makerProblemTargets_.insert_or_assign(key, MakerProblemTarget { *version, playable });
    return playable;
}

void Application::refreshMakerProblems(bool force)
{
    const auto now = std::chrono::steady_clock::now();
    // Scan loaded boards and saved-level metadata only while the panel is visible.
    if (!force && now - makerProblemsCheckedAt_ < std::chrono::milliseconds(250)) return;
    makerProblems_ = makerSession_->problems([this](std::string_view name) {
        try {
            const auto saved = makerDraftStore_.resolveLevel(std::nullopt, name);
            return saved && makerProblemTargetPlayable(saved->id);
        } catch (const std::exception&) {
            return false;
        }
    }, [this](std::string_view id) { return makerProblemTargetPlayable(id); });
    makerProblemsCheckedAt_ = now;
    if (makerProblemFocus_ && std::ranges::find(makerProblems_, *makerProblemFocus_) == makerProblems_.end()) {
        makerProblemFocus_.reset();
    }
}

void Application::jumpToMakerProblem(const MakerSession::Problem& problem)
{
    auto& maker = *makerSession_;
    maker.endStroke();
    if (problem.screen && maker.currentScreenId() != *problem.screen && !maker.selectOverworldScreen(*problem.screen)) {
        return;
    }
    maker.cancelMove();
    maker.clearSelection();
    maker.clearTileInspection();
    maker.clearLinkSelection();
    maker.clearLevelSelectorSource();
    resetMakerViewportFraming();
    makerProblemFocus_ = problem;
    if (problem.cell && makerScreenContains(maker.editor(), *problem.cell)) {
        const auto cell = *problem.cell;
        (void)maker.selectLayer(static_cast<uint32_t>(cell.z));
        makerViewportCamera_.frameSelection({ std::max(0, cell.x - 1), std::max(0, cell.y - 1), cell.z },
            { std::min(static_cast<int>(maker.editor().documentWidth()) - 1, cell.x + 1),
                std::min(static_cast<int>(maker.editor().documentHeight()) - 1, cell.y + 1), cell.z });
    }
    ui_.cancelPointerInteraction();
}

void Application::drawMakerWorkspace(
    Vec2 viewport, const InputRouter::Frame& input, std::optional<Vec2> wheelPosition)
{
    const float scale = std::clamp(
        std::max(SDL_GetWindowDisplayScale(window_.nativeHandle()), viewport.y / 1080.0f),
        1.0f, 3.0f);
    if (makerPlaytest_) {
        const MakerLayout layout = MakerView::playtestLayout(viewport, scale);
        const UiRect scene = fitSceneViewport(layout.board, viewport);
        ui_.setSceneViewport(scene);
        if (!optionsMenu_.isOpen()) {
            auto& playtest = *makerPlaytest_;
            auto& board = playtest.activeBoard();
            const auto* selector = currentMakerPlaytestSelector();
            const std::string hint = selector ? makerPlaytestSelectorHint(*selector) : std::string {};
            if (input.toggleMakerPlaytestPressed ||
                makerView_.drawPlaytest(ui_, layout, board.solved,
                    playtest.puzzle ? "Return to Overworld" : "Return to Maker", hint) == MakerCommand::ReturnToMaker) {
                exitMakerPlaytestBoard();
            } else if (const auto cell = board.session.readingLectern()) {
                drawLecternDialog(board.lecternDialog, board.level, *cell, viewport);
            } else if (selector && makerPlaytestSelectorStatus(*selector) != ScreenSelectorStatus::Unavailable) {
                drawMakerPlaytestSelectorPrompt(scene, viewport);
            }
        }
        return;
    }
    const MakerLayout layout = MakerView::layout(viewport, scale);
    const UiRect scene = fitSceneViewport(layout.board, viewport);
    ui_.setSceneViewport(scene);
    // The previous prepared frame has the projection used by the scene the
    // pointer actually sees. After a resize, wait for the new layout to draw.
    const bool projectionReady = makerViewport_ == scene && preparedRenderFrame_.has_value();
    makerViewport_ = scene;

    MakerSession& maker = *makerSession_;
    if (optionsMenu_.isOpen()) {
        maker.endStroke();
        maker.cancelMove();
        makerDetachedCamera_.releaseMouse(window_.nativeHandle());
        makerHover_.reset();
        makerAreaDragAnchor_.reset();
        makerPanPosition_.reset();
        return;
    }
    const bool modalOpen = makerDraftDialog_ || makerColorDialog_ || makerHelpOpen_ ||
        makerCameraDialog_ || makerOverworldMapOpen_;
    const bool toolbarOverflowOpen = makerView_.toolbarOverflowOpen();
    const bool boardAvailable = !modalOpen && !toolbarOverflowOpen;
    const bool showingProblems = makerPanel_ == MakerPanel::Problems;
    if (showingProblems && !modalOpen) refreshMakerProblems();
    const bool navigating = updateMakerViewportNavigation(input.pointer, scene, wheelPosition, boardAvailable);
    const bool canPick = projectionReady && !navigating;
    if (navigating) makerHover_.reset();
    const bool inspecting = makerPanel_ == MakerPanel::Properties;
    const auto pick = [&](Vec2 position) -> std::optional<GridPosition3> {
        if (!canPick) return std::nullopt;
        const auto& keys = input.makerEditor;
        if (!inspecting && maker.overworld() && maker.tool() == MakerSession::Tool::Connect &&
            !keys.moving && !keys.pickModifier && !keys.deleting && !keys.replaceLayer) {
            if (const auto portal = renderer_.pickIsoPortalCell(*preparedRenderFrame_, position, scene)) {
                return portal;
            }
        }
        if (const auto cell = renderer_.pickIsoGridCell(*preparedRenderFrame_, position, scene)) {
            return cell;
        }
        if (!inspecting && maker.overworld() && maker.showNeighbors()) {
            return renderer_.pickIsoGridCellOnPlane(*preparedRenderFrame_, position, scene,
                static_cast<int>(maker.editor().activeLayer()));
        }
        return std::nullopt;
    };
    const LevelEditor& editor = maker.editor();
    const auto cursorCell = boardAvailable && !navigating && !nativeTextFocused()
        ? updateMakerBoardCursor(input.makerBoard.direction, scene, projectionReady) : std::nullopt;
    const auto pointerCell = boardAvailable ? pick(ui_.mousePosition()) : std::nullopt;
    const auto& keys = input.makerEditor;
    const auto hovered = cursorCell && !keys.primaryPressed && !keys.secondaryPressed ? cursorCell : pointerCell;
    std::optional<Vec2> problemMarker;
    if (showingProblems && canPick && !modalOpen && makerProblemFocus_ && makerProblemFocus_->cell &&
        (!makerProblemFocus_->screen || makerProblemFocus_->screen == maker.currentScreenId())) {
        const auto cell = *makerProblemFocus_->cell;
        Vec3 anchor { static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f,
            static_cast<float>(cell.z) + 0.12f };
        if (makerProblemFocus_->kind == MakerSession::ProblemKind::UnpairedPortal) {
            const auto offset = portalEdgeOffset(makerLinkTile(maker, { cell, LinkKind::Portal }));
            anchor.x += static_cast<float>(offset.x) * 0.5f;
            anchor.y += static_cast<float>(offset.y) * 0.5f;
            anchor.z += 0.68f;
        }
        problemMarker = renderer_.projectToPixels(*preparedRenderFrame_, anchor, scene);
    }
    const auto properties = inspecting ? maker.tileProperties() : std::nullopt;
    const auto pickArea = [&](Vec2 position) -> std::optional<GridPosition3> {
        if (!canPick) return std::nullopt;
        const int layer = static_cast<int>(editor.activeLayer());
        auto cell = renderer_.pickIsoGridCell(*preparedRenderFrame_, position, scene);
        if (!cell) {
            cell = renderer_.pickIsoGridCellOnPlane(*preparedRenderFrame_, position, scene, std::max(0, layer - 1));
        }
        if (cell) cell->z = layer;
        return cell;
    };
    using AreaMode = MakerAreaViewState::Mode;
    const auto areaSelection = maker.selection();
    MakerAreaViewState area {
        .mode = inspecting ? std::nullopt : makerAreaMode(maker.tool()),
        .hasSelection = !inspecting && areaSelection.has_value(),
        .hasClipboard = maker.hasClipboard(),
    };
    const auto areaHovered = boardAvailable && area.mode
        ? (cursorCell ? cursorCell : pickArea(ui_.mousePosition())) : std::nullopt;
    const auto validPaste = area.mode == AreaMode::Paste && areaHovered
        ? maker.pastePreview(*areaHovered) : std::nullopt;
    auto outlinedRegion = areaSelection;
    Vec4 outlineColor { 1.0f, 0.78f, 0.35f, 1.0f };
    if (area.mode == AreaMode::Paste) {
        outlinedRegion = validPaste;
        outlineColor = { 0.42f, 0.76f, 1.0f, 1.0f };
        if (!validPaste && areaHovered && area.hasClipboard) {
            const auto size = maker.clipboardSize();
            if (areaHovered->x <= std::numeric_limits<int>::max() - size.x &&
                areaHovered->y <= std::numeric_limits<int>::max() - size.y) {
                outlinedRegion = MakerSession::Region { *areaHovered,
                    { areaHovered->x + size.x - 1, areaHovered->y + size.y - 1, areaHovered->z } };
                outlineColor = { 1.0f, 0.36f, 0.28f, 1.0f };
            }
        }
    } else if (area.mode == AreaMode::FloodFill) {
        outlinedRegion.reset();
        if (areaHovered && makerScreenContains(editor, *areaHovered)) {
            outlinedRegion = MakerSession::Region { *areaHovered, *areaHovered };
            outlineColor = { 0.54f, 0.86f, 0.77f, 1.0f };
        }
    }
    const auto projectOutline = [&](MakerSession::Region region, Vec4 color, float lift)
        -> std::optional<MakerAreaViewState::Outline> {
        if (!canPick) return std::nullopt;
        const auto& [first, last] = region;
        const float left = static_cast<float>(first.x);
        const float top = static_cast<float>(first.y);
        const float right = static_cast<float>(last.x) + 1.0f;
        const float bottom = static_cast<float>(last.y) + 1.0f;
        const float z = static_cast<float>(std::max(first.z, 1)) + lift;
        const auto project = [&](float x, float y) {
            return renderer_.projectToPixels(*preparedRenderFrame_, { x, y, z }, scene);
        };
        const auto a = project(left, top);
        const auto b = project(right, top);
        const auto c = project(right, bottom);
        const auto d = project(left, bottom);
        if (a && b && c && d) return MakerAreaViewState::Outline { { *a, *b, *c, *d }, color };
        return std::nullopt;
    };
    if (outlinedRegion && boardAvailable && !inspecting) area.outline = projectOutline(*outlinedRegion, outlineColor, 0.0f);
    const auto cursorOutline = cursorCell
        ? projectOutline({ *cursorCell, *cursorCell }, { 0.54f, 0.86f, 0.77f, 1.0f }, 0.03f) : std::nullopt;
    if (modalOpen) makerAreaDragAnchor_.reset();
    const bool moving = !inspecting && keys.moving && !keys.pickModifier;
    if (!moving) maker.cancelMove();
    const auto strokeMode = makerStrokeMode(keys);
    const auto resolveHover = [&]() -> std::optional<GridPosition3> {
        if (!hovered || keys.pickModifier || inspecting) return std::nullopt;
        if (moving) return maker.moveTarget(*hovered);
        if (makerAreaMode(maker.tool())) return std::nullopt;
        return maker.previewCell(*hovered, maker.strokeActive() ? maker.strokeMode() : strokeMode);
    };
    makerHover_ = resolveHover();
    TileType boardTile = hovered ? makerTileAt(editor, *hovered) : TileType::Air;
    if (hovered && maker.overworld() && !makerScreenContains(editor, *hovered)) {
        if (const auto location = makerOverworldCellAt(maker, *hovered)) {
            const auto& definition = location->definition;
            const auto cell = location->local;
            if (cell.z >= 0 && std::cmp_less(cell.z, definition.layers.size())) {
                boardTile = charToTileType(definition.layers[cell.z][cell.y][cell.x]).value_or(TileType::Air);
            }
        }
    }
    const bool painting = !inspecting && maker.tool() == MakerSession::Tool::Paint;
    const bool connecting = !inspecting && maker.tool() == MakerSession::Tool::Connect;
    const bool ordering = !inspecting && maker.tool() == MakerSession::Tool::ConnectSelectors;
    const auto selectedLink = connecting ? maker.selectedLinkEndpoint() : std::nullopt;
    MakerLinkPresentation wiring;
    std::string coveredLinkHint;
    if (connecting) {
        wiring.inspector.detail = "Select a device, signal source, portal, or movable object on the board.";
        if (selectedLink) {
            if (maker.overworld() && selectedLink->kind == LinkKind::Portal) {
                populateMakerOverworldPortalInspector(wiring, maker, *selectedLink);
            } else {
                populateMakerLinkInspector(wiring, editor, *selectedLink);
            }
        }
        const auto project = [&](LinkEndpoint endpoint) -> std::optional<Vec2> {
            if (!canPick || ((maker.linkLayerIsolated() || editor.layerLocked()) &&
                    std::cmp_not_equal(endpoint.cell.z, editor.activeLayer()))) return std::nullopt;
            if (maker.overworld() && !makerScreenContains(editor, endpoint.cell)) {
                const auto width = static_cast<int>(editor.documentWidth());
                const auto height = static_cast<int>(editor.documentHeight());
                if (!maker.showNeighbors() || endpoint.cell.x < -width || endpoint.cell.x >= width * 2 ||
                    endpoint.cell.y < -height || endpoint.cell.y >= height * 2) return std::nullopt;
            }
            const auto tile = makerLinkTile(maker, endpoint);
            const auto visual = tileVisual(tile, endpoint.cell, assetManifest_, presentationSettings_);
            Vec3 anchor {
                visual.position.x + visual.size.x * 0.5f,
                visual.position.y + visual.size.y * 0.5f,
                visual.baseElevation + (tileTypeIsGate(tile) ? 0.55f : visual.height * 0.65f),
            };
            if (tileTypeIsPortal(tile)) {
                const auto edge = portalEdgeOffset(tile);
                anchor.x += static_cast<float>(edge.x) * 0.5f;
                anchor.y += static_cast<float>(edge.y) * 0.5f;
                anchor.z = visual.baseElevation + 0.8f;
            }
            return renderer_.projectToPixels(*preparedRenderFrame_, anchor, scene);
        };
        const auto highlight = [](LinkKind kind) -> Vec4 {
            if (kind == LinkKind::Portal) return { 0.68f, 0.79f, 1.0f, 1.0f };
            if (kind == LinkKind::Object) return { 1.0f, 0.76f, 0.45f, 1.0f };
            return { 0.54f, 0.86f, 0.77f, 1.0f };
        };
        if (selectedLink) {
            const auto from = project(*selectedLink);
            auto color = highlight(selectedLink->kind);
            if (wiring.inspector.groupColor) {
                const auto linear = ui::color::displayLinear(*wiring.inspector.groupColor);
                color = { linear.x, linear.y, linear.z, 1.0f };
            }
            if (from) wiring.markers.push_back({ *from, color, true });
            for (const auto peer : wiring.peers) {
                const auto cell = peer.cell;
                std::string member(makerTileGuide::name(makerLinkTile(maker, peer)));
                if (maker.overworld() && peer.kind == LinkKind::Portal) {
                    if (const auto location = makerOverworldCellAt(maker, cell)) {
                        member += "  \xc2\xb7  Screen " + std::to_string(location->screen);
                    }
                } else {
                    member += "  " + std::to_string(cell.x) + ", " + std::to_string(cell.y) +
                        "  \xc2\xb7  Layer " + std::to_string(cell.z + 1);
                }
                wiring.members.push_back(std::move(member));
                const auto to = project(peer);
                if (!to) continue;
                wiring.markers.push_back({ *to, color, false });
                if (!from) continue;
                wiring.lines.push_back({ *from, *to, color,
                    wiring.lines.empty() ? std::string_view(wiring.connectionLabel) : std::string_view {} });
            }
        }
        if (const auto endpoint = hovered ? maker.linkTargetAt(*hovered) : std::nullopt;
            endpoint && endpoint != selectedLink && std::ranges::find(wiring.peers, *endpoint) == wiring.peers.end()) {
            if (const auto marker = project(*endpoint)) wiring.markers.push_back({ *marker, highlight(endpoint->kind), false });
            if (const auto target = makerLinkTile(maker, *endpoint);
                target != boardTile && makerScreenContains(editor, endpoint->cell)) {
                coveredLinkHint = "Connect to " + std::string(makerTileGuide::name(target)) + " under this tile";
            }
        }
        wiring.inspector.title = wiring.title;
        wiring.inspector.members = wiring.members;
        wiring.inspector.groups = wiring.groupNames;
        wiring.inspector.groupColors = wiring.groupColors;
    }
    const auto* hoveredSelector = hovered && maker.overworld() ? maker.levelSelectorAt(*hovered) : nullptr;
    if (ordering) {
        const auto source = maker.selectedLevelSelectorSource();
        wiring.inspector.kind = MakerLinkInspector::Kind::Selector;
        wiring.inspector.canClear = source.has_value();
        wiring.inspector.detail = source
            ? "Click the level this prerequisite unlocks. Click the same destination again to remove the connection."
            : "Click a Level Selector to choose the prerequisite.";
        if (source) {
            if (const auto* selector = maker.levelSelectorAt(*source)) {
                wiring.title = std::string(makerSelectorLabel(selector));
            } else {
                wiring.title = "Prerequisite selected";
            }
        }
        const auto projectSelector = [&](GridPosition3 cell) -> std::optional<Vec2> {
            if (!canPick || !maker.levelSelectorAt(cell) ||
                (editor.layerLocked() && std::cmp_not_equal(cell.z, editor.activeLayer()))) return std::nullopt;
            return renderer_.projectToPixels(*preparedRenderFrame_,
                { static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f,
                    static_cast<float>(cell.z) + 0.12f }, scene);
        };
        constexpr Vec4 connected { 0.54f, 0.86f, 0.77f, 1.0f };
        constexpr Vec4 selected { 1.0f, 0.77f, 0.43f, 1.0f };
        for (const auto& connection : maker.levelSelectorConnections()) {
            const auto from = projectSelector(connection.source);
            const auto to = projectSelector(connection.destination);
            const bool highlight = connection.source == source || connection.destination == source;
            const Vec4 color = highlight ? selected : connected;
            if (from && to) wiring.lines.push_back({ *from, *to, color, highlight ? "Unlocks" : "", true });
            if (source == connection.source) {
                if (to) wiring.markers.push_back({ *to, color, false });
                wiring.members.push_back("Unlocks " + std::string(makerSelectorLabel(maker.levelSelectorAt(connection.destination))));
            } else if (source == connection.destination) {
                if (from) wiring.markers.push_back({ *from, color, false });
                wiring.members.push_back("Requires " + std::string(makerSelectorLabel(maker.levelSelectorAt(connection.source))));
            }
        }
        if (source) {
            if (const auto from = projectSelector(*source)) wiring.markers.push_back({ *from, selected, true });
        }
        if (hoveredSelector && hovered != source) {
            if (const auto to = projectSelector(*hovered)) wiring.markers.push_back({ *to, connected, false });
            coveredLinkHint = makerSelectorLabel(hoveredSelector);
        }
        wiring.inspector.title = wiring.title;
        wiring.inspector.members = wiring.members;
    }
    if (inspecting && canPick && !modalOpen) {
        const auto mark = [&](GridPosition3 cell, TileType tile, bool selected) {
            const auto visual = tileVisual(tile, cell, assetManifest_, presentationSettings_);
            const auto position = renderer_.projectToPixels(*preparedRenderFrame_, {
                visual.position.x + visual.size.x * 0.5f,
                visual.position.y + visual.size.y * 0.5f,
                visual.baseElevation + visual.height * 0.65f,
            }, scene);
            if (position) wiring.markers.push_back({ *position, { 1.0f, 0.78f, 0.35f, 1.0f }, selected });
        };
        if (properties && (!editor.layerLocked() || std::cmp_equal(properties->cell.z, editor.activeLayer()))) {
            mark(properties->cell, properties->tile, true);
        }
        if (hovered && makerScreenContains(editor, *hovered) && boardTile != TileType::Air &&
            (!properties || *hovered != properties->cell)) {
            mark(*hovered, boardTile, false);
        }
    }
    const TileType brush = editor.selectedTile();
    const auto hoverStrokeMode = maker.strokeActive() ? maker.strokeMode() : strokeMode;
    const bool showPlacementHint = (painting || hoverStrokeMode == MakerSession::StrokeMode::Replace) &&
        !moving && !makerStrokeErases(maker, hoverStrokeMode) && makerHover_ && !makerView_.tileSearchFocused();
    const bool newOverworldScreen = showPlacementHint && maker.overworld() &&
        !makerScreenContains(editor, *makerHover_) && !makerOverworldCellAt(maker, *makerHover_);
    const std::string_view placementError = showPlacementHint && !maker.placingSelector() &&
        makerScreenContains(editor, *makerHover_)
        ? editor.tilePlacementError(*makerHover_, brush) : std::string_view {};
    std::string selectorHint;
    if (hoveredSelector && painting && !moving && !keys.pickModifier &&
        !maker.strokeActive() && hoverStrokeMode == MakerSession::StrokeMode::Brush) {
        selectorHint = std::string(makerSelectorLabel(hoveredSelector)) + "  \xc2\xb7  Click to attach a level";
    }
    const auto placementHint = [&]() -> std::string_view {
        if (inspecting && maker.overworld() && hovered && !makerScreenContains(editor, *hovered)) {
            return "Switch to this screen to edit its tile properties.";
        }
        if (area.mode == AreaMode::Paste && areaHovered && !validPaste) {
            return maker.overworld() ? "The copied area must fit on this screen." : "The copied area must fit on the board.";
        }
        if (!selectorHint.empty()) return selectorHint;
        if (!showPlacementHint) return coveredLinkHint;
        if (!placementError.empty()) return placementError;
        if (newOverworldScreen) return "Paint here to create an overworld screen.";
        if (maker.placingSelector()) return makerTileGuide::levelSelector.placementHint;
        return makerTileGuide::forTile(brush, maker.overworld()).placementHint;
    };
    MakerPropertyPanel propertyPanel;
    std::array<uint8_t, 4> minecartDirections {};
    std::string minecartRoute;
    std::string elevatorStops;
    if (properties) {
        std::size_t directionCount = 0;
        for (uint8_t direction = 0; direction < minecartDirections.size(); ++direction) {
            if ((properties->minecartDirectionsMask & (1U << direction)) != 0) {
                minecartDirections[directionCount++] = direction;
            }
        }
        if (const auto& route = properties->minecartRoute) {
            minecartRoute = route->loop ? "Loop" : "Out and back";
            minecartRoute += " | " + std::to_string(route->cells) + " cells, " +
                std::to_string(route->stops) + (route->stops == 1 ? " stop" : " stops");
        }
        for (const int layer : properties->elevatorStops) {
            if (!elevatorStops.empty()) elevatorStops += ", ";
            elevatorStops += std::to_string(layer + 1);
        }
        propertyPanel = {
            .cell = properties->cell,
            .tile = properties->tile,
            .gateStartOpen = properties->gateStartOpen,
            .lockPlateStartEnabled = properties->lockPlateStartEnabled,
            .minecartDirection = properties->minecartDirection,
            .minecartDirections = std::span(minecartDirections).first(directionCount),
            .minecartRoute = minecartRoute,
            .elevatorStopOnLayer = properties->elevatorStopOnLayer,
            .elevatorStops = elevatorStops,
        };
    }
    auto frameRegion = areaSelection;
    const auto frameCell = [&]() -> std::optional<GridPosition3> {
        if (properties) return properties->cell;
        if (selectedLink) return selectedLink->cell;
        if (ordering && maker.selectedLevelSelectorSource()) return maker.selectedLevelSelectorSource();
        return areaSelection ? std::nullopt : cursorCell;
    }();
    if (frameCell && (makerScreenContains(editor, *frameCell) || (maker.overworld() && maker.showNeighbors() &&
            frameCell->x >= -static_cast<int>(editor.documentWidth()) &&
            frameCell->x < 2 * static_cast<int>(editor.documentWidth()) &&
            frameCell->y >= -static_cast<int>(editor.documentHeight()) &&
            frameCell->y < 2 * static_cast<int>(editor.documentHeight())))) {
        frameRegion = MakerSession::Region { *frameCell, *frameCell };
    }
    const auto assets = renderer_.assetLoadingStats();
    const MakerViewInput viewInput {
        .wheelY = input.pointer.wheel.y,
        .wheelPosition = wheelPosition,
        .navigation = input.maker,
        .extendTextSelection = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0,
        .interactive = !modalOpen && !navigating,
        .tileThumbnail = [this](TileType tile) { return renderer_.makerTileThumbnail(tile); },
    };
    const MakerViewAction action = makerView_.draw(ui_, layout, {
        .brush = brush,
        .quarterTurns = editor.selectedTileQuarterTurns(),
        .layer = editor.activeLayer(),
        .layerCount = editor.documentDepth(),
        .erasing = maker.tool() == MakerSession::Tool::Erase,
        .canUndo = maker.canUndo(),
        .canRedo = maker.canRedo(),
        .hovered = area.mode ? areaHovered : connecting || ordering || inspecting ? hovered : makerHover_,
        .status = maker.status().empty() && (assets.pendingModels || assets.pendingTextures)
            ? "Loading board assets" : maker.status(),
        .draftName = maker.draftName(),
        .unsavedChanges = maker.hasUnsavedChanges(),
        .placementHint = placementHint(),
        .placementBlocked = !placementError.empty() || (area.mode == AreaMode::Paste && areaHovered && !validPaste),
        .boardTile = boardTile == TileType::Air ? std::nullopt : std::optional { boardTile },
        .linkInspector = connecting || ordering ? std::optional { wiring.inspector } : std::nullopt,
        .linkLines = wiring.lines,
        .linkMarkers = wiring.markers,
        .linkViewport = scene,
        .linkLayerIsolated = maker.linkLayerIsolated(),
        .layerLocked = editor.layerLocked(),
        .overworld = maker.overworld(),
        .showNeighbors = maker.showNeighbors(),
        .overworldScreen = maker.overworld() ? GridPosition { maker.currentScreenSlot().x, maker.currentScreenSlot().y }
                                            : GridPosition {},
        .placingSelector = maker.placingSelector(),
        .hasMultipleOverworldScreens = maker.overworld() && maker.overworldLayout().screens.size() > 1,
        .area = area,
        .panel = makerPanel_,
        .layerPanel = {
            .waterLayer = editor.waterLayer(),
            .canAdd = makerPanel_ == MakerPanel::Layers && maker.canAddLayer(),
            .canDelete = makerPanel_ == MakerPanel::Layers && maker.canDeleteLayer(),
            .canSetWater = makerPanel_ == MakerPanel::Layers,
        },
        .propertyPanel = propertyPanel,
        .topDown = makerViewportCamera_.topDown(),
        .canFrameSelection = frameRegion.has_value(),
        .showViewportControls = !makerCameraDialog_ &&
            !(makerDraftDialog_ && showsMakerResizePreview(makerDraftDialog_->mode)),
        .problems = showingProblems ? std::span<const MakerSession::Problem>(makerProblems_) :
            std::span<const MakerSession::Problem> {},
        .problemMarker = problemMarker,
        .boardCursorFocused = makerBoardCursor_.has_value(),
        .boardCursorOutline = cursorOutline,
        .boardCursorHint = cursorCell ? (input_.activeDevice() == ActiveInputDevice::Gamepad
            ? "D-pad or stick to move. Confirm uses the tool."
            : "Arrow keys to move. Enter uses the tool.") : "",
    }, viewInput);
    if (makerOverworldMapOpen_) {
        MakerViewInput mapInput = viewInput;
        mapInput.interactive = true;
        drawMakerOverworldMap(viewport, scale, mapInput);
        return;
    }
    if (makerHelpOpen_) {
        MakerViewInput helpInput = viewInput;
        helpInput.navigation.previous |= input_.keyPressed(SDL_SCANCODE_UP);
        helpInput.navigation.next |= input_.keyPressed(SDL_SCANCODE_DOWN);
        helpInput.navigation.activate |= input_.keyPressed(SDL_SCANCODE_RETURN) ||
            input_.keyPressed(SDL_SCANCODE_KP_ENTER);
        if (makerView_.drawHelp(ui_, viewport, scale, input_.bindings(), helpInput) ||
            input_.actionPressed(InputAction::MenuBack)) {
            makerHelpOpen_ = false;
            ui_.cancelPointerInteraction();
        }
        return;
    }
    if (makerColorDialog_) {
        drawMakerColorDialog(viewport, scale, viewInput);
        return;
    }
    if (makerCameraDialog_) {
        drawMakerCameraDialog(layout, viewInput, canPick && preparedRenderFrame_->cameraValid);
        return;
    }
    if (auto& dialog = makerDraftDialog_) {
        MakerViewInput dialogInput = viewInput;
        dialogInput.interactive = true;
        const bool resizePreview = showsMakerResizePreview(dialog->mode);
        if (isMakerDraftPicker(dialog->mode)) {
            const auto pickerNavigation = std::exchange(dialog->pickerNavigation, {});
            dialogInput.navigation.previous |= pickerNavigation.previous || input_.keyPressed(SDL_SCANCODE_UP);
            dialogInput.navigation.next |= pickerNavigation.next || input_.keyPressed(SDL_SCANCODE_DOWN);
            dialogInput.navigation.activate |= pickerNavigation.activate ||
                input_.keyPressed(SDL_SCANCODE_RETURN) || input_.keyPressed(SDL_SCANCODE_KP_ENTER);
        }
        if (dialog->mode == MakerDraftDialogMode::NewDraft ||
            dialog->mode == MakerDraftDialogMode::DiscardNew ||
            dialog->mode == MakerDraftDialogMode::ConfirmDeleteLayer || dialog->recoveryKind || resizePreview) {
            dialogInput.navigation.activate |= input_.keyPressed(SDL_SCANCODE_RETURN) ||
                input_.keyPressed(SDL_SCANCODE_KP_ENTER);
        }
        const auto pointerAction = makerView_.drawDraftDialog(ui_, viewport, scale, {
            .mode = dialog->mode,
            .names = dialog->names,
            .error = dialog->error,
            .pendingName = dialog->pendingName,
            .removedTileCount = dialog->removedTileCount,
            .resizePreviewPanel = resizePreview ? std::optional { layout.palette } : std::nullopt,
            .legacySelectorTarget = dialog->legacySelectorTarget,
            .layer = dialog->layer,
            .removedSelectorCount = dialog->layerLoss.selectors,
            .removedDecorationCount = dialog->layerLoss.decorations,
            .removedElevatorStopCount = dialog->layerLoss.elevatorStops,
            .removesWaterLayer = dialog->layerLoss.water,
            .overworld = dialog->recoveryKind ? *dialog->recoveryKind == MakerRecoveryKind::Overworld : maker.overworld(),
            .canRecoverAutosave = !dialog->recoveryKind || makerAutosaves_[makerRecoveryIndex(*dialog->recoveryKind)].pending.has_value(),
            .otherAutosaveAvailable = dialog->recoveryKind && makerAutosaves_[1 - makerRecoveryIndex(*dialog->recoveryKind)].unresolved(),
        }, dialogInput);
        // Dismiss after drawing the modal so the frame's pointer edges never reach the board.
        const auto inputAction = std::exchange(dialog->inputAction, {});
        handleMakerDraftDialogAction(inputAction.command != MakerDraftDialogAction::Command::None
            ? inputAction : pointerAction);
        return;
    }
    if (navigating || makerView_.toolbarOverflowOpen() ||
        (toolbarOverflowOpen && action.command == MakerCommand::None)) return;

    const MakerCommand command = action.command == MakerCommand::None
        ? makerShortcutCommand(input, maker) : action.command;
    const auto refreshLayerView = [&] {
        makerViewport_.reset();
        makerHover_.reset();
        makerAreaDragAnchor_.reset();
        makerBoardReveal_ = makerBoardCursor_.has_value();
        ui_.cancelPointerInteraction();
    };
    if (command == MakerCommand::None && !action.tile) {
        if (keys.toggleLayerLockPressed) {
            maker.toggleLayerLock();
            makerViewport_.reset();
        } else if (!inspecting && keys.recentTileSlot) {
            if (maker.selectRecentTile(*keys.recentTileSlot) && connecting && maker.linkLayerIsolated()) {
                makerViewport_.reset();
            }
        }
    }
    switch (command) {
    case MakerCommand::None: break;
    case MakerCommand::ToggleToolbarOverflow: return;
    case MakerCommand::ToggleBoardCursor:
        toggleMakerBoardCursor(pointerCell);
        return;
    case MakerCommand::ShowPalette:
        if (maker.tool() == MakerSession::Tool::Connect || maker.tool() == MakerSession::Tool::ConnectSelectors) {
            maker.setTool(MakerSession::Tool::Paint);
        }
        setMakerPanel(MakerPanel::Palette);
        return;
    case MakerCommand::ShowLayers:
        setMakerPanel(MakerPanel::Layers);
        return;
    case MakerCommand::ShowProperties:
        setMakerPanel(MakerPanel::Properties);
        return;
    case MakerCommand::ShowProblems:
        setMakerPanel(MakerPanel::Problems);
        return;
    case MakerCommand::JumpToProblem:
        if (action.problem && *action.problem < makerProblems_.size()) {
            const auto problem = makerProblems_[*action.problem];
            refreshMakerProblems(true);
            if (std::ranges::find(makerProblems_, problem) != makerProblems_.end()) jumpToMakerProblem(problem);
        }
        return;
    case MakerCommand::Back:
        handleShellEvent(ShellBackPressed {});
        return;
    case MakerCommand::ToggleOverworldMode:
        toggleMakerOverworld();
        return;
    case MakerCommand::OverworldMap:
        if (!maker.overworld()) break;
        maker.endStroke();
        maker.cancelMove();
        blurNativeTextInput();
        makerHover_.reset();
        makerAreaDragAnchor_.reset();
        ui_.cancelPointerInteraction();
        makerView_.beginOverworldMap();
        makerOverworldMapOpen_ = true;
        return;
    case MakerCommand::ToggleTopDown:
    case MakerCommand::FrameSelection:
        maker.endStroke();
        maker.cancelMove();
        makerAreaDragAnchor_.reset();
        makerHover_.reset();
        ui_.cancelPointerInteraction();
        if (command == MakerCommand::ToggleTopDown) makerViewportCamera_.toggleTopDown();
        else if (frameRegion) makerViewportCamera_.frameSelection(frameRegion->min, frameRegion->max);
        else makerViewportCamera_.resetFraming();
        return;
    case MakerCommand::ToggleOverworldNeighbors:
        maker.endStroke();
        maker.cancelMove();
        maker.toggleNeighbors();
        resetMakerViewportFraming();
        break;
    case MakerCommand::PreviousOverworldScreen:
    case MakerCommand::NextOverworldScreen: {
        const auto& screens = maker.overworldLayout().screens;
        const auto current = std::ranges::find(screens, maker.currentScreenId(), &OverworldScreenSpec::id);
        if (current != screens.end()) {
            const auto count = static_cast<std::size_t>(screens.size());
            const auto index = static_cast<std::size_t>(current - screens.begin());
            const auto offset = command == MakerCommand::NextOverworldScreen ? 1 : count - 1;
            if (maker.selectOverworldScreen(screens[(index + offset) % count].id)) {
                maker.clearTileInspection();
                resetMakerViewportFraming();
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
        blurNativeTextInput();
        ui_.cancelPointerInteraction();
        makerHover_.reset();
        makerView_.beginHelp();
        makerHelpOpen_ = true;
        return;
    case MakerCommand::Camera:
        maker.endStroke();
        maker.cancelMove();
        blurNativeTextInput();
        ui_.cancelPointerInteraction();
        makerHover_.reset();
        makerView_.beginCameraDialog();
        makerCameraDialog_ = MakerCameraDialog { editor.cameraAngles() };
        return;
    case MakerCommand::Paint:
    case MakerCommand::Erase:
    case MakerCommand::Connect:
    case MakerCommand::ConnectSelectors:
    case MakerCommand::SelectArea:
    case MakerCommand::FloodFill:
    case MakerCommand::PasteArea: {
        const auto tool = command == MakerCommand::Paint ? MakerSession::Tool::Paint :
            command == MakerCommand::Erase ? MakerSession::Tool::Erase :
            command == MakerCommand::Connect ? MakerSession::Tool::Connect :
            command == MakerCommand::ConnectSelectors ? MakerSession::Tool::ConnectSelectors :
            command == MakerCommand::SelectArea ? MakerSession::Tool::SelectArea :
            command == MakerCommand::FloodFill ? MakerSession::Tool::FloodFill : MakerSession::Tool::PasteArea;
        if (tool == MakerSession::Tool::PasteArea && !maker.hasClipboard()) break;
        if (inspecting || (!showingProblems && tool != MakerSession::Tool::Paint && tool != MakerSession::Tool::Erase)) {
            setMakerPanel(MakerPanel::Palette);
        }
        if (tool != maker.tool()) {
            if (maker.linkLayerIsolated()) makerViewport_.reset();
            if (makerAreaMode(tool) || makerAreaMode(maker.tool())) {
                makerAreaDragAnchor_.reset();
                ui_.cancelPointerInteraction();
            }
        }
        maker.setTool(tool);
        break;
    }
    case MakerCommand::FillSelection: if (!inspecting) (void)maker.fillSelection(); break;
    case MakerCommand::EraseSelection: if (!inspecting) (void)maker.clearSelectionTiles(); break;
    case MakerCommand::CopySelection: if (!inspecting) (void)maker.copySelection(); break;
    case MakerCommand::CutSelection: if (!inspecting) (void)maker.cutSelection(); break;
    case MakerCommand::Disconnect: (void)maker.disconnectSelectedLink(); break;
    case MakerCommand::ClearLinkSelection:
        if (ordering) maker.clearLevelSelectorSource();
        else maker.clearLinkSelection();
        break;
    case MakerCommand::NewLinkGroup:
        openMakerDraftDialog(MakerDraftDialogMode::NameLinkGroup);
        return;
    case MakerCommand::RenameLinkGroup:
        if (wiring.inspector.activeGroup) {
            openMakerDraftDialog(MakerDraftDialogMode::RenameLinkGroup,
                wiring.groupNames[*wiring.inspector.activeGroup]);
        }
        return;
    case MakerCommand::EditLinkGroupColor:
        if (selectedLink && wiring.inspector.groupColor) {
            openMakerColorDialog(*selectedLink, *wiring.inspector.groupColor, wiring.connectionLabel);
        }
        return;
    case MakerCommand::AssignLinkGroup:
        if (action.linkGroup && *action.linkGroup < wiring.groupColors.size()) {
            (void)maker.assignSelectedLinkGroup(wiring.groupColors[*action.linkGroup]);
        }
        break;
    case MakerCommand::SelectOtherLinkEndpoint:
        if (selectedLink) {
            if (const auto other = otherMakerLinkEndpoint(editor, *selectedLink)) {
                (void)maker.selectLinkEndpoint(*other);
            }
        }
        break;
    case MakerCommand::Rotate: if (!inspecting) maker.rotateTile(); break;
    case MakerCommand::LayerDown:
    case MakerCommand::LayerUp:
        maker.stepLayer(command == MakerCommand::LayerDown ? -1 : 1);
        makerAreaDragAnchor_.reset();
        makerBoardReveal_ = makerBoardCursor_.has_value();
        if (editor.layerLocked() || (connecting && maker.linkLayerIsolated())) {
            makerViewport_.reset();
        }
        break;
    case MakerCommand::SelectLayer:
        if (action.layer && maker.selectLayer(*action.layer)) refreshLayerView();
        break;
    case MakerCommand::AddLayerAbove:
    case MakerCommand::AddLayerBelow:
        if (maker.addLayer(command == MakerCommand::AddLayerAbove)) refreshLayerView();
        break;
    case MakerCommand::DeleteLayer: {
        if (!maker.canDeleteLayer()) break;
        const auto loss = maker.layerLoss();
        if (loss.any()) {
            openMakerDraftDialog(MakerDraftDialogMode::ConfirmDeleteLayer);
            makerDraftDialog_->layer = editor.activeLayer();
            makerDraftDialog_->layerLoss = loss;
            makerDraftDialog_->removedTileCount = loss.tiles;
        } else if (maker.deleteLayer()) {
            refreshLayerView();
        }
        return;
    }
    case MakerCommand::ToggleWaterLayer:
        if (maker.setWaterLayer(editor.waterLayer() != editor.activeLayer())) refreshLayerView();
        break;
    case MakerCommand::ToggleLayerLock:
        maker.toggleLayerLock();
        refreshLayerView();
        break;
    case MakerCommand::ToggleLinkLayer:
        maker.toggleLinkLayer();
        makerViewport_.reset();
        break;
    case MakerCommand::ToggleGateStartOpen:
        if (properties && properties->gateStartOpen) (void)maker.setInspectedGateStartOpen(!*properties->gateStartOpen);
        break;
    case MakerCommand::ToggleLockPlateStartEnabled:
        if (properties && properties->lockPlateStartEnabled) {
            (void)maker.setInspectedLockPlateStartEnabled(!*properties->lockPlateStartEnabled);
        }
        break;
    case MakerCommand::SetMinecartDirection:
        if (action.minecartDirection) (void)maker.setInspectedMinecartDirection(*action.minecartDirection);
        break;
    case MakerCommand::ToggleElevatorStop:
        if (inspecting) (void)maker.toggleInspectedElevatorStop();
        else (void)maker.toggleSelectedElevatorStop();
        break;
    case MakerCommand::Undo:
    case MakerCommand::Redo: {
        const auto width = editor.documentWidth();
        const auto height = editor.documentHeight();
        const auto depth = editor.documentDepth();
        const auto water = editor.waterLayer();
        const auto angles = editor.cameraAngles();
        const auto screen = maker.overworld() ? maker.currentScreenId() : OverworldScreenId {};
        const auto slot = maker.overworld() ? maker.currentScreenSlot() : OverworldSlot {};
        const auto screenCount = maker.overworld() ? maker.overworldLayout().screens.size() : 0U;
        if (command == MakerCommand::Undo) maker.undo();
        else maker.redo();
        if (editor.documentWidth() != width || editor.documentHeight() != height || editor.documentDepth() != depth ||
            editor.waterLayer() != water || editor.cameraAngles() != angles ||
            (maker.overworld() && (maker.currentScreenId() != screen || maker.currentScreenSlot() != slot ||
                maker.overworldLayout().screens.size() != screenCount))) {
            resetMakerViewportFraming();
        }
        break;
    }
    case MakerCommand::NewDraft:
        openMakerDraftDialog(MakerDraftDialogMode::NewDraft);
        return;
    case MakerCommand::ResizeBoard:
        openMakerDraftDialog(MakerDraftDialogMode::ResizeBoard);
        return;
    case MakerCommand::Save:
        openMakerDraftDialog(MakerDraftDialogMode::Save, maker.draftName());
        return;
    case MakerCommand::Load:
        openMakerDraftDialog(MakerDraftDialogMode::Load);
        return;
    case MakerCommand::TestPlay:
        startMakerPlaytest();
        return;
    case MakerCommand::ReturnToMaker: break;
    }
    if (action.tile) {
        maker.selectTile(*action.tile);
        if (connecting && maker.linkLayerIsolated()) makerViewport_.reset();
    }
    if (!makerViewport_) {
        makerHover_.reset();
        return;
    }

    if (cursorCell && (input.makerBoard.activatePressed || input.makerBoard.contextPressed)) {
        activateMakerBoardCursor(*cursorCell, input);
        return;
    }
    if (makerBoardCursor_ && (ui_.clicked(scene) || (keys.secondaryPressed && ui_.hovered(scene)))) {
        clearMakerBoardCursor();
    }

    if (makerPanel_ == MakerPanel::Properties) {
        if (ui_.clicked(scene)) {
            if (const auto cell = pick(*ui_.mousePressPosition())) (void)maker.inspectTile(*cell);
            else maker.clearTileInspection();
        }
        return;
    }

    if (maker.overworld() && keys.secondaryPressed && hovered && openMakerOverworldItem(*hovered)) {
        return;
    }

    if (const auto mode = makerAreaMode(maker.tool()); mode && !keys.pickModifier && !moving) {
        if (mode == AreaMode::Rectangle) {
            const auto drag = makerView_.tileSearchFocused() ? std::nullopt : ui_.dragPosition("maker.area", scene);
            if (const auto press = ui_.mousePressPosition(); drag && press) {
                makerAreaDragAnchor_ = pickArea(*press);
            }
            if (drag && makerAreaDragAnchor_ && scene.size.x >= 1.0f && scene.size.y >= 1.0f) {
                const Vec2 corner {
                    std::clamp(drag->x, scene.position.x, scene.position.x + scene.size.x - 1.0f),
                    std::clamp(drag->y, scene.position.y, scene.position.y + scene.size.y - 1.0f),
                };
                if (const auto cell = pickArea(corner); cell && !maker.selectArea(*makerAreaDragAnchor_, *cell)) {
                    makerAreaDragAnchor_.reset();
                }
            }
            if (!drag || !ui_.mouseDown()) makerAreaDragAnchor_.reset();
        } else if (!makerView_.tileSearchFocused() && ui_.clicked(scene)) {
            if (const auto cell = pickArea(*ui_.mousePressPosition())) {
                if (mode == AreaMode::FloodFill) (void)maker.floodFill(*cell);
                else (void)maker.pasteSelection(*cell);
            }
        }
        makerHover_.reset();
        return;
    }
    makerAreaDragAnchor_.reset();

    if (keys.pickModifier || moving) {
        maker.endStroke();
        if (ui_.clicked(scene)) {
            if (const auto cell = pick(*ui_.mousePressPosition())) {
                if (moving) (void)maker.moveCell(*cell);
                else {
                    const bool isolated = maker.tool() == MakerSession::Tool::Connect && maker.linkLayerIsolated();
                    if (maker.pickTile(*cell) && isolated) makerViewport_.reset();
                }
            }
        }
        makerHover_ = resolveHover();
        return;
    }

    const auto tool = maker.tool();
    if (strokeMode == MakerSession::StrokeMode::Brush && !maker.strokeActive() &&
        (tool == MakerSession::Tool::Connect || tool == MakerSession::Tool::ConnectSelectors)) {
        if (ui_.clicked(scene)) {
            if (const auto cell = pick(*ui_.mousePressPosition())) {
                if (tool == MakerSession::Tool::ConnectSelectors) (void)maker.clickLevelSelectorConnection(*cell);
                else (void)maker.connectCell(*cell);
            }
        }
        return;
    }
    if (maker.overworld() && tool == MakerSession::Tool::Paint && strokeMode == MakerSession::StrokeMode::Brush &&
        !maker.strokeActive() && ui_.clicked(scene)) {
        if (const auto cell = pick(*ui_.mousePressPosition()); cell && openMakerOverworldItem(*cell, true)) return;
    }

    const auto drag = makerView_.tileSearchFocused()
        ? std::nullopt : ui_.dragPosition("maker.board", scene);
    if (drag) {
        if (const auto press = ui_.mousePressPosition()) {
            maker.endStroke();
            if (const auto cell = pick(*press)) maker.beginStroke(*cell, strokeMode);
        }
        maker.continueStroke(pick(*drag), keys.lineConstraint);
    }
    if (maker.strokeActive() && (!drag || !ui_.mouseDown())) {
        maker.endStroke();
    }
    makerHover_ = resolveHover();
}

// One UI frame: sizes, the pointer mapping, every panel and overlay that can
// draw, and the ImGui/UI frame that brackets them.
//
// Returns whether the developer workspace is visible, because the renderer
// needs the same answer a few lines later - that one value is the only thing
// this phase leaves behind, which is what made it liftable out of a 281-line
// loop body.
bool Application::drawUiFrame(
    const InputRouter::Frame& routedInput, [[maybe_unused]] float dt)
{
    const auto uiStart = std::chrono::steady_clock::now();
    const Vec2 windowSize = window_.size();
    const Vec2 pixelSize = window_.sizeInPixels();
#if SOKOBAN_ENABLE_DEBUG_UI
    bool developerWorkspaceVisible =
        (evidenceOutputDirectory_.empty() || evidenceDebugUi_) &&
        !shellMenuOpen();
    if (!developerWorkspaceVisible) {
        tools_->releaseDetachedCameraMouse(window_.nativeHandle());
        renderer_.setGameViewportDisplay(std::nullopt);
    }
#else
    constexpr bool developerWorkspaceVisible = false;
#endif
    const auto mapPointer = [&](Vec2 position) {
        return renderer_.mapPointerToGameViewport(
            scaleUiPosition(position, windowSize, pixelSize), pixelSize);
    };
    const auto mapPointerEdge = [&](std::optional<Vec2> position) -> std::optional<Vec2> {
        return position ? std::optional { mapPointer(*position) } : std::nullopt;
    };

    ui_.beginFrame(pixelSize, {
        .position = mapPointer(routedInput.pointer.position),
        .down = routedInput.pointer.primaryDown,
        .pressedAt = mapPointerEdge(routedInput.pointer.primaryPressedAt),
        .releasedAt = mapPointerEdge(routedInput.pointer.primaryReleasedAt),
    });

    const auto debugFrameStart = std::chrono::steady_clock::now();
    renderer_.beginDebugUiFrame();
    const auto workspaceStart = std::chrono::steady_clock::now();
#if SOKOBAN_ENABLE_DEBUG_UI
    if (developerWorkspaceVisible) {
        const VkExtent2D gameExtent = renderer_.renderExtent();
        const DebugUi::DrawResult workspace = DebugUi::draw({
            .texture = renderer_.gameViewportTexture(),
            .width = gameExtent.width,
            .height = gameExtent.height,
        });
        if (workspace.gameplayFullWindow) {
            // Bypass the docked texture view so the renderer can present the
            // game directly across the full swapchain client area.
            developerWorkspaceVisible = false;
            renderer_.setGameViewportDisplay(std::nullopt);
        } else if (workspace.viewportWidth > 0.0f &&
            workspace.viewportHeight > 0.0f) {
            renderer_.setGameViewportDisplay(
                VulkanRenderer::GameViewportDisplay {
                    .position = {
                        workspace.viewportX,
                        workspace.viewportY,
                    },
                    .size = {
                        workspace.viewportWidth,
                        workspace.viewportHeight,
                    },
                    .hovered = workspace.viewportHovered,
                    .focused = workspace.viewportFocused,
                });
        } else {
            renderer_.setGameViewportDisplay(std::nullopt);
        }
    } else {
        renderer_.setGameViewportDisplay(std::nullopt);
    }
#endif
    const auto overlaysStart = std::chrono::steady_clock::now();
    if (!makerOpen_) {
        if (const auto cell = gameplaySession_.readingLectern(); cell && !shellMenuOpen()) {
            drawLecternDialog(lecternDialog_, level_, *cell, pixelSize);
        } else if (screenPreviewActive_) {
            drawScreenPreviewOverlay(pixelSize);
        } else {
            drawSelectorPrompt(
                preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        }
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->drawShaderHotReloadOverlay(renderer_);
        tools_->drawDraftExitConfirmation();
        tools_->drawBrushPreview(
            renderer_,
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        tools_->drawDecorationGizmo(
            renderer_,
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr,
            input_.mousePosition(),
            window_.size(),
            window_.sizeInPixels(),
            renderer_.wantsMouseCapture());
        tools_->drawSelectorLabels(
            renderer_,
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
#endif
    }

    if (makerOpen_) {
        drawMakerWorkspace(pixelSize, routedInput, mapPointerEdge(routedInput.pointer.wheelPosition));
    } else if (titleScreen_.isOpen() && optionsMenu_.isOpen()) {
        TitleScreen::drawBackground(ui_, pixelSize);
    } else if (const std::optional<TitleAction> titleAction = titleScreen_.draw(
                   ui_, pixelSize, routedInput.title)) {
        handleShellEvent(ShellTitleAction { *titleAction });
    }

    if (const auto query = optionsMenuView_.controlsSearchQuery(optionsMenu_.state());
        query && *query != optionsMenu_.state().controlsFilter) {
        (void)optionsMenu_.dispatch(settingsCoordinator_.userSettings(),
            options::intent::SetControlsFilter { std::string(*query) });
    }
    if (const std::optional<OptionsAction> optionsAction =
            optionsMenu_.handleInput(
                settingsCoordinator_.userSettings(),
                routedInput.options)) {
        handleShellEvent(ShellOptionsAction { *optionsAction });
    }
    const GamepadPresentation gamepadPresentation =
        input_.activeGamepadPresentation();
    if (const std::optional<OptionsMenuIntent> intent =
            optionsMenuView_.draw(
                ui_,
                pixelSize,
                optionsMenu_.state(),
                settingsCoordinator_.userSettings(),
                &inputPrompts_,
                &gamepadPresentation,
                {
                    .wheelY = routedInput.pointer.wheel.y,
                    .wheelPosition = mapPointerEdge(routedInput.pointer.wheelPosition),
                    .revealSelection = routedInput.options.up ||
                        routedInput.options.down || routedInput.options.left ||
                        routedInput.options.right || routedInput.options.confirm,
                    .extendTextSelection = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0,
                })) {
        if (const std::optional<OptionsAction> optionsAction =
                optionsMenu_.dispatch(
                    settingsCoordinator_.userSettings(),
                    *intent)) {
            handleShellEvent(ShellOptionsAction { *optionsAction });
        }
    }
    updateNativeTextInput();
    if (!ui_.drawData().sceneViewport) {
        drawAssetLoadingOverlay(pixelSize);
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    tools_->animationPreviewDebugUi.update(dt, renderer_);
#endif
    ui_.endFrame();
    if (!startupReported_) {
        const auto elapsedUs = [](auto start, auto end) {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                end - start).count();
        };
        log::info(log::Category::Application)
            << "UI first-frame work (us): begin="
            << elapsedUs(uiStart, debugFrameStart)
            << " debug-frame=" << elapsedUs(debugFrameStart, workspaceStart)
            << " workspace=" << elapsedUs(workspaceStart, overlaysStart)
            << " overlays=" << elapsedUs(overlaysStart, std::chrono::steady_clock::now());
    }
    return developerWorkspaceVisible;
}

// The end of a smoke run: the evidence capture one frame early, the capture
// finish, the residency and phase-timing report, and the stop.
void Application::finishSmokeRunIfDue(std::uint64_t renderedFrames)
{
    if (!evidenceOutputDirectory_.empty() &&
        renderedFrames + 1 == smokeFrames_) {
        captureEvidenceScene();
    }
    if (smokeFrames_ > 0 && renderedFrames >= smokeFrames_) {
        if (!evidenceOutputDirectory_.empty()) {
            finishEvidenceCapture();
        }
        const VulkanModelResources::LoadingStats assetStats =
            renderer_.assetLoadingStats();
        log::info(log::Category::Rendering)
            << "Texture residency "
            << assetStats.textureResidencyBytes << " / "
            << assetStats.textureResidencyBudgetBytes
            << " bytes; mip levels "
            << assetStats.residentTextureMipLevels << " / "
            << assetStats.availableTextureMipLevels << "; "
            << assetStats.mipDegradedTextures
            << " reduced textures omitted "
            << assetStats.mipOmittedBytes << " bytes.";
        // Eviction counters were reachable only from the debug panel,
        // which a headless smoke run never draws - so the two numbers that
        // say whether residency was under pressure, and whether it coped,
        // were missing from exactly the captures taken to find out.
        log::info(log::Category::Rendering)
            << "Residency evictions " << assetStats.residencyEvictions
            << "; capacity blocks " << assetStats.residencyBudgetBlocks
            << " (" << assetStats.residencyOversizedBlocks
            << " larger than the budget, "
            << assetStats.residencyMipPlanBlocks
            << " no mip tail fits, "
            << assetStats.residencyNoVictimBlocks
            << " nothing evictable)"
            << "; dropped draws " << assetStats.droppedDrawInstances
            << ", dropped skinned poses "
            << assetStats.droppedSkinningInstances
            << "; model residency " << assetStats.modelResidencyBytes
            << " / " << assetStats.modelResidencyBudgetBytes
            << " bytes (peak " << assetStats.modelResidencyPeakBytes
            << ").";
        log::info(log::Category::Rendering)
            << "Asset transient memory " << assetStats.transientAssetBytes
            << " bytes (peak " << assetStats.transientAssetPeakBytes
            << "); prepared payload " << assetStats.preparedAssetBytes
            << " + " << assetStats.activeDecodeReservationBytes
            << " decoding bytes / "
            << assetStats.preparedAssetBudgetBytes
            << " budget; prepared-budget deferrals "
            << assetStats.preparedBudgetDeferrals
            << ", oversized starts " << assetStats.oversizedAssetStarts
            << "; skinned packing " << assetStats.skinnedPackingPasses
            << " passes, " << assetStats.skinnedPackingAllocations
            << " allocations, " << assetStats.skinnedPackingTemporaryBytes
            << " cumulative temporary bytes (peak "
            << assetStats.skinnedPackingPeakBytes << "), "
            << assetStats.skinnedUploadBytes << " upload bytes"
            << "; queued source bytes "
            << assetStats.modelStages.queuedSourceBytes << " models, "
            << assetStats.textureStages.queuedSourceBytes << " textures, "
            << assetStats.animationStages.queuedSourceBytes << " animations"
            << "; decoding source bytes "
            << assetStats.modelStages.decodingSourceBytes << " models, "
            << assetStats.textureStages.decodingSourceBytes << " textures, "
            << assetStats.animationStages.decodingSourceBytes << " animations"
            << "; residency deferrals "
            << assetStats.modelStages.residencyDeferrals << " models over "
            << assetStats.modelStages.residencyDeferredMicroseconds
            << " us, " << assetStats.textureStages.residencyDeferrals
            << " textures over "
            << assetStats.textureStages.residencyDeferredMicroseconds
            << " us.";
        log::info(log::Category::Application)
            << "Smoke run finished after " << renderedFrames
            << " frames.";
        running_ = false;
    }
}

bool Application::run()
{
    checkStartupClose(window_);
    startupConstructionMicroseconds_ =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - startupStarted_).count();
    const auto launchStart = std::chrono::steady_clock::now();
    if (smokeFrames_ > 0) {
        // Nobody is here to press New Game, and the title screen draws no
        // world behind it, so a smoke run that stayed on the title would
        // never record a tile, model, shadow or AO pass.
        log::info(log::Category::Application)
            << "Smoke run: starting a new game and rendering "
            << smokeFrames_ << " frames.";
        startNewGame();
        if (evidenceLevel_ >= 0) {
            const LevelLocation location { evidenceLevel_, evidenceScreen_ };
            if (!campaign_.screenExists(location.level, location.screen) ||
                !campaign_.startPuzzle(playerProfile_, location)) {
                throw std::invalid_argument("Evidence puzzle screen does not exist");
            }
            loadCurrentScreen();
        }
    } else if (evidenceOutputDirectory_.empty()) {
        applyLaunchRequest();
    }
    checkStartupClose(window_);
    const auto launchFinished = std::chrono::steady_clock::now();
    std::uint64_t renderedFrames = 0;
    CpuProfiler::instance().setCurrentThreadName("Main");
    auto previousFrameStart = std::chrono::steady_clock::now();
    const auto confirmingQuit = [this] {
        return optionsMenu_.isOpen() &&
            optionsMenu_.state().page == OptionsMenuPage::QuitConfirmation;
    };
    while (running_) {
        const bool wasConfirmingQuit = confirmingQuit();
        const auto applicationFrameStart = std::chrono::steady_clock::now();
        if (renderedFrames > 0 && !evidenceSceneCaptured_) {
            applicationIntervalTelemetry_.record(
                std::chrono::duration<double, std::milli>(
                    applicationFrameStart - previousFrameStart).count());
        }
        previousFrameStart = applicationFrameStart;
        CpuProfiler::instance().beginFrame(renderedFrames + 1);
        framePacer_.beginFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
        // Serviced here, between frames, where no ImGui or UI frame is open.
        if (tools_->bakeThumbnailsRequested) {
            tools_->bakeThumbnailsRequested = false;
            (void)bakeTileThumbnails();
            // The bake drives its own frames. Do not use an older gameplay
            // frame for editor picking or gizmo projection afterward.
            preparedRenderFrame_.reset();
            // The files on disk changed, so drop what the palette already
            // loaded; it would otherwise keep showing the old pictures until
            // the next launch.
            renderer_.invalidateTileThumbnails();
        }
        tools_->serviceShaderHotReload(renderer_);
        observeEditorLevelPublications();
        if (renderedFrames > 0) {
            serviceSourceWatcher();
        }
        tools_->serviceSolutionStore();
#endif
        const auto eventsStart = std::chrono::steady_clock::now();
        input_.beginFrame();

        {
            SOKOBAN_PROFILE_SCOPE("Application.Events");
            SDL_Event event {};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    ui_.cancelPointerInteraction();
                    makerPanPosition_.reset();
                    if (makerOpen_) {
                        makerSession_->endStroke();
                        makerSession_->cancelMove();
                    }
                }
                if (handleMakerCameraEvent(event) || handleNativeTextInputEvent(event)) {
                    continue;
                }
                renderer_.handleEvent(event);
#if SOKOBAN_ENABLE_DEBUG_UI
                const bool detachedCameraConsumed =
                    tools_->handleDetachedCameraEvent(
                        event,
                        window_.nativeHandle(),
                        !shellMenuOpen() && evidenceOutputDirectory_.empty(),
                        renderer_.wantsMouseCapture());
                if (detachedCameraConsumed) {
                    continue;
                }
#endif
                InputRouter::EventContext eventContext {
                    .bindingCapture = optionsMenu_.capturingBinding(),
                    .shellMenuOpen = shellMenuOpen(),
                    .keyboardCaptured = renderer_.wantsKeyboardCapture(),
                    .mouseCaptured = renderer_.wantsMouseCapture(),
                };
#if SOKOBAN_ENABLE_DEBUG_UI
                eventContext.editorEditing =
                    tools_->levelEditor.editingDocument();
                if (tools_->detachedCameraCapturingMouse()) {
                    eventContext.keyboardCaptured = false;
                    eventContext.mouseCaptured = false;
                }
#endif
                const InputRouter::EventResult routedEvent =
                    inputRouter_.routeEvent(event, input_, eventContext);
                if (routedEvent.bindingCandidate) {
                    if (const std::optional<OptionsAction> action =
                            optionsMenu_.provideBindingCandidate(
                                settingsCoordinator_.userSettings(),
                                *routedEvent.bindingCandidate)) {
                        handleShellEvent(ShellOptionsAction { *action });
                    }
                }
                if (routedEvent.closeRequested) {
                    handleShellEvent(ShellCloseRequested {});
                }
            }
        }

        const auto eventsFinished = std::chrono::steady_clock::now();
        const InputRouter::BackAction backAction = !makerOpen_ && levelTransition_.active()
            ? InputRouter::BackAction::None
            : inputRouter_.backAction(input_, inputRoutingContext());
        switch (backAction) {
        case InputRouter::BackAction::CloseDraftConfirmation:
#if SOKOBAN_ENABLE_DEBUG_UI
            tools_->draftExitConfirmationOpen = false;
#endif
            break;
        case InputRouter::BackAction::OpenDraftConfirmation:
#if SOKOBAN_ENABLE_DEBUG_UI
            tools_->draftExitConfirmationOpen = true;
#endif
            break;
        case InputRouter::BackAction::CancelDecorationPlacement:
#if SOKOBAN_ENABLE_DEBUG_UI
            tools_->levelEditor.cancelDecorationPlacement();
#endif
            break;
        case InputRouter::BackAction::ShellBack:
            if ((makerHelpOpen_ || makerCameraDialog_) && !optionsMenu_.isOpen()) break;
            if (makerOverworldMapOpen_ && !optionsMenu_.isOpen()) {
                if (!makerView_.cancelOverworldMapAction()) closeMakerOverworldMap();
            } else if (makerPanPosition_ && !optionsMenu_.isOpen()) {
                makerPanPosition_.reset();
            } else if (makerColorDialog_ && !optionsMenu_.isOpen()) {
                makerColorDialog_->cancelRequested = true;
            } else if (makerDraftDialog_ && !optionsMenu_.isOpen()) {
                makerDraftDialog_->inputAction = { .command = MakerDraftDialogAction::Command::Cancel };
            } else if (nativeTextFocused()) {
                blurNativeTextInput();
            } else if (makerPlaytest_ && !optionsMenu_.isOpen()) {
                exitMakerPlaytestBoard();
            } else if (makerOpen_ && !optionsMenu_.isOpen() && makerView_.closeToolbarOverflow()) {
                ui_.cancelPointerInteraction();
            } else if (makerOpen_ && !optionsMenu_.isOpen() && makerBoardCursor_) {
                clearMakerBoardCursor();
            } else if (makerOpen_ && !optionsMenu_.isOpen() && makerPanel_ != MakerPanel::Palette) {
                setMakerPanel(MakerPanel::Palette);
            } else if (makerOpen_ && !optionsMenu_.isOpen() && makerSession_->selectedLinkEndpoint()) {
                makerSession_->clearLinkSelection();
            } else if (makerOpen_ && !optionsMenu_.isOpen() && makerSession_->selectedLevelSelectorSource()) {
                makerSession_->clearLevelSelectorSource();
            } else if (makerOpen_ && !optionsMenu_.isOpen() &&
                (makerAreaMode(makerSession_->tool()) || makerSession_->selection())) {
                if (makerSession_->tool() == MakerSession::Tool::PasteArea) {
                    makerSession_->setTool(MakerSession::Tool::SelectArea);
                } else if (makerSession_->selection()) {
                    makerSession_->clearSelection();
                } else {
                    makerSession_->setTool(MakerSession::Tool::Paint);
                }
                makerAreaDragAnchor_.reset();
                ui_.cancelPointerInteraction();
            } else {
                handleShellEvent(ShellBackPressed {});
            }
            break;
        case InputRouter::BackAction::None:
            break;
        }

        const InputRouter::Frame routedInput =
            inputRouter_.routeFrame(input_, inputRoutingContext());
#if SOKOBAN_ENABLE_DEBUG_UI
        handleDraftPlaybackShortcuts(routedInput);
#endif
        const float measuredDt = frameTimer_.tick(simulationTiming_);
        // Animated evidence advances by a fixed step, independent of GPU speed.
        // Otherwise freeze the scene for the existing image A/B workflow.
        const float wallClockDt = evidenceOutputDirectory_.empty()
            ? measuredDt
            : (evidenceAnimate_ && renderedFrames + 1 < smokeFrames_ ? 1.0f / 60.0f : 0.0f);
        const float dt = SimulationTiming::scaledDelta(
            wallClockDt,
            settingsCoordinator_.userSettings().gameplay.simulationSpeed);
        makerDetachedCamera_.update(wallClockDt, input_);
        const auto updateStart = std::chrono::steady_clock::now();
        {
            SOKOBAN_PROFILE_SCOPE("Application.Update");
            update(
                dt,
                routedInput,
                preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        }
        const auto uiStart = std::chrono::steady_clock::now();
        if (!evidenceSceneCaptured_) {
            applicationUpdateTelemetry_.record(
                std::chrono::duration<double, std::milli>(uiStart - updateStart).count());
        }
        bool developerWorkspaceVisible = false;
        {
            SOKOBAN_PROFILE_SCOPE("Application.UI");
            // Menus and developer tools remain responsive in wall-clock time,
            // independent of the selected gameplay simulation speed.
            developerWorkspaceVisible = drawUiFrame(routedInput, wallClockDt);
        }
        if (makerOpen_ && makerSession_ && !makerPlaytest_) updateMakerAutosave(*makerSession_);
        if (!running_) {
            renderer_.discardDebugUiFrame();
            CpuProfiler::instance().endFrame();
            break;
        }
        if (!wasConfirmingQuit && confirmingQuit() && campaign_.gameLoaded()) {
            // Gameplay is paused by the dialog. Start saving that checkpoint
            // while the player decides, and still capture it on exit.
            checkpointCurrentScreen(true);
        }
        const auto buildStart = std::chrono::steady_clock::now();
        if (!evidenceSceneCaptured_) {
            applicationUiTelemetry_.record(
                std::chrono::duration<double, std::milli>(buildStart - uiStart).count());
        }
        {
            SOKOBAN_PROFILE_SCOPE("Application.Build render frame");
            RenderFrameData frame = buildRenderFrame(routedInput);
            auto preview = buildScreenPreviewRenderFrame(
                renderFrameArenas_[renderFrameArenaIndex_]);
            preparedRenderFrame_ = renderer_.prepareFrame(
                std::move(frame), std::move(preview));
        }
        if (!evidenceSceneCaptured_) {
            applicationBuildTelemetry_.record(
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - buildStart).count());
        }
        const auto renderStart = std::chrono::steady_clock::now();
        {
            SOKOBAN_PROFILE_SCOPE("Application.Render");
            renderer_.drawFrame(
                *preparedRenderFrame_,
                ui_.drawData(),
                developerWorkspaceVisible);
        }
        // Publish CPU work before deliberate pacing and evidence readback.
        // The application telemetry also includes the profiler's processing.
        CpuProfiler::instance().endFrame();
        if (!evidenceSceneCaptured_) {
            applicationFrameTelemetry_.record(
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - applicationFrameStart).count());
        }
        if (!startupReported_) {
            startupReported_ = true;
            log::info(log::Category::Application)
                << "Application startup phases (us): construction="
                << startupConstructionMicroseconds_
                << " first-frame="
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - startupStarted_)
                       .count();
            const auto elapsedUs = [](auto start, auto end) {
                return std::chrono::duration_cast<std::chrono::microseconds>(
                    end - start).count();
            };
            log::info(log::Category::Application)
                << "Application first-frame work (us): launch="
                << elapsedUs(launchStart, launchFinished)
                << " services=" << elapsedUs(applicationFrameStart, eventsStart)
                << " events=" << elapsedUs(eventsStart, eventsFinished)
                << " input=" << elapsedUs(eventsFinished, updateStart)
                << " update=" << elapsedUs(updateStart, uiStart)
                << " ui=" << elapsedUs(uiStart, buildStart)
                << " build=" << elapsedUs(buildStart, renderStart)
                << " render=" << elapsedUs(renderStart, std::chrono::steady_clock::now());
            const RenderStats stats = renderer_.renderStats();
            log::info(log::Category::Application)
                << "Renderer first-frame work (ms): scheduling="
                << stats.assetSchedulingTiming.latestMilliseconds
                << " fence=" << stats.frameFenceWaitTiming.latestMilliseconds
                << " maintenance=" << stats.assetMaintenanceTiming.latestMilliseconds
                << " acquire=" << stats.imageAcquisitionTiming.latestMilliseconds
                << " commands=" << stats.commandRecordingTiming.latestMilliseconds
                << " present=" << stats.submitPresentTiming.latestMilliseconds;
        }
        ++renderedFrames;
        if (renderer_.hasFatalFailure()) {
            log::error(log::Category::Application)
                << "Rendering stopped: " << renderer_.fatalFailureMessage();
            running_ = false;
        } else {
            finishSmokeRunIfDue(renderedFrames);
        }
        if (running_) {
            serviceUpcomingAssetPrefetch();
            const auto pacingStart = std::chrono::steady_clock::now();
            framePacer_.pace();
            applicationPacingTelemetry_.record(
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - pacingStart).count());
        }
    }
    audioSystem_.reset();
    log::debug(log::Category::Application) << "Audio stopped for shutdown.";
#if SOKOBAN_ENABLE_DEBUG_UI
    observeEditorLevelSaves();
#endif
    return !renderer_.hasFatalFailure();
}

void Application::updateMakerPlaytest(float dt, const InputRouter::Frame& input)
{
    if (optionsMenu_.isOpen()) {
        audioSystem_->update(dt, false, false);
        return;
    }

    if (input.gameplay.interactPressed && tryEnterMakerPlaytestSelector()) {
        audioSystem_->update(dt, false, false);
        return;
    }

    auto& playtest = *makerPlaytest_;
    auto& board = playtest.activeBoard();
    auto& session = board.session;
    auto& presentation = board.presentation;
    const auto* world = !playtest.puzzle && playtest.overworld ? &*playtest.overworld : nullptr;
    const CameraAngles angles = makerPlaytestAngles(board.level, world, session.state());
    presentation.advanceClocks(dt, session.moving() && session.activeAction().reversed);
    presentation.updateCameraPitch(input.showTopDownView ? 0.0f : angles.pitchDegrees,
        dt, config::cameraPitchTransitionSeconds);
    presentation.updateCameraYaw(angles.yawDegrees, dt, config::cameraPitchTransitionSeconds);

    const auto reading = session.readingLectern();
    const auto result = GameplayLoop::update(
        board.level, session, presentation, input.gameplay, dt, true);
    if (reading && (input.gameplay.interactPressed || input.gameplay.dismissPressed)) {
        audioSystem_->update(dt, false, false);
        return;
    }
    if (session.readingLectern() && session.readingLectern() != reading) {
        board.lecternDialog.reset();
    }
    if (!reading || !session.readingLectern() || session.moving() || result.stateCommitted) {
        board.particles.update(dt);
    }
    if (input.gameplay.restartPressed && !reading) {
        board.solved = false;
    }
    if (result.draftSolved && playtest.puzzle) playtest.progress.recordCompletion(playtest.puzzle->id);
    board.solved |= result.draftSolved;
    presentGameplayEffects(board.level, session, presentation, board.particles,
        board.turretFlames, result, input.gameplay, dt);
    updateGameplayAudio(dt, session, presentation);
}

void Application::update(
    float dt,
    const InputRouter::Frame& input,
    const VulkanRenderer::PreparedFrame* previousRenderFrame)
{
    screenPreviewActive_ = false;
    if (makerPlaytest_) {
        updateMakerPlaytest(dt, input);
        return;
    }
#if !SOKOBAN_ENABLE_DEBUG_UI
    (void)previousRenderFrame;
#endif

    const bool reversed =
        gameplaySession_.moving() &&
        gameplaySession_.activeAction().reversed;
    presentation_.advanceClocks(dt, reversed);
#if SOKOBAN_ENABLE_DEBUG_UI
    const bool editorDraftPlaying = tools_->levelEditor.playingDraft();
    if (!editorDraftPlaying && solveCapture_ && solveCapture_->playingDraft) {
        solveCapture_.reset();
    }
#else
    constexpr bool editorDraftPlaying = false;
#endif
    const bool detachedCameraActive =
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->detachedCameraActive();
#else
        false;
#endif
    const bool showOverworldMap =
        !detachedCameraActive && input.showOverworldMap &&
        campaign_.inOverworld() &&
        !editorDraftPlaying;
    const CameraAngles cameraAngles = gameplayCameraAngles();
    presentation_.updateCameraPitch(
        (!detachedCameraActive &&
                (input.showTopDownView || showOverworldMap))
            ? 0.0f
            : cameraAngles.pitchDegrees,
        dt,
        config::cameraPitchTransitionSeconds);
    presentation_.updateCameraYaw(
        cameraAngles.yawDegrees, dt, config::cameraPitchTransitionSeconds);
    const float overviewStep = config::cameraPitchTransitionSeconds <= 0.0f
        ? 1.0f
        : std::max(dt, 0.0f) / config::cameraPitchTransitionSeconds;
    overworldOverviewProgress_ = showOverworldMap
        ? std::min(overworldOverviewProgress_ + overviewStep, 1.0f)
        : std::max(overworldOverviewProgress_ - overviewStep, 0.0f);

#if SOKOBAN_ENABLE_DEBUG_UI
    // Last frame's board interaction decides the editor's tool cursor; any
    // menu, modal or fly camera puts the ordinary cursor back.
    tools_->updateEditorCursor(
        !shellMenuOpen() && !tools_->draftExitConfirmationOpen &&
        !detachedCameraActive && tools_->levelEditor.editingDocument());
#endif
    if (shellMenuOpen()) {
        audioSystem_->update(dt, false, false);
        return;
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    if (tools_->draftExitConfirmationOpen) {
        audioSystem_->update(dt, false, false);
        return;
    }
    if (detachedCameraActive) {
        tools_->updateDetachedCamera(dt, input_);
    }
    if (tools_->levelEditor.editingDocument()) {
        audioSystem_->update(dt, false, false);
        audioSystem_->playMusicForLevel(
            campaign_.inOverworld() ? 0 : campaign_.currentLevel());
        if (!detachedCameraActive) {
            tools_->updateEditorInteraction(
                input.editor,
                previousRenderFrame,
                renderer_,
                window_.size(),
                window_.sizeInPixels());
        }
        return;
    }
#endif

    if (overworldFogReveal_) {
        overworldFogReveal_->progress += std::max(dt, 0.0f) /
            config::fogOfWarRevealDurationSeconds;
        if (overworldFogReveal_->progress >= 1.0f) {
            overworldFogReveal_.reset();
        }
    }

    if (levelTransition_.active()) {
        updateLevelTransition(dt);
        audioSystem_->update(dt, false, false);
        return;
    }

    if (!gameplaySession_.readingLectern() && updateScreenPreview(
            !detachedCameraActive && input.previewScreen,
            dt)) {
        audioSystem_->update(dt, false, false);
        return;
    }

    const auto readingBeforeUpdate = gameplaySession_.readingLectern();
    if (!readingBeforeUpdate && input.gameplay.interactPressed &&
        tryEnterSelector()) {
        audioSystem_->update(dt, false, false);
        return;
    }
    const bool dismissingLectern = readingBeforeUpdate &&
        (input.gameplay.interactPressed || input.gameplay.dismissPressed);
    const GameplayLoop::InputFrame gameplayInput = detachedCameraActive && !dismissingLectern
        ? GameplayLoop::InputFrame {} : input.gameplay;
    if (!readingBeforeUpdate) {
        if (!editorDraftPlaying) {
            campaign_.addElapsedTime(dt);
        }
        particleSystem_.update(dt);
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    if (solveCapture_) {
        solveCapture_->frames.push_back({ .input = gameplayInput, .elapsedSeconds = dt });
    }
#endif
    const GameplayLoop::UpdateResult gameplayResult = GameplayLoop::update(
        level_,
        gameplaySession_,
        presentation_,
        gameplayInput,
        dt,
        editorDraftPlaying);
    if (dismissingLectern) {
        audioSystem_->update(dt, false, false);
        return;
    }
    if (const auto reading = gameplaySession_.readingLectern(); reading && reading != readingBeforeUpdate) {
        lecternDialog_.reset();
    }
    if (readingBeforeUpdate && (!gameplaySession_.readingLectern() ||
        gameplaySession_.moving() || gameplayResult.stateCommitted)) {
        if (!editorDraftPlaying) {
            campaign_.addElapsedTime(dt);
        }
        particleSystem_.update(dt);
    }
    if (gameplayResult.stateCommitted && campaign_.inOverworld() &&
        overworldMap_ && !editorDraftPlaying) {
        const std::optional<OverworldScreenId> playerScreen =
            CampaignSession::sharedPlayerScreen(
                *overworldMap_, gameplaySession_.state());
        if (playerScreen &&
            *playerScreen != campaign_.activeOverworldScreen()) {
            const bool firstVisit = !campaign_.overworldScreenDiscovered(
                playerProfile_, *playerScreen);
            if (!campaign_.transitionOverworldScreen(
                    playerProfile_, *playerScreen)) {
                log::error(log::Category::Gameplay)
                    << "Could not commit overworld screen transition to "
                    << *playerScreen;
            } else {
                upcomingAssetPrefetchPending_ = true;
                if (firstVisit && !gameplaySession_.state().players.empty()) {
                    const GridPosition3 entry =
                        gameplaySession_.state().players.front().cell;
                    overworldFogReveal_ = OverworldFogReveal {
                        .screen = *playerScreen,
                        .origin = {
                            static_cast<float>(entry.x) + 0.5f,
                            static_cast<float>(entry.y) + 0.5f,
                        },
                    };
                }
            }
        }
    }
    presentGameplayEffects(level_, gameplaySession_, presentation_, particleSystem_,
        turretFlames_, gameplayResult, input.gameplay, dt);
#if SOKOBAN_ENABLE_DEBUG_UI
    if (gameplayResult.draftSolved) {
        tools_->levelEditor.markDraftSolved();
    }
    if (solveCapture_ && (solveCapture_->playingDraft ? gameplayResult.draftSolved
                                                    : gameplayResult.screenSolved)) {
        auto solve = std::move(solveCapture_);
        tools_->saveSolve(std::move(*solve));
    } else if (gameplayResult.screenSolved && !campaign_.inOverworld() && developerFilesWritable()) {
        log::info(log::Category::Editor)
            << "Campaign solve of level" << campaign_.currentLevel()
            << "/screen" << campaign_.currentScreen() << " resumed from a checkpoint. "
               "Play Draft starts the screen at its authored state and captures the full solve.";
    }
#endif
    if (gameplayResult.screenSolved) {
        advanceScreen();
    } else if (!editorDraftPlaying && gameplayResult.stateCommitted &&
        campaign_.deferCheckpoint()) {
        checkpointCurrentScreen(true);
    }
    if (!gameplaySession_.readingLectern() && campaign_.updateDeferredCheckpoint(
            dt,
            gameplaySession_.moving(),
            editorDraftPlaying)) {
        checkpointCurrentScreen(true);
    }

    updateGameplayAudio(dt, gameplaySession_, presentation_);
    audioSystem_->playMusicForLevel(
        campaign_.inOverworld() ? 0 : campaign_.currentLevel(),
        gameplaySession_.activeHeroCharacter());
}

void Application::presentGameplayEffects(const Level& level, const GameplaySession& session,
    GameplayPresentation& presentation, ParticleSystem& particles,
    TurretFlameSystem& turretFlames, const GameplayLoop::UpdateResult& result,
    const GameplayLoop::InputFrame& input, float dt)
{
    if (result.mirrorActivated) {
        audioSystem_->playOneShot("mirror-swap");
        for (GridPosition3 destination :
             result.mirrorSwapDestinations) {
            particles.emit(
                {
                    static_cast<float>(destination.x) + 0.5f,
                    static_cast<float>(destination.y) + 0.5f,
                    static_cast<float>(destination.z) +
                        config::mirrorSwapSmokeElevation,
                },
                mirrorSwapParticleEffect_);
        }
    }
    if (result.witchSwapped) {
        for (GridPosition3 destination :
             result.witchSwapDestinations) {
            particles.emit(
                {
                    static_cast<float>(destination.x) + 0.5f,
                    static_cast<float>(destination.y) + 0.5f,
                    static_cast<float>(destination.z) +
                        config::mirrorSwapSmokeElevation,
                },
                witchSwapParticleEffect_);
        }
    }
    for (GridPosition3 cell : result.crystalPuffs) {
        particles.emit(
            { static_cast<float>(cell.x) + 0.5f,
              static_cast<float>(cell.y) + 0.5f,
              static_cast<float>(cell.z) + 0.4f },
            crystalChangeParticleEffect_);
    }
    if (input.undoPressed || input.undoDown || input.restartPressed) {
        turretFlames.reset();
    }
    turretFlames.update(level, session.state(), session.readingLectern() ? 0.0f : dt,
        &presentation);
    for (const GameplayLoop::UpdateResult::TurretShotPresentation& shot :
         result.turretShots) {
        turretFlames.triggerShot(shot.shot, shot.impactDelaySeconds);
        presentation.triggerTurretShot(
            shot.shot.turret.id,
            shot.shot.direction);
    }
    for (const auto sound : result.sounds) {
        audioSystem_->playOneShot(soundSetName(sound));
    }
}

void Application::updateGameplayAudio(float dt, const GameplaySession& session,
    const GameplayPresentation& presentation)
{
    bool playerMoving = false;
    bool pushing = false;
    for (const GameplayPresentation::PlayerVisual& player :
         presentation.players()) {
        playerMoving |= player.motion.moving;
        pushing |= player.motion.moving &&
            (player.animationUse == AnimationUse::PlayerPush ||
                player.animationUse == AnimationUse::PlayerPull);
    }
    const bool minecartMoving = std::ranges::any_of(
        presentation.minecarts(), [](const auto& cart) { return cart.moving; });
    const bool elevatorMoving = std::ranges::any_of(
        presentation.elevators(), [](const auto& elevator) { return elevator.moving; });
    audioSystem_->update(dt, playerMoving, pushing, minecartMoving, elevatorMoving,
        AtmosphericAudio::listenerPosition(session.state(), presentation,
            session.activeHeroController()));
}

void Application::loadCurrentScreen()
{
    overworldFogReveal_.reset();
    refreshLevelCatalogIfNeeded();
    if (!campaign_.inOverworld() && !campaign_.screenExists(
            campaign_.currentLevel(), campaign_.currentScreen())) {
        restoreProfileLocation();
    }
    const CampaignSession::WorldRestore restore =
        campaign_.prepareWorldLoad(playerProfile_);

    const std::optional<LevelLocation> location = campaign_.inOverworld()
        ? std::nullopt
        : std::optional<LevelLocation> { campaign_.location() };
    const std::filesystem::path path = campaign_.inOverworld()
        ? (overworldMap_ ? overworldRoot() : overworldPath())
        : screenPath(campaign_.currentLevel(), campaign_.currentScreen());

    auto puzzleDefinition = location
        ? std::optional { Level::loadDefinitionFromFile(path) } : std::nullopt;
    Level loadedLevel = puzzleDefinition
        ? Level::loadFromDefinition(*puzzleDefinition, pathText(path))
        : catalogOverworldLevel();

    const bool restored = applyLevel(
        std::move(loadedLevel),
        restore.snapshot ? &*restore.snapshot : nullptr,
        location,
        campaign_.inOverworld() && overworldMap_.has_value());
    gameplayGroundGeometrySource_ = (campaign_.inOverworld() && overworldMap_
        ? path / "layout.json" : path).lexically_relative(assetRoot_ / "levels");
    if (restore.checkpointMatched && !restored) {
        log::warning(log::Category::Persistence)
            << "Discarded invalid gameplay checkpoint for "
            << (campaign_.inOverworld() ? "the overworld" : "puzzle");
        if (campaign_.inOverworld()) {
            playerProfile_.overworldCheckpoint.reset();
        } else {
            playerProfile_.activeScreen.reset();
        }
    }
    campaign_.finishWorldLoad(playerProfile_);
#if SOKOBAN_ENABLE_DEBUG_UI
    draftWorldLoaded_ = false;
    if (location && developerFilesWritable()) {
        const auto& state = gameplaySession_.state();
        const EntityId firstController = state.players.empty()
            ? invalidEntityId : rules::playerControllerId(state, 0);
        // A validated empty history starts on the authored board. The
        // controller and ambient-motion flag are independent of that history.
        const bool atAuthoredStart = !restored ||
            (restore.snapshot->undoStack.empty() && !gameplaySession_.automaticMotionPaused() &&
                gameplaySession_.activeHeroController() == firstController);
        if (atAuthoredStart) {
            solveCapture_ = std::make_unique<solution::SolveToStore>(solution::SolveToStore {
                .name = "level" + std::to_string(location->level) +
                    "/screen" + std::to_string(location->screen),
                .definition = std::move(*puzzleDefinition),
                .playingDraft = false,
            });
        }
    }
    tools_->levelEditor.setPlayingDraft(false);
    tools_->levelEditor.setEditingDocument(false);
    tools_->hoverCell.reset();
    tools_->pickedCell.reset();
#endif
    checkpointCurrentScreen(true);
    upcomingAssetPrefetchPending_ = true;

    log::debug(log::Category::Gameplay)
        << "player entered "
        << (campaign_.inOverworld() ? "the overworld" : "puzzle");
}

bool Application::applyLevel(
    Level level,
    const GameplaySession::Snapshot* snapshot,
    std::optional<LevelLocation> location,
    bool composedOverworld)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    solveCapture_.reset();
#endif
    // A level change makes speculative work for the prior world irrelevant.
    // Running tasks may finish, but queued disk work is discarded before it
    // competes with the assets the player is about to see.
    renderer_.cancelQueuedAssetPrefetches();
    // A composed overworld includes distant screens and every terrain variant.
    // drawFrame requests its exact visible models, splats and dependencies.
    // Warm action clips and brief effects before first use in both worlds.
    if (composedOverworld) {
        renderer_.ensureAssets(renderActionAssetRequirementsForLevel(
            level, assetManifest_, &animationCatalog_));
    } else {
        renderer_.ensureAssets(renderAssetRequirementsForLevel(
            level, assetManifest_, location, &animationCatalog_));
    }
    gameplayGroundGeometryStore_.invalidate();
    gameplayGroundGeometrySource_.clear();
    level_ = std::move(level);
    bool restored = snapshot && gameplaySession_.restore(
        level_, *snapshot, campaign_.inOverworld());
    if (restored && composedOverworld && overworldMap_ &&
        !overworldActionStateAllowed(*overworldMap_, gameplaySession_.state())) {
        restored = false;
    }
    if (!restored) {
        gameplaySession_.reset(level_);
    }
    if (composedOverworld && overworldMap_) {
        gameplaySession_.setActionAdmissionPolicy([this](const GameState& state) {
            return overworldMap_ &&
                overworldActionStateAllowed(*overworldMap_, state);
        });
    } else {
        gameplaySession_.clearActionAdmissionPolicy();
    }
    presentation_.resetEntities(gameplaySession_.state());
    audioSystem_->setAtmosphericLevel(level_);
    audioSystem_->playMusicForLevel(
        campaign_.inOverworld() ? 0 : campaign_.currentLevel(),
        gameplaySession_.activeHeroCharacter());
    particleSystem_.reset();
    turretFlames_.reset();
    campaign_.markWorldLoaded();
    return restored;
}

void Application::advanceScreen()
{
    if (campaign_.inOverworld()) {
        log::warning(log::Category::Gameplay)
            << "Ignored an overworld completion event; overworlds may not "
               "contain End tiles";
        return;
    }
    const int moveCount = gameplaySession_.playerMoveCount();
    beginLevelTransition([this, moveCount] {
        refreshLevelCatalogIfNeeded();
        if (!campaign_.screenExists(campaign_.currentLevel(), campaign_.currentScreen())) {
            loadCurrentScreen();
            return;
        }
        handlePuzzleCompleted(campaign_.completePuzzle(
            playerProfile_, moveCount, &catalogOverworldLevel()));
    });
}

void Application::handlePuzzleCompleted(
    const CampaignSession::PuzzleCompleted& completed)
{
    auto& checkpoint = playerProfile_.overworldCheckpoint;
    if (!completed.nextPuzzle && overworldMap_ && checkpoint &&
        checkpoint->topologyFingerprint == overworldMap_->fingerprint()) {
        const auto screen = CampaignSession::sharedPlayerScreen(
            *overworldMap_, checkpoint->session.state);
        if (screen && *screen != checkpoint->activeScreen &&
            campaign_.transitionOverworldScreen(playerProfile_, *screen)) {
            checkpoint->activeScreen = *screen;
        }
    }
    persistProfile(true);
    if (completed.nextPuzzle) {
        (void)campaign_.startPuzzle(playerProfile_, *completed.nextPuzzle);
    }
    loadCurrentScreen();
}

void Application::beginLevelTransition(
    std::function<void()> midpointAction)
{
    if (!midpointAction || !levelTransition_.start()) {
        return;
    }
    levelTransitionMidpointAction_ = std::move(midpointAction);
    screenPreviewActive_ = false;
}

void Application::updateLevelTransition(float dt)
{
    const LevelTransition::UpdateResult result = levelTransition_.update(dt);
    if (!result.midpointReached || !levelTransitionMidpointAction_) {
        return;
    }

    std::function<void()> action =
        std::move(levelTransitionMidpointAction_);
    levelTransitionMidpointAction_ = {};
    action();
}

const Level::ScreenSelector* Application::currentOverworldSelector() const
{
    if (!campaign_.gameLoaded() || !campaign_.inOverworld() ||
        levelTransition_.active() || shellMenuOpen()
#if SOKOBAN_ENABLE_DEBUG_UI
        || tools_->levelEditor.editingDocument() ||
        tools_->levelEditor.playingDraft() || tools_->detachedCameraActive()
#endif
    ) {
        return nullptr;
    }
    return CampaignSession::selectorForInteraction(level_, gameplaySession_);
}

bool Application::tryEnterSelector()
{
    const Level::ScreenSelector* sharedSelector =
        currentOverworldSelector();
    if (!sharedSelector || !sharedSelector->target) {
        if (sharedSelector) {
            log::warning(log::Category::Gameplay)
                << "Selector " << sharedSelector->id
                << " has no assigned screen";
        }
        return false;
    }
    if (!campaign_.screenExists(
            sharedSelector->target->level,
            sharedSelector->target->screen)) {
        log::warning(log::Category::Gameplay)
            << "Selector " << sharedSelector->id
            << " targets missing screen " << sharedSelector->target->level
            << ':' << sharedSelector->target->screen;
        return false;
    }
    if (campaign_.selectorViewState(
            playerProfile_, *sharedSelector->target).status ==
        ScreenSelectorStatus::Unavailable) {
        log::debug(log::Category::Gameplay)
            << "Selector " << sharedSelector->id
            << " is not playable until the previous screen is solved";
        return false;
    }

    // Queue the current return point before changing the profile context.
    checkpointCurrentScreen(true);
    const LevelLocation target = *sharedSelector->target;
    beginLevelTransition([this, target] {
        refreshLevelCatalogIfNeeded();
        (void)campaign_.startPuzzle(playerProfile_, target);
        loadCurrentScreen();
    });
    return true;
}

bool Application::updateScreenPreview(bool requested, float dt)
{
    if (!requested) {
        return false;
    }

    const Level::ScreenSelector* selector =
        currentOverworldSelector();
    if (!selector || !selector->target ||
        !campaign_.screenExists(
            selector->target->level, selector->target->screen) ||
        campaign_.selectorViewState(
            playerProfile_, *selector->target).status ==
            ScreenSelectorStatus::Unavailable) {
        return false;
    }

    if (!screenPreviewLevel_ ||
        screenPreviewTarget_ != selector->target) {
        try {
            Level preview = Level::loadFromFile(screenPath(
                selector->target->level,
                selector->target->screen));
            screenPreviewSession_.reset(preview);
            screenPreviewPresentation_.resetEntities(
                screenPreviewSession_.state());
            screenPreviewLevel_ = std::move(preview);
            screenPreviewTarget_ = *selector->target;
            previewGroundGeometryStore_.invalidate();
            previewGroundGeometrySource_ = screenPath(
                selector->target->level, selector->target->screen)
                .lexically_relative(assetRoot_ / "levels");
        } catch (const std::exception& error) {
            log::error(log::Category::Gameplay)
                << "Could not preview screen "
                << selector->target->level << ':'
                << selector->target->screen << ": " << error.what();
            screenPreviewLevel_.reset();
            screenPreviewTarget_.reset();
            return false;
        }
    }

    screenPreviewPresentation_.advanceClocks(dt, false);
    screenPreviewActive_ = true;
    return true;
}

std::optional<RenderFrameData>
Application::buildScreenPreviewRenderFrame(FrameArena& arena) const
{
    if (!screenPreviewActive_ || !screenPreviewLevel_ ||
        !screenPreviewTarget_) {
        return std::nullopt;
    }

    const GameState& projectedState = screenPreviewSession_.projectedStateView();
    const auto& activationPreview = screenPreviewSession_.activationPreview(*screenPreviewLevel_);
    RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = *screenPreviewLevel_,
        .state = screenPreviewSession_.state(),
        .activeHeroController =
            screenPreviewSession_.activeHeroController(),
        .moving = false,
        .projectedState = projectedState,
        .presentation = screenPreviewPresentation_,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .conveyorBeltScrollOffset =
            screenPreviewPresentation_.conveyorBeltScrollOffset(
                screenPreviewSession_.stepDurationSeconds()),
        .levelLocation = screenPreviewTarget_,
        .selectorState = [this](LevelLocation target) {
            return campaign_.selectorViewState(playerProfile_, target);
        },
        .cachedActivationPreviews = true,
        .activationPreview = activationPreview ? &*activationPreview : nullptr,
        .invalidMirrorBeams = screenPreviewSession_.invalidMirrorBeams(*screenPreviewLevel_),
        .invalidMirrorEntities = screenPreviewSession_.invalidMirrorPreview(*screenPreviewLevel_).entities,
        .groundGeometryCache = &previewGroundGeometryCache_,
    }, arena);
    if (presentationSettings_.geometry.smoothGroundRim &&
        presentationSettings_.geometry.processGroundGeometry) {
        frame.processedGroundArtifact = previewGroundGeometryStore_.get(
            previewGroundGeometrySource_, *screenPreviewLevel_, assetManifest_,
            previewGroundGeometryCache_.rebuildCount(), assetRoot_);
    }
    // The preview and live world can contain the same stable actor IDs. Keep
    // their GPU animation instances independent so one idle pose cannot
    // overwrite the other's skinning buffers.
    constexpr uint64_t previewInstanceNamespace = uint64_t { 1 } << 63;
    for (RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.animationInstanceId != 0) {
            tile.animationInstanceId |= previewInstanceNamespace;
        }
    }
    return frame;
}

void Application::drawSelectorPrompt(
    const VulkanRenderer::PreparedFrame* frame)
{
    if (!frame) {
        return;
    }

    const Level::ScreenSelector* selector =
        currentOverworldSelector();
    if (!selector || !selector->target ||
        !campaign_.screenExists(
            selector->target->level, selector->target->screen) ||
        campaign_.selectorViewState(
            playerProfile_, *selector->target).status ==
            ScreenSelectorStatus::Unavailable ||
        presentation_.players().empty()) {
        return;
    }

    const std::optional<UiRect> playerBounds =
        renderer_.primaryPlayerBoundsToPixels(*frame);
    std::optional<Vec2> arrowTip;
    if (playerBounds) {
        constexpr float gapAbovePlayer = 10.0f;
        constexpr float minimumArrowTipY = 56.0f;
        arrowTip = Vec2 {
            playerBounds->position.x + playerBounds->size.x * 0.5f,
            std::max(
                playerBounds->position.y - gapAbovePlayer,
                minimumArrowTipY),
        };
    }
    const BindingDeviceClass device =
        input_.activeDevice() == ActiveInputDevice::Gamepad
        ? BindingDeviceClass::Gamepad
        : BindingDeviceClass::Keyboard;
    const InputBinding* enterBinding =
        SelectorPrompt::bindingView(
            input_.bindings(), InputAction::MenuConfirm, device);
    const InputBinding* previewBinding =
        SelectorPrompt::bindingView(
            input_.bindings(), InputAction::PreviewScreen, device);
    if (arrowTip && enterBinding && previewBinding) {
        const GamepadPresentation gamepad = input_.activeGamepadPresentation();
        const std::optional<InputPromptGlyph> enterGlyph =
            inputPrompts_.glyphForBinding(*enterBinding, gamepad);
        const std::optional<InputPromptGlyph> previewGlyph =
            inputPrompts_.glyphForBinding(*previewBinding, gamepad);
        if (enterGlyph && previewGlyph) {
            SelectorPrompt::draw(ui_, *arrowTip, *enterGlyph, *previewGlyph);
        } else {
            const std::string_view enterLabel =
                SelectorPrompt::bindingLabel(
                    input_.bindings(), InputAction::MenuConfirm, device, ui_.frameArena());
            const std::string_view previewLabel =
                SelectorPrompt::bindingLabel(
                    input_.bindings(), InputAction::PreviewScreen, device, ui_.frameArena());
            if (!enterLabel.empty() && !previewLabel.empty()) {
                SelectorPrompt::draw(ui_, *arrowTip, enterLabel, previewLabel);
            }
        }
    }
}

void Application::drawAssetLoadingOverlay(Vec2 viewport)
{
    const VulkanModelResources::LoadingStats stats =
        renderer_.assetLoadingStats();
    const uint32_t pending = stats.pendingModels + stats.pendingTextures +
        stats.pendingAnimations;
    if (pending == 0 && stats.failedAssets == 0) {
        return;
    }

    const float width = std::min(460.0f, std::max(260.0f, viewport.x - 32.0f));
    const UiRect panel {
        .position = { (viewport.x - width) * 0.5f, 22.0f },
        .size = { width, stats.failedAssets > 0 ? 104.0f : 78.0f },
    };
    ui_.rect(panel, { 0.025f, 0.035f, 0.045f, 0.90f });
    ui_.rect({
        .position = panel.position,
        .size = { panel.size.x, 2.0f },
    }, { 0.24f, 0.70f, 0.95f, 0.95f });
    ui_.centeredText({
        .position = { panel.position.x, panel.position.y + 8.0f },
        .size = { panel.size.x, 24.0f },
    }, "Loading world assets", { 0.91f, 0.95f, 1.0f, 1.0f }, 19.0f);

    const float progress = stats.requestedAssets == 0
        ? 0.0f
        : std::clamp(
            static_cast<float>(stats.readyRequestedAssets) /
                static_cast<float>(stats.requestedAssets),
            0.0f,
            1.0f);
    const UiRect track {
        .position = { panel.position.x + 18.0f, panel.position.y + 39.0f },
        .size = { panel.size.x - 36.0f, 10.0f },
    };
    ui_.rect(track, { 0.12f, 0.16f, 0.20f, 1.0f });
    ui_.rect({
        .position = track.position,
        .size = { track.size.x * progress, track.size.y },
    }, { 0.24f, 0.70f, 0.95f, 1.0f });

    char detail[96] {};
    std::snprintf(detail, sizeof(detail), "%u / %u ready  -  %u pending",
        stats.readyRequestedAssets, stats.requestedAssets, pending);
    ui_.centeredText({
        .position = { panel.position.x, panel.position.y + 53.0f },
        .size = { panel.size.x, 20.0f },
    }, detail, { 0.76f, 0.82f, 0.88f, 1.0f }, 15.0f);
    if (stats.failedAssets > 0) {
        ui_.centeredText({
            .position = { panel.position.x, panel.position.y + 76.0f },
            .size = { panel.size.x, 20.0f },
        }, "Some art failed to load; fallback visuals are in use.",
            { 1.0f, 0.72f, 0.38f, 1.0f }, 14.0f);
    }
}

void Application::drawScreenPreviewOverlay(Vec2 viewport)
{
    ScreenPreviewOverlay::draw(ui_, viewport);
}

void Application::openTitleScreen()
{
    titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
    titleScreen_.open();
}

std::vector<SaveSlotInfo> Application::saveSlotInfos() const
{
    std::vector<SaveSlotInfo> slots;
    for (const SaveSlotManager::SlotSummary& summary :
        saveSlots_.slotSummaries(
            playerProfile_, campaign_.overworldTargets())) {
        slots.push_back({
            .state = summary.state,
            .completed = summary.completed,
            .currentLevel = summary.currentLevel,
            .completedLevels = summary.completedLevels,
        });
    }
    return slots;
}

bool Application::switchSaveSlot(int slot)
{
    if (slot < 0 || slot >= SaveSlotManager::slotCount) {
        return false;
    }
    if (slot == saveSlots_.activeSlot()) {
        return true;
    }

    // Settle the outgoing slot on disk first. Not gated on the world being
    // idle - see the destructor for why that gate was wrong under concurrency.
    if (campaign_.gameLoaded()) {
        checkpointCurrentScreen(true);
    } else {
        persistProfile(true);
    }

    std::optional<PlayerProfile> switched;
    try {
        switched = saveSlots_.switchTo(slot, playerProfile_);
    } catch (const std::exception& error) {
        const std::string message =
            "Could not switch to save slot " + std::to_string(slot + 1) +
            ": " + error.what();
        log::error(log::Category::Persistence) << message;
        titleScreen_.setSaveSlotError(message);
        return false;
    }
    if (!switched) {
        return false;
    }
    playerProfile_ = std::move(*switched);
    titleScreen_.setSaveSlotError({});
    log::info(log::Category::Persistence)
        << saveSlots_.progressStatus();

    // The new slot's world loads on Continue/New Game, like at boot.
    campaign_.resetForProfile(playerProfile_);
    renderer_.cancelQueuedAssetPrefetches();
    upcomingAssetPrefetchPending_ = true;
    openTitleScreen();
    return true;
}

void Application::deleteSaveSlot(int slot)
{
    if (slot < 0 || slot >= SaveSlotManager::slotCount) {
        return;
    }
    const SaveSlotManager::DeleteResult deletion = saveSlots_.deleteSlot(slot);
    if (!deletion.succeeded) {
        const std::string message =
            "Could not delete save slot " + std::to_string(slot + 1) +
            ": " + deletion.message;
        log::error(log::Category::Persistence) << message;
        // Keep the current profile and world exactly as they were. Refreshing
        // the summaries retains the saved slot instead of presenting a false
        // empty state.
        titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
        titleScreen_.setSaveSlotError(message);
        return;
    }

    if (slot == saveSlots_.activeSlot()) {
        // The file stays absent until the player starts playing again.
        playerProfile_.resetProgress();
        campaign_.resetForProfile(playerProfile_);
        renderer_.cancelQueuedAssetPrefetches();
        upcomingAssetPrefetchPending_ = true;
    }
    titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
    if (deletion.cleanupPending) {
        log::warning(log::Category::Persistence) << deletion.message;
        titleScreen_.setSaveSlotError(deletion.message);
    } else {
        titleScreen_.setSaveSlotError({});
    }
}

void Application::persistSettings(bool immediate)
{
    saveSlots_.saveSettings(playerProfile_, immediate);
}

ShellFacts Application::shellFacts() const
{
    return {
        .gameLoaded = campaign_.gameLoaded(),
        .optionsOpen = optionsMenu_.isOpen(),
        .titleOpen = titleScreen_.isOpen(),
        .titleAtMainPage = titleScreen_.page() == TitleScreen::Page::Main,
#if SOKOBAN_ENABLE_DEBUG_UI
        .editorActive = tools_->levelEditor.editingDocument() || draftWorldLoaded_,
#endif
        .makerOpen = makerOpen_,
    };
}

void Application::handleShellEvent(const ShellEvent& event)
{
    for (const ShellCommand& command : shellFlow_.handle(event, shellFacts())) {
        executeShellCommand(command);
    }
}

void Application::executeShellCommand(const ShellCommand& command)
{
    std::visit(flow::Overloaded {
        [&](const shell::LoadCurrentScreen&) {
            restoreProfileLocation();
            loadCurrentScreen();
        },
        [&](const shell::ResumeCampaign&) {
#if SOKOBAN_ENABLE_DEBUG_UI
            resumeCampaign();
#endif
        },
        [&](const shell::CloseTitle&) { titleScreen_.close(); },
        [&](const shell::OpenTitle&) { openTitleScreen(); },
        [&](const shell::TitleBack&) { titleScreen_.back(); },
        [&](const shell::OpenMaker&) {
            if (!makerPuzzleSession_) makerPuzzleSession_.emplace();
            if (!makerSession_) makerSession_ = &*makerPuzzleSession_;
            makerOpen_ = true;
            refreshMakerSavedLevels();
            checkMakerRecovery();
        },
        [&](const shell::CloseMaker&) {
            makerHelpOpen_ = false;
            if (makerOverworldMapOpen_) closeMakerOverworldMap();
            if (makerCameraDialog_) closeMakerCameraDialog();
            if (makerPlaytest_) returnToMaker();
            if (makerDraftDialog_) closeMakerDraftDialog();
            if (makerColorDialog_) closeMakerColorDialog();
            (void)makerView_.closeToolbarOverflow();
            clearMakerBoardCursor();
            blurNativeTextInput();
            makerSession_->endStroke();
            updateMakerAutosave(*makerSession_, true);
            makerSession_->cancelMove();
            makerSession_->clearTileInspection();
            makerPanel_ = MakerPanel::Palette;
            makerOpen_ = false;
            makerViewport_.reset();
            makerPanPosition_.reset();
            makerHover_.reset();
            makerAreaDragAnchor_.reset();
        },
        [&](const shell::StartNewGame&) { startNewGame(); },
        [&](const shell::StartNewGameOnSlot& start) {
            (void)executeNewGameOnSlot(
                start,
                [&](int slot) { return switchSaveSlot(slot); },
                [&] { startNewGame(); });
        },
        [&](const shell::SwitchSlot& switchSlot) {
            (void)switchSaveSlot(switchSlot.slot);
        },
        [&](const shell::DeleteSlot& deleteSlot) {
            deleteSaveSlot(deleteSlot.slot);
        },
        [&](const shell::OpenOptions& open) {
            makerDetachedCamera_.releaseMouse(window_.nativeHandle());
            optionsMenu_.open(open.pauseContext);
        },
        [&](const shell::CloseOptions&) { optionsMenu_.close(); },
        [&](const shell::OptionsBack&) { optionsMenu_.back(); },
        [&](const shell::ApplySettings& apply) {
            applySettingsEffects(
                settingsCoordinator_.applyUserSettings(
                    apply.settings));
        },
        [&](const shell::RequestQuitConfirmation&) {
            makerDetachedCamera_.releaseMouse(window_.nativeHandle());
            if (makerDraftDialog_) makerDraftDialog_->inputAction = {};
            if (makerColorDialog_) {
                makerColorDialog_->tabNavigation = {};
                makerColorDialog_->cancelRequested = false;
                makerColorPicker_.endAdjustment();
            }
            blurNativeTextInput();
            optionsMenu_.requestQuitConfirmation();
        },
        [&](const shell::Quit&) { running_ = false; },
    }, command);
}

bool Application::shellMenuOpen() const
{
    return optionsMenu_.isOpen() || titleScreen_.isOpen() || makerOpen_;
}

void Application::startNewGame()
{
    titleScreen_.close();
    refreshLevelCatalogIfNeeded();
    campaign_.startNewGame(playerProfile_);
    loadCurrentScreen();
}

void Application::checkpointCurrentScreen(bool immediateSave)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (draftWorldLoaded_) {
        persistProfile(immediateSave);
        return;
    }
#endif
    campaign_.writeCheckpoint(playerProfile_, gameplaySession_.snapshot());
    persistProfile(immediateSave);
}

void Application::applySettingsEffects(const SettingsEffects& effects)
{
    if (effects.window) {
        if (effects.window->fullscreen) {
            window_.setFullscreen(true);
        } else if (effects.window->maximized) {
            window_.setWindowedMaximized();
        } else {
            window_.setWindowedSize(
                effects.window->width, effects.window->height);
        }
    }
    if (effects.antiAliasingSamples) {
        renderer_.setAntiAliasingMode(
            antiAliasingModeForSamples(*effects.antiAliasingSamples));
    }
    if (effects.renderScalePercent) {
        renderer_.setRenderScalePercent(*effects.renderScalePercent);
    }
    if (effects.presentation) {
        renderer_.setPresentationPolicy(*effects.presentation);
    }
    if (effects.frameRateLimit) {
        framePacer_.setFrameRateLimit(*effects.frameRateLimit);
    }
    if (effects.audio) {
        audioSystem_->setMasterVolume(effects.audio->masterVolume);
        audioSystem_->setMusicVolume(effects.audio->musicVolume);
        audioSystem_->setSoundVolume(effects.audio->soundVolume);
    }
    if (effects.input) {
        input_.setBindings(*effects.input);
    }
    if (effects.saveProgress) {
        persistProfile(effects.immediatePersistence);
    }
    if (effects.saveSettings) {
        persistSettings(effects.immediatePersistence);
    }
}

void Application::persistProfile(bool immediate)
{
    // Slots with no progress stay off disk entirely (settings persist through
    // their own shared store), preserving the fresh-install empty slate.
    if (!campaign_.gameLoaded() && playerProfile_.progressEmpty()) {
        return;
    }
    saveSlots_.saveProgress(playerProfile_, immediate);
}

InputRouter::RoutingContext Application::inputRoutingContext() const
{
    InputRouter::RoutingContext context {
        .optionsOpen = optionsMenu_.isOpen(),
        .titleOpen = titleScreen_.isOpen(),
        .makerOpen = makerOpen_,
        .makerPlaying = makerPlaytest_.has_value(),
        .lecternOpen = makerPlaytest_
            ? makerPlaytest_->activeBoard().session.readingLectern().has_value()
            : gameplaySession_.readingLectern().has_value() && !shellMenuOpen(),
        .keyboardCaptured = renderer_.wantsKeyboardCapture(),
        .mouseCaptured = renderer_.wantsMouseCapture(),
        .nativeTextEditing = nativeTextFocused(),
        .makerDialogOpen = makerDraftDialog_.has_value() || makerColorDialog_.has_value() ||
            makerHelpOpen_ || makerCameraDialog_.has_value() || makerOverworldMapOpen_ || makerView_.toolbarOverflowOpen(),
        .makerBoardFocused = makerBoardCursor_.has_value(),
    };
#if SOKOBAN_ENABLE_DEBUG_UI
    context.editorEditing = tools_->levelEditor.editingDocument();
    context.tilePlacementReady = context.editorEditing &&
        tools_->levelEditor.tool() == LevelEditor::Tool::Tiles;
    context.decorationPlacementReady =
        context.editorEditing &&
        tools_->levelEditor.placingDecoration();
    context.tileDecorationPlacementReady = context.decorationPlacementReady &&
        tools_->levelEditor.tool() == LevelEditor::Tool::TileDecorations;
    context.editorViewportShortcutsAllowed =
        renderer_.allowsViewportShortcuts();
    context.draftPlaying = tools_->levelEditor.playingDraft();
    context.draftExitConfirmationOpen = tools_->draftExitConfirmationOpen;
#endif
    return context;
}

std::filesystem::path Application::screenPath(
    int levelIndex,
    int screenIndex) const
{
    if (const auto* path = puzzleCatalog_.findScreen({ levelIndex, screenIndex })) {
        return *path;
    }
    throw std::out_of_range("puzzle screen " + std::to_string(levelIndex) + ':' +
        std::to_string(screenIndex) + " is not in the staged catalog");
}

std::filesystem::path Application::overworldPath() const
{
    return assetRoot_ / "levels" / "overworld.scr";
}

std::filesystem::path Application::overworldRoot() const
{
    return assetRoot_ / "levels" / "overworld";
}

const Level& Application::catalogOverworldLevel() const
{
    return overworldMap_ ? overworldMap_->level() : *legacyOverworld_;
}

void Application::refreshLevelCatalogIfNeeded()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    observeEditorLevelPublications();
#endif
    if (levelCatalogStale_) {
        try {
            buildLevelCatalog();
        } catch (const std::exception& error) {
            levelCatalogStale_ = false;
            log::error(log::Category::Gameplay)
                << "Could not refresh staged campaign catalog: " << error.what();
        }
    }
}

void Application::buildLevelCatalog()
{
    PuzzleCatalog puzzleCatalog = loadPuzzleCatalog(assetRoot_ / "levels");
    std::vector<int> screenCounts = puzzleCatalog.screenCounts();
    std::optional<OverworldMap> overworld;
    std::optional<Level> legacyOverworld;
    std::vector<OverworldScreenId> screenIds;
    const std::filesystem::path layoutPath = overworldRoot() / "layout.json";
    if (std::filesystem::is_regular_file(layoutPath)) {
        overworld.emplace(OverworldMap::load(overworldRoot()));
        overworld->validatePuzzleSelectors(
            screenCounts, OverworldValidationMode::Structural);
        screenIds.reserve(overworld->screens().size());
        for (const OverworldScreenRuntime& screen : overworld->screens()) {
            screenIds.push_back(screen.id);
        }
    } else {
        legacyOverworld.emplace(Level::loadFromFile(overworldPath()));
        screenIds.push_back(1);
        for (const auto& selector : legacyOverworld->selectors()) {
            if (selector.target && !puzzleCatalog.findScreen(*selector.target)) {
                throw std::runtime_error("overworld selector " + std::to_string(selector.id) +
                    " targets a puzzle outside the staged catalog");
            }
        }
    }

    campaign_.setLevelScreenCounts(std::move(screenCounts));
    campaign_.setOverworldSelectors(
        (overworld ? overworld->level() : *legacyOverworld).selectors());
    campaign_.setOverworldTopology(overworld ? overworld->fingerprint() : 0,
        std::move(screenIds), overworld ? overworld->startScreen() : 1);
    puzzleCatalog_ = std::move(puzzleCatalog);
    overworldMap_ = std::move(overworld);
    legacyOverworld_ = std::move(legacyOverworld);
    levelCatalogStale_ = false;
}

void Application::restoreProfileLocation()
{
    refreshLevelCatalogIfNeeded();
    const bool restoringOverworld =
        playerProfile_.worldContext == PlayerProfile::WorldContext::Overworld;
    const LevelLocation saved {
        .level = playerProfile_.currentLevel,
        .screen = playerProfile_.currentScreen,
    };
    if (!campaign_.restoreProfileLocation(playerProfile_)) {
        if (restoringOverworld) {
            log::warning(log::Category::Persistence)
                << "Saved overworld checkpoint does not match the current "
                   "topology; returning to the authored start";
        } else {
            log::warning(log::Category::Persistence)
                << "Saved level location " << saved.level << ':' <<
                saved.screen << " does not exist; falling back to the overworld";
        }
    }
}

void Application::serviceUpcomingAssetPrefetch()
{
    if (!upcomingAssetPrefetchPending_ || levelCatalogStale_) {
        return;
    }
    upcomingAssetPrefetchPending_ = false;
    renderer_.cancelQueuedAssetPrefetches();
#if SOKOBAN_ENABLE_DEBUG_UI
    if (tools_->levelEditor.editingDocument() ||
        tools_->levelEditor.playingDraft()) {
        return;
    }
#endif
    std::vector<LevelLocation> targets;
    if (!campaign_.gameLoaded() && !campaign_.inOverworld()) {
        targets.push_back({ campaign_.currentLevel(), campaign_.currentScreen() });
    } else {
        targets = puzzleAssetPrefetchTargets(
            campaign_, playerProfile_, overworldMap_ ? &*overworldMap_ : nullptr);
    }
    RenderAssetRequirements requirements;
    for (LevelLocation target : targets) {
        try {
            requirements.merge(renderAssetRequirementsForLevel(
                Level::loadFromFile(screenPath(target.level, target.screen)),
                assetManifest_,
                target,
                &animationCatalog_));
        } catch (const std::exception& error) {
            log::warning(log::Category::Assets)
                << "asset preload skipped "
                << target.level << ':' << target.screen
                << ": " << error.what();
        }
    }
    renderer_.preloadAssets(requirements);
}

FrameArena& Application::beginRenderFrameArena()
{
    renderFrameArenaIndex_ =
        (renderFrameArenaIndex_ + 1) % renderFrameArenas_.size();
    FrameArena& arena = renderFrameArenas_[renderFrameArenaIndex_];
    arena.reset();
    return arena;
}

#if SOKOBAN_ENABLE_DEBUG_UI
void Application::resumeCampaign()
{
    tools_->splatPainter.close();
    tools_->draftExitConfirmationOpen = false;
    if (draftWorldLoaded_ || levelCatalogStale_) {
        if (!draftWorldLoaded_) {
            checkpointCurrentScreen(false);
        }
        loadCurrentScreen();
    } else {
        tools_->levelEditor.setEditingDocument(false);
        tools_->hoverCell.reset();
        tools_->pickedCell.reset();
        upcomingAssetPrefetchPending_ = true;
    }
    DebugUi::requestGameViewportFocus();
}

void Application::startEditorDraft(Level level, bool fromAuthoredStart)
{
    // F5 can return to the editor while the draft still owns the gameplay
    // session. Save the campaign before the first draft replaces that session.
    if (!draftWorldLoaded_) {
        checkpointCurrentScreen(true);
        draftWorldLoaded_ = true;
    }
    // A puzzle draft keeps the campaign wherever it currently is,
    // so derive its render assets from the edited document rather
    // than from the unrelated campaign location. Composed
    // overworld drafts select maps through their per-screen
    // regions instead.
    const std::optional<LevelLocation> draftLocation =
        tools_->levelEditor.draftOverworldMap()
        ? std::nullopt
        : levelLocationFromScreenPath(
              tools_->levelEditor.loadedDocumentPath());
    // Playing a draft leaves the document view; a half-finished
    // paint session would otherwise keep painting on the level
    // being played.
    tools_->splatPainter.close();
    (void)applyLevel(
        std::move(level), nullptr, draftLocation);
    if (tools_->levelEditor.draftOverworldMap()) {
        gameplaySession_.setActionAdmissionPolicy(
            [this](const GameState& state) {
                const OverworldMap* map =
                    tools_->levelEditor.draftOverworldMap();
                return map &&
                    overworldActionStateAllowed(*map, state);
            });
    } else if (fromAuthoredStart && !tools_->levelEditor.editingOverworld() &&
        developerFilesWritable()) {
        const LevelEditor& editor = tools_->levelEditor;
        const auto& path = editor.loadedDocumentPath();
        std::string name = "the editor draft";
        if (!path.empty()) {
            const auto absolute = std::filesystem::absolute(path).lexically_normal();
            const auto sourceRoot = std::filesystem::absolute(editor.sourceLevelRoot()).lexically_normal();
            const auto relative = absolute.lexically_relative(sourceRoot);
            const auto display = (!relative.empty() && *relative.begin() != ".."
                    ? relative : absolute).generic_u8string();
            name.assign(display.begin(), display.end());
            name += " (draft)";
        }
        solveCapture_ = std::make_unique<solution::SolveToStore>(solution::SolveToStore {
            .name = std::move(name),
            .definition = editor.documentDefinition(),
            .playingDraft = true,
        });
    }
}

void Application::playEditorDraft(std::optional<GridPosition3> heroStart)
{
    if (std::optional<Level> level =
            tools_->levelEditor.beginDraftPlayback(
                &tools_->overworldMapEditor, heroStart)) {
        startEditorDraft(std::move(*level), !heroStart);
    }
}

namespace {

// Copies an edited source file over its staged copy, so a later load (and
// the next launch) sees it without a content build.
void mirrorIntoRuntime(
    const std::filesystem::path& source,
    const std::filesystem::path& runtime)
{
    std::ifstream stream(source, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read " + source.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    std::filesystem::create_directories(runtime.parent_path());
    atomicFile::write(runtime, contents.str());
}

} // namespace

void Application::configureSourceWatcher()
{
    const std::filesystem::path sourceAssets = SOKOBAN_SOURCE_ASSET_DIR;
    SourceWatcher& watcher = tools_->sourceWatcher;
    watcher.watchTree(SOKOBAN_SOURCE_LEVEL_DIR, { ".scr", ".json" }, true);
    std::vector<std::filesystem::path> files {
        sourceAssets / "manifest.json",
        sourceAssets / "animation_catalog.json",
    };
    for (const AssetManifest::Texture& texture : assetManifest_.textures()) {
        files.push_back(sourceAssets / texture.path);
    }
    watcher.setWatchedFiles(files);
    (void)watcher.poll();
    log::info(log::Category::Assets)
        << "Watching " << watcher.watchedCount()
        << " source files for edits made outside the game.";
}

void Application::observeEditorLevelSaves()
{
    const auto revision = tools_->levelEditor.sourceLevelSaveRevision();
    if (developerFilesWritable() && revision != editorSourceLevelSaveRevision_) {
        editorSourceLevelSaveRevision_ = revision;
        tools_->requestSolutionReconcile();
    }
}

void Application::observeEditorLevelPublications()
{
    observeEditorLevelSaves();
    const auto levelRevision = tools_->levelEditor.runtimePublicationRevision();
    const auto overworldRevision = tools_->overworldMapEditor.runtimePublicationRevision();
    if (levelRevision == editorLevelPublicationRevision_ &&
        overworldRevision == editorOverworldPublicationRevision_) {
        return;
    }
    editorLevelPublicationRevision_ = levelRevision;
    editorOverworldPublicationRevision_ = overworldRevision;
    invalidateLevelCatalog();
}

void Application::invalidateLevelCatalog()
{
    // The active world still uses the old topology until a world load.
    levelCatalogStale_ = true;
    screenPreviewActive_ = false;
    screenPreviewLevel_.reset();
    screenPreviewTarget_.reset();
    gameplayGroundGeometryStore_.invalidate();
    previewGroundGeometryStore_.invalidate();
    renderer_.cancelQueuedAssetPrefetches();
}

void Application::serviceSourceWatcher()
{
    // Smoke and evidence runs must see exactly the staged content.
    if (!developerFilesWritable()) {
        return;
    }
    const std::uint64_t now = SDL_GetTicks();
    if (now - tools_->lastSourcePollTicks < 500) {
        return;
    }
    tools_->lastSourcePollTicks = now;
    if (!tools_->sourceWatcherConfigured) {
        tools_->sourceWatcherConfigured = true;
        configureSourceWatcher();
        // Levels may have changed while the game was closed.
        tools_->requestSolutionReconcile();
        return;
    }
    const std::filesystem::path sourceLevels = SOKOBAN_SOURCE_LEVEL_DIR;
    const std::filesystem::path sourceAssets = SOKOBAN_SOURCE_ASSET_DIR;
    std::vector<std::filesystem::path> changedLevels;
    for (const std::filesystem::path& changed : tools_->sourceWatcher.poll()) {
        try {
            const std::filesystem::path relativeToLevels =
                changed.lexically_relative(sourceLevels);
            if (!relativeToLevels.empty() &&
                *relativeToLevels.begin() != "..") {
                if (*relativeToLevels.begin() != "Deleted") {
                    changedLevels.push_back(changed);
                }
                continue;
            }
            if (changed == (sourceAssets / "manifest.json").lexically_normal()) {
                reloadSourceManifest(changed);
                continue;
            }
            if (changed ==
                (sourceAssets / "animation_catalog.json").lexically_normal()) {
                reloadSourceAnimationCatalog(changed);
                continue;
            }
            for (const AssetManifest::Texture& texture :
                 assetManifest_.textures()) {
                if ((sourceAssets / texture.path).lexically_normal() ==
                    changed) {
                    reloadSourceTexture(changed, texture.name, texture.path);
                    break;
                }
            }
        } catch (const std::exception& error) {
            log::error(log::Category::Assets)
                << "Could not reload " << changed.string() << ": "
                << error.what();
        }
    }
    if (!changedLevels.empty()) {
        try {
            reloadSourceLevels(changedLevels);
        } catch (const std::exception& error) {
            log::error(log::Category::Assets)
                << "Could not reload source levels: " << error.what();
        }
    }
}

void Application::reloadSourceLevels(
    const std::vector<std::filesystem::path>& sources)
{
    const std::filesystem::path sourceLevels = SOKOBAN_SOURCE_LEVEL_DIR;
    const std::filesystem::path runtimeLevels = assetRoot_ / "levels";
    const auto result = LevelProjectStore::publishRuntime(sourceLevels, runtimeLevels);
    if (!result.succeeded) {
        throw std::runtime_error(result.message);
    }
    invalidateLevelCatalog();
    // A saved draft may now match a recording waiting in solutions/drafts/.
    tools_->requestSolutionReconcile();

    LevelEditor& editor = tools_->levelEditor;
    const std::filesystem::path openDocument = editor.loadedDocumentPath().empty()
        ? std::filesystem::path {}
        : std::filesystem::absolute(editor.loadedDocumentPath()).lexically_normal();
    const auto changedDocument = std::ranges::find(sources, openDocument);
    if (changedDocument != sources.end()) {
        if (editor.dirty()) {
            log::warning(log::Category::Editor)
                << changedDocument->filename().string()
                << " changed on disk; the editor keeps your unsaved draft.";
        } else if (std::filesystem::is_regular_file(*changedDocument) &&
            editor.reloadFromDisk()) {
            log::info(log::Category::Editor)
                << "Reloaded " << changedDocument->filename().string()
                << " in the editor after it changed on disk.";
        }
    }

    if (!campaign_.gameLoaded() || campaign_.inOverworld() ||
        editor.editingDocument() || editor.playingDraft() || levelTransition_.active()) {
        return;
    }
    const auto currentScreen = screenPath(campaign_.currentLevel(), campaign_.currentScreen());
    const auto currentSource = sourceLevels / currentScreen.lexically_relative(runtimeLevels);
    if (std::ranges::find(sources, currentSource.lexically_normal()) == sources.end()) {
        return;
    }
    campaign_.writeCheckpoint(playerProfile_, gameplaySession_.snapshot());
    loadCurrentScreen();
    log::info(log::Category::Gameplay)
        << "Reloaded gameplay after its source levels changed.";
}

void Application::reloadSourceTexture(
    const std::filesystem::path& source,
    const std::string& textureName,
    const std::string& relativePath)
{
    const std::filesystem::path runtime = assetRoot_ / relativePath;
    mirrorIntoRuntime(source, runtime);
    // Prepared (BC7) copies of the old pixels would otherwise win on the
    // next load.
    invalidateCompressedTextureArtifactsForSource(assetRoot_, relativePath);
    (void)refreshContentPackageIndex(assetRoot_);
    const RenderTexture texture =
        assetManifest_.findTextureIdByName(textureName);
    // A texture that is not resident yet picks the file up when it loads.
    const bool updated = !texture.isNone() &&
        renderer_.updateTexture(texture, loadRgbaImage(runtime));
    log::info(log::Category::Assets)
        << "Reloaded texture " << textureName
        << (updated ? "" : " (loads on first use)");
}

void Application::reloadSourceManifest(const std::filesystem::path& source)
{
    AssetManifest updated = AssetManifest::loadFromFile(source);
    if (assetManifest_ == updated) {
        // An edit that needed a restart may have been reverted.
        if (!tools_->manifestReloadStatus.empty()) {
            tools_->manifestReloadStatus =
                "manifest.json matches the running game again.";
        }
        return;
    }
    if (!assetManifest_.adoptLiveFields(updated)) {
        tools_->manifestReloadStatus =
            "manifest.json changed in a way that needs a restart (a model, "
            "texture, animation, role or tile model). Tile scales and "
            "volumes apply live.";
        log::warning(log::Category::Assets) << tools_->manifestReloadStatus;
        return;
    }
    mirrorIntoRuntime(source, assetRoot_ / "manifest.json");
    (void)refreshContentPackageIndex(assetRoot_);
    presentationSettings_.applyTileScales(assetManifest_);
    gameplayGroundGeometryStore_.invalidate();
    previewGroundGeometryStore_.invalidate();
    audioSystem_->applyManifestVolumes();
    tools_->manifestReloadStatus =
        "Applied manifest.json live (tile scales and volumes).";
    log::info(log::Category::Assets) << tools_->manifestReloadStatus;
}

void Application::reloadSourceAnimationCatalog(
    const std::filesystem::path& source)
{
    AnimationCatalogEditor& editor = tools_->animationCatalogEditor;
    if (editor.dirty()) {
        log::warning(log::Category::Assets)
            << "animation_catalog.json changed on disk; the Animation tab "
               "keeps your unsaved edits.";
        return;
    }
    if (!editor.reload(assetManifest_)) {
        log::warning(log::Category::Assets) << editor.status();
        return;
    }
    mirrorIntoRuntime(source, assetRoot_ / "animation_catalog.json");
    (void)refreshContentPackageIndex(assetRoot_);
    animationCatalog_ = editor.catalog();
    log::info(log::Category::Assets)
        << "Reloaded animation_catalog.json after it changed on disk.";
}

void Application::handleDraftPlaybackShortcuts(const InputRouter::Frame& input)
{
    if (levelTransition_.active()) {
        return;
    }
    if (input.toggleDraftPlaybackPressed &&
        tools_->levelEditor.playingDraft()) {
        tools_->stopDraftPlayback();
        return;
    }
    if (!tools_->levelEditor.editingDocument()) {
        return;
    }
    if (input.toggleDraftPlaybackPressed) {
        playEditorDraft(std::nullopt);
    } else if (input.playDraftFromCursorPressed) {
        // Off the board, the editor reports that the cursor must be on it.
        playEditorDraft(
            tools_->pickedCell.value_or(GridPosition3 { -1, -1, 0 }));
    }
}
#endif

#if SOKOBAN_ENABLE_DEBUG_UI
// Builds the editor-only frame from the open document, pending move,
// neighbouring overworld screens, and palette selector state.
RenderFrameData Application::buildEditorRenderFrame(
    const InputRouter::EditorInput& editorInput, float beltScrollOffset,
    FrameArena& arena)
{
    const auto& editorLevels = tools_->levelEditor.levelBrowserSnapshot();
    const std::optional<LevelEditor::MoveObject>& pendingMove =
        tools_->levelEditor.pendingMove();
    const std::optional<TileType> movedTile =
        pendingMove &&
            pendingMove->kind == LevelEditor::MoveObject::Kind::Tile
        ? std::optional<TileType> { pendingMove->tile }
        : std::nullopt;
    ArenaArray<RenderFrameBuilder::EditorInput::OverworldNeighbor>
        overworldNeighbors(arena, 8);
    const std::optional<OverworldScreenId> editedOverworldScreen =
        tools_->levelEditor.overworldScreenId();
    const OverworldMapEditor& topology = tools_->overworldMapEditor;
    const bool matchingTopologyRoot =
        tools_->levelEditor.showOverworldNeighbors() &&
        editedOverworldScreen && topology.loaded() &&
        tools_->matchingOverworldEditorRoot();
    if (tools_->levelEditor.showOverworldNeighbors() &&
        editedOverworldScreen && topology.loaded() &&
        matchingTopologyRoot) {
        const OverworldScreenSpec* active =
            topology.screen(*editedOverworldScreen);
        if (active) {
            for (const OverworldScreenSpec& candidate :
                 topology.layout().screens) {
                const int deltaX = candidate.slot.x - active->slot.x;
                const int deltaY = candidate.slot.y - active->slot.y;
                if ((deltaX == 0 && deltaY == 0) ||
                    std::abs(deltaX) > 1 || std::abs(deltaY) > 1) {
                    continue;
                }
                if (const Level::Definition* definition =
                        tools_->groundPaintingNeighborDefinition(candidate.id)) {
                    overworldNeighbors.push_back({
                        .screen = candidate.id,
                        .origin = {
                            deltaX * static_cast<int>(
                                topology.layout().screenWidth),
                            deltaY * static_cast<int>(
                                topology.layout().screenHeight),
                        },
                        .width = topology.layout().screenWidth,
                        .height = topology.layout().screenHeight,
                        .definition = definition,
                    });
                }
            }
        }
    }
    return RenderFrameBuilder::buildEditor({
        .manifest = assetManifest_,
        .editor = tools_->levelEditor,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .showDebugView = tools_->levelEditorDebugUi.showDebugView(),
        .hoverCell = tools_->hoverCell,
        .hoverDecoration = tools_->hoverDecoration,
        .deleting =
            (editorInput.deleting &&
                tools_->levelEditor.tool() == LevelEditor::Tool::Tiles) ||
            (editorInput.moving && !pendingMove),
        .selectingMoveSource = editorInput.moving && !pendingMove,
        .editorPreviewTile = editorInput.moving
            ? movedTile
            : std::nullopt,
        .worldAnimationTimeSeconds =
            presentation_.worldAnimationTimeSeconds(),
        .animationTransitionTimeSeconds =
            presentation_.animationTransitionTimeSeconds(),
        .conveyorBeltScrollOffset = beltScrollOffset,
        .levelLocation =
            levelLocationFromScreenPath(tools_->levelEditor.loadedDocumentPath()),
        .overworldScreen = editedOverworldScreen,
        .overworldNeighbors = overworldNeighbors,
        .selectorState = [this, &editorLevels](LevelLocation target) {
            const auto level = std::ranges::find(
                editorLevels,
                target.level,
                &LevelEditor::LevelDirectory::index);
            const bool targetExists = level != editorLevels.end() &&
                std::ranges::find(
                    level->screens,
                    target.screen,
                    &LevelEditor::ScreenFile::index) != level->screens.end();
            return ScreenSelectorViewState {
                .status = targetExists
                    ? playerProfile_.selectorStatus(target)
                    : ScreenSelectorStatus::Unavailable,
                .lastScreenInLevel = targetExists &&
                    level->screens.back().index == target.screen,
            };
        },
        .groundGeometryCache = &editorGroundGeometryCache_,
    }, arena);
}
#endif

// The two evidence point-light fixtures: one animated caster, or the stress
// grid. Only a capture run turns either on, and neither has anything to say
// about an ordinary frame, which is why they are out of buildRenderFrame().
void Application::appendEvidencePointLights(RenderFrameData& frame) const
{
    if (evidencePointLightEnabled_) {
        std::optional<std::size_t> animatedCaster;
        for (std::size_t tileIndex = 0;
             tileIndex < frame.tiles.size();
             ++tileIndex) {
            if (frame.tiles[tileIndex].animationInstanceId != 0) {
                animatedCaster = tileIndex;
                break;
            }
        }
        frame.lighting.pointLightCount = 1;
        frame.lighting.pointLights[0] = {
            .position = {
                static_cast<float>(frame.levelWidth) * 0.42f,
                static_cast<float>(frame.levelHeight) * 0.45f,
                2.25f,
            },
            .color = { 1.0f, 0.72f, 0.48f },
            .intensity = 7.0f,
            .range = 3.5f,
            .castsShadows = true,
            .shadowBias = 0.012f,
            .shadowOpacity = 0.85f,
            .emitterTileIndex = animatedCaster,
        };
    } else if (evidencePointLightStressEnabled_) {
        constexpr std::array<Vec3, RenderFrameData::pointLightCapacity>
            stressColors {
                Vec3 { 1.0f, 0.42f, 0.30f },
                Vec3 { 0.35f, 0.62f, 1.0f },
                Vec3 { 0.45f, 1.0f, 0.52f },
                Vec3 { 1.0f, 0.82f, 0.34f },
                Vec3 { 0.90f, 0.38f, 1.0f },
                Vec3 { 0.30f, 1.0f, 0.92f },
                Vec3 { 1.0f, 0.56f, 0.74f },
                Vec3 { 0.72f, 0.76f, 1.0f },
            };
        constexpr std::array<Vec2, RenderFrameData::pointLightCapacity>
            stressPositions {
                Vec2 { 0.18f, 0.18f }, Vec2 { 0.50f, 0.15f },
                Vec2 { 0.82f, 0.18f }, Vec2 { 0.85f, 0.50f },
                Vec2 { 0.82f, 0.82f }, Vec2 { 0.50f, 0.85f },
                Vec2 { 0.18f, 0.82f }, Vec2 { 0.15f, 0.50f },
            };
        const float width = static_cast<float>(frame.levelWidth);
        const float height = static_cast<float>(frame.levelHeight);
        const float range = std::max(width, height) * 1.35f;
        frame.lighting.pointLightCount =
            RenderFrameData::pointLightCapacity;
        for (std::size_t lightIndex = 0;
             lightIndex < RenderFrameData::pointLightCapacity;
             ++lightIndex) {
            frame.lighting.pointLights[lightIndex] = {
                .position = {
                    width * stressPositions[lightIndex].x,
                    height * stressPositions[lightIndex].y,
                    2.1f + 0.15f * static_cast<float>(lightIndex % 3),
                },
                .color = stressColors[lightIndex],
                .intensity = 3.5f,
                .range = range,
                .castsShadows = true,
                .shadowBias = 0.012f,
                .shadowOpacity = 0.72f,
            };
        }
    }
}

CameraAngles Application::gameplayCameraAngles() const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (tools_->levelEditor.editingDocument()) {
        return tools_->levelEditor.cameraAngles().value_or(CameraAngles {});
    }
    const OverworldMap* draftOverworld = tools_->levelEditor.draftOverworldMap();
    const bool editorDraftPlaying = tools_->levelEditor.playingDraft();
#else
    constexpr const OverworldMap* draftOverworld = nullptr;
    constexpr bool editorDraftPlaying = false;
#endif
    const OverworldMap* overworld = draftOverworld
        ? draftOverworld
        : (!editorDraftPlaying && campaign_.inOverworld() && overworldMap_
              ? &*overworldMap_
              : nullptr);
    if (overworld) {
        const std::optional<OverworldScreenId> activeScreen = draftOverworld
            ? CampaignSession::sharedPlayerScreen(*overworld, gameplaySession_.state())
            : std::optional<OverworldScreenId> { campaign_.activeOverworldScreen() };
        if (activeScreen) {
            if (const OverworldScreenRuntime* screen = overworld->screen(*activeScreen)) {
                return screen->definition.cameraAngles.value_or(CameraAngles {});
            }
        }
    }
    return level_.cameraAngles().value_or(CameraAngles {});
}

RenderFrameData Application::buildGroundRimEvidenceFrame(FrameArena& arena) const
{
    RenderFrameData frame(arena);
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 10;
    frame.levelHeight = 7;
    frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, 0, 10, 7, 1 };
    frame.cameraPitchDegrees = 40.0f;
    frame.cameraYawDegrees = -35.0f;
    frame.lighting = presentationSettings_.renderLighting();
    frame.gridOverlay = presentationSettings_.renderGridOverlay();
    frame.outputTransform = presentationSettings_.renderOutputTransform();
    frame.waterRendering = presentationSettings_.water;
    frame.waterGridBounds = { 0, 0, 10, 7 };
    frame.groundSplat = groundSplatTexturesForScreen(
        [this](std::string_view name) { return assetManifest_.findTextureIdByName(name); },
        std::nullopt);
    const auto append = [&](int x, int y) {
        auto tile = tileVisual(TileType::Ground, { x, y, 0 }, assetManifest_, presentationSettings_);
        constexpr std::array<std::string_view, 10> names {
            "GroundRock01", "GroundRock02", "GroundRock03", "GroundRock04", "GroundRock05",
            "GroundRock06", "GroundRock07", "GroundRock08", "GroundRock09", "GroundRock10",
        };
        if (auto model = assetManifest_.findModelIdByName(names[(x + y * 3) % 10])) {
            tile.model = *model;
            tile.groundRockVariant = static_cast<uint32_t>((x + y * 3) % 10);
        }
        frame.tiles.push_back(tile);
    };
    for (int y = 1; y <= 3; ++y) for (int x = 1; x <= 3; ++x) {
        if (x != 2 || y != 2) append(x, y);
    }
    for (int y = 1; y <= 2; ++y) for (int x = 6; x <= 7; ++x) append(x, y);
    for (int x = 5; x <= 8; ++x) append(x, 5);
    append(1, 5);
    frame.waterSurfaces.push_back({ .cell = { 0, 1, 0 }, .position = { 0, 1 },
        .size = { 1, 1 }, .color = { 0.2f, 0.4f, 0.7f, 1.0f }, .elevation = 0.85f });
    frame.lighting.pointLights[0] = { .position = { 4, 3, 4 },
        .color = { 1.0f, 0.82f, 0.65f }, .intensity = 5.0f, .range = 10.0f };
    frame.lighting.pointLightCount = 1;
    processGroundTileGeometry({ frame.tiles.data(), frame.tiles.size() }, assetManifest_, &arena);
    if (presentationSettings_.geometry.processGroundGeometry) {
        processGroundGeometry({ frame.tiles.data(), frame.tiles.size() }, assetManifest_,
            &arena, &gameplayGroundGeometryCache_);
        if (presentationSettings_.geometry.smoothGroundRim) {
            frame.requestedGroundRimWidth = presentationSettings_.geometry.groundRimWidth;
            frame.requestedGroundRimDepth = presentationSettings_.geometry.groundRimDepth;
            for (auto& tile : frame.tiles) if (tile.groundGeometryEligible) {
                tile.groundRimWidth = frame.requestedGroundRimWidth;
                tile.groundRimDepth = frame.requestedGroundRimDepth;
            }
        }
    }
    return frame;
}

RenderFrameData Application::buildMakerPlaytestFrame(FrameArena& arena)
{
    auto& playtest = *makerPlaytest_;
    auto& board = playtest.activeBoard();
    const auto& level = board.level;
    const auto& session = board.session;
    const auto& presentation = board.presentation;
    const GameState projectedState = session.projectedState();
    const auto* world = !playtest.puzzle && playtest.overworld ? &*playtest.overworld : nullptr;
    std::optional<OverworldView> worldView;
    if (world && !presentation.players().empty()) {
        worldView = calculateOverworldView(*world, makerPlaytestScreen(*world, session.state()),
            session.state(), projectedState, presentation.players().front().motion.renderPosition, 0.0f, &arena);
    }
    std::function<bool(GridPosition3)> visibleCell;
    ArenaArray<RenderFrameBuilder::GameplayInput::GroundSplatRegion> splats(
        arena, worldView ? worldView->visibleScreens.size() : 0);
    if (worldView) {
        visibleCell = [world, &worldView](GridPosition3 cell) {
            const auto owner = world->screenAt(cell);
            return owner && std::ranges::find(worldView->visibleScreens, *owner) != worldView->visibleScreens.end();
        };
        for (const auto id : worldView->visibleScreens) {
            if (const auto* screen = world->screen(id)) {
                splats.push_back({
                    .screenId = id, .origin = screen->origin, .width = world->layout().screenWidth,
                    .height = world->layout().screenHeight, .definition = &screen->definition,
                });
            }
        }
    }
    const auto& activation = session.activationPreview(level);
    const auto& projectedActivation = session.activationPreview(level, true);
    RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = level,
        .state = session.state(),
        .activeHeroController = session.activeHeroController(),
        .moving = session.moving(),
        .projectedState = projectedState,
        .presentation = presentation,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .conveyorBeltScrollOffset = presentation.conveyorBeltScrollOffset(
            session.stepDurationSeconds()),
        .cameraPitchDegrees = presentation.cameraPitchDegrees(),
        .cameraYawDegrees = presentation.cameraYawDegrees(),
        .cameraExtent = worldView ? std::optional { worldView->cameraExtent } : std::nullopt,
        .cameraOffset = worldView ? worldView->cameraOffset : Vec2 {},
        .visibleCell = std::move(visibleCell),
        .groundSplatRegions = splats,
        .selectorInstanceState = [this, world](const Level::ScreenSelector& selector) -> std::optional<ScreenSelectorViewState> {
            if (!world) return std::nullopt;
            const auto* runtime = MakerLevelProgress::findSelector(world->selectors(), selector.id);
            return runtime ? ScreenSelectorViewState { .status = makerPlaytestSelectorStatus(*runtime) }
                           : ScreenSelectorViewState {};
        },
        .cachedActivationPreviews = true,
        .activationPreview = activation ? &*activation : nullptr,
        .projectedActivationPreview = session.moving() && projectedActivation
            ? &*projectedActivation : nullptr,
        .invalidMirrorBeams = session.invalidMirrorBeams(level),
        .projectedInvalidMirrorBeams = session.invalidMirrorBeams(level, true),
        .invalidMirrorEntities = session.invalidMirrorPreview(level).entities,
        .projectedInvalidMirrorEntities = session.invalidMirrorPreview(level, true).entities,
        .groundGeometryCache = &board.groundGeometry,
    }, arena);
    board.particles.appendRenderData(frame);
    board.turretFlames.appendRenderData(frame, turretParticleEffects_);
    return frame;
}

RenderFrameData Application::buildRenderFrame(const InputRouter::Frame& input)
{
    FrameArena& arena = beginRenderFrameArena();
    if (makerPlaytest_) {
        return buildMakerPlaytestFrame(arena);
    }
    if (ui_.drawData().sceneViewport && makerSession_) {
        const auto& maker = *makerSession_;
        const auto& editor = maker.editor();
        const auto& keys = input.makerEditor;
        const auto mode = maker.strokeActive() ? maker.strokeMode() : makerStrokeMode(keys);
        const bool inspecting = makerPanel_ == MakerPanel::Properties;
        const bool moving = !inspecting && keys.moving && !keys.pickModifier;
        const bool selectingMoveSource = moving && !editor.pendingMove();
        const bool erasing = !inspecting && (selectingMoveSource || (!moving && makerStrokeErases(maker, mode)));
        const bool connecting = maker.tool() == MakerSession::Tool::Connect &&
            !moving && mode == MakerSession::StrokeMode::Brush;
        const bool ordering = maker.tool() == MakerSession::Tool::ConnectSelectors &&
            !moving && mode == MakerSession::StrokeMode::Brush;
        auto hoverCell = inspecting || makerView_.tileSearchFocused() || connecting || ordering ||
                (!moving && makerAreaMode(maker.tool()))
            ? std::nullopt : makerHover_;
        if (hoverCell && !erasing && !moving && !maker.placingSelector() &&
            makerScreenContains(editor, *hoverCell) && !editor.tilePlacementError(*hoverCell, editor.selectedTile()).empty()) {
            hoverCell.reset();
        }
        std::vector<RenderFrameBuilder::EditorInput::OverworldNeighbor> neighbors;
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
                if ((x == 0 && y == 0) || std::abs(x) > 1 || std::abs(y) > 1) continue;
                if (const auto* definition = maker.overworldDefinition(screen.id)) {
                    const GridPosition origin {
                        x * static_cast<int>(world.screenWidth), y * static_cast<int>(world.screenHeight)
                    };
                    neighbors.push_back({
                        .screen = screen.id, .origin = origin,
                        .width = world.screenWidth, .height = world.screenHeight, .definition = definition,
                    });
                    left = std::min(left, origin.x);
                    top = std::min(top, origin.y);
                    right = std::max(right, origin.x + static_cast<int>(world.screenWidth));
                    bottom = std::max(bottom, origin.y + static_cast<int>(world.screenHeight));
                    depth = std::max(depth, static_cast<uint32_t>(definition->layers.size()));
                }
            }
        }
        const auto resizePreviewSize = makerDraftDialog_ && showsMakerResizePreview(makerDraftDialog_->mode)
            ? std::optional { makerView_.resizePreviewSize() } : std::nullopt;
        using LinkColorPreview = RenderFrameBuilder::EditorInput::LinkColorPreview;
        using ColorKind = LinkColorPreview::Kind;
        std::optional<LinkColorPreview> linkColorPreview;
        // UI input is processed before this frame is built, so a drag or
        // valid text edit reaches the scene on the same frame as the picker.
        if (const auto& dialog = makerColorDialog_;
            dialog && !LevelEditor::sameLinkColor(dialog->initial, makerColorPicker_.color())) {
            const auto endpoint = dialog->endpoint;
            const auto color = makerColorPicker_.color();
            if (endpoint.kind == LinkKind::Portal && maker.overworld()) {
                const auto location = makerOverworldCellAt(maker, endpoint.cell);
                if (const auto* portal = maker.overworldPortalAt(endpoint.cell); portal && location) {
                    linkColorPreview = LinkColorPreview {
                        .kind = ColorKind::Portal, .original = dialog->initial, .color = color,
                        .portalScope = portal->scope, .overworldPortalPair = portal->overworldPair,
                        .overworldPortalScreen = location->screen,
                    };
                }
            } else if (endpoint.kind == LinkKind::Portal) {
                const auto& portals = editor.portals();
                const auto portal = std::ranges::find(portals, endpoint.cell, &Level::Portal::cell);
                if (portal != portals.end()) {
                    linkColorPreview = LinkColorPreview { ColorKind::Portal, dialog->initial, color, portal->scope };
                }
            } else {
                linkColorPreview = LinkColorPreview {
                    endpoint.kind == LinkKind::Object ? ColorKind::Object : ColorKind::Device,
                    dialog->initial, color,
                };
            }
        }
        RenderFrameData frame = RenderFrameBuilder::buildEditor({
            .manifest = assetManifest_,
            .editor = editor,
            .settings = presentationSettings_,
            .animations = &animationCatalog_,
            .hoverCell = hoverCell,
            .deleting = erasing,
            .selectingMoveSource = selectingMoveSource,
            .allowBoardExpansion = false,
            .worldAnimationTimeSeconds = presentation_.worldAnimationTimeSeconds(),
            .animationTransitionTimeSeconds = presentation_.animationTransitionTimeSeconds(),
            .overworldScreen = maker.overworld() ? std::optional { maker.currentScreenId() } : std::nullopt,
            .overworldNeighbors = neighbors,
            .selectorState = [this](LevelLocation target) { return makerSelectorState(puzzleCatalog_, target); },
            .groundGeometryCache = &editorGroundGeometryCache_,
            .resizePreviewSize = resizePreviewSize,
            .isolateActiveLayer = !inspecting && maker.tool() == MakerSession::Tool::Connect && maker.linkLayerIsolated(),
            .linkColorPreview = linkColorPreview,
            .allowOverworldScreenExpansion = maker.overworld() && maker.showNeighbors(),
            .previewOverworldSelector = !inspecting && maker.placingSelector() && !moving && !erasing,
            .pickOverworldPortals = maker.overworld(),
            .pickOverworldSelectors = maker.overworld(),
        }, arena);
        if (makerCameraDialog_) {
            const CameraAngles preview = makerCameraDialog_->angles.value_or(CameraAngles {});
            frame.cameraPitchDegrees = preview.pitchDegrees;
            frame.cameraYawDegrees = preview.yawDegrees;
        }
        frame.gridOverlay = { { 0.10f, 0.12f, 0.14f, 0.78f }, 2.0f };
        if (maker.overworld() && (!maker.strokeActive() || !makerOverworldCameraExtent_)) {
            // Keep the projection stable under the cursor until the brush is released.
            makerOverworldCameraExtent_ = RenderFrameData::CameraExtent {
                .originX = left - 1,
                .originY = top - 1,
                .originZ = -1,
                .width = static_cast<uint32_t>(right - left + 2),
                .height = static_cast<uint32_t>(bottom - top + 2),
                .depth = depth + 1,
            };
        }
        if (maker.overworld()) frame.cameraExtent = makerOverworldCameraExtent_;
        else if (!resizePreviewSize) frame.cameraExtent = RenderFrameData::CameraExtent {
            .originX = -1, .originY = -1, .originZ = -1,
            .width = frame.levelWidth + 2, .height = frame.levelHeight + 2, .depth = frame.levelDepth + 1,
        };
        if (!makerCameraDialog_ && !resizePreviewSize) makerViewportCamera_.apply(frame);
        makerDetachedCamera_.apply(frame);
        return frame;
    }
    if (evidenceEffects_ == "rock-scratches") {
        if (!evidenceRockScratchFixture_) {
            evidenceRockScratchFixture_ =
                std::make_unique<RockScratchEvidenceFixture>(&animationCatalog_);
        }
        return evidenceRockScratchFixture_->build(assetManifest_, presentationSettings_,
            gameplayGroundGeometryCache_, arena);
    }
    if (evidenceGroundRimFixture_) {
        return buildGroundRimEvidenceFrame(arena);
    }
    const float beltScrollOffset =
        presentation_.conveyorBeltScrollOffset(
            gameplaySession_.stepDurationSeconds());
#if SOKOBAN_ENABLE_DEBUG_UI
    if (std::optional<RenderFrameData> preview =
            tools_->animationPreviewDebugUi.previewFrame(
                assetManifest_, presentationSettings_, &arena)) {
        RenderFrameData frame = std::move(*preview);
        tools_->applyDetachedCamera(frame);
        return frame;
    }
    if (tools_->levelEditor.editingDocument()) {
        RenderFrameData frame =
            buildEditorRenderFrame(input.editor, beltScrollOffset, arena);
        tools_->applyDetachedCamera(frame);
        return frame;
    }
#endif

    if (!campaign_.gameLoaded()) {
        // Title-only: nothing to draw behind the fullscreen menu.
        return RenderFrameData {};
    }

    // Held by reference for the duration of the call, so it has to outlive it.
    const GameState& projectedState = gameplaySession_.projectedStateView();
#if SOKOBAN_ENABLE_DEBUG_UI
    const OverworldMap* draftOverworld =
        tools_->levelEditor.draftOverworldMap();
    const bool editorDraftPlaying = tools_->levelEditor.playingDraft();
#else
    constexpr const OverworldMap* draftOverworld = nullptr;
    constexpr bool editorDraftPlaying = false;
#endif
    // A regular editor draft does not change the campaign location. Do not
    // let an overworld underneath that draft contribute its camera,
    // visibility mask, or splat regions to the draft frame.
    const bool renderCampaignOverworld =
        !editorDraftPlaying &&
        campaign_.inOverworld() && overworldMap_;
    const OverworldMap* renderedOverworld = draftOverworld
        ? draftOverworld
        : (renderCampaignOverworld
              ? &*overworldMap_
              : nullptr);
    std::optional<OverworldView> overworldView;
    std::optional<OverworldScreenId> renderedActiveScreen;
    if (renderedOverworld && !presentation_.players().empty()) {
        renderedActiveScreen = draftOverworld
            ? CampaignSession::sharedPlayerScreen(
                  *renderedOverworld, gameplaySession_.state())
            : std::optional<OverworldScreenId> {
                  campaign_.activeOverworldScreen() };
    }
    if (renderedOverworld && renderedActiveScreen &&
        !presentation_.players().empty()) {
        overworldView = calculateOverworldView(
            *renderedOverworld,
            *renderedActiveScreen,
            gameplaySession_.state(),
            projectedState,
            presentation_.players().front().motion.renderPosition,
            overworldOverviewProgress_, &arena);
    }
    std::function<bool(GridPosition3)> visibleOverworldCell;
    ArenaArray<RenderFrameBuilder::GameplayInput::GroundSplatRegion>
        groundSplatRegions(arena, overworldView ? overworldView->visibleScreens.size() : 0);
    if (overworldView && renderedOverworld) {
        visibleOverworldCell = [
            renderedOverworld,
            &overworldView
        ](GridPosition3 cell) {
            const std::optional<OverworldScreenId> owner =
                renderedOverworld->screenAt(cell);
            const auto& visibleScreens = overworldView->visibleScreens;
            return owner && std::ranges::find(visibleScreens, *owner) !=
                visibleScreens.end();
        };
        for (OverworldScreenId screenId : overworldView->visibleScreens) {
            const OverworldScreenRuntime* screen =
                renderedOverworld->screen(screenId);
            if (screen == nullptr) {
                continue;
            }
            groundSplatRegions.push_back({
                .screenId = screenId,
                .origin = screen->origin,
                .width = renderedOverworld->layout().screenWidth,
                .height = renderedOverworld->layout().screenHeight,
                .definition = &screen->definition,
            });
        }
    }
    const auto& activationPreview = gameplaySession_.activationPreview(level_);
    const auto& projectedActivationPreview = gameplaySession_.activationPreview(level_, true);
    RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = level_,
        .state = gameplaySession_.state(),
        .activeHeroController = gameplaySession_.activeHeroController(),
        .moving = gameplaySession_.moving(),
        .projectedState = projectedState,
        .presentation = presentation_,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .conveyorBeltScrollOffset = beltScrollOffset,
        .cameraPitchDegrees = presentation_.cameraPitchDegrees(),
        .cameraYawDegrees = presentation_.cameraYawDegrees(),
        .cameraExtent = overworldView
            ? std::optional<RenderFrameData::CameraExtent> {
                  overworldView->cameraExtent }
            : std::nullopt,
        .cameraExtentTransitionTarget = overworldView
            ? std::optional<RenderFrameData::CameraExtent> {
                  overworldView->overviewCameraExtent }
            : std::nullopt,
        .cameraExtentTransitionProgress = overworldView
            ? overworldView->overviewProgress
            : 0.0f,
        .cameraOffset = overworldView
            ? overworldView->cameraOffset
            : Vec2 {},
        .visibleCell = std::move(visibleOverworldCell),
        .groundSplatRegions = groundSplatRegions,
        // Draft play does not move the campaign. Select the map belonging to
        // the edited puzzle even when the campaign is in the overworld or on
        // another screen, matching the editor preview above.
#if SOKOBAN_ENABLE_DEBUG_UI
        .levelLocation = editorDraftPlaying
            ? levelLocationFromScreenPath(
                  tools_->levelEditor.loadedDocumentPath())
            : campaign_.inOverworld()
                ? std::nullopt
                : std::optional<LevelLocation> { campaign_.location() },
#else
        .levelLocation = campaign_.inOverworld()
            ? std::nullopt
            : std::optional<LevelLocation> { campaign_.location() },
#endif
        .selectorState = [this](LevelLocation target) {
            return campaign_.selectorViewState(playerProfile_, target);
        },
        .cachedActivationPreviews = true,
        .activationPreview = activationPreview ? &*activationPreview : nullptr,
        .projectedActivationPreview = gameplaySession_.moving() && projectedActivationPreview
            ? &*projectedActivationPreview : nullptr,
        .invalidMirrorBeams = gameplaySession_.invalidMirrorBeams(level_),
        .projectedInvalidMirrorBeams = gameplaySession_.invalidMirrorBeams(level_, true),
        .invalidMirrorEntities = gameplaySession_.invalidMirrorPreview(level_).entities,
        .projectedInvalidMirrorEntities = gameplaySession_.invalidMirrorPreview(level_, true).entities,
        .groundGeometryCache = &gameplayGroundGeometryCache_,
    }, arena);
    frame.groundChunksRequested = !editorDraftPlaying && presentationSettings_.geometry.groundChunks &&
        presentationSettings_.geometry.processGroundGeometry && presentationSettings_.geometry.smoothGroundRim;
    frame.groundChunkMeshoptimizer = presentationSettings_.geometry.groundChunkMeshoptimizer;
    if (!editorDraftPlaying && !gameplayGroundGeometrySource_.empty() &&
        presentationSettings_.geometry.smoothGroundRim &&
        presentationSettings_.geometry.processGroundGeometry) {
        frame.processedGroundArtifact = gameplayGroundGeometryStore_.get(
            gameplayGroundGeometrySource_, level_, assetManifest_,
            gameplayGroundGeometryCache_.rebuildCount(), assetRoot_);
    }
    if (renderCampaignOverworld && overworldView && renderedOverworld) {
        appendOverworldFogVolumes(
            frame.overworldFogVolumes,
            *renderedOverworld,
            overworldView->visibleScreens,
            playerProfile_.overworldDiscovery.screens,
            overworldFogReveal_);
    }
    particleSystem_.appendRenderData(frame);
    turretFlames_.appendRenderData(frame, turretParticleEffects_);
    frame.levelTransitionAmount = levelTransition_.amount();
    if (evidenceWaterEnabled_) {
        appendEvidenceWaterFixture(frame);
    }
    appendEvidencePointLights(frame);
    appendEvidenceEffects(frame);
    if (evidenceWaterDisabled_) {
        frame.waterSurfaces.clear();
    }
    if (evidenceWaterReflectionsDisabled_) {
        frame.waterRendering.reflectionStrength = 0.0f;
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    tools_->applyDetachedCamera(frame);
#endif
    return frame;
}

} // namespace sokoban
