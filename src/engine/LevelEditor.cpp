#include "engine/LevelEditor.hpp"

#include "engine/OverworldMapEditor.hpp"

#include "engine/LevelCatalog.hpp"
#include "engine/OverworldMap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace sokoban {
namespace {

std::filesystem::path normalizedAbsolutePath(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

bool pathStartsWith(const std::filesystem::path& path, const std::filesystem::path& root)
{
    const std::filesystem::path normalizedPath = normalizedAbsolutePath(path);
    const std::filesystem::path normalizedRoot = normalizedAbsolutePath(root);
    auto pathIt = normalizedPath.begin();
    auto rootIt = normalizedRoot.begin();

    for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt) {
        if (pathIt == normalizedPath.end() || *pathIt != *rootIt) {
            return false;
        }
    }

    return true;
}

std::optional<int> parseNumberedName(std::string_view value, std::string_view prefix, std::string_view suffix = {})
{
    if (!value.starts_with(prefix) || value.size() < prefix.size() + suffix.size()) {
        return std::nullopt;
    }
    if (!suffix.empty() && !value.ends_with(suffix)) {
        return std::nullopt;
    }

    const size_t numberStart = prefix.size();
    const size_t numberEnd = value.size() - suffix.size();
    if (numberStart == numberEnd) {
        return std::nullopt;
    }

    int number = 0;
    const char* begin = value.data() + numberStart;
    const char* end = value.data() + numberEnd;
    const auto result = std::from_chars(begin, end, number);
    if (result.ec != std::errc {} || result.ptr != end || number < 0) {
        return std::nullopt;
    }

    return number;
}

std::filesystem::path levelDirectoryPath(const std::filesystem::path& root, int levelIndex)
{
    return root / ("level" + std::to_string(levelIndex));
}

std::filesystem::path screenFilePath(const std::filesystem::path& levelDirectory, int screenIndex)
{
    return levelDirectory / ("screen" + std::to_string(screenIndex) + ".scr");
}

void writeScreenRows(
    const std::filesystem::path& path,
    const std::vector<std::string>& rows)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot write screen " + path.string());
    }
    for (const std::string& row : rows) {
        file << row << '\n';
    }
    file.close();
    if (!file) {
        throw std::runtime_error("cannot write screen " + path.string());
    }
}

template <typename Mutate>
void rewriteOverworldSelectors(
    const std::filesystem::path& root,
    Mutate&& mutate)
{
    auto rewriteFile = [&](const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("cannot read overworld " + path.string());
        }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
        Level::Definition definition =
            Level::parseDefinition(lines, path.string());
        mutate(definition.selectors);
        writeScreenRows(path, Level::serializeDefinition(definition));
    };

    const std::filesystem::path layoutPath = root / "overworld/layout.json";
    if (std::filesystem::is_regular_file(layoutPath)) {
        const OverworldLayout layout = loadOverworldLayout(layoutPath);
        for (const OverworldScreenSpec& screen : layout.screens) {
            rewriteFile(root / "overworld" / screen.file);
        }
        return;
    }

    const std::filesystem::path path = root / "overworld.scr";
    if (std::filesystem::is_regular_file(path)) {
        rewriteFile(path);
    }
}

} // namespace

void LevelEditor::initialize(
    const std::filesystem::path& sourceLevelRoot,
    const std::filesystem::path& runtimeLevelRoot,
    int currentLevel,
    int currentScreen)
{
    document_.sourceLevelRoot = sourceLevelRoot;
    document_.runtimeLevelRoot = runtimeLevelRoot;
    document_.browserRoot = sourceLevelRoot;

    const std::filesystem::path currentSourcePath = document_.browserRoot /
        ("level" + std::to_string(currentLevel)) /
        ("screen" + std::to_string(currentScreen) + ".scr");
    if (std::filesystem::exists(currentSourcePath)) {
        (void)loadDocument(currentSourcePath, false);
    } else {
        newDocument(document_.requestedWidth, document_.requestedHeight, false);
    }
    document_.playingDraft = false;
    document_.editingDocument = false;
    editHistory_.clear();
    drafts_.clear();
    pendingMove_.reset();
}

void LevelEditor::setPlayingDraft(bool playingDraft)
{
    document_.playingDraft = playingDraft;
    if (!playingDraft) {
        draftOverworldMap_.reset();
    }
    if (playingDraft) {
        document_.editingDocument = false;
        pendingMove_.reset();
    }
}

bool LevelEditor::playingDraft() const
{
    return document_.playingDraft;
}

void LevelEditor::setEditingDocument(bool editingDocument)
{
    document_.editingDocument = editingDocument;
    if (editingDocument) {
        document_.playingDraft = false;
    } else {
        pendingMove_.reset();
    }
}

bool LevelEditor::editingDocument() const
{
    return document_.editingDocument;
}

void LevelEditor::setShowOverworldNeighbors(bool show)
{
    showOverworldNeighbors_ = show;
}

bool LevelEditor::showOverworldNeighbors() const
{
    return showOverworldNeighbors_;
}

void LevelEditor::markDraftSolved()
{
    document_.status = "Draft solved.";
}

void LevelEditor::setRequestedSize(int width, int height)
{
    document_.requestedWidth = std::max(width, 1);
    document_.requestedHeight = std::max(height, 1);
}

int LevelEditor::requestedWidth() const
{
    return document_.requestedWidth;
}

int LevelEditor::requestedHeight() const
{
    return document_.requestedHeight;
}

void LevelEditor::setActiveLayer(int layer)
{
    const int lastLayer = std::max(static_cast<int>(document_.layers.size()) - 1, 0);
    document_.activeLayer = std::clamp(layer, 0, lastLayer);
}

void LevelEditor::setWaterLayer(std::optional<uint32_t> layer)
{
    if (layer && *layer >= document_.layers.size()) {
        document_.status = "Water layer must refer to an existing layer.";
        return;
    }
    if (document_.waterLayer == layer) {
        return;
    }

    const DocumentSnapshot before = captureDocumentSnapshot();
    document_.waterLayer = layer;
    document_.dirty = true;
    document_.status = layer
        ? "Water enabled on layer " + std::to_string(*layer + 1) + "."
        : "Water disabled.";
    recordDocumentChange(before);
}

void LevelEditor::setLayerLocked(bool locked)
{
    document_.layerLocked = locked;
}

void LevelEditor::setSelectedTile(TileType tile)
{
    document_.selectedTile = tile;
    document_.tool = Tool::Tiles;
}

void LevelEditor::setTool(Tool tool)
{
    document_.tool = tool;
}

void LevelEditor::setSelectedDecorationModel(std::string modelName)
{
    document_.selectedDecorationModel = std::move(modelName);
    document_.tool = Tool::Decorations;
}

bool LevelEditor::placeSelector(GridPosition3 cell)
{
    if (!editingOverworld()) {
        document_.status =
            "Screen selectors can only be placed in overworld.scr.";
        return false;
    }
    const auto existing = std::ranges::find(
        document_.selectors, cell, &Level::ScreenSelector::cell);
    if (existing != document_.selectors.end()) {
        document_.selectedSelector = static_cast<std::size_t>(
            std::distance(document_.selectors.begin(), existing));
        document_.tool = Tool::Selectors;
        document_.status =
            "Selected Selector " + std::to_string(existing->id) + ".";
        return true;
    }
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= static_cast<int>(documentWidth()) ||
        cell.y >= static_cast<int>(documentHeight()) ||
        cell.z >= static_cast<int>(documentDepth())) {
        document_.status = "Selectors must be placed on the level board.";
        return false;
    }

    uint32_t nextId = 1;
    for (const Level::ScreenSelector& selector : document_.selectors) {
        nextId = std::max(nextId, selector.id + 1);
    }
    const DocumentSnapshot before = captureDocumentSnapshot();
    document_.selectors.push_back({ .id = nextId, .cell = cell });
    std::ranges::sort(
        document_.selectors, {}, &Level::ScreenSelector::id);
    const auto added = std::ranges::find(
        document_.selectors, nextId, &Level::ScreenSelector::id);
    document_.selectedSelector = static_cast<std::size_t>(
        std::distance(document_.selectors.begin(), added));
    document_.tool = Tool::Selectors;
    document_.dirty = true;
    document_.status =
        "Placed Selector " + std::to_string(nextId) + ".";
    recordDocumentChange(before);
    return true;
}

bool LevelEditor::selectSelector(std::size_t index)
{
    if (index >= document_.selectors.size()) {
        return false;
    }
    document_.selectedSelector = index;
    document_.tool = Tool::Selectors;
    return true;
}

void LevelEditor::clearSelectorSelection()
{
    document_.selectedSelector.reset();
}

bool LevelEditor::updateSelectedSelectorTarget(
    std::optional<LevelLocation> target)
{
    if (!document_.selectedSelector ||
        *document_.selectedSelector >= document_.selectors.size()) {
        return false;
    }
    if (target && (target->level < 0 || target->screen < 0)) {
        document_.status = "Selector targets must not be negative.";
        return false;
    }
    if (target) {
        const std::optional<OverworldScreenId> current = overworldScreenId();
        const std::optional<OverworldScreenId> owner =
            selectorLevelOwner(target->level);
        if (current && owner && *owner != *current) {
            document_.status =
                "Puzzle level " + std::to_string(target->level) +
                " is already assigned to overworld screen " +
                std::to_string(*owner) + ".";
            return false;
        }
    }
    Level::ScreenSelector& selector =
        document_.selectors[*document_.selectedSelector];
    if (selector.target == target) {
        return true;
    }
    const DocumentSnapshot before = captureDocumentSnapshot();
    selector.target = target;
    document_.dirty = true;
    document_.status = target
        ? "Assigned Selector " + std::to_string(selector.id) +
            " to level " + std::to_string(target->level) +
            " screen " + std::to_string(target->screen) + "."
        : "Unassigned Selector " + std::to_string(selector.id) + ".";
    recordDocumentChange(before);
    return true;
}

