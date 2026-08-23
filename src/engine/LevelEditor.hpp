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
#include <utility>
#include <vector>

namespace sokoban {

class OverworldMapEditor;

// Headless editor state and commands. UI layers should only read this state and
// invoke these operations; no presentation framework is required to use it.
class LevelEditor {
public:
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
        int currentScreen);

    void setPlayingDraft(bool playingDraft);
    [[nodiscard]] bool playingDraft() const;
    void setEditingDocument(bool editingDocument);
    [[nodiscard]] bool editingDocument() const;
    void markDraftSolved();

    void setRequestedSize(int width, int height);
    [[nodiscard]] int requestedWidth() const;
    [[nodiscard]] int requestedHeight() const;
    void setActiveLayer(int layer);
    void setWaterLayer(std::optional<uint32_t> layer);
    void setLayerLocked(bool locked);
    void setShowOverworldNeighbors(bool show);
    void setSelectedTile(TileType tile);
    void setTool(Tool tool);
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
    [[nodiscard]] bool saveDocument(const std::filesystem::path& path);
    [[nodiscard]] Level::Definition documentDefinition() const;
    [[nodiscard]] Level documentToLevel() const;
    [[nodiscard]] std::optional<Level> beginDraftPlayback(
        const OverworldMapEditor* topologyDraft = nullptr);
    [[nodiscard]] const OverworldMap* draftOverworldMap() const
    {
        return draftOverworldMap_ ? &*draftOverworldMap_ : nullptr;
    }

    void paintCell(GridPosition3 position);
    void eraseCell(GridPosition3 position);
    void setCell(GridPosition3 position, TileType tile);
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

    void addLevelAt(int levelIndex);
    void renameLevel(const LevelDirectory& level, std::string name);
    void deleteLevel(const LevelDirectory& level);
    void addScreenAt(const LevelDirectory& level, int screenIndex);
    void renameScreen(
        const LevelDirectory& level,
        int screenIndex,
        std::string name);
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
        std::vector<Level::Decoration> decorations;
        std::vector<Level::ScreenSelector> selectors;
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
        std::vector<Level::Decoration> decorations;
        std::vector<Level::ScreenSelector> selectors;
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
    };

    void recordDocumentChange(const DocumentSnapshot& before);
    void cacheActiveDraft();
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
    [[nodiscard]] bool applyProjectMutation(
        const LevelProjectStore::Mutation& mutation);
    void loadFirstAvailableScreen();
    [[nodiscard]] bool validDecorationTransform(
        const Level::Decoration& decoration) const;
    [[nodiscard]] std::optional<OverworldScreenId> overworldScreenIdForPath(
        const std::filesystem::path& path) const;

    Document document_;
    std::vector<EditActionRecord> editHistory_;
    std::map<std::filesystem::path, DraftState> drafts_;
    std::optional<DocumentSnapshot> decorationTransformBefore_;
    std::optional<MoveObject> pendingMove_;
    std::optional<OverworldMap> draftOverworldMap_;
    bool showOverworldNeighbors_ = false;
};

} // namespace sokoban
