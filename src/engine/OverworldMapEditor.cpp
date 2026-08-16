#include "engine/OverworldMapEditor.hpp"

#include "engine/TileTypes.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <system_error>

namespace sokoban {
namespace {

void writeDefinition(
    const std::filesystem::path& path,
    const Level::Definition& definition)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        throw std::runtime_error(
            "cannot write overworld screen " + path.string());
    }
    for (const std::string& line : Level::serializeDefinition(definition)) {
        file << line << '\n';
    }
    file.close();
    if (!file) {
        throw std::runtime_error(
            "cannot write overworld screen " + path.string());
    }
}

std::optional<OverworldScreenId> screenIdFromFilename(
    const std::filesystem::path& path)
{
    const std::string name = path.filename().string();
    constexpr std::string_view prefix = "screen";
    constexpr std::string_view suffix = ".scr";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)) {
        return std::nullopt;
    }
    const char* begin = name.data() + prefix.size();
    const char* end = name.data() + name.size() - suffix.size();
    uint32_t id = 0;
    const auto parsed = std::from_chars(begin, end, id);
    if (parsed.ec != std::errc {} || parsed.ptr != end || id == 0) {
        return std::nullopt;
    }
    return id;
}

bool containsId(
    const std::vector<OverworldScreenId>& ids,
    OverworldScreenId id)
{
    return std::ranges::find(ids, id) != ids.end();
}

} // namespace

void OverworldMapEditor::initialize(
    const std::filesystem::path& projectLevelRoot,
    std::optional<std::filesystem::path> runtimeLevelRoot)
{
    projectLevelRoot_ = std::filesystem::absolute(projectLevelRoot)
        .lexically_normal();
    if (runtimeLevelRoot) {
        runtimeLevelRoot_ = std::filesystem::absolute(*runtimeLevelRoot)
            .lexically_normal();
    } else {
        runtimeLevelRoot_.reset();
    }
    cellTool_.reset();
    (void)reload();
}

bool OverworldMapEditor::reload()
{
    cellTool_.reset();
    try {
        State loaded;
        const std::filesystem::path root = projectLevelRoot_ / "overworld";
        loaded.layout = loadOverworldLayout(root / "layout.json");
        loaded.screens.reserve(loaded.layout.screens.size());
        for (const OverworldScreenSpec& spec : loaded.layout.screens) {
            loaded.screens.push_back({
                .spec = spec,
                .definition = Level::loadDefinitionFromFile(root / spec.file),
            });
        }
        loaded.selected = loaded.layout.start.screen;
        state_ = std::move(loaded);
        savedState_ = state_;
        undo_.clear();
        redo_.clear();
        loaded_ = true;
        status_ = "Loaded composed overworld layout.";
        return true;
    } catch (const std::exception& error) {
        state_ = {};
        savedState_ = {};
        undo_.clear();
        redo_.clear();
        loaded_ = false;
        status_ = error.what();
        return false;
    }
}

bool OverworldMapEditor::dirty() const
{
    return state_.layout != savedState_.layout ||
        state_.screens != savedState_.screens ||
        state_.retiredIds != savedState_.retiredIds ||
        state_.restoredIds != savedState_.restoredIds;
}

std::vector<OverworldMapEditor::ScreenSummary>
OverworldMapEditor::screens() const
{
    std::vector<ScreenSummary> result;
    result.reserve(state_.screens.size());
    for (const DraftScreen& screen : state_.screens) {
        result.push_back({
            .id = screen.spec.id,
            .slot = screen.spec.slot,
            .path = projectLevelRoot_ / "overworld" / screen.spec.file,
            .selectorCount = screen.definition.selectors.size(),
            .start = state_.layout.start.screen == screen.spec.id,
            .selected = state_.selected == screen.spec.id,
        });
    }
    std::ranges::sort(result, {}, &ScreenSummary::id);
    return result;
}

std::vector<OverworldScreenId> OverworldMapEditor::deletedScreens() const
{
    std::vector<OverworldScreenId> result;
    std::error_code error;
    const std::filesystem::path root =
        projectLevelRoot_ / "overworld/Deleted";
    if (!std::filesystem::is_directory(root, error)) {
        return result;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file(error)) {
            if (const auto id = screenIdFromFilename(entry.path())) {
                result.push_back(*id);
            }
        }
    }
    std::ranges::sort(result);
    return result;
}

