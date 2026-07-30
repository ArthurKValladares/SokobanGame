#pragma once

#include "engine/Level.hpp"
#include "engine/LevelProjectStore.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sokoban {

// Headless editor state and commands. UI layers should only read this state and
// invoke these operations; no presentation framework is required to use it.
class LevelEditor {
public:
    enum class Tool {
        Tiles,
        Decorations,
    };

    struct ScreenFile {
        int index = 0;
        std::filesystem::path path;
    };

    struct LevelDirectory {
        int index = 0;
        std::filesystem::path path;
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
    [[nodiscard]] bool loadDocument(const std::filesystem::path& path, bool recordHistory = true);
    [[nodiscard]] bool saveDocument(const std::filesystem::path& path);
    [[nodiscard]] Level documentToLevel() const;
    [[nodiscard]] std::optional<Level> beginDraftPlayback();

    void paintCell(GridPosition3 position);
    void eraseCell(GridPosition3 position);
    void setCell(GridPosition3 position, TileType tile);
    [[nodiscard]] bool placeDecoration(GridPosition3 surfaceCell);
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
    [[nodiscard]] GridPosition3 resolveEditTarget(
        GridPosition3 pickedCell,
        bool deleting,
        bool replaceLayer) const;
    [[nodiscard]] bool tryUndoEdit();

    void addLevelAt(int levelIndex);
    void deleteLevel(const LevelDirectory& level);
    void addScreenAt(const LevelDirectory& level, int screenIndex);
    void deleteScreen(const LevelDirectory& level, int screenIndex);
    void restoreDeletedLevel(const std::filesystem::path& deletedLevelPath);
    [[nodiscard]] bool canPermanentlyDelete(const std::filesystem::path& path) const;
    [[nodiscard]] bool permanentlyDelete(const std::filesystem::path& path);
    [[nodiscard]] std::vector<LevelDirectory> collectLevelDirectories() const;
    [[nodiscard]] std::vector<LevelDirectory> collectDeletedLevels() const;

    [[nodiscard]] uint32_t documentWidth() const;
    [[nodiscard]] uint32_t documentHeight() const;
    [[nodiscard]] uint32_t documentDepth() const;
    [[nodiscard]] uint32_t activeLayer() const;
    [[nodiscard]] std::optional<uint32_t> waterLayer() const;
    [[nodiscard]] bool layerLocked() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] const std::vector<std::string>& documentRows() const;
    [[nodiscard]] const Level::LayerRows& documentLayers() const;
    [[nodiscard]] TileType selectedTile() const;
    [[nodiscard]] Tool tool() const;
    [[nodiscard]] const std::string& selectedDecorationModel() const;
    [[nodiscard]] const std::vector<Level::Decoration>& decorations() const;
    [[nodiscard]] std::optional<std::size_t> selectedDecorationIndex() const;
    [[nodiscard]] const Level::Decoration* selectedDecoration() const;
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
    [[nodiscard]] const std::string& status() const;

private:
    struct Document {
        Level::LayerRows layers;
        std::optional<uint32_t> waterLayer;
        std::vector<Level::Decoration> decorations;
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
        bool layerLocked = false;
        bool dirty = false;
        bool playingDraft = false;
        bool editingDocument = false;
    };

    struct DocumentSnapshot {
        Level::LayerRows layers;
        std::optional<uint32_t> waterLayer;
        std::vector<Level::Decoration> decorations;
        std::filesystem::path filePath;
        // Undoing a load has to restore where the document came from too, or
        // the restored contents would be attributed to the wrong screen.
        std::filesystem::path loadedPath;
        int requestedWidth = 12;
        int requestedHeight = 8;
        int activeLayer = 0;
        std::optional<std::size_t> selectedDecoration;
        bool dirty = false;
    };

    struct EditActionRecord {
        DocumentSnapshot before;
        DocumentSnapshot after;
    };

    void recordDocumentChange(const DocumentSnapshot& before);
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

    Document document_;
    std::vector<EditActionRecord> editHistory_;
    std::optional<DocumentSnapshot> decorationTransformBefore_;
};

} // namespace sokoban
