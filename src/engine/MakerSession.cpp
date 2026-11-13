#include "engine/MakerSession.hpp"

#include "engine/EditorInteraction.hpp"
#include "engine/MakerDraftStore.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace sokoban {
namespace {

LevelEditor::LinkEndpointKind connectionTargetKind(LevelEditor::LinkEndpointKind kind)
{
    using Kind = LevelEditor::LinkEndpointKind;
    if (kind == Kind::Source) return Kind::Device;
    if (kind == Kind::Device) return Kind::Source;
    return kind;
}

} // namespace

MakerSession::MakerSession()
{
    editor_.enableExplicitLinkEditing();
    editor_.newDocument(defaultDraftWidth, defaultDraftHeight, false);
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
    editor_.markDraftSaved();
}

void MakerSession::setTool(Tool tool)
{
    endStroke();
    status_.clear();
    if (tool != tool_) clearLinkSelection();
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
    if (!editor_.tryUndoEdit()) return false;
    updateUnsavedChanges();
    return true;
}

bool MakerSession::redo()
{
    endStroke();
    status_.clear();
    if (!editor_.tryRedoEdit()) return false;
    updateUnsavedChanges();
    return true;
}

std::optional<LevelEditor::LinkEndpoint> MakerSession::selectedLinkEndpoint() const
{
    if (selectedLinkEndpoint_ && editor_.hasLinkEndpoint(*selectedLinkEndpoint_)) {
        return selectedLinkEndpoint_;
    }
    return std::nullopt;
}

bool MakerSession::selectLinkEndpoint(LevelEditor::LinkEndpoint endpoint)
{
    if (!editor_.hasLinkEndpoint(endpoint)) return false;
    endStroke();
    status_.clear();
    selectedLinkEndpoint_ = endpoint;
    return true;
}

void MakerSession::clearLinkSelection()
{
    selectedLinkEndpoint_.reset();
}

bool MakerSession::connectCell(GridPosition3 cell)
{
    if (tool_ != Tool::Connect) return false;
    const auto clicked = linkTargetAt(cell);
    if (!clicked) {
        clearLinkSelection();
        status_ = "Select a source, device, portal, or movable object.";
        return false;
    }
    const auto selected = selectedLinkEndpoint();
    if (clicked == selected) return true;
    if (!selected || clicked->kind != connectionTargetKind(selected->kind)) return selectLinkEndpoint(*clicked);

    editor_.clearStatus();
    if (editor_.connectLinkEndpoints(*selected, *clicked)) {
        updateUnsavedChanges();
        // Keep the device selected so adding another source is one click.
        if (clicked->kind == LevelEditor::LinkEndpointKind::Device) selectedLinkEndpoint_ = *clicked;
    }
    status_ = editor_.status();
    return true;
}

std::optional<LevelEditor::LinkEndpoint> MakerSession::linkTargetAt(GridPosition3 cell) const
{
    if (const auto selected = selectedLinkEndpoint()) {
        const LevelEditor::LinkEndpoint target { cell, connectionTargetKind(selected->kind) };
        if (editor_.hasLinkEndpoint(target)) return target;
    }
    return editor_.linkEndpointAt(cell);
}

void MakerSession::toggleLinkLayer()
{
    linkLayerIsolated_ = !linkLayerIsolated_;
}

bool MakerSession::assignSelectedLinkGroup(Vec3 color)
{
    const auto endpoint = selectedLinkEndpoint();
    if (!endpoint) return false;
    editor_.clearStatus();
    const bool changed = editor_.assignLinkEndpoint(*endpoint, color);
    status_ = editor_.status();
    if (changed) updateUnsavedChanges();
    return changed;
}

bool MakerSession::disconnectSelectedLink()
{
    const auto endpoint = selectedLinkEndpoint();
    if (!endpoint) return false;
    editor_.clearStatus();
    const bool changed = editor_.disconnectLinkEndpoint(*endpoint);
    status_ = editor_.status();
    if (changed) updateUnsavedChanges();
    return changed;
}

bool MakerSession::createSelectedLinkGroup(std::string name)
{
    const auto endpoint = selectedLinkEndpoint();
    if (!endpoint) return false;
    editor_.clearStatus();
    const bool changed = editor_.createLinkGroup(*endpoint, std::move(name));
    status_ = editor_.status();
    if (changed) updateUnsavedChanges();
    return changed;
}

bool MakerSession::renameLinkGroup(Level::LinkGroupKind kind, Vec3 color, std::string name)
{
    editor_.clearStatus();
    const bool changed = editor_.setLinkGroupName(kind, color, std::move(name));
    status_ = editor_.status();
    if (changed) updateUnsavedChanges();
    return changed;
}

bool MakerSession::toggleSelectedElevatorStop()
{
    const auto selected = selectedLinkEndpoint();
    if (!selected || selected->kind != LevelEditor::LinkEndpointKind::Device) return false;
    const auto& elevators = editor_.elevators();
    const auto elevator = std::ranges::find(elevators, selected->cell, &Level::Elevator::cell);
    const auto layer = static_cast<int>(editor_.activeLayer());
    if (elevator == elevators.end() || elevator->cell.z == layer) return false;

    auto stops = elevator->levels;
    if (std::erase(stops, layer) == 0) stops.push_back(layer);
    editor_.clearStatus();
    const bool changed = editor_.setElevatorLevels(static_cast<std::size_t>(elevator - elevators.begin()), std::move(stops));
    status_ = editor_.status();
    if (changed) updateUnsavedChanges();
    return changed;
}

