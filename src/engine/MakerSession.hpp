#pragma once

#include "engine/LevelEditor.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class MakerDraftStore;
struct MakerRecoverySnapshot;

// Owns the active Maker draft and its edit history independently of the campaign.
class MakerSession {
public:
    enum class Tool { Paint, Erase, Connect, ConnectSelectors, SelectArea, FloodFill, PasteArea };
    enum class StrokeMode { Brush, Delete, Replace };
    enum class SaveDraftResult { Saved, NameExists, Failed };

    struct SelectorConnection {
        GridPosition3 source;
        GridPosition3 destination;
    };

    struct Region {
        GridPosition3 min;
        GridPosition3 max;

        bool operator==(const Region&) const = default;
    };

    struct LayerLoss {
        std::size_t tiles = 0;
        std::size_t selectors = 0;
        std::size_t decorations = 0;
        std::size_t elevatorStops = 0;
        bool water = false;

        [[nodiscard]] bool any() const { return tiles || selectors || decorations || elevatorStops || water; }
    };

    struct MinecartRouteInfo {
        std::size_t cells = 0;
        std::size_t stops = 0;
        bool loop = false;
    };

    struct TileProperties {
        GridPosition3 cell;
        TileType tile;
        std::optional<bool> gateStartOpen;
        std::optional<bool> lockPlateStartEnabled;
        std::optional<uint8_t> minecartDirection;
        uint8_t minecartDirectionsMask = 0;
        std::optional<MinecartRouteInfo> minecartRoute;
        std::optional<bool> elevatorStopOnLayer;
        std::vector<int> elevatorStops;
    };

    enum class ProblemKind {
        MissingPlayerStart, UnpairedPortal, BrokenLevelSelector, UnwiredDevice, UnusedSignalSource,
    };

    struct Problem {
        ProblemKind kind;
        std::optional<OverworldScreenId> screen;
        // Coordinates are local to the problem's screen.
        std::optional<GridPosition3> cell;
        std::string message;

        bool operator==(const Problem&) const = default;
    };

    static constexpr uint32_t minDraftDimension = 1;
    static constexpr uint32_t maxDraftDimension = 64;
    static constexpr uint32_t defaultDraftWidth = 10;
    static constexpr uint32_t defaultDraftHeight = 8;

    MakerSession();

