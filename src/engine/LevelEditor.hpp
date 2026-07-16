#pragma once

#include "engine/Level.hpp"

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
    void setLayerLocked(bool locked);
    void setSelectedTile(TileType tile);
    void selectDocument(const std::filesystem::path& path);
    [[nodiscard]] bool setBrowserRoot(const std::filesystem::path& path);

    void newDocument(int width, int height, bool recordHistory = true);
    void resizeDocument(int width, int height, bool recordHistory = true);
    void addLayer();
    void deleteActiveLayer();
    [[nodiscard]] bool loadDocument(const std::filesystem::path& path, bool recordHistory = true);
    [[nodiscard]] bool saveDocument(const std::filesystem::path& path);
    [[nodiscard]] Level documentToLevel() const;
    [[nodiscard]] std::optional<Level> beginDraftPlayback();

    void paintCell(GridPosition3 position);
    void eraseCell(GridPosition3 position);
    void setCell(GridPosition3 position, TileType tile);
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
    [[nodiscard]] bool layerLocked() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] const std::vector<std::string>& documentRows() const;
    [[nodiscard]] const Level::LayerRows& documentLayers() const;
    [[nodiscard]] TileType selectedTile() const;
    [[nodiscard]] const std::filesystem::path& documentPath() const;
    [[nodiscard]] const std::filesystem::path& browserRoot() const;
    [[nodiscard]] const std::string& status() const;

private:
    struct Document {
        Level::LayerRows layers;
        std::filesystem::path filePath;
        std::filesystem::path browserRoot;
        std::filesystem::path sourceLevelRoot;
        std::filesystem::path runtimeLevelRoot;
        std::string status;
        int requestedWidth = 12;
        int requestedHeight = 8;
        int activeLayer = 0;
        TileType selectedTile = TileType::Wall;
        bool layerLocked = false;
        bool dirty = false;
        bool playingDraft = false;
        bool editingDocument = false;
    };

    struct DocumentSnapshot {
        Level::LayerRows layers;
        std::filesystem::path filePath;
        int requestedWidth = 12;
        int requestedHeight = 8;
        int activeLayer = 0;
        bool dirty = false;
    };

    struct EditActionRecord {
        DocumentSnapshot before;
        DocumentSnapshot after;
    };

    void recordDocumentChange(const DocumentSnapshot& before);
    void applyDocumentSnapshot(const DocumentSnapshot& snapshot);
    [[nodiscard]] DocumentSnapshot captureDocumentSnapshot() const;
    [[nodiscard]] EditActionRecord invertEditActionRecord(const EditActionRecord& record) const;
    [[nodiscard]] std::filesystem::path runtimeMirrorPath(const std::filesystem::path& sourcePath) const;
    [[nodiscard]] std::filesystem::path deletedLevelRoot() const;
    [[nodiscard]] bool isActiveLevelDirectory(const LevelDirectory& level) const;
    [[nodiscard]] std::vector<std::string> defaultScreenRows() const;
    [[nodiscard]] std::filesystem::path uniqueDeletedLevelPath(const std::filesystem::path& levelPath) const;
    [[nodiscard]] bool writeScreenFile(const std::filesystem::path& path, const std::vector<std::string>& rows);
    [[nodiscard]] bool mirrorBrowserRootToRuntime();
    void loadFirstAvailableScreen();

    Document document_;
    std::vector<EditActionRecord> editHistory_;
};

} // namespace sokoban
