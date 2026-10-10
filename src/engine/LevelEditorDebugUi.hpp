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
#include <utility>
#include <vector>

namespace sokoban {

class AssetManifest;

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
        // Adds a map with its own registered mask and selects tile assignments.
        std::function<bool()> createGroundSplatMap;
        // Gives the selected map a fresh blank mask and starts mask painting.
        std::function<bool()> createGroundBlendMask;
        std::function<const AssetManifest&()> assetManifest;
        // Rendered preview of a tile type for the palette, or 0 when there is
        // none to show (still loading, no model, or thumbnails unavailable).
        // An ImGui ImTextureID, typed as uint64_t so this header does not
        // require imgui.h - which builds without developer tools omit.
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
    // Refreshes the Path field after a save or load made outside this panel
    // (the Ctrl+S shortcut).
    void syncDocumentPath(const LevelEditor& editor);
    [[nodiscard]] bool showDebugView() const { return showDebugView_; }

private:
    void drawGroundPaintTab(LevelEditor& editor, SplatPainter& painter, const Callbacks& callbacks);
    void drawTilePalette(
        LevelEditor& editor,
        const InputBindings& bindings,
        const Callbacks& callbacks);
    // The two halves of the decoration palette: the mesh library, and the
    // inspector for whichever decoration is selected.
    void drawDecorationMeshLibrary(
        LevelEditor& editor, const Callbacks& callbacks);
    void drawSelectedDecorationInspector(LevelEditor& editor);
    void drawDecorationPalette(
        LevelEditor& editor,
        const Callbacks& callbacks);
    void drawTileDecorationPalette(
        LevelEditor& editor, const InputBindings& bindings, const Callbacks& callbacks);
    void drawPlacedDecorations(LevelEditor& editor, bool tileDecorations);
    void drawSelectorPalette(LevelEditor& editor);
    void drawFileBrowser(
        LevelEditor& editor,
        OverworldMapEditor& overworldEditor);
    // The three parts of the overworld tab that are not the map canvas.
    void drawOverworldToolbar(OverworldMapEditor& overworldEditor);
    void drawSelectedOverworldScreen(
        LevelEditor& editor, OverworldMapEditor& overworldEditor);
    void drawOverworldDeletionsAndStatus(
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
    std::string groundPaintActionStatus_;
    std::optional<LevelEditor::Tool> selectedToolTab_;
    // The link group (by color) the Links section recolors.
    std::optional<Vec3> selectedLinkGroup_;
    std::optional<std::size_t> selectedLecternIndex_;
    std::optional<std::size_t> selectedGateIndex_;
    std::optional<std::size_t> selectedLockPlateIndex_;
    std::optional<std::size_t> selectedElevatorIndex_;
    std::optional<std::size_t> selectedMinecartIndex_;
    // The elevator stop list being typed, and which record (index and stops)
    // it was last filled from.
    std::string elevatorLevelsBuffer_;
    std::optional<std::pair<std::size_t, std::vector<int>>> elevatorLevelsSource_;
    int requestedWidth_ = 12;
    int requestedHeight_ = 8;
    std::optional<LevelEditor::LevelDirectory> pendingRenameLevel_;
    std::optional<int> pendingRenameScreen_;
    std::string renameBuffer_;
    std::optional<LevelEditor::LevelDirectory> pendingDeleteLevel_;
    std::filesystem::path pendingPermanentDeletePath_;
    std::filesystem::path overworldEditorRoot_;
    int overworldMoveSlot_[2] { 0, 0 };
    int overworldRestoreSlot_[2] { 0, 0 };
    bool showDebugView_ = false;
    bool elevatorLevelsEditing_ = false;
    bool elevatorLevelsError_ = false;
    bool renamePopupOpen_ = false;
    bool deleteLevelConfirmationOpen_ = false;
    bool permanentDeleteConfirmationOpen_ = false;
};

} // namespace sokoban