    [[nodiscard]] const LevelEditor& editor() const { return editor_; }
    [[nodiscard]] Tool tool() const { return tool_; }
    [[nodiscard]] std::string_view status() const { return status_; }
    [[nodiscard]] std::string_view draftName() const { return draftName_; }
    [[nodiscard]] const std::optional<std::string>& draftId() const { return draftId_; }
    [[nodiscard]] bool hasUnsavedChanges() const { return unsavedChanges_ || needsSave_ || strokeChanged_; }
    [[nodiscard]] uint64_t editRevision() const { return editRevision_; }
    [[nodiscard]] bool newDraft(uint32_t width, uint32_t height);
    [[nodiscard]] bool overworld() const { return overworld_.has_value(); }
    [[nodiscard]] bool newOverworldDraft(uint32_t screenWidth, uint32_t screenHeight);
    [[nodiscard]] OverworldScreenId currentScreenId() const { return currentScreen_; }
    [[nodiscard]] OverworldSlot currentScreenSlot() const;
    [[nodiscard]] const OverworldLayout& overworldLayout() const;
    [[nodiscard]] const Level::Definition* overworldDefinition(OverworldScreenId screen) const;
    bool selectOverworldScreen(OverworldScreenId screen);
    bool moveOverworldScreen(OverworldScreenId screen, OverworldSlot destination);
    bool deleteOverworldScreen(OverworldScreenId screen);
    [[nodiscard]] bool showNeighbors() const { return showNeighbors_; }
    void toggleNeighbors();
    [[nodiscard]] bool placingSelector() const { return placingSelector_; }
    void selectLevelSelectorBrush();
    [[nodiscard]] const Level::ScreenSelector* levelSelectorAt(GridPosition3 cell) const;
    bool attachSelectorLevel(GridPosition3 cell, std::optional<std::string> draftName,
        std::optional<std::string> draftId = {});
    [[nodiscard]] std::optional<GridPosition3> selectedLevelSelectorSource() const;
    bool clickLevelSelectorConnection(GridPosition3 cell);
    void clearLevelSelectorSource();
    [[nodiscard]] std::vector<SelectorConnection> levelSelectorConnections() const;
    bool editOverworldLectern(GridPosition3 cell, std::string text);
    [[nodiscard]] std::size_t resizeTileLossCount(uint32_t width, uint32_t height) const;
    [[nodiscard]] bool resizeDraft(uint32_t width, uint32_t height, bool allowCrop = false);
    [[nodiscard]] SaveDraftResult saveDraft(const MakerDraftStore& store,
        std::string_view name, bool overwrite = false);
    [[nodiscard]] bool loadDraft(const MakerDraftStore& store, std::string_view name);
    void updateSavedDraftName(std::string_view id, std::string name);
    void updateSavedOverworldName(std::string_view previousName, std::string name);
    void detachDeletedDraft(std::string_view name, const std::optional<std::string>& id);
    [[nodiscard]] MakerRecoverySnapshot recoverySnapshot() const;
    [[nodiscard]] bool restoreRecovery(MakerRecoverySnapshot snapshot);
    bool setCameraAngles(std::optional<CameraAngles> angles);
    void setTool(Tool tool);
    void selectTile(TileType tile);
    bool selectRecentTile(std::size_t slot);
    bool pickTile(GridPosition3 cell);
    void rotateTile();
    void stepLayer(int delta);
    void toggleLayerLock();
    bool selectLayer(uint32_t layer);
    [[nodiscard]] bool canAddLayer() const;
    bool addLayer(bool above);
    [[nodiscard]] bool canDeleteLayer() const;
    [[nodiscard]] LayerLoss layerLoss() const;
    bool deleteLayer(bool allowLoss = false);
    bool setWaterLayer(bool enabled);
    bool inspectTile(GridPosition3 cell);
    [[nodiscard]] std::optional<TileProperties> tileProperties() const;
    void clearTileInspection();
    bool setInspectedGateStartOpen(bool startOpen);
    bool setInspectedLockPlateStartEnabled(bool startEnabled);
    bool setInspectedMinecartDirection(uint8_t direction);
    bool toggleInspectedElevatorStop();
    [[nodiscard]] std::optional<Region> selection() const;
    bool selectArea(GridPosition3 anchor, GridPosition3 corner);
    void clearSelection();
    bool fillSelection();
    bool clearSelectionTiles();
    bool floodFill(GridPosition3 cell);
    bool copySelection();
    bool cutSelection();
    bool pasteSelection(GridPosition3 origin);
    [[nodiscard]] bool hasClipboard() const { return clipboard_.has_value(); }
    [[nodiscard]] GridPosition clipboardSize() const;
    [[nodiscard]] std::optional<Region> pastePreview(GridPosition3 origin) const;
    [[nodiscard]] std::optional<GridPosition3> moveTarget(GridPosition3 cell) const;
    bool moveCell(GridPosition3 cell);
    void cancelMove();
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] std::optional<LevelEditor::LinkEndpoint> selectedLinkEndpoint() const;
    [[nodiscard]] std::optional<LevelEditor::LinkEndpoint> linkTargetAt(GridPosition3 cell) const;
    // Cells are relative to the active screen, even for a portal on a
    // different screen. The inspector can list peers outside the viewport.
    [[nodiscard]] const Level::Portal* overworldPortalAt(GridPosition3 cell) const;
    [[nodiscard]] std::vector<GridPosition3> overworldPortalPeers(GridPosition3 cell) const;
    [[nodiscard]] bool linkLayerIsolated() const { return linkLayerIsolated_; }
    void toggleLinkLayer();
    bool connectCell(GridPosition3 cell);
    bool selectLinkEndpoint(LevelEditor::LinkEndpoint endpoint);
    void clearLinkSelection();
    bool assignSelectedLinkGroup(Vec3 color);
    bool disconnectSelectedLink();
    bool createSelectedLinkGroup(std::string name);
    bool renameLinkGroup(Level::LinkGroupKind kind, Vec3 color, std::string name);
    bool recolorSelectedLinkGroup(Vec3 color);
    bool toggleSelectedElevatorStop();

    // Finish an open edit stroke and compile a separate runtime level for Test
    // Play. Invalid drafts stay editable and report the loader's explanation.
    [[nodiscard]] std::optional<Level> buildPlaytestLevel();
    [[nodiscard]] std::optional<OverworldMap> buildOverworldPlaytestMap();
    // A supplied lookup can check both presence and playability of saved levels.
    [[nodiscard]] std::vector<Problem> problems(
        const std::function<bool(std::string_view)>& savedDraftExists = {},
        const std::function<bool(std::string_view)>& savedDraftIdExists = {}) const;

    [[nodiscard]] std::optional<GridPosition3> previewCell(GridPosition cell) const;
    [[nodiscard]] std::optional<GridPosition3> previewCell(GridPosition3 cell, StrokeMode mode) const;
    [[nodiscard]] bool strokeActive() const
    {
        return overworld_ ? overworldBeforeStroke_.has_value() : editor_.strokeActive();
    }
    [[nodiscard]] StrokeMode strokeMode() const { return strokeMode_; }
    // An in-bounds press owns the stroke even when its first cell is unchanged.
    bool beginStroke(GridPosition cell);
    bool beginStroke(GridPosition3 cell, StrokeMode mode);
    // A missing cell breaks interpolation while the pointer is off the board.
    bool continueStroke(std::optional<GridPosition> cell);
    bool continueStroke(std::optional<GridPosition3> cell, bool straight);
    bool endStroke();

