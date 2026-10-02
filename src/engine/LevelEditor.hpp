#pragma once

#include "engine/Level.hpp"
#include "engine/LevelProjectStore.hpp"
#include "engine/OverworldMap.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sokoban {

class OverworldMapEditor;

// Headless editor state and commands. UI layers should only read this state and
// invoke these operations; no presentation framework is required to use it.
class LevelEditor {
public:
    struct SaveResult {
        enum class Outcome {
            Failed,
            Saved,
            SourceSavedMirrorStale,
            SourceAndMirrorSavedIndexStale,
        };

        Outcome outcome = Outcome::Failed;

        [[nodiscard]] bool succeeded() const noexcept
        {
            return outcome == Outcome::Saved;
        }

        [[nodiscard]] bool sourceSaved() const noexcept
        {
            return outcome != Outcome::Failed;
        }

        [[nodiscard]] bool mirrorStale() const noexcept
        {
            return outcome == Outcome::SourceSavedMirrorStale;
        }

        [[nodiscard]] bool packageIndexStale() const noexcept
        {
            return outcome == Outcome::SourceAndMirrorSavedIndexStale;
        }

        // Preserve the existing if/CHECK calling convention while allowing
        // callers that handle partial saves to inspect the exact outcome.
        operator bool() const noexcept { return succeeded(); }
    };

    enum class Tool {
        Tiles,
        Decorations,
        Selectors,
    };

    // Movement is tool-independent: selectors carry extra assignment data,
    // but participate in picking and placement like other authored objects.
    struct MoveObject {
        enum class Kind {
            Tile,
            ScreenSelector,
        };

        Kind kind = Kind::Tile;
        GridPosition3 source;
        TileType tile = TileType::Air;
        uint32_t selectorId = 0;
    };

    struct ScreenFile {
        int index = 0;
        std::filesystem::path path;
        std::string name;
    };

    struct LevelDirectory {
        int index = 0;
        std::filesystem::path path;
        std::string name;
        std::vector<ScreenFile> screens;
    };

    void initialize(
        const std::filesystem::path& sourceLevelRoot,
        const std::filesystem::path& runtimeLevelRoot,
        int currentLevel,
        int currentScreen,
        const std::filesystem::path& sourceManifestPath = {},
        const std::filesystem::path& runtimeManifestPath = {});

    void setPlayingDraft(bool playingDraft);
    [[nodiscard]] bool playingDraft() const;
    void setEditingDocument(bool editingDocument);
    [[nodiscard]] bool editingDocument() const;
    void markDraftSolved();

    void setRequestedSize(int width, int height);
    [[nodiscard]] int requestedWidth() const;
    [[nodiscard]] int requestedHeight() const;
    void setActiveLayer(int layer);
    // PageUp/PageDown: moves the active layer and reports it in status().
    void stepActiveLayer(int delta);
    void setWaterLayer(std::optional<uint32_t> layer);
    void setCharacter(CharacterType character);
    void setLayerLocked(bool locked);
    // The L shortcut; reports the new state in status().
    void toggleLayerLock();
    void setShowOverworldNeighbors(bool show);
    void setSelectedTile(TileType tile);
    void setTool(Tool tool);
    // Tiles -> Decorations -> Selectors (overworld screens only) -> Tiles.
    void cycleTool();
    // Recently chosen tiles, newest first. A tile keeps its slot until newer
    // choices push it out, so number-key shortcuts stay stable while you
    // alternate between tiles already in the list.
    static constexpr std::size_t recentTileCapacity = 9;
    [[nodiscard]] const std::vector<TileType>& recentTiles() const;
    [[nodiscard]] bool selectRecentTile(std::size_t slot);
    // Eyedropper: selects the tile shown in the picked column (or on the
    // active layer when the layer is locked). Returns the picked tile.
    std::optional<TileType> pickTile(GridPosition3 pickedCell);
    void setSelectedDecorationModel(std::string modelName);
    void selectDocument(const std::filesystem::path& path);
    [[nodiscard]] bool setBrowserRoot(const std::filesystem::path& path);

