#pragma once

#include "engine/LevelEditor.hpp"
#include "engine/SplatPainter.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace sokoban {

// ImGui adapter for LevelEditor. This class owns presentation-only state and
// delegates every editor operation to the headless model.
class LevelEditorDebugUi {
public:
    struct Callbacks {
        std::function<void(Level)> playDraft;
        std::function<void()> returnToCurrentScreen;
        // Opens the splat map for the document being edited. Returns false
        // when it has none; the painter's status says why.
        std::function<bool()> openGroundPainting;
        // Creates and registers one for a screen that has none, then opens it.
        std::function<bool()> createGroundSplatMap;
    };

    void initialize(const LevelEditor& editor);
    void draw(
        LevelEditor& editor,
        SplatPainter& painter,
        const Callbacks& callbacks);

private:
    void syncDocumentPath(const LevelEditor& editor);
    void drawGroundPaintTab(SplatPainter& painter, const Callbacks& callbacks);
    void drawTilePalette(LevelEditor& editor);
    void drawFileBrowser(LevelEditor& editor);
    void drawActiveLevelsTab(LevelEditor& editor);
    void drawDeletedLevelsTab(LevelEditor& editor);
    void drawDeleteLevelConfirmation(LevelEditor& editor);
    void drawPermanentDeleteConfirmation(LevelEditor& editor);

    std::string filePathBuffer_;
    std::string browserRootBuffer_;
    int requestedWidth_ = 12;
    int requestedHeight_ = 8;
    std::optional<LevelEditor::LevelDirectory> pendingDeleteLevel_;
    bool deleteLevelConfirmationOpen_ = false;
    std::filesystem::path pendingPermanentDeletePath_;
    bool permanentDeleteConfirmationOpen_ = false;
};

} // namespace sokoban
