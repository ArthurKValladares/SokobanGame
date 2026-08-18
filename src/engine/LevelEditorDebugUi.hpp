#pragma once

#include "engine/DecorationMeshCatalog.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/OverworldMapEditor.hpp"
#include "engine/InputBindings.hpp"
#include "engine/TileTypes.hpp"
#include "engine/SplatPainter.hpp"

#include <cstdint>
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
        // Rendered preview of a tile type for the palette, or 0 when there is
        // none to show (still loading, no model, or thumbnails unavailable).
        // An ImGui ImTextureID, typed as uint64_t so this header does not
        // require imgui.h - which non-debug builds compile without.
        std::function<uint64_t(TileType)> tileThumbnail;
        // Re-bakes the palette pictures. Same work as the
        // --bake-tile-thumbnails command line, offered here because that flag
        // is easy to forget and awkward to pass when launching from an IDE.
        std::function<bool()> bakeTileThumbnails;
        std::function<const std::vector<DecorationMeshCatalog::Entry>&()>
            decorationMeshes;
        std::function<const std::string&()> decorationMeshStatus;
        std::function<void()> refreshDecorationMeshes;
        // Registers an arbitrary source mesh on first selection and returns
        // the stable manifest model name used by the level document.
        std::function<std::optional<std::string>(
            const std::filesystem::path&)> registerDecorationMesh;
    };

    void initialize(const LevelEditor& editor);
    void draw(
        LevelEditor& editor,
        OverworldMapEditor& overworldEditor,
        SplatPainter& painter,
        const InputBindings& bindings,
        const Callbacks& callbacks);

private:
    void syncDocumentPath(const LevelEditor& editor);
    void drawGroundPaintTab(SplatPainter& painter, const Callbacks& callbacks);
    void drawTilePalette(LevelEditor& editor, const Callbacks& callbacks);
    void drawDecorationPalette(
        LevelEditor& editor,
        const Callbacks& callbacks);
    void drawSelectorPalette(LevelEditor& editor);
    void drawFileBrowser(
        LevelEditor& editor,
        OverworldMapEditor& overworldEditor);
    void drawOverworldTab(
        LevelEditor& editor,
        OverworldMapEditor& overworldEditor);
    void drawActiveLevelsTab(LevelEditor& editor);
    void drawDeletedLevelsTab(LevelEditor& editor);
    void drawRenamePopup(LevelEditor& editor);
    void drawDeleteLevelConfirmation(LevelEditor& editor);
    void drawPermanentDeleteConfirmation(LevelEditor& editor);

    std::string filePathBuffer_;
    std::string browserRootBuffer_;
    std::string decorationFilter_;
    std::string decorationRegistrationStatus_;
    std::optional<LevelEditor::Tool> selectedToolTab_;
    int requestedWidth_ = 12;
    int requestedHeight_ = 8;
    std::optional<LevelEditor::LevelDirectory> pendingRenameLevel_;
    std::optional<int> pendingRenameScreen_;
    std::string renameBuffer_;
    bool renamePopupOpen_ = false;
    std::optional<LevelEditor::LevelDirectory> pendingDeleteLevel_;
    bool deleteLevelConfirmationOpen_ = false;
    std::filesystem::path pendingPermanentDeletePath_;
    bool permanentDeleteConfirmationOpen_ = false;
    std::filesystem::path overworldEditorRoot_;
    int overworldMoveSlot_[2] { 0, 0 };
    int overworldRestoreSlot_[2] { 0, 0 };
};

} // namespace sokoban