bool LevelEditor::deleteSelectedSelector()
{
    if (!document_.selectedSelector ||
        *document_.selectedSelector >= document_.selectors.size()) {
        return false;
    }
    const DocumentSnapshot before = captureDocumentSnapshot();
    const uint32_t id =
        document_.selectors[*document_.selectedSelector].id;
    document_.selectors.erase(
        document_.selectors.begin() +
        static_cast<std::ptrdiff_t>(*document_.selectedSelector));
    if (document_.selectors.empty()) {
        document_.selectedSelector.reset();
    } else {
        document_.selectedSelector = std::min(
            *document_.selectedSelector,
            document_.selectors.size() - 1);
    }
    document_.dirty = true;
    document_.status = "Deleted Selector " + std::to_string(id) + ".";
    recordDocumentChange(before);
    return true;
}

void LevelEditor::selectDocument(const std::filesystem::path& path)
{
    document_.filePath = path;
    document_.status = "Selected " + path.string();
}

std::filesystem::path LevelEditor::draftKey(
    const std::filesystem::path& path)
{
    return path.empty()
        ? std::filesystem::path {}
        : normalizedAbsolutePath(path);
}

void LevelEditor::cacheActiveDraft()
{
    const std::filesystem::path key = draftKey(document_.loadedPath);
    if (key.empty()) {
        return;
    }
    if (!document_.dirty) {
        drafts_.erase(key);
        return;
    }
    drafts_.insert_or_assign(
        key,
        DraftState {
            .document = document_,
            .editHistory = editHistory_,
        });
}

bool LevelEditor::setBrowserRoot(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::exists(path, error) ||
        !std::filesystem::is_directory(path, error)) {
        document_.status = "Browser root does not exist or is not a directory.";
        return false;
    }

    document_.browserRoot = path;
    document_.status = "Browser root changed.";
    return true;
}

void LevelEditor::paintCell(GridPosition3 position)
{
    setCell(position, document_.selectedTile);
}

void LevelEditor::eraseCell(GridPosition3 position)
{
    setCell(position, TileType::Air);
}

const std::optional<LevelEditor::MoveObject>& LevelEditor::pendingMove() const
{
    return pendingMove_;
}

bool LevelEditor::beginMove(GridPosition3 source)
{
    const auto selector = std::ranges::find(
        document_.selectors, source, &Level::ScreenSelector::cell);
    if (selector != document_.selectors.end()) {
        document_.selectedSelector = static_cast<std::size_t>(
            std::distance(document_.selectors.begin(), selector));
        pendingMove_ = MoveObject {
            .kind = MoveObject::Kind::ScreenSelector,
            .source = source,
            .selectorId = selector->id,
        };
        document_.status =
            "Object selected. Hold Move Tile and click its destination.";
        return true;
    }

    const auto tileAt = [&](GridPosition3 cell) {
        if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
            cell.x >= static_cast<int>(documentWidth()) ||
            cell.y >= static_cast<int>(documentHeight()) ||
            cell.z >= static_cast<int>(documentDepth())) {
            return TileType::Air;
        }
        return charToTileType(
            document_.layers[static_cast<std::size_t>(cell.z)]
                [static_cast<std::size_t>(cell.y)]
                [static_cast<std::size_t>(cell.x)]).value_or(TileType::Air);
    };

    const TileType tile = tileAt(source);
    if (tile == TileType::Air) {
        pendingMove_.reset();
        document_.status = "Select an occupied object to move.";
        return false;
    }
    pendingMove_ = MoveObject {
        .kind = MoveObject::Kind::Tile,
        .source = source,
        .tile = tile,
    };
    document_.status =
        "Object selected. Hold Move Tile and click its destination.";
    return true;
}

void LevelEditor::cancelMove()
{
    if (pendingMove_) {
        document_.status = "Cancelled object move.";
    }
    pendingMove_.reset();
}

bool LevelEditor::moveObject(GridPosition3 destination)
{
    const std::optional<MoveObject> move = pendingMove_;
    pendingMove_.reset();
    if (!move) {
        document_.status = "Select an occupied object to move first.";
        return false;
    }
    if (move->source == destination) {
        document_.status = "Move destination must differ from the source.";
        return false;
    }

    const auto tileAt = [&](GridPosition3 cell) {
        if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
            cell.x >= static_cast<int>(documentWidth()) ||
            cell.y >= static_cast<int>(documentHeight()) ||
            cell.z >= static_cast<int>(documentDepth())) {
            return TileType::Air;
        }
        return charToTileType(
            document_.layers[static_cast<std::size_t>(cell.z)]
                [static_cast<std::size_t>(cell.y)]
                [static_cast<std::size_t>(cell.x)]).value_or(TileType::Air);
    };
    const auto selectorAt = [&](GridPosition3 cell) {
        return std::ranges::find(
            document_.selectors, cell, &Level::ScreenSelector::cell);
    };

    if (move->kind == MoveObject::Kind::ScreenSelector) {
        const auto selector = std::ranges::find(
            document_.selectors,
            move->selectorId,
            &Level::ScreenSelector::id);
        if (selector == document_.selectors.end()) {
            document_.status = "The selected object no longer exists.";
            return false;
        }
        if (destination.x < 0 || destination.y < 0 || destination.z < 0 ||
            destination.x >= static_cast<int>(documentWidth()) ||
            destination.y >= static_cast<int>(documentHeight()) ||
            destination.z >= static_cast<int>(documentDepth())) {
            document_.status = "The object must stay on the level board.";
            return false;
        }
        if (tileAt(destination) != TileType::Air ||
            selectorAt(destination) != document_.selectors.end()) {
            document_.status = "Move destination must be empty.";
            return false;
        }

        const DocumentSnapshot before = captureDocumentSnapshot();
        selector->cell = destination;
        document_.selectedSelector = static_cast<std::size_t>(
            std::distance(document_.selectors.begin(), selector));
        document_.dirty = true;
        document_.status = "Moved object.";
        recordDocumentChange(before);
        return true;
    }

    if (move->tile == TileType::Air) {
        document_.status = "The selected object no longer exists.";
        return false;
    }
    if (tileAt(destination) != TileType::Air ||
        selectorAt(destination) != document_.selectors.end()) {
        document_.status = "Move destination must be empty.";
        return false;
    }

    const DocumentSnapshot before = captureDocumentSnapshot();
    const std::size_t historySize = editHistory_.size();
    setCell(move->source, TileType::Air);
    const std::size_t afterEraseHistorySize = editHistory_.size();
    setCell(destination, move->tile);
    if (editHistory_.size() == afterEraseHistorySize) {
        applyDocumentSnapshot(before);
        editHistory_.resize(historySize);
        document_.status = "Tile cannot be moved to that destination.";
        return false;
    }

    editHistory_.resize(historySize);
    document_.status = "Moved object.";
    recordDocumentChange(before);
    return true;
}

GridPosition3 LevelEditor::resolveMoveTarget(GridPosition3 pickedCell) const
{
    if (pendingMove_) {
        return resolveEditTarget(pickedCell, false, false);
    }
    if (document_.layerLocked) {
        pickedCell.z = document_.activeLayer;
        return pickedCell;
    }
    if (std::ranges::find(
            document_.selectors,
            pickedCell,
            &Level::ScreenSelector::cell) != document_.selectors.end()) {
        return pickedCell;
    }
    return resolveEditTarget(pickedCell, true, false);
}

GridPosition3 LevelEditor::resolveEditTarget(
    GridPosition3 pickedCell,
    bool deleting,
    bool replaceLayer) const
{
    if (document_.layerLocked) {
        pickedCell.z = document_.activeLayer;
        return pickedCell;
    }

    auto topmostOccupiedLayer = [&]() -> std::optional<int> {
        for (int z = static_cast<int>(document_.layers.size()) - 1;
             z >= 0;
             --z) {
            if (pickedCell.y < 0 || pickedCell.x < 0 ||
                pickedCell.y >= static_cast<int>(
                    document_.layers[static_cast<size_t>(z)].size()) ||
                pickedCell.x >= static_cast<int>(
                    document_.layers[static_cast<size_t>(z)]
                        [static_cast<size_t>(pickedCell.y)].size())) {
                continue;
            }
            if (charToTileType(
                    document_.layers[static_cast<size_t>(z)]
                        [static_cast<size_t>(pickedCell.y)]
                        [static_cast<size_t>(pickedCell.x)])
                    .value_or(TileType::Air) != TileType::Air) {
                return z;
            }
        }
        return std::nullopt;
    };

    if (deleting) {
        pickedCell.z = topmostOccupiedLayer().value_or(pickedCell.z);
        return pickedCell;
    }
    if (replaceLayer) {
        return pickedCell;
    }

    const bool outsideDocument =
        pickedCell.x < 0 || pickedCell.y < 0 ||
        pickedCell.x >= static_cast<int>(documentWidth()) ||
        pickedCell.y >= static_cast<int>(documentHeight());
    if (outsideDocument) {
        pickedCell.z = 0;
        return pickedCell;
    }

    const std::optional<int> occupied = topmostOccupiedLayer();
    pickedCell.z = occupied ? *occupied + 1 : 0;
    return pickedCell;
}

