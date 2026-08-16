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

bool containsPlayer(const Level::Definition& definition)
{
    return std::ranges::any_of(
        definition.layers,
        [](const std::vector<std::string>& layer) {
            return std::ranges::any_of(
                layer,
                [](const std::string& row) {
                    return row.find(tileTypeToChar(TileType::Player)) !=
                        std::string::npos;
                });
        });
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
    (void)reload();
}

bool OverworldMapEditor::reload()
{
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
        const auto playerScreen = std::ranges::find_if(
            loaded.screens,
            [](const DraftScreen& screen) {
                return containsPlayer(screen.definition);
            });
        loaded.selected = playerScreen != loaded.screens.end()
            ? std::optional<OverworldScreenId> { playerScreen->spec.id }
            : std::optional<OverworldScreenId> {
                  loaded.layout.screens.front().id };
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

bool OverworldMapEditor::addAdjacentScreen(
    OverworldScreenId source,
    OverworldSlot slot)
{
    if (!screen(source) || !slotAvailable(slot)) {
        status_ = "Choose an empty cardinal slot beside an existing screen.";
        return false;
    }
    const OverworldScreenSpec* sourceSpec = screen(source);
    if (!sourceSpec ||
        std::abs(slot.x - sourceSpec->slot.x) +
                std::abs(slot.y - sourceSpec->slot.y) !=
            1) {
        status_ = "New screens must be placed directly north, east, south, or west.";
        return false;
    }
    if (!addScreen(slot)) {
        return false;
    }
    status_ = "Added adjacent ground screen " +
        std::to_string(*state_.selected) + ".";
    return true;
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
        " to the draft.");
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
    if (!draftScreen(id)) {
        status_ = "Cannot delete a missing overworld screen.";
        return false;
    }
    State before = state_;
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
    state_.selected = state_.screens.empty()
        ? std::nullopt
        : std::optional<OverworldScreenId> { state_.screens.front().spec.id };
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
            " to the draft.");
        return true;
    } catch (const std::exception& error) {
        status_ = error.what();
        return false;
    }
}

bool OverworldMapEditor::undo()
{
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
                tileTypeToChar(TileType::Air))));
    std::ranges::fill(
        definition.layers.front(),
        std::string(
            state_.layout.screenWidth,
            tileTypeToChar(TileType::Ground)));
    return definition;
}

void OverworldMapEditor::record(State before, std::string status)
{
    undo_.push_back(std::move(before));
    redo_.clear();
    status_ = std::move(status);
}

} // namespace sokoban