const OverworldScreenSpec* OverworldMapEditor::screen(
    OverworldScreenId id) const
{
    const DraftScreen* found = draftScreen(id);
    return found ? &found->spec : nullptr;
}

const Level::Definition* OverworldMapEditor::definition(
    OverworldScreenId id) const
{
    const DraftScreen* found = draftScreen(id);
    return found ? &found->definition : nullptr;
}

std::filesystem::path OverworldMapEditor::screenPath(
    OverworldScreenId id) const
{
    const OverworldScreenSpec* found = screen(id);
    return found
        ? projectLevelRoot_ / "overworld" / found->file
        : std::filesystem::path {};
}

OverworldDraftOverride OverworldMapEditor::draftOverride(
    std::optional<OverworldDefinitionOverride> activeDefinition) const
{
    OverworldDraftOverride result;
    result.layout = state_.layout;
    result.definitions.reserve(state_.screens.size());
    for (const DraftScreen& screen : state_.screens) {
        const bool active = activeDefinition &&
            activeDefinition->screen == screen.spec.id;
        const bool hasProjectFile = std::filesystem::is_regular_file(
            projectLevelRoot_ / "overworld" / screen.spec.file);
        // Existing component files are deliberately left file-backed. They
        // may have been saved by LevelEditor after this topology draft was
        // opened, and its latest work must win over our loaded snapshot.
        if (active || !hasProjectFile) {
            result.definitions.push_back({
                .screen = screen.spec.id,
                .definition = active
                    ? activeDefinition->definition
                    : screen.definition,
            });
        }
    }
    if (activeDefinition && std::ranges::none_of(
            state_.screens,
            [&](const DraftScreen& screen) {
                return screen.spec.id == activeDefinition->screen;
            })) {
        throw std::runtime_error(
            "active overworld component is absent from the topology draft");
    }
    return result;
}

std::string OverworldMapEditor::cellToolPrompt() const
{
    if (!cellTool_) {
        return {};
    }
    switch (cellTool_->kind) {
    case CellToolKind::SetStart:
        return "Click a supported walkable cell to set the overworld start.";
    case CellToolKind::AddConnectedScreen:
        return "Click a facing boundary cell to add and connect the new screen.";
    case CellToolKind::ConnectExisting:
        return "Click a facing boundary cell to connect to screen " +
            std::to_string(*cellTool_->target) + ".";
    }
    return {};
}

bool OverworldMapEditor::beginSetStartCell(OverworldScreenId source)
{
    if (!screen(source)) {
        status_ = "Open an existing overworld screen before picking its start.";
        return false;
    }
    state_.selected = source;
    cellTool_ = CellTool {
        .kind = CellToolKind::SetStart,
        .source = source,
    };
    status_ = cellToolPrompt();
    return true;
}

bool OverworldMapEditor::beginAddConnectedScreenCell(
    OverworldScreenId source,
    OverworldSlot slot)
{
    if (!screen(source) || !slotAvailable(slot)) {
        status_ = "Adding a connected screen requires an existing source and empty slot.";
        return false;
    }
    cellTool_ = CellTool {
        .kind = CellToolKind::AddConnectedScreen,
        .source = source,
        .newScreenSlot = slot,
    };
    state_.selected = source;
    status_ = cellToolPrompt();
    return true;
}

bool OverworldMapEditor::beginConnectCell(
    OverworldScreenId source,
    OverworldScreenId target)
{
    if (!screen(source) || !screen(target) || source == target) {
        status_ = "A connection picker requires two different existing screens.";
        return false;
    }
    cellTool_ = CellTool {
        .kind = CellToolKind::ConnectExisting,
        .source = source,
        .target = target,
    };
    state_.selected = source;
    status_ = cellToolPrompt();
    return true;
}

void OverworldMapEditor::cancelCellTool()
{
    if (cellTool_) {
        cellTool_.reset();
        status_ = "Cancelled overworld cell picking.";
    }
}

bool OverworldMapEditor::applyCellTool(
    OverworldScreenId visibleScreen,
    GridPosition3 cell,
    const Level::Definition* visibleDefinition)
{
    if (!cellTool_) {
        status_ = "No overworld cell-picking tool is active.";
        return false;
    }
    if (visibleScreen != cellTool_->source) {
        status_ = "The cell picker is armed for screen " +
            std::to_string(cellTool_->source) + ". Open that screen first.";
        return false;
    }

    const CellTool tool = *cellTool_;
    bool applied = false;
    switch (tool.kind) {
    case CellToolKind::SetStart:
        applied = setStart(tool.source, cell, visibleDefinition);
        break;
    case CellToolKind::AddConnectedScreen:
        applied = addConnectedScreen(
            tool.source, *tool.newScreenSlot, cell, visibleDefinition);
        break;
    case CellToolKind::ConnectExisting:
        applied = connect(
            tool.source, *tool.target, cell, visibleDefinition);
        break;
    }
    if (applied) {
        cellTool_.reset();
    }
    return applied;
}

