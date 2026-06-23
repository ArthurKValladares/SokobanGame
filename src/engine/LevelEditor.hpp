#pragma once

#include "engine/Level.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

namespace sokoban {

class LevelEditor {
public:
    struct Callbacks {
        std::function<void(Level)> playDraft;
        std::function<void()> returnToCurrentScreen;
    };

    void initialize(
        const std::filesystem::path& sourceLevelRoot,
        const std::filesystem::path& runtimeLevelRoot,
        int currentLevel,
        int currentScreen);
    void draw(const Callbacks& callbacks);

    void setPlayingDraft(bool playingDraft);
    [[nodiscard]] bool playingDraft() const;
    void setEditingDocument(bool editingDocument);
    [[nodiscard]] bool editingDocument() const;
    void markDraftSolved();
    void paintCell(GridPosition position);
    [[nodiscard]] bool tryUndoEdit();
    [[nodiscard]] uint32_t documentWidth() const;
    [[nodiscard]] uint32_t documentHeight() const;
    [[nodiscard]] const std::vector<std::string>& documentRows() const;
    [[nodiscard]] TileType selectedTile() const;

private:
    struct Document {
        std::vector<std::string> rows;
        std::filesystem::path filePath;
        std::filesystem::path browserRoot;
        std::filesystem::path sourceLevelRoot;
        std::filesystem::path runtimeLevelRoot;
        std::string filePathBuffer;
        std::string browserRootBuffer;
        std::string status;
        int requestedWidth = 12;
        int requestedHeight = 8;
        TileType selectedTile = TileType::Wall;
        bool dirty = false;
        bool playingDraft = false;
        bool editingDocument = false;
    };

    struct DocumentSnapshot {
        std::vector<std::string> rows;
        std::filesystem::path filePath;
        std::string filePathBuffer;
        int requestedWidth = 12;
        int requestedHeight = 8;
        bool dirty = false;
    };

    struct EditActionRecord {
        DocumentSnapshot before;
        DocumentSnapshot after;
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

    void drawTilePalette();
    void drawFileBrowser();
    void drawActiveLevelsTab();
    void drawDeletedLevelsTab();
    void drawDeleteLevelConfirmation();
    void drawPermanentDeleteConfirmation();
    void newDocument(int width, int height, bool recordHistory = true);
    void resizeDocument(int width, int height, bool recordHistory = true);
    void loadDocument(const std::filesystem::path& path, bool recordHistory = true);
    void saveDocument(const std::filesystem::path& path);
    void playDocument(const Callbacks& callbacks);
    void addLevelAt(int levelIndex);
    void requestDeleteLevel(const LevelDirectory& level);
    void confirmDeleteLevel();
    void addScreenAt(const LevelDirectory& level, int screenIndex);
    void deleteScreen(const LevelDirectory& level, int screenIndex);
    void restoreDeletedLevel(const std::filesystem::path& deletedLevelPath);
    void requestPermanentDelete(const std::filesystem::path& path);
    void confirmPermanentDelete();
    void recordDocumentChange(const DocumentSnapshot& before);
    void applyDocumentSnapshot(const DocumentSnapshot& snapshot);
    [[nodiscard]] Level documentToLevel() const;
    [[nodiscard]] DocumentSnapshot captureDocumentSnapshot() const;
    [[nodiscard]] EditActionRecord invertEditActionRecord(const EditActionRecord& record) const;
    [[nodiscard]] std::filesystem::path runtimeMirrorPath(const std::filesystem::path& sourcePath) const;
    [[nodiscard]] std::filesystem::path deletedLevelRoot() const;
    [[nodiscard]] std::vector<LevelDirectory> collectLevelDirectories() const;
    [[nodiscard]] std::vector<LevelDirectory> collectDeletedLevels() const;
    [[nodiscard]] std::vector<std::string> defaultScreenRows() const;
    [[nodiscard]] std::filesystem::path uniqueDeletedLevelPath(const std::filesystem::path& levelPath) const;
    void writeScreenFile(const std::filesystem::path& path, const std::vector<std::string>& rows);
    void mirrorBrowserRootToRuntime();
    void loadFirstAvailableScreen();

    Document document_;
    std::vector<EditActionRecord> editHistory_;
    std::optional<size_t> editUndoCursor_;
    std::filesystem::path pendingDeleteLevelPath_;
    int pendingDeleteLevelIndex_ = -1;
    bool deleteLevelConfirmationOpen_ = false;
    std::filesystem::path pendingPermanentDeletePath_;
    bool permanentDeleteConfirmationOpen_ = false;
};

} // namespace sokoban