GridPosition3 LevelEditor::resolveSelectorTarget(
    GridPosition3 pickedCell) const
{
    if (document_.layerLocked) {
        pickedCell.z = document_.activeLayer;
        return pickedCell;
    }

    const auto selector = std::ranges::find_if(
        document_.selectors,
        [&](const Level::ScreenSelector& candidate) {
            return candidate.cell.x == pickedCell.x &&
                candidate.cell.y == pickedCell.y;
        });
    if (selector != document_.selectors.end()) {
        return selector->cell;
    }

    for (int z = static_cast<int>(document_.layers.size()) - 1;
         z >= 0;
         --z) {
        if (pickedCell.x < 0 || pickedCell.y < 0 ||
            pickedCell.y >= static_cast<int>(
                document_.layers[static_cast<std::size_t>(z)].size()) ||
            pickedCell.x >= static_cast<int>(
                document_.layers[static_cast<std::size_t>(z)]
                    [static_cast<std::size_t>(pickedCell.y)].size())) {
            continue;
        }
        const TileType tile = charToTileType(
            document_.layers[static_cast<std::size_t>(z)]
                [static_cast<std::size_t>(pickedCell.y)]
                [static_cast<std::size_t>(pickedCell.x)])
                                  .value_or(TileType::Air);
        if (tile != TileType::Air) {
            pickedCell.z = z + 1;
            return pickedCell;
        }
    }
    pickedCell.z = 0;
    return pickedCell;
}

void LevelEditor::setCell(GridPosition3 position, TileType tile)
{
    if (document_.layers.empty() || position.z < 0) {
        return;
    }
    if (editingOverworld() && tile == TileType::End) {
        document_.status =
            "End tiles are not allowed in overworld screens.";
        return;
    }
    if (editingOverworld() &&
        (position.x < 0 || position.y < 0 ||
         position.x >= static_cast<int>(documentWidth()) ||
         position.y >= static_cast<int>(documentHeight()))) {
        document_.status =
            "Overworld screen dimensions are fixed by layout.json.";
        return;
    }
    const int oldHeight = static_cast<int>(documentHeight());
    const int oldWidth = static_cast<int>(documentWidth());
    const int prependColumns = std::max(-position.x, 0);
    const int prependRows = std::max(-position.y, 0);
    const int appendColumns = std::max(position.x - oldWidth + 1, 0);
    const int appendRows = std::max(position.y - oldHeight + 1, 0);
    const bool expandsDocument =
        prependColumns > 0 || prependRows > 0 ||
        appendColumns > 0 || appendRows > 0;
    if (tile == TileType::Air && expandsDocument) {
        return;
    }
    const int width = oldWidth + prependColumns + appendColumns;
    const int height = oldHeight + prependRows + appendRows;
    GridPosition3 translatedPosition {
        position.x + prependColumns,
        position.y + prependRows,
        position.z,
    };

    auto documentTileAt = [&](GridPosition3 cell) {
        if (cell.x < 0 ||
            cell.y < 0 ||
            cell.z < 0 ||
            cell.y >= height ||
            cell.x >= width ||
            cell.z >= static_cast<int>(document_.layers.size())) {
            return TileType::Air;
        }
        const int oldX = cell.x - prependColumns;
        const int oldY = cell.y - prependRows;
        if (oldX < 0 || oldY < 0 || oldX >= oldWidth || oldY >= oldHeight) {
            return cell.z == 0 ? TileType::Ground : TileType::Air;
        }
        return charToTileType(
            document_.layers[static_cast<size_t>(cell.z)]
                [static_cast<size_t>(oldY)]
                [static_cast<size_t>(oldX)]).value_or(TileType::Air);
    };

    if (tile == TileType::Ladder) {
        constexpr std::array<GridPosition, 4> offsets {
            GridPosition { 0, -1 },
            GridPosition { 1, 0 },
            GridPosition { 0, 1 },
            GridPosition { -1, 0 },
        };
        const bool adjacentGround = std::ranges::any_of(offsets, [&](GridPosition offset) {
            return documentTileAt({
                translatedPosition.x + offset.x,
                translatedPosition.y + offset.y,
                translatedPosition.z,
            }) == TileType::Ground;
        });
        if (!adjacentGround) {
            document_.status = "Ladders must be next to ground on the same layer.";
            return;
        }
    }

    const char character = tileTypeToChar(tile);
    if (tile == TileType::Air &&
        translatedPosition.z >= static_cast<int>(document_.layers.size())) {
        return;
    }
    if (!expandsDocument &&
        translatedPosition.z < static_cast<int>(document_.layers.size()) &&
        document_.layers[static_cast<size_t>(translatedPosition.z)]
            [static_cast<size_t>(translatedPosition.y)]
            [static_cast<size_t>(translatedPosition.x)] == character) {
        return;
    }

    const DocumentSnapshot before = captureDocumentSnapshot();
    if (expandsDocument) {
        for (size_t layerIndex = 0;
             layerIndex < document_.layers.size();
             ++layerIndex) {
            std::vector<std::string> expanded(
                static_cast<size_t>(height),
                std::string(
                    static_cast<size_t>(width),
                    tileTypeToChar(TileType::Air)));
            for (size_t y = 0; y < document_.layers[layerIndex].size(); ++y) {
                std::copy(
                    document_.layers[layerIndex][y].begin(),
                    document_.layers[layerIndex][y].end(),
                    expanded[y + static_cast<size_t>(prependRows)].begin() +
                        prependColumns);
            }
            document_.layers[layerIndex] = std::move(expanded);
        }
        document_.requestedWidth = width;
        document_.requestedHeight = height;
        for (Level::Decoration& decoration : document_.decorations) {
            decoration.position.x += static_cast<float>(prependColumns);
            decoration.position.y += static_cast<float>(prependRows);
        }
        for (Level::ScreenSelector& selector : document_.selectors) {
            selector.cell.x += prependColumns;
            selector.cell.y += prependRows;
        }
    }

    while (translatedPosition.z >= static_cast<int>(document_.layers.size())) {
        document_.layers.emplace_back(
            static_cast<size_t>(height),
            std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Air)));
    }

    if (tile == TileType::Player) {
        for (std::vector<std::string>& layer : document_.layers) {
            for (std::string& documentRow : layer) {
                std::ranges::replace(documentRow, tileTypeToChar(TileType::Player), tileTypeToChar(TileType::Air));
            }
        }
    }

    document_.layers[static_cast<size_t>(translatedPosition.z)]
        [static_cast<size_t>(translatedPosition.y)]
        [static_cast<size_t>(translatedPosition.x)] = character;
    document_.activeLayer = translatedPosition.z;
    document_.dirty = true;
    if (expandsDocument) {
        document_.status =
            "Expanded level to " + std::to_string(width) + " x " +
            std::to_string(height) + " and painted layer " +
            std::to_string(translatedPosition.z + 1) + ".";
    } else {
        document_.status = tile == TileType::Air
            ? "Deleted tile from layer " +
                std::to_string(translatedPosition.z + 1) + "."
            : "Painted layer " +
                std::to_string(translatedPosition.z + 1) + ".";
    }
    recordDocumentChange(before);
}

bool LevelEditor::placeDecoration(GridPosition3 surfaceCell)
{
    if (document_.selectedDecorationModel.empty()) {
        document_.status = "Select a registered decoration mesh first.";
        return false;
    }
    if (surfaceCell.x < 0 || surfaceCell.y < 0 || surfaceCell.z < 0 ||
        surfaceCell.x >= static_cast<int>(documentWidth()) ||
        surfaceCell.y >= static_cast<int>(documentHeight())) {
        document_.status = "Decorations must be placed over the level board.";
        return false;
    }

    const DocumentSnapshot before = captureDocumentSnapshot();
    document_.decorations.push_back({
        .model = document_.selectedDecorationModel,
        .position = {
            static_cast<float>(surfaceCell.x) + 0.5f,
            static_cast<float>(surfaceCell.y) + 0.5f,
            static_cast<float>(surfaceCell.z),
        },
    });
    document_.selectedDecoration = document_.decorations.size() - 1;
    document_.dirty = true;
    document_.status = "Placed decoration " +
        document_.selectedDecorationModel + ".";
    recordDocumentChange(before);
    return true;
}

void LevelEditor::cancelDecorationPlacement()
{
    document_.selectedDecorationModel.clear();
    document_.status = "Cancelled decoration placement.";
}

bool LevelEditor::selectDecoration(std::size_t index)
{
    if (index >= document_.decorations.size()) {
        return false;
    }
    document_.selectedDecoration = index;
    document_.tool = Tool::Decorations;
    return true;
}

void LevelEditor::clearDecorationSelection()
{
    document_.selectedDecoration.reset();
}

bool LevelEditor::updateSelectedDecoration(
    const Level::Decoration& decoration)
{
    if (!document_.selectedDecoration ||
        *document_.selectedDecoration >= document_.decorations.size()) {
        return false;
    }
    if (!validDecorationTransform(decoration)) {
        document_.status = "Decoration transform is invalid.";
        return false;
    }

    Level::Decoration& current =
        document_.decorations[*document_.selectedDecoration];
    if (current == decoration) {
        return false;
    }
    const DocumentSnapshot before = captureDocumentSnapshot();
    current = decoration;
    document_.dirty = true;
    document_.status = "Updated decoration transform.";
    recordDocumentChange(before);
    return true;
}