bool OverworldMapEditor::selectScreen(OverworldScreenId id)
{
    if (!screen(id)) {
        status_ = "Cannot select a missing overworld screen.";
        return false;
    }
    state_.selected = id;
    status_ = "Selected overworld screen " + std::to_string(id) + ".";
    return true;
}

bool OverworldMapEditor::addScreen(OverworldSlot slot)
{
    if (!loaded_ || !slotAvailable(slot)) {
        status_ = "The requested overworld slot is already occupied.";
        return false;
    }
    State before = state_;
    const OverworldScreenId id = nextScreenId();
    OverworldScreenSpec spec {
        .id = id,
        .file = "screen" + std::to_string(id) + ".scr",
        .slot = slot,
    };
    state_.layout.screens.push_back(spec);
    state_.screens.push_back({ spec, defaultDefinition() });
    state_.selected = id;
    record(std::move(before),
        "Added screen " + std::to_string(id) +
        " to the draft. Connect it before saving.");
    return true;
}

bool OverworldMapEditor::addConnectedScreen(
    OverworldScreenId from,
    OverworldSlot slot,
    GridPosition3 fromCell,
    const Level::Definition* sourceDefinition)
{
    const DraftScreen* source = draftScreen(from);
    if (!source || !slotAvailable(slot)) {
        status_ = "A connected screen requires an existing source and empty slot.";
        return false;
    }
    if (!supportedWalkable(
            sourceDefinition ? *sourceDefinition : source->definition,
            fromCell)) {
        status_ = "The source connection cell must be supported and walkable.";
        return false;
    }

    State before = state_;
    const OverworldScreenId id = nextScreenId();
    OverworldScreenSpec spec {
        .id = id,
        .file = "screen" + std::to_string(id) + ".scr",
        .slot = slot,
    };
    const auto destination = matchingEndpoint(source->spec, spec, fromCell);
    if (!destination) {
        status_ = "New screens must occupy a cardinal neighboring slot and use a facing boundary cell.";
        return false;
    }

    Level::Definition definition = defaultDefinition();
    if (destination->z < 0 ||
        destination->z >= static_cast<int>(definition.layers.size())) {
        status_ = "The connection layer is unavailable in the new screen.";
        return false;
    }
    definition.layers[static_cast<std::size_t>(destination->z)]
        [static_cast<std::size_t>(destination->y)]
        [static_cast<std::size_t>(destination->x)] =
            tileTypeToChar(TileType::Air);

    state_.layout.screens.push_back(spec);
    state_.screens.push_back({ spec, std::move(definition) });
    state_.layout.connections.push_back({
        .a = { from, fromCell },
        .b = { id, *destination },
    });
    state_.selected = id;
    record(std::move(before),
        "Added and connected overworld screen " + std::to_string(id) + ".");
    return true;
}

bool OverworldMapEditor::moveScreen(
    OverworldScreenId id,
    OverworldSlot slot)
{
    DraftScreen* target = draftScreen(id);
    if (!target || !slotAvailable(slot, id)) {
        status_ = "The requested overworld slot is unavailable.";
        return false;
    }
    if (std::ranges::any_of(
            state_.layout.connections,
            [id](const OverworldConnection& connection) {
                return connection.a.screen == id || connection.b.screen == id;
            })) {
        status_ = "Disconnect a screen before moving it.";
        return false;
    }
    State before = state_;
    target->spec.slot = slot;
    const auto layoutScreen = std::ranges::find(
        state_.layout.screens, id, &OverworldScreenSpec::id);
    if (layoutScreen != state_.layout.screens.end()) {
        layoutScreen->slot = slot;
    }
    record(std::move(before), "Moved overworld screen " + std::to_string(id) + ".");
    return true;
}

