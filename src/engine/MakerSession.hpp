#pragma once

#include "engine/LevelEditor.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

// Owns the Maker draft for this application session. No campaign paths or
// project store are needed to edit it.
class MakerSession {
public:
    enum class Tool { Paint, Erase };

    MakerSession();

    [[nodiscard]] const LevelEditor& editor() const { return editor_; }
    [[nodiscard]] Tool tool() const { return tool_; }
    [[nodiscard]] std::string_view status() const { return status_; }
    void setTool(Tool tool);
    void selectTile(TileType tile);
    void rotateTile();
    void stepLayer(int delta);
    bool undo();
    bool redo();

    [[nodiscard]] std::optional<GridPosition3> previewCell(GridPosition cell) const;
    [[nodiscard]] bool strokeActive() const { return editor_.strokeActive(); }
    // An in-bounds press owns the stroke even when its first cell is unchanged.
    bool beginStroke(GridPosition cell);
    // A missing cell breaks interpolation while the pointer is off the board.
    bool continueStroke(std::optional<GridPosition> cell);
    bool endStroke();

private:
    bool visitCell(GridPosition cell);

    LevelEditor editor_;
    Tool tool_ = Tool::Paint;
    std::string status_;
    std::optional<GridPosition> lastCell_;
    std::vector<bool> visited_;
};

} // namespace sokoban