bool MakerSession::newDraft(uint32_t width, uint32_t height)
{
    if (width < minDraftDimension || width > maxDraftDimension ||
        height < minDraftDimension || height > maxDraftDimension) {
        status_ = "Board width and height must be between " +
            std::to_string(minDraftDimension) + " and " + std::to_string(maxDraftDimension) + ".";
        return false;
    }

    replaceDraft({ .layers = {
        std::vector<std::string>(height, std::string(width, tileTypeToChar(TileType::Ground))),
        std::vector<std::string>(height, std::string(width, tileTypeToChar(TileType::Air))),
    } }, {});
    editor_.setActiveLayer(1);
    status_ = "Created new draft";
    return true;
}

std::size_t MakerSession::resizeTileLossCount(uint32_t width, uint32_t height) const
{
    if (width >= editor_.documentWidth() && height >= editor_.documentHeight()) {
        return 0;
    }

    const char air = tileTypeToChar(TileType::Air);
    std::size_t count = 0;
    for (const auto& layer : editor_.documentLayers()) {
        for (std::size_t y = 0; y < layer.size(); ++y) {
            const std::string_view row = layer[y];
            const auto kept = y < height ? std::min<std::size_t>(width, row.size()) : 0;
            count += static_cast<std::size_t>(std::ranges::count_if(row.substr(kept),
                [air](char tile) { return tile != air; }));
        }
    }
    count += static_cast<std::size_t>(std::ranges::count_if(editor_.coveredPlates(),
        [width, height](const Level::Plate& plate) {
            return std::cmp_greater_equal(plate.cell.x, width) ||
                std::cmp_greater_equal(plate.cell.y, height);
        }));
    return count;
}

bool MakerSession::resizeDraft(uint32_t width, uint32_t height, bool allowCrop)
{
    const auto currentWidth = editor_.documentWidth();
    const auto currentHeight = editor_.documentHeight();
    const auto maxWidth = std::max(maxDraftDimension, currentWidth);
    const auto maxHeight = std::max(maxDraftDimension, currentHeight);
    if (width < minDraftDimension || width > maxWidth ||
        height < minDraftDimension || height > maxHeight) {
        status_ = "Board width must be between " + std::to_string(minDraftDimension) +
            " and " + std::to_string(maxWidth) + ", and height between " +
            std::to_string(minDraftDimension) + " and " + std::to_string(maxHeight) + " tiles.";
        return false;
    }
    if (width == currentWidth && height == currentHeight) {
        return true;
    }
    if (!allowCrop && resizeTileLossCount(width, height) != 0) {
        status_ = "Shrinking to this size would remove tiles.";
        return false;
    }

    endStroke();
    editor_.resizeDocument(static_cast<int>(width), static_cast<int>(height));
    updateUnsavedChanges();
    status_ = "Resized board";
    return true;
}

MakerSession::SaveDraftResult MakerSession::saveDraft(
    const MakerDraftStore& store, std::string_view name, bool overwrite)
{
    endStroke();
    try {
        std::string savedName(name);
        store.save(savedName, editor_.documentDefinition(), overwrite);
        editor_.markDraftSaved();
        draftName_ = std::move(savedName);
        unsavedChanges_ = false;
        status_ = "Saved draft";
        return SaveDraftResult::Saved;
    } catch (const MakerDraftStore::NameExists& error) {
        status_ = error.what();
        return SaveDraftResult::NameExists;
    } catch (const std::exception& error) {
        status_ = error.what();
        return SaveDraftResult::Failed;
    }
}

bool MakerSession::loadDraft(const MakerDraftStore& store, std::string_view name)
{
    try {
        replaceDraft(store.load(name), std::string(name));
        status_ = "Loaded draft";
        return true;
    } catch (const std::exception& error) {
        status_ = error.what();
        return false;
    }
}

void MakerSession::replaceDraft(Level::Definition definition, std::string name)
{
    editor_.loadDraft(std::move(definition));
    draftName_ = std::move(name);
    unsavedChanges_ = false;
    strokeChanged_ = false;
    clearLinkSelection();
    linkLayerIsolated_ = false;
    lastCell_.reset();
    visited_.clear();
}

std::optional<Level> MakerSession::buildPlaytestLevel()
{
    endStroke();
    status_.clear();
    try {
        return editor_.documentToLevel();
    } catch (const std::runtime_error& error) {
        status_ = error.what();
        return std::nullopt;
    }
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
    if (tool_ == Tool::Connect || !previewCell(cell) || !editor_.beginStroke()) {
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
    strokeChanged_ |= changed;
    return changed;
}

bool MakerSession::endStroke()
{
    lastCell_.reset();
    strokeChanged_ = false;
    const bool changed = editor_.endStroke();
    if (changed) updateUnsavedChanges();
    return changed;
}

void MakerSession::updateUnsavedChanges()
{
    unsavedChanges_ = editor_.dirty();
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