bool LevelEditor::beginSelectedDecorationTransform()
{
    if (decorationTransformBefore_ ||
        !document_.selectedDecoration ||
        *document_.selectedDecoration >= document_.decorations.size()) {
        return false;
    }
    decorationTransformBefore_ = captureDocumentSnapshot();
    return true;
}

bool LevelEditor::previewSelectedDecorationTransform(
    const Level::Decoration& decoration)
{
    if (!decorationTransformBefore_ ||
        !document_.selectedDecoration ||
        *document_.selectedDecoration >= document_.decorations.size() ||
        !validDecorationTransform(decoration)) {
        return false;
    }
    Level::Decoration& current =
        document_.decorations[*document_.selectedDecoration];
    if (current == decoration) {
        return false;
    }
    current = decoration;
    document_.dirty = true;
    document_.status = "Transforming decoration.";
    return true;
}

bool LevelEditor::endSelectedDecorationTransform(bool commit)
{
    if (!decorationTransformBefore_) {
        return false;
    }
    const DocumentSnapshot before = *decorationTransformBefore_;
    decorationTransformBefore_.reset();
    if (!commit) {
        applyDocumentSnapshot(before);
        document_.status = "Cancelled decoration transform.";
        return true;
    }
    recordDocumentChange(before);
    document_.status = "Updated decoration transform.";
    return true;
}

bool LevelEditor::transformingSelectedDecoration() const
{
    return decorationTransformBefore_.has_value();
}

bool LevelEditor::validDecorationTransform(
    const Level::Decoration& decoration) const
{
    const auto finite = [](Vec3 value) {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    };
    const bool lightValid = !decoration.pointLight ||
        (finite(decoration.pointLight->offset) &&
         finite(decoration.pointLight->color) &&
         std::isfinite(decoration.pointLight->intensity) &&
         std::isfinite(decoration.pointLight->range) &&
         std::isfinite(decoration.pointLight->shadowBias) &&
         std::isfinite(decoration.pointLight->shadowOpacity) &&
         decoration.pointLight->color.x >= 0.0f &&
         decoration.pointLight->color.y >= 0.0f &&
         decoration.pointLight->color.z >= 0.0f &&
         decoration.pointLight->intensity >= 0.0f &&
         decoration.pointLight->range > 0.0f &&
         decoration.pointLight->shadowBias >= 0.0f &&
         decoration.pointLight->shadowOpacity >= 0.0f &&
         decoration.pointLight->shadowOpacity <= 1.0f);
    return !decoration.model.empty() &&
        finite(decoration.position) &&
        finite(decoration.rotationDegrees) &&
        finite(decoration.scale) &&
        decoration.scale.x > 0.0f &&
        decoration.scale.y > 0.0f &&
        decoration.scale.z > 0.0f &&
        lightValid;
}

bool LevelEditor::duplicateSelectedDecoration()
{
    if (!document_.selectedDecoration ||
        *document_.selectedDecoration >= document_.decorations.size()) {
        return false;
    }
    const DocumentSnapshot before = captureDocumentSnapshot();
    Level::Decoration duplicate =
        document_.decorations[*document_.selectedDecoration];
    duplicate.position.x += 0.25f;
    duplicate.position.y += 0.25f;
    document_.decorations.push_back(std::move(duplicate));
    document_.selectedDecoration = document_.decorations.size() - 1;
    document_.dirty = true;
    document_.status = "Duplicated decoration.";
    recordDocumentChange(before);
    return true;
}

bool LevelEditor::deleteSelectedDecoration()
{
    if (!document_.selectedDecoration ||
        *document_.selectedDecoration >= document_.decorations.size()) {
        return false;
    }
    const DocumentSnapshot before = captureDocumentSnapshot();
    document_.decorations.erase(
        document_.decorations.begin() +
        static_cast<std::ptrdiff_t>(*document_.selectedDecoration));
    if (document_.decorations.empty()) {
        document_.selectedDecoration.reset();
    } else {
        document_.selectedDecoration = std::min(
            *document_.selectedDecoration,
            document_.decorations.size() - 1);
    }
    document_.dirty = true;
    document_.status = "Deleted decoration.";
    recordDocumentChange(before);
    return true;
}

bool LevelEditor::tryUndoEdit()
{
    pendingMove_.reset();
    if (editHistory_.empty()) {
        return false;
    }

    const EditActionRecord inverse = invertEditActionRecord(editHistory_.back());
    applyDocumentSnapshot(inverse.after);
    editHistory_.pop_back();
    document_.status = "Undid editor change.";
    return true;
}

uint32_t LevelEditor::documentWidth() const
{
    return document_.layers.empty() || document_.layers.front().empty()
        ? 0U
        : static_cast<uint32_t>(document_.layers.front().front().size());
}

uint32_t LevelEditor::documentHeight() const
{
    return document_.layers.empty() ? 0U : static_cast<uint32_t>(document_.layers.front().size());
}

uint32_t LevelEditor::documentDepth() const
{
    return static_cast<uint32_t>(document_.layers.size());
}

uint32_t LevelEditor::activeLayer() const
{
    return static_cast<uint32_t>(std::max(document_.activeLayer, 0));
}

std::optional<uint32_t> LevelEditor::waterLayer() const
{
    return document_.waterLayer;
}

bool LevelEditor::layerLocked() const
{
    return document_.layerLocked;
}

const std::vector<std::string>& LevelEditor::documentRows() const
{
    return document_.layers[static_cast<size_t>(document_.activeLayer)];
}

const Level::LayerRows& LevelEditor::documentLayers() const
{
    return document_.layers;
}

TileType LevelEditor::selectedTile() const
{
    return document_.selectedTile;
}

LevelEditor::Tool LevelEditor::tool() const
{
    return document_.tool;
}

const std::string& LevelEditor::selectedDecorationModel() const
{
    return document_.selectedDecorationModel;
}

const std::vector<Level::Decoration>& LevelEditor::decorations() const
{
    return document_.decorations;
}

std::optional<std::size_t> LevelEditor::selectedDecorationIndex() const
{
    return document_.selectedDecoration;
}

const Level::Decoration* LevelEditor::selectedDecoration() const
{
    if (!document_.selectedDecoration ||
        *document_.selectedDecoration >= document_.decorations.size()) {
        return nullptr;
    }
    return &document_.decorations[*document_.selectedDecoration];
}

const std::vector<Level::ScreenSelector>& LevelEditor::selectors() const
{
    return document_.selectors;
}

std::optional<std::size_t> LevelEditor::selectedSelectorIndex() const
{
    return document_.selectedSelector;
}

const Level::ScreenSelector* LevelEditor::selectedSelector() const
{
    if (!document_.selectedSelector ||
        *document_.selectedSelector >= document_.selectors.size()) {
        return nullptr;
    }
    return &document_.selectors[*document_.selectedSelector];
}

bool LevelEditor::editingOverworld() const
{
    if (document_.loadedPath.empty()) {
        return false;
    }
    if (document_.loadedPath.filename() == "overworld.scr") {
        return true;
    }
    return overworldScreenIdForPath(document_.loadedPath).has_value();
}

std::optional<OverworldScreenId> LevelEditor::overworldScreenId() const
{
    return overworldScreenIdForPath(document_.loadedPath);
}

