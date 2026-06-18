#pragma once

#include "engine/Level.hpp"

#include <filesystem>
#include <functional>
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

    void initialize(const std::filesystem::path& assetRoot, int currentLevel, int currentScreen);
    void draw(const Callbacks& callbacks);

    void setPlayingDraft(bool playingDraft);
    [[nodiscard]] bool playingDraft() const;
    void markDraftSolved();

private:
    struct Document {
        std::vector<std::string> rows;
        std::filesystem::path filePath;
        std::filesystem::path browserRoot;
        std::string filePathBuffer;
        std::string browserRootBuffer;
        std::string status;
        int requestedWidth = 12;
        int requestedHeight = 8;
        char selectedTile = '#';
        bool dirty = false;
        bool playingDraft = false;
    };

    void drawTilePalette();
    void drawFileBrowser();
    void drawGrid();
    void newDocument(int width, int height);
    void resizeDocument(int width, int height);
    void loadDocument(const std::filesystem::path& path);
    void saveDocument(const std::filesystem::path& path);
    void playDocument(const Callbacks& callbacks);
    [[nodiscard]] Level documentToLevel() const;

    Document document_;
};

} // namespace sokoban