bool OverworldMapEditor::deleteScreen(OverworldScreenId id)
{
    if (state_.screens.size() <= 1) {
        status_ = "An overworld must contain at least one screen.";
        return false;
    }
    if (state_.layout.start.screen == id) {
        status_ = "Move the overworld start before deleting its screen.";
        return false;
    }
    if (!draftScreen(id)) {
        status_ = "Cannot delete a missing overworld screen.";
        return false;
    }
    State before = state_;
    std::erase_if(state_.layout.connections,
        [id](const OverworldConnection& connection) {
            return connection.a.screen == id || connection.b.screen == id;
        });
    std::erase_if(state_.layout.screens,
        [id](const OverworldScreenSpec& spec) { return spec.id == id; });
    std::erase_if(state_.screens,
        [id](const DraftScreen& screen) { return screen.spec.id == id; });

    if (containsId(state_.restoredIds, id)) {
        std::erase(state_.restoredIds, id);
    } else if (std::ranges::any_of(
                   savedState_.screens,
                   [id](const DraftScreen& screen) {
                       return screen.spec.id == id;
                   })) {
        state_.retiredIds.push_back(id);
    }
    state_.selected = state_.layout.start.screen;
    record(std::move(before),
        "Removed screen " + std::to_string(id) +
        " from the draft; saving moves its file to Deleted.");
    return true;
}

bool OverworldMapEditor::restoreDeletedScreen(
    OverworldScreenId id,
    OverworldSlot slot)
{
    if (!loaded_ || screen(id) || !slotAvailable(slot)) {
        status_ = "The deleted screen ID or requested slot is unavailable.";
        return false;
    }
    const std::filesystem::path deletedPath = projectLevelRoot_ /
        "overworld/Deleted" /
        ("screen" + std::to_string(id) + ".scr");
    try {
        Level::Definition restored = Level::loadDefinitionFromFile(deletedPath);
        State before = state_;
        OverworldScreenSpec spec {
            .id = id,
            .file = "screen" + std::to_string(id) + ".scr",
            .slot = slot,
        };
        state_.layout.screens.push_back(spec);
        state_.screens.push_back({ spec, std::move(restored) });
        state_.restoredIds.push_back(id);
        state_.selected = id;
        record(std::move(before),
            "Restored screen " + std::to_string(id) +
            " to the draft. Connect it before saving.");
        return true;
    } catch (const std::exception& error) {
        status_ = error.what();
        return false;
    }
}

bool OverworldMapEditor::setStart(
    OverworldScreenId screenId,
    GridPosition3 cell,
    const Level::Definition* sourceDefinition)
{
    const DraftScreen* target = draftScreen(screenId);
    if (!target || !supportedWalkable(
            sourceDefinition ? *sourceDefinition : target->definition,
            cell)) {
        status_ = "The overworld start must be a supported walkable cell.";
        return false;
    }
    State before = state_;
    state_.layout.start = { screenId, cell };
    record(std::move(before), "Updated the overworld start cell.");
    return true;
}

bool OverworldMapEditor::connect(
    OverworldScreenId from,
    OverworldScreenId to,
    GridPosition3 fromCell,
    const Level::Definition* sourceDefinition)
{
    const DraftScreen* source = draftScreen(from);
    const DraftScreen* destinationScreen = draftScreen(to);
    if (!source || !destinationScreen || from == to) {
        status_ = "A connection requires two different existing screens.";
        return false;
    }
    const auto destination = matchingEndpoint(
        source->spec, destinationScreen->spec, fromCell);
    if (!destination) {
        status_ = "Connection endpoints must face across cardinal neighboring slots.";
        return false;
    }
    std::optional<Level::Definition> latestDestination;
    const std::filesystem::path destinationPath =
        projectLevelRoot_ / "overworld" / destinationScreen->spec.file;
    if (std::filesystem::is_regular_file(destinationPath)) {
        try {
            latestDestination = Level::loadDefinitionFromFile(
                destinationPath);
        } catch (const std::exception& error) {
            status_ = error.what();
            return false;
        }
    }
    if (!supportedWalkable(
            sourceDefinition ? *sourceDefinition : source->definition,
            fromCell) ||
        !supportedWalkable(
            latestDestination
                ? *latestDestination
                : destinationScreen->definition,
            *destination)) {
        status_ = "Both connection endpoints must be supported walkable cells.";
        return false;
    }
    const auto endpointUsed = [&](OverworldScreenId id, GridPosition3 cell) {
        return std::ranges::any_of(
            state_.layout.connections,
            [&](const OverworldConnection& connection) {
                return (connection.a.screen == id && connection.a.cell == cell) ||
                    (connection.b.screen == id && connection.b.cell == cell);
            });
    };
    if (endpointUsed(from, fromCell) || endpointUsed(to, *destination)) {
        status_ = "A connection endpoint may be used only once.";
        return false;
    }
    State before = state_;
    state_.layout.connections.push_back({
        .a = { from, fromCell },
        .b = { to, *destination },
    });
    record(std::move(before), "Connected overworld screens.");
    return true;
}