private:
    enum class LayerEdit { Insert, Erase, Water };
    struct OverworldComponent {
        OverworldScreenId screen = 0;
        // A world history entry shares every component it didn't modify.
        std::shared_ptr<const Level::Definition> definition;
    };
    struct OverworldState {
        OverworldLayout layout;
        std::vector<OverworldComponent> components;
        uint64_t revision = 0;
    };
    struct OverworldCell {
        OverworldSlot slot;
        GridPosition3 local;
        OverworldScreenId screen = 0;
    };
    // An editor loaded with a neighbouring screen, kept between hover
    // previews. It is valid for as long as that screen still has the same
    // definition object; every edit installs a new one.
    struct NeighborPreview {
        // Written out rather than implicit, for Clang: a nested class with
        // member initializers has no usable implicit default constructor
        // until the enclosing class is complete, std::optional asks before
        // then, and the answer sticks - emplace() would not compile.
        NeighborPreview() { }

        OverworldScreenId screen = 0;
        std::shared_ptr<const Level::Definition> definition;
        LevelEditor editor;
    };

    [[nodiscard]] OverworldDraftOverride overworldDraft() const;
    [[nodiscard]] bool validateOverworldEdit(const OverworldDraftOverride& draft);
    void replaceOverworld(OverworldDraftOverride draft, std::string name);
    [[nodiscard]] std::optional<OverworldCell> overworldCell(
        GridPosition3 cell, bool visibleOnly = true) const;
    [[nodiscard]] const Level::ScreenSelector* levelSelector(Level::SelectorRef reference) const;
    [[nodiscard]] std::optional<GridPosition3> levelSelectorCell(
        Level::SelectorRef reference, bool visibleOnly) const;
    [[nodiscard]] bool dependsOnSelector(Level::SelectorRef selector, Level::SelectorRef prerequisite) const;
    void pruneSelectorConnections(const std::set<std::pair<OverworldScreenId, uint32_t>>& deleted);
    void removeOverworldPlayerFromOtherScreens(OverworldScreenId screen);
    [[nodiscard]] bool regionContains(GridPosition3 cell) const;
    void commitRegionEdit();
    [[nodiscard]] bool editOverworldLayers(LayerEdit edit, uint32_t layer, std::optional<uint32_t> water);
    [[nodiscard]] std::optional<TileProperties> tilePropertiesAt(GridPosition3 cell) const;
    bool toggleElevatorStopAt(GridPosition3 cell);
    [[nodiscard]] bool hasOverworldPortal(GridPosition3 cell) const;
    [[nodiscard]] Vec3 unusedOverworldPortalColor(std::optional<Vec3> reserved = std::nullopt) const;
    bool connectOverworldPortals(GridPosition3 first, GridPosition3 second);
    bool disconnectOverworldPortal(GridPosition3 cell);
    bool recolorOverworldPortals(GridPosition3 cell, Vec3 color);
    bool updateOverworldPortals(std::span<const Level::Portal> updates, std::string status);
    [[nodiscard]] bool visitOverworldCell(GridPosition3 cell);
    [[nodiscard]] bool updateOverworldDefinition(OverworldScreenId screen, Level::Definition definition);
    void recordOverworldChange(OverworldState before);
    void restoreCurrentOverworldScreen();
    void replaceDraft(Level::Definition definition, std::string name);
    bool visitCell(GridPosition3 cell);
    void updateUnsavedChanges();

    LevelEditor editor_;
    Tool tool_ = Tool::Paint;
    std::string status_;
    std::string draftName_;
    std::optional<std::string> draftId_;
    uint64_t editRevision_ = 0;
    bool unsavedChanges_ = false;
    bool needsSave_ = false;
    bool strokeChanged_ = false;
    std::optional<LevelEditor::LinkEndpoint> selectedLinkEndpoint_;
    std::optional<Level::SelectorRef> selectedLevelSelectorSource_;
    std::optional<GridPosition3> inspectedCell_;
    std::optional<Region> selection_;
    std::optional<LevelEditor::RegionClipboard> clipboard_;
    bool linkLayerIsolated_ = false;
    StrokeMode strokeMode_ = StrokeMode::Brush;
    GridPosition strokeAnchor_;
    std::optional<GridPosition> lastCell_;
    std::vector<bool> visited_;
    std::optional<OverworldState> overworld_;
    std::optional<OverworldState> overworldBeforeStroke_;
    std::vector<OverworldState> overworldUndo_;
    std::vector<OverworldState> overworldRedo_;
    std::set<std::pair<int, int>> overworldVisited_;
    mutable std::optional<NeighborPreview> neighborPreview_;
    OverworldScreenId currentScreen_ = 0;
    uint64_t nextOverworldRevision_ = 0;
    uint64_t savedOverworldRevision_ = 0;
    bool showNeighbors_ = true;
    bool placingSelector_ = false;
};

} // namespace sokoban
