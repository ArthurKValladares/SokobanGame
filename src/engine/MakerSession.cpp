#include "engine/MakerSession.hpp"

#include "engine/EditorInteraction.hpp"

#include <cstddef>

namespace sokoban {

MakerSession::MakerSession()
{
    editor_.newDocument(10, 8, false);
    const auto historyLimits = editor_.historyLimits();
    editor_.setHistoryLimits({ .maxRecords = 0 });
    (void)editor_.beginStroke();
    editor_.eraseCell({ 0, 0, 1 });
    editor_.setCell({ 1, 3, 1 }, TileType::Rogue);
    editor_.setCell({ 3, 3, 1 }, TileType::Rock);
    editor_.setCell({ 7, 3, 1 }, TileType::End);
    editor_.setCell({ 3, 2, 1 }, TileType::Wall);
    editor_.setCell({ 4, 2, 1 }, TileType::Wall);
    editor_.setCell({ 5, 2, 1 }, TileType::Wall);
    editor_.setCell({ 5, 3, 1 }, TileType::Wall);
    editor_.endStroke();
    editor_.setHistoryLimits(historyLimits);
    editor_.setSelectedTile(TileType::Wall);
}

void MakerSession::setTool(Tool tool)
{
    endStroke();
    status_.clear();
    tool_ = tool;
}

void MakerSession::selectTile(TileType tile)
{
    if (tile == TileType::Air) {
        setTool(Tool::Erase);
        return;
    }
    setTool(Tool::Paint);
    editor_.setSelectedTile(tile);
}

void MakerSession::rotateTile()
{
    endStroke();
    status_.clear();
    editor_.rotateSelectedTile();
}

void MakerSession::stepLayer(int delta)
{
    endStroke();
    status_.clear();
    editor_.setActiveLayer(static_cast<int>(editor_.activeLayer()) + delta);
}

bool MakerSession::undo()
{
    endStroke();
    status_.clear();
    return editor_.tryUndoEdit();
}

bool MakerSession::redo()
{
    endStroke();
    status_.clear();
    return editor_.tryRedoEdit();
}

std::optional<GridPosition3> MakerSession::previewCell(GridPosition cell) const
{
    if (cell.x < 0 || cell.y < 0 ||
        cell.x >= static_cast<int>(editor_.documentWidth()) ||
        cell.y >= static_cast<int>(editor_.documentHeight())) {
        return std::nullopt;
    }
    return GridPosition3 { cell.x, cell.y, static_cast<int>(editor_.activeLayer()) };
}

bool MakerSession::beginStroke(GridPosition cell)
{
    if (!previewCell(cell) || !editor_.beginStroke()) {
        return false;
    }
    visited_.assign(static_cast<std::size_t>(editor_.documentWidth()) *
            editor_.documentHeight(), false);
    (void)continueStroke(cell);
    return true;
}

bool MakerSession::continueStroke(std::optional<GridPosition> cell)
{
    if (!strokeActive()) {
        return false;
    }
    if (!cell || !previewCell(*cell)) {
        lastCell_.reset();
        return false;
    }
    if (lastCell_ == cell) {
        return false;
    }

    bool changed = false;
    if (lastCell_) {
        for (const GridPosition crossed : EditorInteraction::gridLine(*lastCell_, *cell)) {
            changed = visitCell(crossed) || changed;
        }
    } else {
        changed = visitCell(*cell);
    }
    lastCell_ = cell;
    return changed;
}

bool MakerSession::endStroke()
{
    lastCell_.reset();
    return editor_.endStroke();
}

bool MakerSession::visitCell(GridPosition cell)
{
    const std::size_t index = static_cast<std::size_t>(cell.y) *
        editor_.documentWidth() + static_cast<std::size_t>(cell.x);
    if (visited_[index]) {
        return false;
    }
    visited_[index] = true;
    const GridPosition3 target { cell.x, cell.y, static_cast<int>(editor_.activeLayer()) };
    editor_.clearStatus();
    status_.clear();
    const bool changed = tool_ == Tool::Erase ? editor_.eraseCell(target) : editor_.paintCell(target);
    if (!changed) {
        status_ = editor_.status();
    }
    return changed;
}

} // namespace sokoban