    void newDocument(int width, int height, bool recordHistory = true);
    void resizeDocument(int width, int height, bool recordHistory = true);
    void addLayerAbove();
    void addLayerBelow();
    void deleteActiveLayer();
    // Opens a path for editing without discarding unsaved work in the current
    // path. Returning to a previously opened dirty path restores its draft
    // and its document-local undo history.
    [[nodiscard]] bool openDocument(const std::filesystem::path& path);
    [[nodiscard]] bool loadDocument(const std::filesystem::path& path, bool recordHistory = true);
    [[nodiscard]] SaveResult saveDocument(const std::filesystem::path& path);
    // The loaded file changed outside the editor (a text editor, a merge).
    // Reloads it when there are no unsaved edits, and clears the history,
    // which described the old contents. Returns whether it reloaded; the
    // editor's own saves never trigger a reload.
    [[nodiscard]] bool reloadFromDisk();
    // Ctrl+S: saves back to the file the document was loaded from. A new,
    // never-saved document has no such file and needs an explicit path.
    [[nodiscard]] SaveResult saveLoadedDocument();
    [[nodiscard]] Level::Definition documentDefinition() const;
    [[nodiscard]] Level documentToLevel() const;
    // `heroStart`, when given, is a picked board cell: playback moves the
    // draft's first hero start to the placement cell above it (puzzle
    // screens only). The document itself is not changed.
    [[nodiscard]] std::optional<Level> beginDraftPlayback(
        const OverworldMapEditor* topologyDraft = nullptr,
        std::optional<GridPosition3> heroStart = std::nullopt);
    [[nodiscard]] const OverworldMap* draftOverworldMap() const
    {
        return draftOverworldMap_ ? &*draftOverworldMap_ : nullptr;
    }

    // Each returns whether the document changed.
    bool paintCell(GridPosition3 position);
    bool eraseCell(GridPosition3 position);
    bool setCell(GridPosition3 position, TileType tile);
    // A stroke groups every change made until endStroke() into one undo
    // record, so a drag that paints forty cells undoes in one step. Undo,
    // redo and document switches end an open stroke first.
    [[nodiscard]] bool beginStroke();
    // Returns whether the stroke changed the document.
    bool endStroke();
    [[nodiscard]] bool strokeActive() const;
    [[nodiscard]] bool beginMove(GridPosition3 source);
    void cancelMove();
    [[nodiscard]] bool moveObject(GridPosition3 destination);
    [[nodiscard]] const std::optional<MoveObject>& pendingMove() const;
    [[nodiscard]] GridPosition3 resolveMoveTarget(
        GridPosition3 pickedCell) const;
    [[nodiscard]] bool placeDecoration(GridPosition3 surfaceCell);
    void cancelDecorationPlacement();
    [[nodiscard]] bool selectDecoration(std::size_t index);
    void clearDecorationSelection();
    [[nodiscard]] bool updateSelectedDecoration(
        const Level::Decoration& decoration);
    // Gizmo drags preview many transforms but produce one undo record. The
    // session begins from a full document snapshot so cancellation is exact.
    [[nodiscard]] bool beginSelectedDecorationTransform();
    [[nodiscard]] bool previewSelectedDecorationTransform(
        const Level::Decoration& decoration);
    [[nodiscard]] bool endSelectedDecorationTransform(bool commit = true);
    [[nodiscard]] bool transformingSelectedDecoration() const;
    [[nodiscard]] bool duplicateSelectedDecoration();
    [[nodiscard]] bool deleteSelectedDecoration();
    [[nodiscard]] bool placeSelector(GridPosition3 cell);
    [[nodiscard]] bool selectSelector(std::size_t index);
    void clearSelectorSelection();
    [[nodiscard]] bool updateSelectedSelectorTarget(
        std::optional<LevelLocation> target);
    [[nodiscard]] bool deleteSelectedSelector();
    [[nodiscard]] GridPosition3 resolveEditTarget(
        GridPosition3 pickedCell,
        bool deleting,
        bool replaceLayer) const;
    // Unlocked selector clicks prefer a flag already in the picked column;
    // locked editing treats selectors like every other object on that layer.
    // Empty unlocked columns resolve to the surface placement cell.
    [[nodiscard]] GridPosition3 resolveSelectorTarget(
        GridPosition3 pickedCell) const;
    [[nodiscard]] bool tryUndoEdit();
    // Re-applies the most recently undone change. Any new edit clears the
    // redo history.
    [[nodiscard]] bool tryRedoEdit();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    void addLevelAt(int levelIndex);
    void renameLevel(const LevelDirectory& level, const std::string& name);
    void deleteLevel(const LevelDirectory& level);
    void addScreenAt(const LevelDirectory& level, int screenIndex);
    void renameScreen(
        const LevelDirectory& level,
        int screenIndex,
        const std::string& name);
    void deleteScreen(const LevelDirectory& level, int screenIndex);
    void restoreDeletedLevel(const std::filesystem::path& deletedLevelPath);
    [[nodiscard]] bool canPermanentlyDelete(const std::filesystem::path& path) const;
    [[nodiscard]] bool permanentlyDelete(const std::filesystem::path& path);
    [[nodiscard]] std::vector<LevelDirectory> collectLevelDirectories() const;
    [[nodiscard]] std::vector<LevelDirectory> collectDeletedLevels() const;
    [[nodiscard]] static std::string selectorTargetLabel(
        const Level::ScreenSelector& selector,
        const std::vector<LevelDirectory>& levels);