bool OverworldMapEditor::disconnect(std::size_t connectionIndex)
{
    if (connectionIndex >= state_.layout.connections.size()) {
        status_ = "Cannot remove a missing connection.";
        return false;
    }
    State before = state_;
    state_.layout.connections.erase(
        state_.layout.connections.begin() +
        static_cast<std::ptrdiff_t>(connectionIndex));
    record(std::move(before), "Disconnected overworld screens.");
    return true;
}

bool OverworldMapEditor::undo()
{
    cellTool_.reset();
    if (undo_.empty()) {
        status_ = "No topology edit to undo.";
        return false;
    }
    redo_.push_back(state_);
    state_ = std::move(undo_.back());
    undo_.pop_back();
    status_ = "Undid topology edit.";
    return true;
}

bool OverworldMapEditor::redo()
{
    cellTool_.reset();
    if (redo_.empty()) {
        status_ = "No topology edit to redo.";
        return false;
    }
    undo_.push_back(state_);
    state_ = std::move(redo_.back());
    redo_.pop_back();
    status_ = "Redid topology edit.";
    return true;
}

bool OverworldMapEditor::save()
{
    if (!loaded_) {
        status_ = "No composed overworld is loaded.";
        return false;
    }
    const State draft = state_;
    const LevelProjectStore::Result result = LevelProjectStore::transact(
        projectLevelRoot_,
        runtimeLevelRoot_,
        [draft](const std::filesystem::path& root) {
            const std::filesystem::path overworldRoot = root / "overworld";
            const std::filesystem::path deletedRoot = overworldRoot / "Deleted";
            std::filesystem::create_directories(overworldRoot);

            for (OverworldScreenId id : draft.retiredIds) {
                const std::filesystem::path source = overworldRoot /
                    ("screen" + std::to_string(id) + ".scr");
                if (!std::filesystem::exists(source)) {
                    continue;
                }
                std::filesystem::create_directories(deletedRoot);
                const std::filesystem::path destination = deletedRoot /
                    source.filename();
                if (std::filesystem::exists(destination)) {
                    throw std::runtime_error(
                        "deleted overworld screen already exists: " +
                        destination.string());
                }
                std::filesystem::rename(source, destination);
            }
            for (OverworldScreenId id : draft.restoredIds) {
                std::error_code error;
                std::filesystem::remove(
                    deletedRoot /
                        ("screen" + std::to_string(id) + ".scr"),
                    error);
                if (error) {
                    throw std::runtime_error(
                        "cannot retire restored overworld file: " +
                        error.message());
                }
            }
            for (const DraftScreen& screen : draft.screens) {
                const std::filesystem::path activePath =
                    overworldRoot / screen.spec.file;
                // The staged project already contains every existing
                // component. Preserve it in case LevelEditor saved newer tile
                // work after this topology model was loaded. Only new and
                // restored screens need their in-memory definition written.
                if (!std::filesystem::is_regular_file(activePath)) {
                    writeDefinition(activePath, screen.definition);
                }
            }
            writeOverworldLayout(
                overworldRoot / "layout.json", draft.layout);
        });
    if (!result.succeeded) {
        status_ = result.originalsPreserved
            ? "Overworld save rejected; original files were preserved: " +
                result.message
            : "Overworld save failed and rollback was incomplete: " +
                result.message;
        return false;
    }

    const std::optional<OverworldScreenId> selected = state_.selected;
    if (!reload()) {
        return false;
    }
    if (selected && screen(*selected)) {
        state_.selected = *selected;
        savedState_.selected = *selected;
    }
    status_ = "Saved and validated the complete overworld project.";
    return true;
}

OverworldMapEditor::DraftScreen* OverworldMapEditor::draftScreen(
    OverworldScreenId id)
{
    const auto found = std::ranges::find(
        state_.screens, id,
        [](const DraftScreen& value) { return value.spec.id; });
    return found == state_.screens.end() ? nullptr : &*found;
}

