#include "engine/MakerSession.hpp"

#include "engine/EditorInteraction.hpp"
#include "engine/MakerDraftStore.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
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

Level::Definition emptyOverworldComponent(
    uint32_t width, uint32_t height, std::size_t depth = 2,
    std::optional<uint32_t> water = std::nullopt)
{
    Level::Definition definition;
    definition.character = CharacterType::Lorekeeper;
    definition.waterLayer = water;
    definition.layers.assign(depth, std::vector<std::string>(
        height, std::string(width, tileTypeToChar(TileType::Air))));
    return definition;
}

int floorDivide(int value, int divisor)
{
    return value / divisor - (value % divisor < 0 ? 1 : 0);
}

int positiveRemainder(int value, int divisor)
{
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

bool hasOverworldPlayer(const Level::Definition& definition)
{
    return std::ranges::any_of(definition.layers, [](const auto& rows) {
        return std::ranges::any_of(rows, [](const std::string& row) {
            return row.find(tileTypeToChar(TileType::Player)) != std::string::npos;
        });
    });
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
    if ((tool == Tool::ConnectSelectors && !overworld()) || (tool == Tool::PasteArea && !hasClipboard())) return;
    endStroke();
    status_.clear();
    if (tool != tool_) {
        clearLinkSelection();
        clearLevelSelectorSource();
        cancelMove();
    }
    placingSelector_ = false;
    tool_ = tool;
}

void MakerSession::selectTile(TileType tile)
{
    if (overworld() && tile == TileType::End) {
        status_ = "End tiles are only available in puzzle levels.";
        return;
    }
    if (tile == TileType::Air) {
        setTool(Tool::Erase);
        return;
    }
    setTool(tool_ == Tool::SelectArea || tool_ == Tool::FloodFill ? tool_ : Tool::Paint);
    editor_.setSelectedTile(overworld() && tileTypeIsPlayerStart(tile) ? TileType::Player : tile);
}

bool MakerSession::selectRecentTile(std::size_t slot)
{
    if (!editor_.selectRecentTile(slot)) return false;
    setTool(tool_ == Tool::SelectArea || tool_ == Tool::FloodFill ? tool_ : Tool::Paint);
    status_ = editor_.status();
    return true;
}

bool MakerSession::pickTile(GridPosition3 cell)
{
    endStroke();
    cancelMove();
    if (overworld()) {
        const auto target = overworldCell(cell);
        if (!target || target->screen == 0) return false;
        if (target->screen != currentScreen_) {
            LevelEditor neighbor;
            const auto& definition = *overworldDefinition(target->screen);
            if (editor_.layerLocked() && editor_.activeLayer() >= definition.layers.size()) return false;
            neighbor.loadOverworldDraft(definition, target->screen);
            neighbor.setLayerLocked(editor_.layerLocked());
            neighbor.setActiveLayer(static_cast<int>(editor_.activeLayer()));
            const auto tile = neighbor.pickTile(target->local);
            if (!tile) return false;
            selectTile(*tile);
            editor_.rotateSelectedTile(static_cast<int>(neighbor.selectedTileQuarterTurns()) -
                static_cast<int>(editor_.selectedTileQuarterTurns()));
            status_ = neighbor.status();
            return true;
        }
        cell = target->local;
    }
    const bool picked = editor_.pickTile(cell).has_value();
    if (picked) setTool(Tool::Paint);
    status_ = editor_.status();
    return picked;
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
    const uint32_t previous = editor_.activeLayer();
    editor_.setActiveLayer(static_cast<int>(editor_.activeLayer()) + delta);
    if (editor_.activeLayer() != previous) clearSelection();
}

void MakerSession::toggleLayerLock()
{
    endStroke();
    editor_.toggleLayerLock();
    status_ = editor_.status();
}

bool MakerSession::regionContains(GridPosition3 cell) const
{
    return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 &&
        std::cmp_less(cell.x, editor_.documentWidth()) && std::cmp_less(cell.y, editor_.documentHeight()) &&
        std::cmp_less(cell.z, editor_.documentDepth());
}

std::optional<MakerSession::Region> MakerSession::selection() const
{
    if (selection_ && std::cmp_equal(selection_->min.z, editor_.activeLayer()) &&
        regionContains(selection_->min) && regionContains(selection_->max)) return selection_;
    return std::nullopt;
}

bool MakerSession::selectArea(GridPosition3 anchor, GridPosition3 corner)
{
    const auto layer = static_cast<int>(editor_.activeLayer());
    anchor.z = corner.z = layer;
    if (!regionContains(anchor)) {
        status_ = overworld() ? "Start the selection on the active overworld screen." :
            "Start the selection on the board.";
        return false;
    }
    endStroke();
    corner.x = std::clamp(corner.x, 0, static_cast<int>(editor_.documentWidth()) - 1);
    corner.y = std::clamp(corner.y, 0, static_cast<int>(editor_.documentHeight()) - 1);
    selection_ = Region {
        { std::min(anchor.x, corner.x), std::min(anchor.y, corner.y), layer },
        { std::max(anchor.x, corner.x), std::max(anchor.y, corner.y), layer },
    };
    status_ = "Selected " + std::to_string(selection_->max.x - selection_->min.x + 1) + " x " +
        std::to_string(selection_->max.y - selection_->min.y + 1) + " tiles";
    return true;
}

void MakerSession::clearSelection()
{
    selection_.reset();
}

bool MakerSession::fillSelection()
{
    const auto area = selection();
    if (!area) return false;
    endStroke();
    editor_.clearStatus();
    if (!editor_.fillRegion(area->min, area->max)) {
        status_ = editor_.status().empty() ? "Selection already matches the brush." : editor_.status();
        return false;
    }
    commitRegionEdit();
    status_ = "Filled selection";
    return true;
}

bool MakerSession::clearSelectionTiles()
{
    const auto area = selection();
    if (!area) return false;
    endStroke();
    editor_.clearStatus();
    if (!editor_.fillRegion(area->min, area->max, true)) {
        status_ = editor_.status().empty() ? "The selection is empty." : editor_.status();
        return false;
    }
    commitRegionEdit();
    status_ = "Cleared selection";
    return true;
}

bool MakerSession::floodFill(GridPosition3 cell)
{
    cell.z = static_cast<int>(editor_.activeLayer());
    if (!regionContains(cell)) {
        status_ = overworld() ? "Flood fill starts on the active overworld screen." :
            "Flood fill starts on the board.";
        return false;
    }
    endStroke();
    editor_.clearStatus();
    if (!editor_.floodFill(cell)) {
        status_ = editor_.status().empty() ? "This area already matches the brush." : editor_.status();
        return false;
    }
    commitRegionEdit();
    status_ = "Flood filled area";
    return true;
}

bool MakerSession::copySelection()
{
    const auto area = selection();
    if (!area) return false;
    endStroke();
    auto copied = editor_.copyRegion(area->min, area->max);
    if (!copied) return false;
    clipboard_ = std::move(copied);
    status_ = "Copied selection";
    return true;
}

bool MakerSession::cutSelection()
{
    const auto area = selection();
    if (!area) return false;
    endStroke();
    auto copied = editor_.copyRegion(area->min, area->max);
    if (!copied) return false;
    editor_.clearStatus();
    if (editor_.fillRegion(area->min, area->max, true)) {
        commitRegionEdit();
    } else if (!editor_.status().empty()) {
        status_ = editor_.status();
        return false;
    }
    clipboard_ = std::move(copied);
    status_ = "Cut selection";
    return true;
}

std::optional<MakerSession::Region> MakerSession::pastePreview(GridPosition3 origin) const
{
    origin.z = static_cast<int>(editor_.activeLayer());
    if (!clipboard_ || !regionContains(origin) ||
        clipboard_->width > editor_.documentWidth() - static_cast<uint32_t>(origin.x) ||
        clipboard_->height > editor_.documentHeight() - static_cast<uint32_t>(origin.y)) return std::nullopt;
    return Region { origin,
        { origin.x + static_cast<int>(clipboard_->width) - 1,
            origin.y + static_cast<int>(clipboard_->height) - 1, origin.z } };
}

GridPosition MakerSession::clipboardSize() const
{
    return clipboard_ ? GridPosition { static_cast<int>(clipboard_->width), static_cast<int>(clipboard_->height) }
        : GridPosition {};
}

bool MakerSession::pasteSelection(GridPosition3 origin)
{
    if (!clipboard_) return false;
    const auto area = pastePreview(origin);
    if (!area) {
        status_ = overworld() ? "The copied area must fit on the active overworld screen." :
            "The copied area must fit on the board.";
        return false;
    }
    endStroke();
    editor_.clearStatus();
    if (!editor_.pasteRegion(area->min, *clipboard_)) {
        status_ = editor_.status().empty() ? "The board already matches the copied area." : editor_.status();
        return false;
    }
    commitRegionEdit();
    selection_ = *area;
    status_ = "Pasted selection";
    return true;
}

std::optional<GridPosition3> MakerSession::moveTarget(GridPosition3 cell) const
{
    const GridPosition3 target = editor_.resolveMoveTarget(cell);
    if (target.x < 0 || target.y < 0 ||
        std::cmp_greater_equal(target.x, editor_.documentWidth()) ||
        std::cmp_greater_equal(target.y, editor_.documentHeight()) || target.z < 0 ||
        std::cmp_greater_equal(target.z, editor_.documentDepth())) {
        return std::nullopt;
    }
    return target;
}

bool MakerSession::moveCell(GridPosition3 cell)
{
    endStroke();
    const auto target = moveTarget(cell);
    if (!target) {
        status_ = "Move target must be within the board.";
        return false;
    }
    if (!editor_.pendingMove()) {
        const bool selected = editor_.beginMove(*target);
        status_ = editor_.status();
        return selected;
    }
    const bool moved = editor_.moveObject(*target);
    status_ = editor_.status();
    if (moved) updateUnsavedChanges();
    return moved;
}

void MakerSession::cancelMove()
{
    if (editor_.pendingMove()) {
        editor_.cancelMove();
        status_ = editor_.status();
    }
}

bool MakerSession::undo()
{
    endStroke();
    status_.clear();
    if (overworld_) {
        if (overworldUndo_.empty()) return false;
        overworldRedo_.push_back(std::move(*overworld_));
        *overworld_ = std::move(overworldUndo_.back());
        overworldUndo_.pop_back();
        clearLevelSelectorSource();
        restoreCurrentOverworldScreen();
        unsavedChanges_ = overworld_->revision != savedOverworldRevision_;
        return true;
    }
    if (!editor_.tryUndoEdit()) return false;
    updateUnsavedChanges();
    return true;
}

bool MakerSession::redo()
{
    endStroke();
    status_.clear();
    if (overworld_) {
        if (overworldRedo_.empty()) return false;
        overworldUndo_.push_back(std::move(*overworld_));
        *overworld_ = std::move(overworldRedo_.back());
        overworldRedo_.pop_back();
        clearLevelSelectorSource();
        restoreCurrentOverworldScreen();
        unsavedChanges_ = overworld_->revision != savedOverworldRevision_;
        return true;
    }
    if (!editor_.tryRedoEdit()) return false;
    updateUnsavedChanges();
    return true;
}

bool MakerSession::canUndo() const
{
    return overworld_ ? !overworldUndo_.empty() : editor_.canUndo();
}

bool MakerSession::canRedo() const
{
    return overworld_ ? !overworldRedo_.empty() : editor_.canRedo();
}

std::optional<LevelEditor::LinkEndpoint> MakerSession::selectedLinkEndpoint() const
{
    if (!selectedLinkEndpoint_) return std::nullopt;
    const auto endpoint = *selectedLinkEndpoint_;
    if (overworld() && endpoint.kind == LevelEditor::LinkEndpointKind::Portal) {
        if (hasOverworldPortal(endpoint.cell)) return endpoint;
    } else if (editor_.hasLinkEndpoint(endpoint)) {
        return selectedLinkEndpoint_;
    }
    return std::nullopt;
}

bool MakerSession::selectLinkEndpoint(LevelEditor::LinkEndpoint endpoint)
{
    const bool exists = overworld() && endpoint.kind == LevelEditor::LinkEndpointKind::Portal
        ? hasOverworldPortal(endpoint.cell) : editor_.hasLinkEndpoint(endpoint);
    if (!exists) return false;
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
    if (overworld() && clicked->kind == LevelEditor::LinkEndpointKind::Portal) {
        (void)connectOverworldPortals(selected->cell, clicked->cell);
        return true;
    }

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
    const auto selected = selectedLinkEndpoint();
    if (overworld()) {
        const auto address = overworldCell(cell);
        if (!address || address->screen == 0) return std::nullopt;
        const bool onNeighbor = address->screen != currentScreen_;
        using Kind = LevelEditor::LinkEndpointKind;
        if ((onNeighbor || (selected && selected->kind == Kind::Portal)) && hasOverworldPortal(cell)) {
            return LevelEditor::LinkEndpoint { cell, Kind::Portal };
        }
        if (onNeighbor) return std::nullopt;
    }
    if (selected) {
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
    if (overworld() && endpoint->kind == LevelEditor::LinkEndpointKind::Portal) {
        return disconnectOverworldPortal(endpoint->cell);
    }
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

bool MakerSession::recolorSelectedLinkGroup(Vec3 color)
{
    const auto endpoint = selectedLinkEndpoint();
    if (!endpoint) return false;
    endStroke();
    if (overworld() && endpoint->kind == LevelEditor::LinkEndpointKind::Portal) {
        return recolorOverworldPortals(endpoint->cell, color);
    }
    editor_.clearStatus();
    const bool changed = editor_.recolorLinkGroup(*endpoint, color);
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
    if (overworld()) return newOverworldDraft(width, height);
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

bool MakerSession::newOverworldDraft(uint32_t width, uint32_t height)
{
    if (width < minDraftDimension || width > maxDraftDimension ||
        height < minDraftDimension || height > maxDraftDimension) {
        status_ = "Overworld screen width and height must be between " +
            std::to_string(minDraftDimension) + " and " +
            std::to_string(maxDraftDimension) + " tiles.";
        return false;
    }
    auto firstScreen = emptyOverworldComponent(width, height);
    std::ranges::fill(firstScreen.layers.front(), std::string(width, tileTypeToChar(TileType::Ground)));
    replaceOverworld({
        .layout = OverworldLayout {
            .screenWidth = width, .screenHeight = height,
            .screens = { { .id = 1, .file = "screen1.scr" } },
        },
        .definitions = { { .screen = 1, .definition = std::move(firstScreen) } },
    }, {});
    editor_.setActiveLayer(1);
    status_ = "Created overworld draft";
    return true;
}

const OverworldLayout& MakerSession::overworldLayout() const
{
    static const OverworldLayout empty;
    return overworld_ ? overworld_->layout : empty;
}

OverworldSlot MakerSession::currentScreenSlot() const
{
    const auto& screens = overworldLayout().screens;
    const auto found = std::ranges::find(screens, currentScreen_, &OverworldScreenSpec::id);
    return found != screens.end() ? found->slot : OverworldSlot {};
}

const Level::Definition* MakerSession::overworldDefinition(OverworldScreenId screen) const
{
    if (!overworld_) return nullptr;
    const auto found = std::ranges::find(overworld_->components, screen, &OverworldComponent::screen);
    return found != overworld_->components.end() ? found->definition.get() : nullptr;
}

void MakerSession::restoreCurrentOverworldScreen()
{
    if (!overworldDefinition(currentScreen_)) {
        currentScreen_ = overworld_->layout.screens.front().id;
        clearSelection();
    }
    editor_.loadOverworldDraft(*overworldDefinition(currentScreen_), currentScreen_);
    clearLinkSelection();
}

bool MakerSession::selectOverworldScreen(OverworldScreenId screen)
{
    if (!overworldDefinition(screen)) return false;
    if (screen != currentScreen_) {
        endStroke();
        clearSelection();
        const OverworldSlot previous = currentScreenSlot();
        const auto endpoint = selectedLinkEndpoint();
        currentScreen_ = screen;
        restoreCurrentOverworldScreen();
        if (endpoint && endpoint->kind == LevelEditor::LinkEndpointKind::Portal) {
            const OverworldSlot current = currentScreenSlot();
            selectedLinkEndpoint_ = *endpoint;
            selectedLinkEndpoint_->cell.x += (previous.x - current.x) * static_cast<int>(overworldLayout().screenWidth);
            selectedLinkEndpoint_->cell.y += (previous.y - current.y) * static_cast<int>(overworldLayout().screenHeight);
        }
    }
    status_ = "Selected overworld screen " + std::to_string(screen);
    return true;
}

void MakerSession::toggleNeighbors()
{
    endStroke();
    showNeighbors_ = !showNeighbors_;
}

void MakerSession::selectLevelSelectorBrush()
{
    if (!overworld()) return;
    setTool(Tool::Paint);
    cancelMove();
    placingSelector_ = true;
}

std::optional<MakerSession::OverworldCell> MakerSession::overworldCell(GridPosition3 cell, bool visibleOnly) const
{
    if (!overworld_ || cell.z < 0) return std::nullopt;
    const auto& layout = overworld_->layout;
    const int width = static_cast<int>(layout.screenWidth);
    const int height = static_cast<int>(layout.screenHeight);
    const auto origin = currentScreenSlot();
    const auto relativeX = floorDivide(cell.x, width);
    const auto relativeY = floorDivide(cell.y, height);
    if (visibleOnly && (relativeX < -1 || relativeX > 1 || relativeY < -1 || relativeY > 1)) return std::nullopt;
    const auto x = static_cast<int64_t>(origin.x) + relativeX;
    const auto y = static_cast<int64_t>(origin.y) + relativeY;
    if (!std::in_range<int>(x) || !std::in_range<int>(y)) return std::nullopt;
    const OverworldSlot slot { static_cast<int>(x), static_cast<int>(y) };
    const auto found = std::ranges::find(layout.screens, slot, &OverworldScreenSpec::slot);
    if (visibleOnly && !showNeighbors_ && found != layout.screens.end() && found->id != currentScreen_) return std::nullopt;
    return OverworldCell {
        .slot = slot,
        .local = { positiveRemainder(cell.x, width), positiveRemainder(cell.y, height), cell.z },
        .screen = found != layout.screens.end() ? found->id : 0,
    };
}

const Level::ScreenSelector* MakerSession::levelSelectorAt(GridPosition3 cell) const
{
    const auto address = overworldCell(cell);
    const auto* definition = address ? overworldDefinition(address->screen) : nullptr;
    if (!definition) return nullptr;
    const auto found = std::ranges::find(definition->selectors, address->local, &Level::ScreenSelector::cell);
    return found != definition->selectors.end() ? &*found : nullptr;
}

const Level::ScreenSelector* MakerSession::levelSelector(Level::SelectorRef reference) const
{
    const auto* definition = overworldDefinition(reference.screen);
    if (!definition) return nullptr;
    const auto found = std::ranges::find(definition->selectors, reference.id, &Level::ScreenSelector::id);
    return found != definition->selectors.end() ? &*found : nullptr;
}

std::optional<GridPosition3> MakerSession::levelSelectorCell(Level::SelectorRef reference, bool visibleOnly) const
{
    const auto* selector = levelSelector(reference);
    if (!selector) return std::nullopt;
    const auto& layout = overworldLayout();
    const auto screen = std::ranges::find(layout.screens, reference.screen, &OverworldScreenSpec::id);
    const OverworldSlot origin = currentScreenSlot();
    const int x = screen->slot.x - origin.x;
    const int y = screen->slot.y - origin.y;
    if (visibleOnly && (x < -1 || x > 1 || y < -1 || y > 1 ||
        (!showNeighbors_ && reference.screen != currentScreen_))) return std::nullopt;
    return GridPosition3 {
        selector->cell.x + x * static_cast<int>(layout.screenWidth),
        selector->cell.y + y * static_cast<int>(layout.screenHeight),
        selector->cell.z,
    };
}

std::optional<GridPosition3> MakerSession::selectedLevelSelectorSource() const
{
    return selectedLevelSelectorSource_ ? levelSelectorCell(*selectedLevelSelectorSource_, false) : std::nullopt;
}

void MakerSession::clearLevelSelectorSource()
{
    selectedLevelSelectorSource_.reset();
}

std::vector<MakerSession::SelectorConnection> MakerSession::levelSelectorConnections() const
{
    std::vector<SelectorConnection> connections;
    if (!overworld_) return connections;
    for (const auto& component : overworld_->components) {
        for (const auto& selector : component.definition->selectors) {
            if (selector.prerequisites.empty()) continue;
            const auto destination = levelSelectorCell({ component.screen, selector.id }, true);
            if (!destination) continue;
            for (const auto& prerequisite : selector.prerequisites) {
                if (const auto source = levelSelectorCell(prerequisite, true)) {
                    connections.push_back({ .source = *source, .destination = *destination });
                }
            }
        }
    }
    return connections;
}

bool MakerSession::dependsOnSelector(Level::SelectorRef selector, Level::SelectorRef prerequisite) const
{
    std::set<std::pair<uint32_t, uint32_t>> visited;
    std::vector pending { selector };
    while (!pending.empty()) {
        const auto candidate = pending.back();
        pending.pop_back();
        if (candidate == prerequisite) return true;
        if (!visited.emplace(candidate.screen, candidate.id).second) continue;
        if (const auto* entry = levelSelector(candidate)) {
            pending.insert(pending.end(), entry->prerequisites.begin(), entry->prerequisites.end());
        }
    }
    return false;
}

bool MakerSession::clickLevelSelectorConnection(GridPosition3 cell)
{
    if (tool_ != Tool::ConnectSelectors) return false;
    endStroke();
    const auto* selector = levelSelectorAt(cell);
    if (!selector) {
        clearLevelSelectorSource();
        status_ = "Choose a level selector to complete first.";
        return false;
    }
    const auto address = *overworldCell(cell);
    const Level::SelectorRef clicked { address.screen, selector->id };
    if (!selectedLevelSelectorSource_) {
        selectedLevelSelectorSource_ = clicked;
        status_ = "Choose the level selector to unlock.";
        return true;
    }
    const auto source = *selectedLevelSelectorSource_;
    if (source == clicked) {
        status_ = "Choose a different level selector to unlock.";
        return false;
    }

    auto definition = *overworldDefinition(clicked.screen);
    auto& prerequisites = std::ranges::find(definition.selectors, clicked.id, &Level::ScreenSelector::id)->prerequisites;
    const bool removed = std::erase(prerequisites, source) != 0;
    if (!removed) {
        if (dependsOnSelector(source, clicked)) {
            status_ = "That connection would create a cycle.";
            return false;
        }
        prerequisites.push_back(source);
    }
    auto before = *overworld_;
    (void)updateOverworldDefinition(clicked.screen, std::move(definition));
    if (clicked.screen == currentScreen_) restoreCurrentOverworldScreen();
    recordOverworldChange(std::move(before));
    status_ = removed ? "Removed level selector connection" : "Connected level selectors";
    return true;
}

void MakerSession::pruneSelectorConnections(const std::set<std::pair<OverworldScreenId, uint32_t>>& deleted)
{
    if (deleted.empty()) return;
    const auto isDeleted = [&](Level::SelectorRef reference) {
        return deleted.contains({ reference.screen, reference.id });
    };
    if (selectedLevelSelectorSource_ && isDeleted(*selectedLevelSelectorSource_)) clearLevelSelectorSource();
    bool refreshCurrent = false;
    for (auto& component : overworld_->components) {
        const auto referencesDeleted = [&](const Level::ScreenSelector& selector) {
            return std::ranges::any_of(selector.prerequisites, isDeleted);
        };
        if (!std::ranges::any_of(component.definition->selectors, referencesDeleted)) continue;
        auto definition = *component.definition;
        for (auto& selector : definition.selectors) std::erase_if(selector.prerequisites, isDeleted);
        component.definition = std::make_shared<const Level::Definition>(std::move(definition));
        refreshCurrent |= component.screen == currentScreen_;
    }
    if (refreshCurrent) restoreCurrentOverworldScreen();
}

void MakerSession::removeOverworldPlayerFromOtherScreens(OverworldScreenId screen)
{
    bool refreshCurrent = false;
    for (auto& component : overworld_->components) {
        if (component.screen == screen || !hasOverworldPlayer(*component.definition)) continue;
        auto definition = *component.definition;
        if (LevelEditor::removeOverworldPlayer(definition)) {
            component.definition = std::make_shared<const Level::Definition>(std::move(definition));
            refreshCurrent |= component.screen == currentScreen_;
        }
    }
    if (refreshCurrent) restoreCurrentOverworldScreen();
}

const Level::Portal* MakerSession::overworldPortalAt(GridPosition3 cell) const
{
    const auto address = overworldCell(cell, false);
    const auto* definition = address ? overworldDefinition(address->screen) : nullptr;
    if (!definition) return nullptr;
    const auto found = std::ranges::find(definition->portals, address->local, &Level::Portal::cell);
    return found != definition->portals.end() ? &*found : nullptr;
}

bool MakerSession::hasOverworldPortal(GridPosition3 cell) const
{
    const auto address = overworldCell(cell, false);
    const auto* definition = address ? overworldDefinition(address->screen) : nullptr;
    if (!definition || std::cmp_greater_equal(address->local.z, definition->layers.size())) return false;
    const auto [x, y, z] = address->local;
    const auto tile = charToTileType(definition->layers[z][y][x]).value_or(TileType::Air);
    if (tileTypeIsPortal(tile)) return true;
    const auto plate = std::ranges::find(definition->plates, address->local, &Level::Plate::cell);
    return plate != definition->plates.end() && tileTypeIsPortal(plate->tile);
}

std::vector<GridPosition3> MakerSession::overworldPortalPeers(GridPosition3 cell) const
{
    std::vector<GridPosition3> peers;
    const auto address = overworldCell(cell, false);
    const auto* portal = overworldPortalAt(cell);
    if (!portal) return peers;
    const auto& layout = overworldLayout();
    const OverworldSlot origin = currentScreenSlot();
    for (const auto& screen : layout.screens) {
        if (portal->overworldPair == 0 && screen.id != address->screen) continue;
        const int offsetX = (screen.slot.x - origin.x) * static_cast<int>(layout.screenWidth);
        const int offsetY = (screen.slot.y - origin.y) * static_cast<int>(layout.screenHeight);
        for (const auto& candidate : overworldDefinition(screen.id)->portals) {
            if (candidate.overworldPair != portal->overworldPair) continue;
            if (portal->overworldPair == 0 && (candidate.scope != portal->scope ||
                !LevelEditor::sameLinkColor(candidate.color, portal->color))) continue;
            const GridPosition3 position { candidate.cell.x + offsetX, candidate.cell.y + offsetY, candidate.cell.z };
            if (position != cell) peers.push_back(position);
        }
    }
    return peers;
}

Vec3 MakerSession::unusedOverworldPortalColor(std::optional<Vec3> reserved) const
{
    const auto key = [](Vec3 color) {
        const auto byte = [](float value) { return static_cast<uint32_t>(std::lround(value * 255.0f)); };
        return (byte(color.x) << 16) | (byte(color.y) << 8) | byte(color.z);
    };
    std::set<uint32_t> used;
    if (reserved) used.insert(key(*reserved));
    for (const auto& component : overworld_->components) {
        for (const auto& portal : component.definition->portals) used.insert(key(portal.color));
    }
    constexpr uint32_t rgbMask = 0xffffff;
    uint32_t rgb = 0x53b6d1;
    while (used.contains(rgb)) rgb = (rgb + 0x9e3779) & rgbMask;
    return {
        static_cast<float>((rgb >> 16) & 0xff) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
        static_cast<float>(rgb & 0xff) / 255.0f,
    };
}

bool MakerSession::connectOverworldPortals(GridPosition3 first, GridPosition3 second)
{
    if (!overworldPortalPeers(first).empty() || !overworldPortalPeers(second).empty()) {
        status_ = "Disconnect an assigned portal before making a new pair.";
        return false;
    }
    const auto firstAddress = overworldCell(first, false);
    const auto secondAddress = overworldCell(second, false);
    if (!firstAddress || !secondAddress) return false;
    uint32_t pair = 0;
    if (firstAddress->screen != secondAddress->screen) {
        std::set<uint32_t> used;
        for (const auto& component : overworld_->components) {
            for (const auto& portal : component.definition->portals) used.insert(portal.overworldPair);
        }
        pair = 1;
        while (used.contains(pair)) ++pair;
    }
    const Vec3 color = unusedOverworldPortalColor();
    const std::array updates {
        Level::Portal { .cell = first, .color = color, .overworldPair = pair },
        Level::Portal { .cell = second, .color = color, .overworldPair = pair },
    };
    return updateOverworldPortals(updates, "Paired portals.");
}

bool MakerSession::disconnectOverworldPortal(GridPosition3 cell)
{
    const auto peers = overworldPortalPeers(cell);
    if (peers.empty()) return false;
    const Vec3 color = unusedOverworldPortalColor();
    std::vector<Level::Portal> updates { { .cell = cell, .color = color } };
    if (overworldPortalAt(cell)->overworldPair != 0 && peers.size() == 1) {
        updates.push_back({ .cell = peers.front(), .color = unusedOverworldPortalColor(color) });
    }
    return updateOverworldPortals(updates, "Unpaired portal.");
}

bool MakerSession::recolorOverworldPortals(GridPosition3 cell, Vec3 color)
{
    const auto valid = [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; };
    if (!valid(color.x) || !valid(color.y) || !valid(color.z)) return false;
    const auto* portal = overworldPortalAt(cell);
    if (!portal || LevelEditor::sameLinkColor(portal->color, color)) return false;
    auto members = overworldPortalPeers(cell);
    members.push_back(cell);
    std::vector<Level::Portal> updates;
    for (const auto member : members) {
        const auto address = *overworldCell(member, false);
        const auto& portals = overworldDefinition(address.screen)->portals;
        for (const auto& existing : portals) {
            if (existing.scope != portal->scope || !LevelEditor::sameLinkColor(existing.color, color)) continue;
            const GridPosition3 relative {
                existing.cell.x + member.x - address.local.x,
                existing.cell.y + member.y - address.local.y,
                existing.cell.z,
            };
            if (std::ranges::find(members, relative) == members.end()) {
                status_ = "That color is already used by other portals.";
                return false;
            }
        }
        Level::Portal updated = *overworldPortalAt(member);
        updated.cell = member;
        updated.color = color;
        updates.push_back(updated);
    }
    return updateOverworldPortals(updates, "Updated portal color.");
}

bool MakerSession::updateOverworldPortals(std::span<const Level::Portal> updates, std::string status)
{
    endStroke();
    auto edited = *overworld_;
    bool refreshCurrent = false;
    for (Level::Portal portal : updates) {
        const auto address = overworldCell(portal.cell, false);
        if (!address || address->screen == 0) return false;
        const auto component = std::ranges::find(edited.components, address->screen, &OverworldComponent::screen);
        auto definition = std::make_shared<Level::Definition>(*component->definition);
        portal.cell = address->local;
        const auto found = std::ranges::find(definition->portals, portal.cell, &Level::Portal::cell);
        if (found != definition->portals.end()) {
            *found = portal;
        } else {
            definition->portals.push_back(portal);
            std::ranges::sort(definition->portals, {}, [](const Level::Portal& record) {
                return std::array { record.cell.z, record.cell.y, record.cell.x };
            });
        }
        component->definition = std::move(definition);
        refreshCurrent |= address->screen == currentScreen_;
    }
    auto before = std::exchange(*overworld_, std::move(edited));
    if (refreshCurrent) {
        const auto selection = selectedLinkEndpoint_;
        restoreCurrentOverworldScreen();
        selectedLinkEndpoint_ = selection;
    }
    recordOverworldChange(std::move(before));
    status_ = std::move(status);
    return true;
}

bool MakerSession::updateOverworldDefinition(OverworldScreenId screen, Level::Definition definition)
{
    auto& components = overworld_->components;
    const auto found = std::ranges::find(components, screen, &OverworldComponent::screen);
    if (found == components.end() || *found->definition == definition) return false;
    found->definition = std::make_shared<const Level::Definition>(std::move(definition));
    return true;
}

void MakerSession::recordOverworldChange(OverworldState before)
{
    constexpr auto limit = LevelEditor::HistoryLimits {}.maxRecords;
    overworld_->revision = ++nextOverworldRevision_;
    overworldUndo_.push_back(std::move(before));
    if (overworldUndo_.size() > limit) overworldUndo_.erase(overworldUndo_.begin());
    overworldRedo_.clear();
    unsavedChanges_ = true;
}

OverworldDraftOverride MakerSession::overworldDraft() const
{
    OverworldDraftOverride result { .layout = overworld_->layout };
    result.definitions.reserve(overworld_->components.size());
    for (const auto& component : overworld_->components) {
        result.definitions.push_back({ .screen = component.screen, .definition = *component.definition });
    }
    return result;
}

void MakerSession::replaceOverworld(OverworldDraftOverride draft, std::string name)
{
    OverworldMap::validateDraft(draft);
    OverworldState state { .layout = std::move(*draft.layout) };
    state.components.reserve(draft.definitions.size());
    OverworldScreenId preferredScreen = state.layout.screens.front().id;
    for (auto& component : draft.definitions) {
        if (hasOverworldPlayer(component.definition)) preferredScreen = component.screen;
        component.definition.character = CharacterType::Lorekeeper;
        state.components.push_back({
            .screen = component.screen,
            .definition = std::make_shared<const Level::Definition>(std::move(component.definition)),
        });
    }
    endStroke();
    clearSelection();
    overworld_ = std::move(state);
    overworldUndo_.clear();
    overworldRedo_.clear();
    savedOverworldRevision_ = nextOverworldRevision_ = 0;
    currentScreen_ = preferredScreen;
    editor_.setHistoryLimits({ .maxRecords = 0 });
    restoreCurrentOverworldScreen();
    setTool(Tool::Paint);
    placingSelector_ = false;
    editor_.setSelectedTile(TileType::Wall);
    draftName_ = std::move(name);
    unsavedChanges_ = false;
    linkLayerIsolated_ = false;
}

bool MakerSession::attachSelectorLevel(GridPosition3 cell, std::optional<std::string> draftName)
{
    endStroke();
    if (draftName && draftName->empty()) {
        status_ = "Choose a saved Maker level.";
        return false;
    }
    const auto address = overworldCell(cell);
    const auto* original = address ? overworldDefinition(address->screen) : nullptr;
    if (!original) return false;
    auto definition = *original;
    const auto selector = std::ranges::find(definition.selectors, address->local, &Level::ScreenSelector::cell);
    if (selector == definition.selectors.end()) return false;
    if (selector->draftName == draftName && !selector->target) return true;
    auto before = *overworld_;
    selector->target.reset();
    selector->draftName = draftName;
    (void)updateOverworldDefinition(address->screen, std::move(definition));
    if (address->screen == currentScreen_) restoreCurrentOverworldScreen();
    recordOverworldChange(std::move(before));
    status_ = draftName ? "Attached " + *draftName : "Removed level attachment";
    return true;
}

bool MakerSession::editOverworldLectern(GridPosition3 cell, std::string text)
{
    endStroke();
    const auto address = overworldCell(cell);
    if (!address || address->screen == 0) return false;
    auto definition = *overworldDefinition(address->screen);
    const auto lectern = std::ranges::find(definition.lecterns, address->local, &Level::Lectern::cell);
    if (lectern == definition.lecterns.end()) return false;
    if (lectern->text == text) return true;
    auto before = *overworld_;
    lectern->text = std::move(text);
    (void)updateOverworldDefinition(address->screen, std::move(definition));
    if (address->screen == currentScreen_) restoreCurrentOverworldScreen();
    recordOverworldChange(std::move(before));
    status_ = "Updated lectern text";
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
    if (overworld()) {
        status_ = "Overworld screens share one size. Paint beyond an edge to add a screen.";
        return false;
    }
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
    if (!selection()) clearSelection();
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
        if (overworld()) {
            store.saveOverworld(savedName, overworldDraft(), overwrite);
            savedOverworldRevision_ = overworld_->revision;
        } else {
            store.save(savedName, editor_.documentDefinition(), overwrite);
        }
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
        if (overworld()) {
            replaceOverworld(store.loadOverworld(name), std::string(name));
        } else {
            replaceDraft(store.load(name), std::string(name));
        }
        status_ = "Loaded draft";
        return true;
    } catch (const std::exception& error) {
        status_ = error.what();
        return false;
    }
}

bool MakerSession::setCameraAngles(std::optional<CameraAngles> angles)
{
    const auto previous = editor_.cameraAngles();
    if (previous == angles) return false;
    if (!angles || angles->valid()) endStroke();
    editor_.setCameraAngles(angles);
    status_ = editor_.status();
    const bool changed = editor_.cameraAngles() != previous;
    if (changed) updateUnsavedChanges();
    return changed;
}

void MakerSession::replaceDraft(Level::Definition definition, std::string name)
{
    editor_.loadDraft(std::move(definition));
    draftName_ = std::move(name);
    unsavedChanges_ = false;
    strokeChanged_ = false;
    clearSelection();
    clearLinkSelection();
    linkLayerIsolated_ = false;
    lastCell_.reset();
    visited_.clear();
}

std::optional<Level> MakerSession::buildPlaytestLevel()
{
    if (overworld()) {
        auto map = buildOverworldPlaytestMap();
        return map ? std::optional { map->level() } : std::nullopt;
    }
    endStroke();
    status_.clear();
    try {
        return editor_.documentToLevel();
    } catch (const std::runtime_error& error) {
        status_ = error.what();
        return std::nullopt;
    }
}

std::optional<OverworldMap> MakerSession::buildOverworldPlaytestMap()
{
    endStroke();
    if (!overworld()) return std::nullopt;
    status_.clear();
    try {
        return OverworldMap::load({}, overworldDraft());
    } catch (const std::runtime_error& error) {
        status_ = error.what();
        return std::nullopt;
    }
}

std::optional<GridPosition3> MakerSession::previewCell(GridPosition cell) const
{
    if (overworld()) {
        const GridPosition3 target { cell.x, cell.y, static_cast<int>(editor_.activeLayer()) };
        return overworldCell(target) ? std::optional { target } : std::nullopt;
    }
    if (cell.x < 0 || cell.y < 0 ||
        cell.x >= static_cast<int>(editor_.documentWidth()) ||
        cell.y >= static_cast<int>(editor_.documentHeight())) {
        return std::nullopt;
    }
    return GridPosition3 { cell.x, cell.y, static_cast<int>(editor_.activeLayer()) };
}

bool MakerSession::beginStroke(GridPosition cell)
{
    return beginStroke({ cell.x, cell.y, static_cast<int>(editor_.activeLayer()) }, StrokeMode::Brush);
}

std::optional<GridPosition3> MakerSession::previewCell(GridPosition3 cell, StrokeMode mode) const
{
    if (overworld()) {
        const auto address = overworldCell(cell);
        if (!address) return std::nullopt;
        const bool deleting = mode == StrokeMode::Delete || (mode == StrokeMode::Brush && tool_ == Tool::Erase);
        if (address->screen == 0) {
            return deleting
                ? std::nullopt : std::optional { GridPosition3 { cell.x, cell.y, static_cast<int>(editor_.activeLayer()) } };
        }
        const auto& definition = *overworldDefinition(address->screen);
        const bool missingLayer = editor_.activeLayer() >= definition.layers.size();
        if (missingLayer && ((mode == StrokeMode::Brush && deleting) ||
                (mode != StrokeMode::Brush && editor_.layerLocked()))) return std::nullopt;
        if (mode == StrokeMode::Brush) {
            return GridPosition3 { cell.x, cell.y, static_cast<int>(editor_.activeLayer()) };
        }
        const auto resolve = [&](const LevelEditor& editor) -> std::optional<GridPosition3> {
            const bool selector = deleting && !editor.layerLocked() &&
                std::ranges::find(editor.selectors(), address->local, &Level::ScreenSelector::cell) !=
                    editor.selectors().end();
            const auto local = selector ? address->local : editor.resolveEditTarget(address->local, deleting, !deleting);
            if (local.z < 0 || std::cmp_greater_equal(local.z, editor.documentDepth())) return std::nullopt;
            return GridPosition3 { cell.x, cell.y, local.z };
        };
        if (address->screen == currentScreen_) return resolve(editor_);
        LevelEditor neighbor;
        neighbor.loadOverworldDraft(definition, address->screen);
        neighbor.setLayerLocked(editor_.layerLocked());
        neighbor.setActiveLayer(static_cast<int>(editor_.activeLayer()));
        neighbor.setSelectedTile(editor_.selectedTile());
        return resolve(neighbor);
    }
    const auto brushCell = previewCell(xy(cell));
    if (!brushCell || mode == StrokeMode::Brush) return brushCell;
    cell = editor_.resolveEditTarget(cell, mode == StrokeMode::Delete, mode == StrokeMode::Replace);
    if (cell.z < 0 || std::cmp_greater_equal(cell.z, editor_.documentDepth())) return std::nullopt;
    return cell;
}

bool MakerSession::beginStroke(GridPosition3 cell, StrokeMode mode)
{
    if ((mode == StrokeMode::Brush && tool_ != Tool::Paint && tool_ != Tool::Erase) ||
        !previewCell(cell, mode) || strokeActive()) {
        return false;
    }
    if (overworld_) {
        overworldBeforeStroke_ = *overworld_;
        overworldVisited_.clear();
        status_.clear();
    } else {
        if (!editor_.beginStroke()) return false;
        visited_.assign(static_cast<std::size_t>(editor_.documentWidth()) *
                editor_.documentHeight(), false);
    }
    strokeMode_ = mode;
    strokeAnchor_ = xy(cell);
    (void)continueStroke(cell, false);
    return true;
}

bool MakerSession::continueStroke(std::optional<GridPosition> cell)
{
    return continueStroke(cell ? std::optional { GridPosition3 {
        cell->x, cell->y, static_cast<int>(editor_.activeLayer()) } } : std::nullopt, false);
}

bool MakerSession::continueStroke(std::optional<GridPosition3> cell, bool straight)
{
    if (!strokeActive()) {
        return false;
    }
    if (!cell || !previewCell(xy(*cell))) {
        lastCell_.reset();
        return false;
    }
    const GridPosition column = straight
        ? EditorInteraction::constrainToAxis(strokeAnchor_, xy(*cell)) : xy(*cell);
    if (lastCell_ == column) {
        return false;
    }

    bool changed = false;
    if (lastCell_) {
        for (const GridPosition crossed : EditorInteraction::gridLine(*lastCell_, column)) {
            changed = visitCell({ crossed.x, crossed.y, cell->z }) || changed;
        }
    } else {
        changed = visitCell({ column.x, column.y, cell->z });
    }
    lastCell_ = column;
    strokeChanged_ |= changed;
    return changed;
}

bool MakerSession::endStroke()
{
    lastCell_.reset();
    if (overworld_) {
        auto before = std::exchange(overworldBeforeStroke_, std::nullopt);
        const bool changed = std::exchange(strokeChanged_, false);
        if (before && changed) recordOverworldChange(std::move(*before));
        return changed;
    }
    strokeChanged_ = false;
    const bool changed = editor_.endStroke();
    if (changed) updateUnsavedChanges();
    return changed;
}

void MakerSession::updateUnsavedChanges()
{
    if (overworld_) {
        auto before = *overworld_;
        if (updateOverworldDefinition(currentScreen_, editor_.documentDefinition())) {
            recordOverworldChange(std::move(before));
        }
        return;
    }
    unsavedChanges_ = editor_.dirty();
}

void MakerSession::commitRegionEdit()
{
    if (!overworld_) {
        updateUnsavedChanges();
        return;
    }

    auto before = *overworld_;
    const auto& original = *overworldDefinition(currentScreen_);
    auto edited = editor_.documentDefinition();
    std::set<uint32_t> remainingIds;
    for (const auto& selector : edited.selectors) remainingIds.insert(selector.id);
    std::set<std::pair<OverworldScreenId, uint32_t>> deleted;
    for (const auto& selector : original.selectors) {
        if (!remainingIds.contains(selector.id)) deleted.emplace(currentScreen_, selector.id);
    }
    if (!updateOverworldDefinition(currentScreen_, std::move(edited))) return;

    pruneSelectorConnections(deleted);
    if (hasOverworldPlayer(*overworldDefinition(currentScreen_))) {
        removeOverworldPlayerFromOtherScreens(currentScreen_);
    }
    recordOverworldChange(std::move(before));
}

bool MakerSession::visitCell(GridPosition3 cell)
{
    if (overworld_) return visitOverworldCell(cell);
    const std::size_t index = static_cast<std::size_t>(cell.y) *
        editor_.documentWidth() + static_cast<std::size_t>(cell.x);
    if (visited_[index]) {
        return false;
    }
    const auto target = previewCell(cell, strokeMode_);
    if (!target) return false;
    visited_[index] = true;
    editor_.clearStatus();
    status_.clear();
    const bool deleting = strokeMode_ == StrokeMode::Delete ||
        (strokeMode_ == StrokeMode::Brush && tool_ == Tool::Erase);
    const bool changed = deleting ? editor_.eraseCell(*target) : editor_.paintCell(*target);
    if (!changed) {
        status_ = editor_.status();
    }
    return changed;
}

bool MakerSession::visitOverworldCell(GridPosition3 cell)
{
    const auto target = previewCell(cell, strokeMode_);
    if (!target || !overworldVisited_.emplace(cell.x, cell.y).second) return false;
    auto address = *overworldCell(*target);
    auto& layout = overworld_->layout;
    const bool creating = address.screen == 0;
    if (creating) {
        int minimumX = address.slot.x, maximumX = address.slot.x;
        int minimumY = address.slot.y, maximumY = address.slot.y;
        OverworldScreenId latest = 0;
        std::size_t depth = 2;
        for (const auto& screen : layout.screens) {
            minimumX = std::min(minimumX, screen.slot.x);
            maximumX = std::max(maximumX, screen.slot.x);
            minimumY = std::min(minimumY, screen.slot.y);
            maximumY = std::max(maximumY, screen.slot.y);
            latest = std::max(latest, screen.id);
            depth = std::max(depth, overworldDefinition(screen.id)->layers.size());
        }
        const auto width = static_cast<int64_t>(maximumX) - minimumX + 1;
        const auto height = static_cast<int64_t>(maximumY) - minimumY + 1;
        if (width > OverworldLayout::maxSlotSpan || height > OverworldLayout::maxSlotSpan ||
            static_cast<uint64_t>(width * height) * layout.screenWidth * layout.screenHeight * depth >
                OverworldLayout::maxComposedCells ||
            latest == std::numeric_limits<OverworldScreenId>::max()) {
            status_ = "The overworld cannot expand beyond the map size limit.";
            return false;
        }
        address.screen = latest + 1;
    }
    std::optional<LevelEditor> otherEditor;
    if (address.screen != currentScreen_) {
        otherEditor.emplace();
        otherEditor->enableExplicitLinkEditing();
        otherEditor->setHistoryLimits({ .maxRecords = 0 });
        const auto* current = overworldDefinition(currentScreen_);
        otherEditor->loadOverworldDraft(creating
            ? emptyOverworldComponent(layout.screenWidth, layout.screenHeight,
                std::max<std::size_t>(2, current->layers.size()), current->waterLayer)
            : *overworldDefinition(address.screen), address.screen);
        otherEditor->setSelectedTile(editor_.selectedTile());
        otherEditor->rotateSelectedTile(static_cast<int>(editor_.selectedTileQuarterTurns()));
    }
    LevelEditor& editor = otherEditor ? *otherEditor : editor_;
    editor.clearStatus();
    const bool deleting = strokeMode_ == StrokeMode::Delete ||
        (strokeMode_ == StrokeMode::Brush && tool_ == Tool::Erase);
    const bool selectorPlacement = !deleting && placingSelector_;
    bool changed = false;
    std::optional<Level::SelectorRef> deletedSelector;
    if (deleting) {
        const auto& selectors = editor.selectors();
        const auto selector = std::ranges::find(selectors, address.local, &Level::ScreenSelector::cell);
        if (selector != selectors.end()) {
            deletedSelector = Level::SelectorRef { address.screen, selector->id };
            (void)editor.selectSelector(static_cast<std::size_t>(selector - selectors.begin()));
            changed = editor.deleteSelectedSelector();
        } else {
            changed = editor.eraseCell(address.local);
        }
    } else if (selectorPlacement) {
        changed = editor.placeSelector(address.local);
    } else {
        changed = editor.paintCell(address.local);
    }

    if (!changed) {
        if (!editor.status().empty()) status_ = editor.status();
        return false;
    }
    if (creating) {
        layout.screens.push_back({
            .id = address.screen,
            .file = "screen" + std::to_string(address.screen) + ".scr",
            .slot = address.slot,
        });
        overworld_->components.push_back({
            .screen = address.screen,
            .definition = std::make_shared<const Level::Definition>(editor.documentDefinition()),
        });
        status_ = "Created overworld screen " + std::to_string(address.screen);
    } else if (!updateOverworldDefinition(address.screen, editor.documentDefinition())) {
        return false;
    }
    if (deletedSelector) pruneSelectorConnections({ { deletedSelector->screen, deletedSelector->id } });

    if (!deleting && !selectorPlacement && editor.selectedTile() == TileType::Player) {
        removeOverworldPlayerFromOtherScreens(address.screen);
    }
    return true;
}

} // namespace sokoban