    [[nodiscard]] uint32_t documentWidth() const;
    [[nodiscard]] uint32_t documentHeight() const;
    [[nodiscard]] uint32_t documentDepth() const;
    [[nodiscard]] uint32_t activeLayer() const;
    [[nodiscard]] std::optional<uint32_t> waterLayer() const;
    [[nodiscard]] CharacterType character() const;
    [[nodiscard]] bool layerLocked() const;
    [[nodiscard]] bool showOverworldNeighbors() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool hasInProgressDraft(
        const std::filesystem::path& path) const;
    [[nodiscard]] const std::vector<std::string>& documentRows() const;
    [[nodiscard]] const Level::LayerRows& documentLayers() const;
    [[nodiscard]] TileType selectedTile() const;
    [[nodiscard]] Tool tool() const;
    [[nodiscard]] const std::string& selectedDecorationModel() const;
    [[nodiscard]] const std::vector<Level::Decoration>& decorations() const;
    [[nodiscard]] std::optional<std::size_t> selectedDecorationIndex() const;
    [[nodiscard]] const Level::Decoration* selectedDecoration() const;
    [[nodiscard]] const std::vector<Level::ScreenSelector>& selectors() const;
    [[nodiscard]] std::optional<std::size_t> selectedSelectorIndex() const;
    [[nodiscard]] const Level::ScreenSelector* selectedSelector() const;
    // Gates, rotators, elevators and minecarts of the document. In the editor a device
    // is linked to every pressure plate of its color (see linkGroups), so
    // these records carry their color but no explicit `pressurePlates`; the
    // lists are filled in from the color groups when the document becomes a
    // Level or is saved.
    [[nodiscard]] const std::vector<Level::Gate>& gates() const;
    [[nodiscard]] const std::vector<Level::Rotator>& rotators() const;
    [[nodiscard]] const std::vector<Level::Elevator>& elevators() const;
    [[nodiscard]] const std::vector<Level::Minecart>& minecarts() const;
    // Plates authored beneath a unit or mirror (see Level::Plate).
    [[nodiscard]] const std::vector<Level::Plate>& coveredPlates() const;
    // The plate at `cell` in the document, uncovered or beneath something.
    [[nodiscard]] std::optional<TileType> documentPlateAt(GridPosition3 cell) const;

    // Linking by color. Every pressure plate, gate, rotator and elevator has
    // a link color; a device is driven by exactly the pressure plates that
    // share its color, so giving several plates and devices one color links
    // them all. Colors match as the 8-bit RGB the picker shows. This is an
    // authoring idea only: the saved screen and the Level carry the explicit
    // plate lists it produces.
    struct LinkGroup {
        Vec3 color {};
        std::vector<GridPosition3> pressurePlates;
        std::vector<GridPosition3> gates;
        std::vector<GridPosition3> rotators;
        std::vector<GridPosition3> elevators;
        std::vector<GridPosition3> minecarts;

