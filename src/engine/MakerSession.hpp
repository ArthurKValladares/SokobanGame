#pragma once

#include "engine/LevelEditor.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class MakerDraftStore;

// Owns the active Maker draft and its edit history independently of the campaign.
class MakerSession {
public:
    enum class Tool { Paint, Erase, Connect };
    enum class SaveDraftResult { Saved, NameExists, Failed };

    static constexpr uint32_t minDraftDimension = 1;
    static constexpr uint32_t maxDraftDimension = 64;
    static constexpr uint32_t defaultDraftWidth = 10;
    static constexpr uint32_t defaultDraftHeight = 8;

    MakerSession();

    [[nodiscard]] const LevelEditor& editor() const { return editor_; }
    [[nodiscard]] Tool tool() const { return tool_; }
    [[nodiscard]] std::string_view status() const { return status_; }
    [[nodiscard]] std::string_view draftName() const { return draftName_; }
    [[nodiscard]] bool hasUnsavedChanges() const { return unsavedChanges_ || strokeChanged_; }
    [[nodiscard]] bool newDraft(uint32_t width, uint32_t height);
    [[nodiscard]] std::size_t resizeTileLossCount(uint32_t width, uint32_t height) const;
    [[nodiscard]] bool resizeDraft(uint32_t width, uint32_t height, bool allowCrop = false);
    [[nodiscard]] SaveDraftResult saveDraft(const MakerDraftStore& store,
        std::string_view name, bool overwrite = false);
    [[nodiscard]] bool loadDraft(const MakerDraftStore& store, std::string_view name);
    void setTool(Tool tool);
    void selectTile(TileType tile);
    void rotateTile();
    void stepLayer(int delta);
    bool undo();
    bool redo();
    [[nodiscard]] std::optional<LevelEditor::LinkEndpoint> selectedLinkEndpoint() const;
    [[nodiscard]] std::optional<LevelEditor::LinkEndpoint> linkTargetAt(GridPosition3 cell) const;
    [[nodiscard]] bool linkLayerIsolated() const { return linkLayerIsolated_; }
    void toggleLinkLayer();
    bool connectCell(GridPosition3 cell);
    bool selectLinkEndpoint(LevelEditor::LinkEndpoint endpoint);
    void clearLinkSelection();
    bool assignSelectedLinkGroup(Vec3 color);
    bool disconnectSelectedLink();
    bool createSelectedLinkGroup(std::string name);
    bool renameLinkGroup(Level::LinkGroupKind kind, Vec3 color, std::string name);
    bool toggleSelectedElevatorStop();

    // Finish an open edit stroke and compile a separate runtime level for Test
    // Play. Invalid drafts stay editable and report the loader's explanation.
    [[nodiscard]] std::optional<Level> buildPlaytestLevel();

    [[nodiscard]] std::optional<GridPosition3> previewCell(GridPosition cell) const;
    [[nodiscard]] bool strokeActive() const { return editor_.strokeActive(); }
    // An in-bounds press owns the stroke even when its first cell is unchanged.
    bool beginStroke(GridPosition cell);
    // A missing cell breaks interpolation while the pointer is off the board.
    bool continueStroke(std::optional<GridPosition> cell);
    bool endStroke();

private:
    void replaceDraft(Level::Definition definition, std::string name);
    bool visitCell(GridPosition cell);
    void updateUnsavedChanges();

    LevelEditor editor_;
    Tool tool_ = Tool::Paint;
    std::string status_;
    std::string draftName_;
    bool unsavedChanges_ = false;
    bool strokeChanged_ = false;
    std::optional<LevelEditor::LinkEndpoint> selectedLinkEndpoint_;
    bool linkLayerIsolated_ = false;
    std::optional<GridPosition> lastCell_;
    std::vector<bool> visited_;
};

} // namespace sokoban