std::optional<OverworldScreenId> LevelEditor::selectorLevelOwner(
    int puzzleLevel) const
{
    if (puzzleLevel < 0) {
        return std::nullopt;
    }
    const std::filesystem::path layoutPath =
        document_.browserRoot / "overworld/layout.json";
    if (!std::filesystem::is_regular_file(layoutPath)) {
        return std::nullopt;
    }
    try {
        const OverworldLayout layout = loadOverworldLayout(layoutPath);
        const std::optional<OverworldScreenId> loadedId = overworldScreenId();
        for (const OverworldScreenSpec& spec : layout.screens) {
            const std::vector<Level::ScreenSelector>* selectors = nullptr;
            Level::Definition definition;
            if (loadedId == spec.id) {
                selectors = &document_.selectors;
            } else {
                definition = Level::loadDefinitionFromFile(
                    document_.browserRoot / "overworld" / spec.file);
                selectors = &definition.selectors;
            }
            if (std::ranges::any_of(
                    *selectors,
                    [puzzleLevel](const Level::ScreenSelector& selector) {
                        return selector.target &&
                            selector.target->level == puzzleLevel;
                    })) {
                return spec.id;
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

bool LevelEditor::dirty() const
{
    return document_.dirty;
}

bool LevelEditor::hasInProgressDraft(
    const std::filesystem::path& path) const
{
    const std::filesystem::path key = draftKey(path);
    if (key.empty()) {
        return false;
    }
    return (document_.dirty && draftKey(document_.loadedPath) == key) ||
        drafts_.contains(key);
}

const std::filesystem::path& LevelEditor::documentPath() const
{
    return document_.filePath;
}

const std::filesystem::path& LevelEditor::loadedDocumentPath() const
{
    return document_.loadedPath;
}

const std::filesystem::path& LevelEditor::browserRoot() const
{
    return document_.browserRoot;
}

const std::filesystem::path& LevelEditor::sourceLevelRoot() const
{
    return document_.sourceLevelRoot;
}

const std::filesystem::path& LevelEditor::runtimeLevelRoot() const
{
    return document_.runtimeLevelRoot;
}

const std::string& LevelEditor::status() const
{
    return document_.status;
}

std::optional<OverworldScreenId> LevelEditor::overworldScreenIdForPath(
    const std::filesystem::path& path) const
{
    if (path.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path normalizedPath = normalizedAbsolutePath(path);
    for (const std::filesystem::path& root : {
             document_.browserRoot,
             document_.sourceLevelRoot }) {
        const std::filesystem::path layoutPath = root / "overworld/layout.json";
        if (!std::filesystem::is_regular_file(layoutPath)) {
            continue;
        }
        try {
            const OverworldLayout layout = loadOverworldLayout(layoutPath);
            for (const OverworldScreenSpec& screen : layout.screens) {
                if (normalizedAbsolutePath(root / "overworld" / screen.file) ==
                    normalizedPath) {
                    return screen.id;
                }
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void LevelEditor::newDocument(int width, int height, bool recordHistory)
{
    const DocumentSnapshot before = captureDocumentSnapshot();
    pendingMove_.reset();
    width = std::max(width, 1);
    height = std::max(height, 1);

    document_.layers = {
        std::vector<std::string>(
            static_cast<size_t>(height),
            std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Ground))),
        std::vector<std::string>(
            static_cast<size_t>(height),
            std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Air))),
    };
    document_.layers[1].front().front() = tileTypeToChar(TileType::Player);
    document_.waterLayer.reset();
    document_.decorations.clear();
    document_.selectors.clear();
    document_.selectedDecoration.reset();
    document_.selectedSelector.reset();
    // A new document belongs to no screen until it is saved as one, so it has
    // no splat map of its own and previews the shared fallback.
    document_.loadedPath.clear();
    document_.requestedWidth = width;
    document_.requestedHeight = height;
    document_.activeLayer = 1;
    document_.dirty = true;
    document_.playingDraft = false;
    document_.editingDocument = true;
    document_.status = "Created new level.";
    if (recordHistory) {
        recordDocumentChange(before);
    }
}

void LevelEditor::resizeDocument(int width, int height, bool recordHistory)
{
    if (editingOverworld() &&
        (width != static_cast<int>(documentWidth()) ||
         height != static_cast<int>(documentHeight()))) {
        document_.status =
            "Overworld screen dimensions are fixed by layout.json.";
        return;
    }
    const DocumentSnapshot before = captureDocumentSnapshot();
    width = std::max(width, 1);
    height = std::max(height, 1);

    for (size_t layerIndex = 0; layerIndex < document_.layers.size(); ++layerIndex) {
        const char fill = layerIndex == 0
            ? tileTypeToChar(TileType::Ground)
            : tileTypeToChar(TileType::Air);
        std::vector<std::string> resized(
            static_cast<size_t>(height),
            std::string(static_cast<size_t>(width), fill));
        const size_t copyHeight = std::min(resized.size(), document_.layers[layerIndex].size());
        for (size_t y = 0; y < copyHeight; ++y) {
            const size_t copyWidth = std::min(resized[y].size(), document_.layers[layerIndex][y].size());
            std::copy_n(document_.layers[layerIndex][y].begin(), copyWidth, resized[y].begin());
        }
        document_.layers[layerIndex] = std::move(resized);
    }

    std::erase_if(
        document_.selectors,
        [&](const Level::ScreenSelector& selector) {
            return selector.cell.x >= width || selector.cell.y >= height;
        });
    if (document_.selectedSelector &&
        *document_.selectedSelector >= document_.selectors.size()) {
        document_.selectedSelector.reset();
    }

    document_.requestedWidth = width;
    document_.requestedHeight = height;
    document_.dirty = true;
    document_.status = "Resized level.";
    if (recordHistory) {
        recordDocumentChange(before);
    }
}

void LevelEditor::addLayerAbove()
{
    insertLayerAt(document_.activeLayer + 1, "Added layer above.");
}

void LevelEditor::addLayerBelow()
{
    insertLayerAt(document_.activeLayer, "Added layer below.");
}

void LevelEditor::insertLayerAt(int insertionIndex, const char* status)
{
    const DocumentSnapshot before = captureDocumentSnapshot();
    const int width = std::max(document_.requestedWidth, 1);
    const int height = std::max(document_.requestedHeight, 1);
    insertionIndex = std::clamp(
        insertionIndex,
        0,
        static_cast<int>(document_.layers.size()));
    if (document_.waterLayer &&
        *document_.waterLayer >= static_cast<uint32_t>(insertionIndex)) {
        ++*document_.waterLayer;
    }
    document_.layers.insert(
        document_.layers.begin() + insertionIndex,
        std::vector<std::string>(
            static_cast<size_t>(height),
            std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Air))));
    for (Level::Decoration& decoration : document_.decorations) {
        if (decoration.position.z >= static_cast<float>(insertionIndex)) {
            decoration.position.z += 1.0f;
        }
    }
    for (Level::ScreenSelector& selector : document_.selectors) {
        if (selector.cell.z >= insertionIndex) {
            ++selector.cell.z;
        }
    }
    document_.activeLayer = insertionIndex;
    document_.dirty = true;
    document_.status = status;
    recordDocumentChange(before);
}

void LevelEditor::deleteActiveLayer()
{
    if (document_.layers.size() <= 1) {
        document_.status = "A level must contain at least one layer.";
        return;
    }

    const DocumentSnapshot before = captureDocumentSnapshot();
    const uint32_t deletedLayer =
        static_cast<uint32_t>(document_.activeLayer);
    if (document_.waterLayer == deletedLayer) {
        document_.waterLayer.reset();
    } else if (document_.waterLayer &&
               *document_.waterLayer > deletedLayer) {
        --*document_.waterLayer;
    }
    document_.layers.erase(document_.layers.begin() + document_.activeLayer);
    for (Level::Decoration& decoration : document_.decorations) {
        if (decoration.position.z >= static_cast<float>(deletedLayer + 1)) {
            decoration.position.z -= 1.0f;
        }
    }
    std::erase_if(
        document_.selectors,
        [&](Level::ScreenSelector& selector) {
            if (selector.cell.z == static_cast<int>(deletedLayer)) {
                return true;
            }
            if (selector.cell.z > static_cast<int>(deletedLayer)) {
                --selector.cell.z;
            }
            return false;
        });
    if (document_.selectedSelector &&
        *document_.selectedSelector >= document_.selectors.size()) {
        document_.selectedSelector.reset();
    }
    document_.activeLayer = std::min(
        document_.activeLayer,
        static_cast<int>(document_.layers.size()) - 1);
    document_.dirty = true;
    document_.status = "Deleted layer.";
    recordDocumentChange(before);
}

bool LevelEditor::openDocument(const std::filesystem::path& path)
{
    const std::filesystem::path targetKey = draftKey(path);
    if (targetKey.empty()) {
        document_.status = "Cannot open an empty document path.";
        return false;
    }

    const std::filesystem::path activeKey = draftKey(document_.loadedPath);
    if (activeKey == targetKey) {
        document_.filePath = path;
        document_.playingDraft = false;
        document_.editingDocument = true;
        document_.status = "Editing " + path.string();
        return true;
    }

    cacheActiveDraft();
    if (auto found = drafts_.find(targetKey); found != drafts_.end()) {
        DraftState draft = std::move(found->second);
        drafts_.erase(found);

        const std::filesystem::path sourceRoot = document_.sourceLevelRoot;
        const std::filesystem::path runtimeRoot = document_.runtimeLevelRoot;
        const std::filesystem::path browserRoot = document_.browserRoot;
        document_ = std::move(draft.document);
        document_.sourceLevelRoot = sourceRoot;
        document_.runtimeLevelRoot = runtimeRoot;
        document_.browserRoot = browserRoot;
        document_.filePath = path;
        document_.loadedPath = path;
        document_.playingDraft = false;
        document_.editingDocument = true;
        document_.status = "Restored in-progress draft " + path.string();
        editHistory_ = std::move(draft.editHistory);
        decorationTransformBefore_.reset();
        pendingMove_.reset();
        draftOverworldMap_.reset();
        return true;
    }

    if (!loadDocument(path, false)) {
        // The current document is still active after a failed load. It owns
        // the cached copy again, so avoid reporting the same draft twice.
        if (!activeKey.empty()) {
            drafts_.erase(activeKey);
        }
        return false;
    }
    editHistory_.clear();
    return true;
}

bool LevelEditor::loadDocument(const std::filesystem::path& path, bool recordHistory)
{
    const DocumentSnapshot before = captureDocumentSnapshot();
    pendingMove_.reset();
    std::ifstream file(path);
    if (!file) {
        document_.status = "Failed to load: " + path.string();
        return false;
    }

    std::vector<std::string> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        rows.push_back(line);
    }

    Level::Definition definition;
    try {
        definition = Level::parseDefinition(rows, path.string());
        if (overworldScreenIdForPath(path)) {
            // The complete composed map validates the single Player tile
            // across every overworld component.
            (void)OverworldMap::load(path.parent_path());
        } else {
            (void)Level::loadFromDefinition(definition, path.string());
        }
    } catch (const std::exception& error) {
        draftOverworldMap_.reset();
        document_.status = error.what();
        return false;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    for (const std::vector<std::string>& layer : definition.layers) {
        height = std::max(height, static_cast<uint32_t>(layer.size()));
        for (const std::string& row : layer) {
            width = std::max(width, static_cast<uint32_t>(row.size()));
        }
    }

    for (size_t layerIndex = 0;
         layerIndex < definition.layers.size();
         ++layerIndex) {
        const char fill = tileTypeToChar(TileType::Air);
        definition.layers[layerIndex].resize(
            height,
            std::string(width, fill));
        for (std::string& row : definition.layers[layerIndex]) {
            row.resize(width, fill);
        }
    }

    document_.layers = std::move(definition.layers);
    document_.waterLayer = definition.waterLayer;
    document_.decorations = std::move(definition.decorations);
    document_.selectors = std::move(definition.selectors);
    document_.selectedDecoration.reset();
    document_.selectedSelector.reset();
    document_.filePath = path;
    // This is the one place the in-memory document takes on a new origin by
    // reading; a browser selection deliberately does not.
    document_.loadedPath = path;
    document_.requestedHeight = static_cast<int>(height);
    document_.requestedWidth = static_cast<int>(width);
    document_.activeLayer = 0;
    document_.dirty = false;
    document_.playingDraft = false;
    document_.editingDocument = true;
    document_.status = "Loaded " + path.string();
    drafts_.erase(draftKey(path));
    if (recordHistory) {
        recordDocumentChange(before);
    }
    return true;
}

bool LevelEditor::saveDocument(const std::filesystem::path& path)
{
    if (document_.layers.empty()) {
        document_.status = "Nothing to save.";
        return false;
    }

    const std::filesystem::path sourcePath = normalizedAbsolutePath(path);
    const bool documentContainsPlayer = std::ranges::any_of(
        document_.layers,
        [](const std::vector<std::string>& layer) {
            return std::ranges::any_of(
                layer,
                [](const std::string& row) {
                    return row.find(tileTypeToChar(TileType::Player)) !=
                        std::string::npos;
                });
        });
    const std::vector<std::string> serialized =
        Level::serializeDefinition({
            .layers = document_.layers,
            .waterLayer = document_.waterLayer,
            .decorations = document_.decorations,
            .selectors = document_.selectors,
        });

    // A component edit can invalidate the unique Player tile, common
    // dimensions, or water metadata. Route it through the
    // same whole-project validator as topology edits so source and runtime can
    // never disagree or expose a half-valid map.
    if (overworldScreenIdForPath(sourcePath)) {
        const std::filesystem::path projectRoot =
            normalizedAbsolutePath(document_.browserRoot);
        const std::filesystem::path relative =
            sourcePath.lexically_relative(projectRoot);
        if (relative.empty() ||
            (relative.begin() != relative.end() && *relative.begin() == "..")) {
            document_.status =
                "Overworld screen is outside the active level project.";
            return false;
        }
        if (!applyProjectMutation(
                [relative, serialized, documentContainsPlayer](
                    const std::filesystem::path& root) {
                    if (documentContainsPlayer) {
                        const OverworldLayout layout = loadOverworldLayout(
                            root / "overworld/layout.json");
                        const std::filesystem::path current =
                            (root / relative).lexically_normal();
                        for (const OverworldScreenSpec& screen :
                             layout.screens) {
                            const std::filesystem::path other =
                                (root / "overworld" / screen.file)
                                    .lexically_normal();
                            if (other == current) {
                                continue;
                            }
                            Level::Definition definition =
                                Level::loadDefinitionFromFile(other);
                            bool changed = false;
                            for (auto& layer : definition.layers) {
                                for (std::string& row : layer) {
                                    const auto before = row;
                                    std::ranges::replace(
                                        row,
                                        tileTypeToChar(TileType::Player),
                                        tileTypeToChar(TileType::Air));
                                    changed = changed || row != before;
                                }
                            }
                            if (changed) {
                                writeScreenRows(
                                    other,
                                    Level::serializeDefinition(definition));
                            }
                        }
                    }
                    writeScreenRows(root / relative, serialized);
                })) {
            return false;
        }
        document_.filePath = sourcePath;
        document_.loadedPath = sourcePath;
        document_.dirty = false;
        drafts_.erase(draftKey(sourcePath));
        document_.status =
            "Saved overworld screen and validated the complete map.";
        return true;
    }

    std::error_code error;
    if (sourcePath.has_parent_path()) {
        std::filesystem::create_directories(sourcePath.parent_path(), error);
        if (error) {
            document_.status = "Failed to create directories: " + error.message();
            return false;
        }
    }

    std::ofstream file(sourcePath, std::ios::trunc);
    if (!file) {
        document_.status = "Failed to save: " + sourcePath.string();
        return false;
    }

    for (const std::string& line : serialized) {
        file << line << '\n';
    }
    file.flush();
    if (!file) {
        document_.status = "Failed to save: " + sourcePath.string();
        return false;
    }
    file.close();

    const std::filesystem::path mirrorPath = runtimeMirrorPath(sourcePath);
    if (!mirrorPath.empty()) {
        if (mirrorPath.has_parent_path()) {
            std::filesystem::create_directories(mirrorPath.parent_path(), error);
            if (error) {
                document_.status = "Saved source, but failed to create runtime mirror directories: " + error.message();
                return false;
            }
        }

        std::ofstream mirrorFile(mirrorPath, std::ios::trunc);
        if (!mirrorFile) {
            document_.status = "Saved source, but failed to update runtime mirror: " + mirrorPath.string();
            return false;
        }

        for (const std::string& line : serialized) {
            mirrorFile << line << '\n';
        }
        mirrorFile.flush();
        if (!mirrorFile) {
            document_.status = "Saved source, but failed to update runtime mirror: " + mirrorPath.string();
            return false;
        }
    }

    document_.filePath = sourcePath;
    // Saving a scratch document into levels/level<N>/screen<M>.scr makes it
    // that screen, so it gains that screen's splat map from here on.
    document_.loadedPath = sourcePath;
    document_.dirty = false;
    drafts_.erase(draftKey(sourcePath));
    document_.status = mirrorPath.empty()
        ? "Saved " + sourcePath.string()
        : "Saved " + sourcePath.string() + " and updated runtime mirror.";
    return true;
}

void LevelEditor::addLevelAt(int levelIndex)
{
    const std::vector<LevelDirectory> levels = collectLevelDirectories();
    if (levelIndex < 0) {
        document_.status = "Cannot add a level at a negative index.";
        return;
    }
    if (levelIndex > static_cast<int>(levels.size())) {
        document_.status = "Level index must be within the current level list.";
        return;
    }

    const std::vector<std::string> rows = defaultScreenRows();
    if (!applyProjectMutation([=](const std::filesystem::path& root) {
            rewriteOverworldSelectors(
                root,
                [=](std::vector<Level::ScreenSelector>& selectors) {
                    for (Level::ScreenSelector& selector : selectors) {
                        if (selector.target &&
                            selector.target->level >= levelIndex) {
                            ++selector.target->level;
                        }
                    }
                });
            for (int index = static_cast<int>(levels.size()) - 1;
                 index >= levelIndex;
                 --index) {
                std::filesystem::rename(
                    levelDirectoryPath(root, index),
                    levelDirectoryPath(root, index + 1));
            }
            writeScreenRows(
                screenFilePath(levelDirectoryPath(root, levelIndex), 0),
                rows);
        })) {
        return;
    }

    const std::filesystem::path newLevelPath = levelDirectoryPath(document_.browserRoot, levelIndex);
    const std::filesystem::path newScreenPath = screenFilePath(newLevelPath, 0);
    if (!loadDocument(newScreenPath)) {
        return;
    }
    document_.status = "Added " + newLevelPath.filename().string() + ".";
}

void LevelEditor::renameLevel(
    const LevelDirectory& level,
    std::string name)
{
    if (!isActiveLevelDirectory(level)) {
        document_.status =
            "Rename requires a level from the active browser root.";
        return;
    }

    if (!applyProjectMutation([=](const std::filesystem::path& root) {
            const std::filesystem::path stagedLevel =
                levelDirectoryPath(root, level.index);
            LevelMetadata metadata = loadLevelMetadata(
                stagedLevel,
                level.screens.size());
            metadata.name = name;
            writeLevelMetadata(stagedLevel, metadata);
        })) {
        return;
    }
    document_.status = name.empty()
        ? "Cleared level name."
        : "Named level " + name + ".";
}

void LevelEditor::deleteLevel(const LevelDirectory& levelToDelete)
{
    if (!isActiveLevelDirectory(levelToDelete)) {
        document_.status = "Delete requires a level from the active browser root.";
        return;
    }

    const std::vector<LevelDirectory> levels = collectLevelDirectories();
    const std::filesystem::path deletedName =
        uniqueDeletedLevelPath(levelToDelete.path).filename();
    if (!applyProjectMutation([=](const std::filesystem::path& root) {
            rewriteOverworldSelectors(
                root,
                [=](std::vector<Level::ScreenSelector>& selectors) {
                    for (Level::ScreenSelector& selector : selectors) {
                        if (!selector.target) {
                            continue;
                        }
                        if (selector.target->level == levelToDelete.index) {
                            selector.target.reset();
                        } else if (
                            selector.target->level > levelToDelete.index) {
                            --selector.target->level;
                        }
                    }
                });
            const std::filesystem::path deletedRoot = root / "Deleted";
            std::filesystem::create_directories(deletedRoot);
            std::filesystem::rename(
                levelDirectoryPath(root, levelToDelete.index),
                deletedRoot / deletedName);
            for (int index = levelToDelete.index + 1;
                 index < static_cast<int>(levels.size());
                 ++index) {
                std::filesystem::rename(
                    levelDirectoryPath(root, index),
                    levelDirectoryPath(root, index - 1));
            }
        })) {
        return;
    }
    loadFirstAvailableScreen();
    document_.status = "Moved level to Deleted.";
}

void LevelEditor::addScreenAt(const LevelDirectory& level, int screenIndex)
{
    if (!isActiveLevelDirectory(level)) {
        document_.status = "Add screen requires a level from the active browser root.";
        return;
    }

    if (screenIndex < 0 || screenIndex > static_cast<int>(level.screens.size())) {
        document_.status = "Screen index must be within the current screen list.";
        return;
    }

    const std::vector<std::string> rows = defaultScreenRows();
    if (!applyProjectMutation([=](const std::filesystem::path& root) {
            rewriteOverworldSelectors(
                root,
                [=](std::vector<Level::ScreenSelector>& selectors) {
                    for (Level::ScreenSelector& selector : selectors) {
                        if (selector.target &&
                            selector.target->level == level.index &&
                            selector.target->screen >= screenIndex) {
                            ++selector.target->screen;
                        }
                    }
                });
            const std::filesystem::path levelRoot =
                levelDirectoryPath(root, level.index);
            LevelMetadata metadata = loadLevelMetadata(
                levelRoot,
                level.screens.size());
            for (int index = static_cast<int>(level.screens.size()) - 1;
                 index >= screenIndex;
                 --index) {
                std::filesystem::rename(
                    screenFilePath(levelRoot, index),
                    screenFilePath(levelRoot, index + 1));
            }
            writeScreenRows(screenFilePath(levelRoot, screenIndex), rows);
            metadata.screenNames.insert(
                metadata.screenNames.begin() + screenIndex,
                std::string {});
            writeLevelMetadata(levelRoot, metadata);
        })) {
        return;
    }
    const std::filesystem::path newScreenPath = screenFilePath(level.path, screenIndex);
    if (!loadDocument(newScreenPath)) {
        return;
    }
    document_.status = "Added " + newScreenPath.filename().string() + ".";
}

void LevelEditor::renameScreen(
    const LevelDirectory& level,
    int screenIndex,
    std::string name)
{
    if (!isActiveLevelDirectory(level)) {
        document_.status =
            "Rename screen requires a level from the active browser root.";
        return;
    }
    if (screenIndex < 0 ||
        screenIndex >= static_cast<int>(level.screens.size())) {
        document_.status = "Screen name requires an existing screen.";
        return;
    }

    if (!applyProjectMutation([=](const std::filesystem::path& root) {
            const std::filesystem::path stagedLevel =
                levelDirectoryPath(root, level.index);
            LevelMetadata metadata = loadLevelMetadata(
                stagedLevel,
                level.screens.size());
            metadata.screenNames[static_cast<std::size_t>(screenIndex)] =
                name;
            writeLevelMetadata(stagedLevel, metadata);
        })) {
        return;
    }
    document_.status = name.empty()
        ? "Cleared screen name."
        : "Named screen " + name + ".";
}

void LevelEditor::deleteScreen(const LevelDirectory& level, int screenIndex)
{
    if (!isActiveLevelDirectory(level)) {
        document_.status = "Delete screen requires a level from the active browser root.";
        return;
    }

    if (level.screens.size() <= 1) {
        document_.status = "Cannot delete the last screen in a level. Delete the level instead.";
        return;
    }

    const auto screen = std::ranges::find_if(level.screens, [screenIndex](const ScreenFile& candidate) {
        return candidate.index == screenIndex;
    });
    if (screen == level.screens.end()) {
        return;
    }

    if (!applyProjectMutation([=](const std::filesystem::path& root) {
            rewriteOverworldSelectors(
                root,
                [=](std::vector<Level::ScreenSelector>& selectors) {
                    for (Level::ScreenSelector& selector : selectors) {
                        if (!selector.target ||
                            selector.target->level != level.index) {
                            continue;
                        }
                        if (selector.target->screen == screenIndex) {
                            selector.target.reset();
                        } else if (selector.target->screen > screenIndex) {
                            --selector.target->screen;
                        }
                    }
                });
            const std::filesystem::path levelRoot =
                levelDirectoryPath(root, level.index);
            LevelMetadata metadata = loadLevelMetadata(
                levelRoot,
                level.screens.size());
            if (!std::filesystem::remove(
                    screenFilePath(levelRoot, screenIndex))) {
                throw std::runtime_error("screen disappeared during deletion");
            }
            for (int index = screenIndex + 1;
                 index < static_cast<int>(level.screens.size());
                 ++index) {
                std::filesystem::rename(
                    screenFilePath(levelRoot, index),
                    screenFilePath(levelRoot, index - 1));
            }
            metadata.screenNames.erase(
                metadata.screenNames.begin() + screenIndex);
            writeLevelMetadata(levelRoot, metadata);
        })) {
        return;
    }
    const int nextScreenIndex = std::min(screenIndex, static_cast<int>(level.screens.size()) - 2);
    if (!loadDocument(screenFilePath(level.path, nextScreenIndex))) {
        return;
    }
    document_.status = "Deleted screen.";
}

void LevelEditor::restoreDeletedLevel(const std::filesystem::path& deletedLevelPath)
{
    const std::filesystem::path normalizedPath = normalizedAbsolutePath(deletedLevelPath);
    const std::filesystem::path normalizedDeletedRoot = normalizedAbsolutePath(deletedLevelRoot());
    std::error_code validationError;
    if (normalizedPath.parent_path() != normalizedDeletedRoot ||
        !std::filesystem::is_directory(normalizedPath, validationError)) {
        document_.status = "Restore requires a level directory from the Deleted tab.";
        return;
    }

    const std::vector<LevelDirectory> levels = collectLevelDirectories();
    const int restoredIndex = levels.empty() ? 0 : levels.back().index + 1;
    const std::filesystem::path deletedName = normalizedPath.filename();
    if (!applyProjectMutation([=](const std::filesystem::path& root) {
            std::filesystem::rename(
                root / "Deleted" / deletedName,
                levelDirectoryPath(root, restoredIndex));
        })) {
        return;
    }
    const std::filesystem::path restoredPath = levelDirectoryPath(document_.browserRoot, restoredIndex);
    const std::filesystem::path firstScreen = screenFilePath(restoredPath, 0);
    if (std::filesystem::exists(firstScreen)) {
        (void)loadDocument(firstScreen);
    }
    document_.status = "Restored " + restoredPath.filename().string() + ".";
}

bool LevelEditor::canPermanentlyDelete(const std::filesystem::path& path) const
{
    const std::filesystem::path normalizedPath = normalizedAbsolutePath(path);
    const std::filesystem::path normalizedDeletedRoot = normalizedAbsolutePath(deletedLevelRoot());
    return pathStartsWith(normalizedPath, normalizedDeletedRoot) &&
        normalizedPath != normalizedDeletedRoot;
}

bool LevelEditor::permanentlyDelete(const std::filesystem::path& path)
{
    if (!canPermanentlyDelete(path)) {
        document_.status = "Permanent delete is only allowed inside the Deleted tab.";
        return false;
    }

    const std::filesystem::path normalizedPath = normalizedAbsolutePath(path);
    const std::filesystem::path relative = normalizedPath.lexically_relative(
        normalizedAbsolutePath(document_.browserRoot));
    if (!applyProjectMutation([=](const std::filesystem::path& root) {
            if (std::filesystem::remove_all(root / relative) == 0) {
                throw std::runtime_error("path does not exist");
            }
        })) {
        return false;
    }

    document_.status = "Permanently deleted " + normalizedPath.filename().string() + ".";
    return true;
}

void LevelEditor::recordDocumentChange(const DocumentSnapshot& before)
{
    const DocumentSnapshot after = captureDocumentSnapshot();
    if (before.layers == after.layers &&
        before.waterLayer == after.waterLayer &&
        before.decorations == after.decorations &&
        before.selectors == after.selectors &&
        before.filePath == after.filePath &&
        before.requestedWidth == after.requestedWidth &&
        before.requestedHeight == after.requestedHeight &&
        before.activeLayer == after.activeLayer &&
        before.dirty == after.dirty) {
        return;
    }

    editHistory_.push_back({
        .before = before,
        .after = after,
    });
}

void LevelEditor::applyDocumentSnapshot(const DocumentSnapshot& snapshot)
{
    pendingMove_.reset();
    document_.layers = snapshot.layers;
    document_.waterLayer = snapshot.waterLayer;
    document_.decorations = snapshot.decorations;
    document_.selectors = snapshot.selectors;
    document_.filePath = snapshot.filePath;
    document_.loadedPath = snapshot.loadedPath;
    document_.requestedWidth = snapshot.requestedWidth;
    document_.requestedHeight = snapshot.requestedHeight;
    document_.activeLayer = snapshot.activeLayer;
    document_.selectedDecoration = snapshot.selectedDecoration;
    document_.selectedSelector = snapshot.selectedSelector;
    document_.dirty = snapshot.dirty;
    document_.playingDraft = false;
    document_.editingDocument = true;
}

Level LevelEditor::documentToLevel() const
{
    return Level::loadFromDefinition(
        documentDefinition(), "level editor draft");
}

Level::Definition LevelEditor::documentDefinition() const
{
    return {
        .layers = document_.layers,
        .waterLayer = document_.waterLayer,
        .decorations = document_.decorations,
        .selectors = document_.selectors,
    };
}

std::optional<Level> LevelEditor::beginDraftPlayback(
    const OverworldMapEditor* topologyDraft)
{
    try {
        Level level;
        if (const std::optional<OverworldScreenId> screenId =
                overworldScreenId()) {
            OverworldDefinitionOverride activeDefinition {
                .screen = *screenId,
                .definition = documentDefinition(),
            };
            OverworldDraftOverride draft;
            if (topologyDraft && topologyDraft->loaded() &&
                normalizedAbsolutePath(topologyDraft->projectLevelRoot()) ==
                    normalizedAbsolutePath(document_.browserRoot)) {
                draft = topologyDraft->draftOverride(
                    std::move(activeDefinition));
            } else {
                draft.definitions.push_back(std::move(activeDefinition));
            }
            OverworldMap map = OverworldMap::load(
                document_.browserRoot / "overworld",
                std::move(draft));
            level = map.level();
            draftOverworldMap_ = std::move(map);
        } else {
            level = documentToLevel();
            draftOverworldMap_.reset();
        }
        setPlayingDraft(true);
        document_.status = draftOverworldMap_
            ? "Playing composed overworld draft; selectors are disabled."
            : "Playing editor draft.";
        return level;
    } catch (const std::exception& error) {
        draftOverworldMap_.reset();
        document_.status = error.what();
        return std::nullopt;
    }
}

LevelEditor::DocumentSnapshot LevelEditor::captureDocumentSnapshot() const
{
    return {
        .layers = document_.layers,
        .waterLayer = document_.waterLayer,
        .decorations = document_.decorations,
        .selectors = document_.selectors,
        .filePath = document_.filePath,
        .loadedPath = document_.loadedPath,
        .requestedWidth = document_.requestedWidth,
        .requestedHeight = document_.requestedHeight,
        .activeLayer = document_.activeLayer,
        .selectedDecoration = document_.selectedDecoration,
        .selectedSelector = document_.selectedSelector,
        .dirty = document_.dirty,
    };
}

LevelEditor::EditActionRecord LevelEditor::invertEditActionRecord(const EditActionRecord& record) const
{
    return {
        .before = record.after,
        .after = record.before,
    };
}

std::filesystem::path LevelEditor::runtimeMirrorPath(const std::filesystem::path& sourcePath) const
{
    const std::filesystem::path normalizedSourceRoot = normalizedAbsolutePath(document_.sourceLevelRoot);
    const std::filesystem::path normalizedSourcePath = normalizedAbsolutePath(sourcePath);
    if (!pathStartsWith(normalizedSourcePath, normalizedSourceRoot)) {
        return {};
    }

    const std::filesystem::path relativePath = normalizedSourcePath.lexically_relative(normalizedSourceRoot);
    if (relativePath.empty()) {
        return {};
    }

    return normalizedAbsolutePath(document_.runtimeLevelRoot / relativePath);
}

std::filesystem::path LevelEditor::deletedLevelRoot() const
{
    return document_.browserRoot / "Deleted";
}

bool LevelEditor::isActiveLevelDirectory(const LevelDirectory& level) const
{
    if (level.index < 0 ||
        normalizedAbsolutePath(level.path) !=
            normalizedAbsolutePath(levelDirectoryPath(document_.browserRoot, level.index))) {
        return false;
    }

    std::error_code error;
    return std::filesystem::is_directory(level.path, error);
}

std::vector<LevelEditor::LevelDirectory> LevelEditor::collectLevelDirectories() const
{
    std::vector<LevelDirectory> levels;
    std::error_code error;
    if (!std::filesystem::exists(document_.browserRoot, error)) {
        return levels;
    }

    for (const auto& entry : std::filesystem::directory_iterator(document_.browserRoot, error)) {
        if (error) {
            break;
        }
        if (!entry.is_directory(error) || entry.path() == deletedLevelRoot()) {
            continue;
        }

        const std::optional<int> levelIndex = parseNumberedName(entry.path().filename().string(), "level");
        if (!levelIndex) {
            continue;
        }

        LevelDirectory level {
            .index = *levelIndex,
            .path = entry.path(),
        };

        for (const auto& screenEntry : std::filesystem::directory_iterator(level.path, error)) {
            if (error) {
                break;
            }
            if (!screenEntry.is_regular_file(error)) {
                continue;
            }

            const std::optional<int> screenIndex = parseNumberedName(screenEntry.path().filename().string(), "screen", ".scr");
            if (!screenIndex) {
                continue;
            }

            level.screens.push_back({
                .index = *screenIndex,
                .path = screenEntry.path(),
            });
        }

        std::ranges::sort(level.screens, {}, &ScreenFile::index);
        const LevelMetadata metadata = loadLevelMetadata(
            level.path,
            level.screens.size());
        level.name = metadata.name;
        for (std::size_t index = 0; index < level.screens.size(); ++index) {
            level.screens[index].name = metadata.screenNames[index];
        }
        levels.push_back(std::move(level));
    }

    std::ranges::sort(levels, {}, &LevelDirectory::index);
    return levels;
}

std::string LevelEditor::selectorTargetLabel(
    const Level::ScreenSelector& selector,
    const std::vector<LevelDirectory>& levels)
{
    if (!selector.target) {
        return "Unassigned";
    }

    std::string levelLabel =
        "Level " + std::to_string(selector.target->level + 1);
    std::string screenLabel =
        "Screen " + std::to_string(selector.target->screen + 1);
    const auto level = std::ranges::find(
        levels, selector.target->level, &LevelDirectory::index);
    if (level != levels.end()) {
        if (!level->name.empty()) {
            levelLabel = level->name;
        }
        const auto screen = std::ranges::find(
            level->screens,
            selector.target->screen,
            &ScreenFile::index);
        if (screen != level->screens.end() && !screen->name.empty()) {
            screenLabel = screen->name;
        }
    }
    return levelLabel + " / " + screenLabel;
}

std::vector<LevelEditor::LevelDirectory> LevelEditor::collectDeletedLevels() const
{
    std::vector<LevelDirectory> levels;
    std::error_code error;
    const std::filesystem::path deletedRoot = deletedLevelRoot();
    if (!std::filesystem::exists(deletedRoot, error)) {
        return levels;
    }

    for (const auto& entry : std::filesystem::directory_iterator(deletedRoot, error)) {
        if (error) {
            break;
        }
        if (entry.is_directory(error)) {
            LevelDirectory level {
                .index = parseNumberedName(entry.path().filename().string(), "level").value_or(0),
                .path = entry.path(),
            };

            for (const auto& screenEntry : std::filesystem::directory_iterator(level.path, error)) {
                if (error) {
                    break;
                }
                if (!screenEntry.is_regular_file(error)) {
                    continue;
                }

                const std::optional<int> screenIndex = parseNumberedName(screenEntry.path().filename().string(), "screen", ".scr");
                if (!screenIndex) {
                    continue;
                }

                level.screens.push_back({
                    .index = *screenIndex,
                    .path = screenEntry.path(),
                });
            }

            std::ranges::sort(level.screens, {}, &ScreenFile::index);
            const LevelMetadata metadata = loadLevelMetadata(
                level.path,
                level.screens.size());
            level.name = metadata.name;
            for (std::size_t index = 0;
                 index < level.screens.size();
                 ++index) {
                level.screens[index].name = metadata.screenNames[index];
            }
            levels.push_back(std::move(level));
        }
    }

    std::ranges::sort(levels, {}, &LevelDirectory::path);
    return levels;
}

std::vector<std::string> LevelEditor::defaultScreenRows() const
{
    const int width = std::max(document_.requestedWidth, 1);
    const int height = std::max(document_.requestedHeight, 1);
    Level::LayerRows layers {
        std::vector<std::string>(
            static_cast<size_t>(height),
            std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Ground))),
        std::vector<std::string>(
            static_cast<size_t>(height),
            std::string(static_cast<size_t>(width), tileTypeToChar(TileType::Air))),
    };
    layers[1][static_cast<size_t>(height / 2)][static_cast<size_t>(width / 2)] =
        tileTypeToChar(TileType::Player);
    return Level::serializeLayerRows(layers);
}

std::filesystem::path LevelEditor::uniqueDeletedLevelPath(const std::filesystem::path& levelPath) const
{
    const std::filesystem::path deletedRoot = deletedLevelRoot();
    std::filesystem::path candidate = deletedRoot / levelPath.filename();
    for (int suffix = 1; std::filesystem::exists(candidate); ++suffix) {
        candidate = deletedRoot / (levelPath.filename().string() + "_deleted" + std::to_string(suffix));
    }
    return candidate;
}

bool LevelEditor::applyProjectMutation(
    const LevelProjectStore::Mutation& mutation)
{
    std::optional<std::filesystem::path> runtimeRoot;
    if (normalizedAbsolutePath(document_.browserRoot) ==
        normalizedAbsolutePath(document_.sourceLevelRoot)) {
        runtimeRoot = document_.runtimeLevelRoot;
    }

    const LevelProjectStore::Result result = LevelProjectStore::transact(
        document_.browserRoot,
        runtimeRoot,
        mutation);
    if (!result.succeeded) {
        document_.status = result.originalsPreserved
            ? "Project change failed; original files were preserved: " +
                result.message
            : "Project change failed and rollback was incomplete; backups "
                "were retained: " + result.message;
        return false;
    }
    return true;
}

void LevelEditor::loadFirstAvailableScreen()
{
    for (const LevelDirectory& level : collectLevelDirectories()) {
        if (!level.screens.empty()) {
            (void)loadDocument(level.screens.front().path);
            return;
        }
    }

    newDocument(document_.requestedWidth, document_.requestedHeight, false);
}

} // namespace sokoban