        [[nodiscard]] bool hasDevice() const
        {
            return !gates.empty() || !rotators.empty() || !elevators.empty() ||
                !minecarts.empty();
        }
    };
    [[nodiscard]] static bool sameLinkColor(Vec3 left, Vec3 right);
    // The color newly painted pressure plates and devices take. Painting a
    // plate or device over the same tile recolors it to this color.
    [[nodiscard]] Vec3 activeLinkColor() const;
    void setActiveLinkColor(Vec3 color);
    // Every pressure plate's color, in z/y/x order.
    [[nodiscard]] const std::vector<Level::LinkColor>& pressurePlateColors() const;
    // The link color of the pressure plate, gate, rotator or elevator at
    // `cell`; empty when nothing linkable is there.
    [[nodiscard]] std::optional<Vec3> linkColorAt(GridPosition3 cell) const;
    // The pressure plates a device of `color` is driven by.
    [[nodiscard]] std::vector<GridPosition3> linkedPressurePlates(Vec3 color) const;
    // Every color in use, with its members, ordered by first appearance.
    [[nodiscard]] std::vector<LinkGroup> linkGroups() const;
    // Recolors the linkable thing at `cell`, one undoable command.
    [[nodiscard]] bool setLinkColor(GridPosition3 cell, Vec3 color);
    // The link-color brush: gives the topmost tile in the picked column
    // (on the active layer while it is locked) the active link color, if it
    // is a pressure plate, gate, rotator or elevator, or a unit standing on
    // a pressure plate. Picks the same tile as pickTile.
    [[nodiscard]] bool paintLinkColorAt(GridPosition3 pickedCell);
    // Recolors every member of the group colored `from`, one undoable
    // command. Choosing another group's color merges the two.
    [[nodiscard]] bool recolorLinkGroup(Vec3 from, Vec3 to);
    // An elevator's stops: the layers its platform travels between, in
    // order. They must be distinct existing layers and include the layer the
    // Elevator tile is on, which is where the platform starts. One undoable
    // command.
    [[nodiscard]] bool setElevatorLevels(std::size_t index, std::vector<int> levels);
    // Chooses the first rail connector the cart follows from its starting
    // stop: 0/1/2/3 are north/east/south/west.
    [[nodiscard]] bool setMinecartInitialDirection(
        std::size_t index, uint8_t direction);
    [[nodiscard]] bool editingOverworld() const;
    // The path shown in the UI, which the file browser changes on a single
    // click. It is a *selection*: the document in memory is unchanged until
    // the selection is actually loaded.
    [[nodiscard]] const std::filesystem::path& documentPath() const;
    // The path the in-memory document actually came from, empty for a new
    // document that has never been saved. Anything deriving from the document
    // itself - such as which screen's ground splat map belongs to it - must
    // use this, not documentPath(), or merely browsing the file list changes
    // what is rendered.
    [[nodiscard]] const std::filesystem::path& loadedDocumentPath() const;
    [[nodiscard]] const std::filesystem::path& browserRoot() const;
    [[nodiscard]] const std::filesystem::path& sourceLevelRoot() const;
    [[nodiscard]] const std::filesystem::path& runtimeLevelRoot() const;
    [[nodiscard]] std::optional<OverworldScreenId> overworldScreenId() const;
    [[nodiscard]] std::optional<OverworldScreenId> selectorLevelOwner(
        int puzzleLevel) const;
    [[nodiscard]] const std::string& status() const;

private:
    struct Document {
        Level::LayerRows layers;
        std::optional<uint32_t> waterLayer;
        std::optional<CharacterType> character;
        std::vector<Level::Decoration> decorations;
        std::vector<Level::ScreenSelector> selectors;
        std::vector<Level::Gate> gates;
        std::vector<Level::Rotator> rotators;
        std::vector<Level::Plate> plates;
        std::vector<Level::Elevator> elevators;
        std::vector<Level::Minecart> minecarts;
        // One per pressure plate, sorted by cell (see linkGroups).
        std::vector<Level::LinkColor> plateColors;
        // Selected path (browser clicks move this).
        std::filesystem::path filePath;
        // Where `layers` was actually read from or written to. Empty for an
        // unsaved new document.
        std::filesystem::path loadedPath;
        std::filesystem::path browserRoot;
        std::filesystem::path sourceLevelRoot;
        std::filesystem::path runtimeLevelRoot;
        std::string status;
        int requestedWidth = 12;
        int requestedHeight = 8;
        int activeLayer = 0;
        TileType selectedTile = TileType::Wall;
        Tool tool = Tool::Tiles;
        std::string selectedDecorationModel;
        std::optional<std::size_t> selectedDecoration;
        std::optional<std::size_t> selectedSelector;
        bool layerLocked = false;
        bool dirty = false;
        bool playingDraft = false;
        bool editingDocument = false;
    };