const OverworldMapEditor::DraftScreen* OverworldMapEditor::draftScreen(
    OverworldScreenId id) const
{
    const auto found = std::ranges::find(
        state_.screens, id,
        [](const DraftScreen& value) { return value.spec.id; });
    return found == state_.screens.end() ? nullptr : &*found;
}

bool OverworldMapEditor::slotAvailable(
    OverworldSlot slot,
    std::optional<OverworldScreenId> ignore) const
{
    return std::ranges::none_of(
        state_.screens,
        [&](const DraftScreen& screen) {
            return screen.spec.slot == slot && screen.spec.id != ignore;
        });
}

OverworldScreenId OverworldMapEditor::nextScreenId() const
{
    OverworldScreenId maximum = 0;
    for (const DraftScreen& screen : state_.screens) {
        maximum = std::max(maximum, screen.spec.id);
    }
    for (OverworldScreenId id : deletedScreens()) {
        maximum = std::max(maximum, id);
    }
    if (maximum == std::numeric_limits<OverworldScreenId>::max()) {
        throw std::runtime_error("overworld screen ID space is exhausted");
    }
    return maximum + 1;
}

Level::Definition OverworldMapEditor::defaultDefinition() const
{
    const std::size_t depth = std::max<std::size_t>(
        state_.screens.empty()
            ? 2
            : state_.screens.front().definition.layers.size(),
        2);
    Level::Definition definition;
    if (!state_.screens.empty()) {
        definition.waterLayer = state_.screens.front().definition.waterLayer;
    }
    definition.layers.assign(
        depth,
        std::vector<std::string>(
            state_.layout.screenHeight,
            std::string(
                state_.layout.screenWidth,
                tileTypeToChar(TileType::Wall))));
    std::ranges::fill(
        definition.layers.front(),
        std::string(
            state_.layout.screenWidth,
            tileTypeToChar(TileType::Ground)));
    return definition;
}

std::optional<GridPosition3> OverworldMapEditor::matchingEndpoint(
    const OverworldScreenSpec& from,
    const OverworldScreenSpec& to,
    GridPosition3 fromCell) const
{
    if (fromCell.x < 0 || fromCell.y < 0 || fromCell.z < 0 ||
        fromCell.x >= static_cast<int>(state_.layout.screenWidth) ||
        fromCell.y >= static_cast<int>(state_.layout.screenHeight)) {
        return std::nullopt;
    }
    const int dx = to.slot.x - from.slot.x;
    const int dy = to.slot.y - from.slot.y;
    if (std::abs(dx) + std::abs(dy) != 1) {
        return std::nullopt;
    }
    GridPosition3 result = fromCell;
    if (dx == 1 &&
        fromCell.x == static_cast<int>(state_.layout.screenWidth) - 1) {
        result.x = 0;
    } else if (dx == -1 && fromCell.x == 0) {
        result.x = static_cast<int>(state_.layout.screenWidth) - 1;
    } else if (dy == 1 &&
               fromCell.y == static_cast<int>(state_.layout.screenHeight) - 1) {
        result.y = 0;
    } else if (dy == -1 && fromCell.y == 0) {
        result.y = static_cast<int>(state_.layout.screenHeight) - 1;
    } else {
        return std::nullopt;
    }
    return result;
}

bool OverworldMapEditor::supportedWalkable(
    const Level::Definition& definition,
    GridPosition3 cell) const
{
    auto tileAt = [&](GridPosition3 target) -> std::optional<TileType> {
        if (target.x < 0 || target.y < 0 || target.z < 0 ||
            target.z >= static_cast<int>(definition.layers.size())) {
            return std::nullopt;
        }
        const auto& layer = definition.layers[static_cast<std::size_t>(target.z)];
        if (target.y >= static_cast<int>(layer.size()) ||
            target.x >= static_cast<int>(layer[static_cast<std::size_t>(target.y)].size())) {
            return std::nullopt;
        }
        return charToTileType(
            layer[static_cast<std::size_t>(target.y)]
                 [static_cast<std::size_t>(target.x)]);
    };
    const std::optional<TileType> tile = tileAt(cell);
    GridPosition3 below = cell;
    --below.z;
    const std::optional<TileType> support = tileAt(below);
    return tile && support &&
        tileTypeAllowsEntity(*tile) && tileTypeSupportsEntity(*support);
}

void OverworldMapEditor::record(State before, std::string status)
{
    undo_.push_back(std::move(before));
    redo_.clear();
    status_ = std::move(status);
}

} // namespace sokoban