    struct DocumentSnapshot {
        Level::LayerRows layers;
        std::optional<uint32_t> waterLayer;
        std::optional<CharacterType> character;
        std::vector<Level::Decoration> decorations;
        std::vector<Level::ScreenSelector> selectors;
        std::vector<Level::Gate> gates;
        std::vector<Level::Rotator> rotators;
        std::vector<Level::Plate> plates;
        std::vector<Level::Elevator> elevators;
        std::vector<Level::Minecart> minecarts;
        // One per pressure plate, sorted by cell (see linkGroups).
        std::vector<Level::LinkColor> plateColors;
        std::filesystem::path filePath;
        // Undoing a load has to restore where the document came from too, or
        // the restored contents would be attributed to the wrong screen.
        std::filesystem::path loadedPath;
        int requestedWidth = 12;
        int requestedHeight = 8;
        int activeLayer = 0;
        std::optional<std::size_t> selectedDecoration;
        std::optional<std::size_t> selectedSelector;
        bool dirty = false;
    };

    struct EditActionRecord {
        DocumentSnapshot before;
        DocumentSnapshot after;
    };

    struct DraftState {
        Document document;
        std::vector<EditActionRecord> editHistory;
        std::vector<EditActionRecord> redoHistory;
    };

    struct ScreenIdentityRemap {
        std::filesystem::path sourcePath;
        std::optional<std::filesystem::path> destinationPath;
        std::optional<LevelLocation> sourceLocation;
        std::optional<LevelLocation> destinationLocation;
    };
    using ScreenIdentityRemaps = std::vector<ScreenIdentityRemap>;

    void recordDocumentChange(const DocumentSnapshot& before);
    // The document as a level definition, with each device's plate list
    // filled in from its color group and the colors of plates that drive
    // nothing kept as editor-only link colors.
    [[nodiscard]] Level::Definition linkedDefinition(
        std::optional<CharacterType> character) const;
    void cacheActiveDraft();
    void noteRecentTile(TileType tile);
    [[nodiscard]] static std::filesystem::path draftKey(
        const std::filesystem::path& path);
    void insertLayerAt(int insertionIndex, const char* status);
    void applyDocumentSnapshot(const DocumentSnapshot& snapshot);
    [[nodiscard]] DocumentSnapshot captureDocumentSnapshot() const;
    [[nodiscard]] EditActionRecord invertEditActionRecord(const EditActionRecord& record) const;
    [[nodiscard]] std::filesystem::path runtimeMirrorPath(const std::filesystem::path& sourcePath) const;
    [[nodiscard]] std::filesystem::path deletedLevelRoot() const;
    [[nodiscard]] bool isActiveLevelDirectory(const LevelDirectory& level) const;
    [[nodiscard]] std::vector<std::string> defaultScreenRows() const;
    [[nodiscard]] std::filesystem::path uniqueDeletedLevelPath(const std::filesystem::path& levelPath) const;
    void applyScreenIdentityRemaps(const ScreenIdentityRemaps& remaps);
    [[nodiscard]] bool applyProjectMutation(
        const LevelProjectStore::Mutation& mutation,
        const ScreenIdentityRemaps& screenIdentityRemaps = {},
        const LevelProjectStore::Mutation& manifestMutation = {});
    void loadFirstAvailableScreen();
    [[nodiscard]] bool validDecorationTransform(
        const Level::Decoration& decoration) const;
    [[nodiscard]] std::optional<OverworldScreenId> overworldScreenIdForPath(
        const std::filesystem::path& path) const;

    Document document_;
    std::vector<EditActionRecord> editHistory_;
    std::vector<EditActionRecord> redoHistory_;
    // Open stroke: the snapshot from before its first change, plus how many
    // cell edits it has absorbed.
    std::optional<DocumentSnapshot> strokeBefore_;
    std::size_t strokeChanges_ = 0;
    // Set while a compound command (moveObject) makes intermediate changes
    // that it records itself.
    bool historySuppressed_ = false;
    Vec3 activeLinkColor_ { 1.0f, 0.72f, 0.12f };
    std::vector<TileType> recentTiles_;
    std::map<std::filesystem::path, DraftState> drafts_;
    std::optional<DocumentSnapshot> decorationTransformBefore_;
    std::optional<MoveObject> pendingMove_;
    std::optional<OverworldMap> draftOverworldMap_;
    bool showOverworldNeighbors_ = false;
    std::filesystem::path sourceManifestPath_;
    std::filesystem::path runtimeManifestPath_;
};

} // namespace sokoban
