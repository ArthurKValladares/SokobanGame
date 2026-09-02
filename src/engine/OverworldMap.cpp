#include "engine/OverworldMap.hpp"

#include "engine/LevelCatalog.hpp"
#include "engine/TileTypes.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sokoban {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr uint32_t maxScreenDimension = 256;
constexpr int64_t maxSlotSpan = 128;
constexpr uint64_t maxComposedCells = 16ULL * 1024ULL * 1024ULL;

[[noreturn]] void fail(
    std::string_view context,
    const std::string& message)
{
    throw std::runtime_error(
        "invalid overworld layout " + std::string(context) + ": " + message);
}

void requireObject(const Json& value, std::string_view context)
{
    if (!value.is_object()) {
        fail(context, "must be an object");
    }
}

void rejectUnknownProperties(
    const Json& value,
    std::initializer_list<std::string_view> allowed,
    std::string_view context)
{
    requireObject(value, context);
    for (const auto& [key, ignored] : value.items()) {
        (void)ignored;
        if (std::ranges::find(allowed, key) == allowed.end()) {
            fail(context, "unknown property '" + key + "'");
        }
    }
}

const Json& required(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        fail(context, "missing required property '" + std::string(key) + "'");
    }
    return *found;
}

int integer(
    const Json& value,
    std::string_view context)
{
    if (!value.is_number_integer()) {
        fail(context, "must be an integer");
    }
    try {
        return value.get<int>();
    } catch (const Json::exception&) {
        fail(context, "is out of range");
    }
}

uint32_t positiveUint32(
    const Json& value,
    std::string_view context)
{
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        fail(context, "must be an integer");
    }
    try {
        const int64_t decoded = value.get<int64_t>();
        if (decoded <= 0 ||
            decoded > static_cast<int64_t>(
                std::numeric_limits<uint32_t>::max())) {
            fail(context, "must be a positive 32-bit integer");
        }
        return static_cast<uint32_t>(decoded);
    } catch (const Json::exception&) {
        fail(context, "is out of range");
    }
}

OverworldSlot slotFromJson(
    const Json& value,
    std::string_view context)
{
    if (!value.is_array() || value.size() != 2) {
        fail(context, "must be an array of two integers");
    }
    return {
        integer(value[0], std::string(context) + "[0]"),
        integer(value[1], std::string(context) + "[1]"),
    };
}

std::filesystem::path screenFileFromJson(
    const Json& value,
    std::string_view context)
{
    if (!value.is_string()) {
        fail(context, "must be a string");
    }
    const std::string decoded = value.get<std::string>();
    if (decoded.empty()) {
        fail(context, "must not be empty");
    }
    const std::filesystem::path path(decoded);
    if (path.is_absolute() || path.has_root_path() ||
        !path.parent_path().empty() || path.filename() != path ||
        decoded == "." || decoded == "..") {
        fail(context, "must name one file directly inside the overworld directory");
    }
    return path;
}

void normalizeLayout(OverworldLayout& layout)
{
    std::ranges::sort(layout.screens, {}, &OverworldScreenSpec::id);
}

OrderedJson layoutToJson(OverworldLayout layout)
{
    normalizeLayout(layout);
    OrderedJson screens = OrderedJson::array();
    for (const OverworldScreenSpec& screen : layout.screens) {
        screens.push_back({
            { "id", screen.id },
            { "file", screen.file.generic_string() },
            { "slot", { screen.slot.x, screen.slot.y } },
        });
    }
    return {
        { "format", layout.format },
        { "screenSize", { layout.screenWidth, layout.screenHeight } },
        { "screens", std::move(screens) },
    };
}

void validateBasicLayout(const OverworldLayout& layout)
{
    if (layout.format != 3) {
        throw std::runtime_error("unsupported overworld layout format " +
            std::to_string(layout.format));
    }
    if (layout.screenWidth == 0 || layout.screenHeight == 0 ||
        layout.screenWidth > maxScreenDimension ||
        layout.screenHeight > maxScreenDimension) {
        throw std::runtime_error(
            "overworld screenSize must be between 1 and " +
            std::to_string(maxScreenDimension));
    }
    if (layout.screens.empty()) {
        throw std::runtime_error("overworld layout must contain at least one screen");
    }

    std::set<OverworldScreenId> ids;
    std::set<std::pair<int, int>> slots;
    std::set<std::string> files;
    for (const OverworldScreenSpec& screen : layout.screens) {
        if (screen.id == 0) {
            throw std::runtime_error("overworld screen IDs must be positive");
        }
        const std::string expected =
            "screen" + std::to_string(screen.id) + ".scr";
        if (screen.file.generic_string() != expected) {
            throw std::runtime_error(
                "overworld screen " + std::to_string(screen.id) +
                " must use stable-ID filename " + expected);
        }
        if (!ids.insert(screen.id).second) {
            throw std::runtime_error(
                "overworld layout contains duplicate screen ID " +
                std::to_string(screen.id));
        }
        if (!slots.emplace(screen.slot.x, screen.slot.y).second) {
            throw std::runtime_error(
                "overworld layout contains duplicate slot " +
                std::to_string(screen.slot.x) + "," +
                std::to_string(screen.slot.y));
        }
        if (!files.insert(screen.file.generic_string()).second) {
            throw std::runtime_error(
                "overworld layout contains duplicate screen file " +
                screen.file.generic_string());
        }
    }
}

const OverworldScreenRuntime* findRuntimeScreen(
    const std::vector<OverworldScreenRuntime>& screens,
    OverworldScreenId id)
{
    const auto found = std::ranges::find(screens, id, &OverworldScreenRuntime::id);
    return found == screens.end() ? nullptr : &*found;
}

void validateComponentDefinition(
    const Level::Definition& definition,
    const OverworldScreenSpec& spec,
    uint32_t width,
    uint32_t height,
    std::optional<uint32_t> expectedWaterLayer)
{
    const std::string name = spec.file.generic_string();
    if (definition.layers.empty()) {
        throw std::runtime_error("overworld screen " + name + " has no layers");
    }
    if (definition.waterLayer != expectedWaterLayer) {
        throw std::runtime_error(
            "all overworld screens must use the same optional water layer; " +
            name + " differs");
    }
    for (std::size_t z = 0; z < definition.layers.size(); ++z) {
        const auto& layer = definition.layers[z];
        if (layer.size() != height) {
            throw std::runtime_error(
                "overworld screen " + name + " layer " +
                std::to_string(z) + " must have exactly " +
                std::to_string(height) + " rows");
        }
        for (std::size_t y = 0; y < layer.size(); ++y) {
            if (layer[y].size() != width) {
                throw std::runtime_error(
                    "overworld screen " + name + " layer " +
                    std::to_string(z) + " row " + std::to_string(y) +
                    " must have exactly " + std::to_string(width) +
                    " columns");
            }
            for (char character : layer[y]) {
                const std::optional<TileType> tile = charToTileType(character);
                if (!tile) {
                    throw std::runtime_error(
                        "overworld screen " + name +
                        " contains an unknown tile character");
                }
                if (*tile == TileType::End) {
                    throw std::runtime_error(
                        "overworld component screens may not contain '" +
                        std::string(1, character) + "' tiles: " + name);
                }
            }
        }
    }
}

bool localCellInScreen(
    const OverworldScreenRuntime& screen,
    GridPosition3 cell,
    uint32_t width,
    uint32_t height)
{
    return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 &&
        cell.x < static_cast<int>(width) &&
        cell.y < static_cast<int>(height) &&
        cell.z < static_cast<int>(screen.depth);
}

GridPosition3 translate(
    const OverworldScreenRuntime& screen,
    GridPosition3 local)
{
    return {
        screen.origin.x + local.x,
        screen.origin.y + local.y,
        local.z,
    };
}

uint64_t fnvAppend(uint64_t hash, std::string_view text)
{
    constexpr uint64_t prime = 1099511628211ULL;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

uint64_t mapFingerprint(
    const OverworldLayout& layout,
    const std::vector<OverworldScreenRuntime>& screens)
{
    uint64_t hash = 14695981039346656037ULL;
    hash = fnvAppend(hash, layoutToJson(layout).dump());
    for (const OverworldScreenRuntime& screen : screens) {
        hash = fnvAppend(hash, "\nscreen:");
        hash = fnvAppend(hash, std::to_string(screen.id));
        for (const std::string& line :
             Level::serializeDefinition(screen.definition)) {
            hash = fnvAppend(hash, "\n");
            hash = fnvAppend(hash, line);
        }
    }
    return hash;
}

// Every screen override names a real screen, exactly once.
//
// Lifted out of load() unchanged. It needs nothing but the override, which is
// why it is a free function while the two stages below are not.
void validateDraftOverrides(
    const std::optional<OverworldDraftOverride>& draftOverride)
{
    if (draftOverride) {
        for (std::size_t index = 0;
             index < draftOverride->definitions.size();
             ++index) {
            const OverworldScreenId id =
                draftOverride->definitions[index].screen;
            if (id == 0 || std::ranges::any_of(
                    draftOverride->definitions.begin(),
                    draftOverride->definitions.begin() +
                        static_cast<std::ptrdiff_t>(index),
                    [id](const OverworldDefinitionOverride& candidate) {
                        return candidate.screen == id;
                    })) {
                throw std::runtime_error(
                    "overworld draft contains a missing or duplicate screen override");
            }
        }
    }
}

} // namespace

OverworldLayout loadOverworldLayout(const std::filesystem::path& path)
{
    try {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error(
                "cannot read overworld layout " + path.string());
        }
        const Json root = Json::parse(file);
        rejectUnknownProperties(
            root,
            { "format", "screenSize", "screens" },
            path.string());

        OverworldLayout layout;
        layout.format = integer(
            required(root, "format", path.string()),
            path.string() + ".format");
        const Json& screenSize = required(root, "screenSize", path.string());
        if (!screenSize.is_array() || screenSize.size() != 2) {
            fail(path.string() + ".screenSize", "must be an array of two integers");
        }
        layout.screenWidth = positiveUint32(
            screenSize[0], path.string() + ".screenSize[0]");
        layout.screenHeight = positiveUint32(
            screenSize[1], path.string() + ".screenSize[1]");
        const Json& screens = required(root, "screens", path.string());
        if (!screens.is_array()) {
            fail(path.string() + ".screens", "must be an array");
        }
        for (std::size_t index = 0; index < screens.size(); ++index) {
            const Json& encoded = screens[index];
            const std::string context =
                path.string() + ".screens[" + std::to_string(index) + "]";
            rejectUnknownProperties(encoded, { "id", "file", "slot" }, context);
            layout.screens.push_back({
                .id = positiveUint32(
                    required(encoded, "id", context), context + ".id"),
                .file = screenFileFromJson(
                    required(encoded, "file", context), context + ".file"),
                .slot = slotFromJson(
                    required(encoded, "slot", context), context + ".slot"),
            });
        }

        normalizeLayout(layout);
        validateBasicLayout(layout);
        return layout;
    } catch (const Json::exception& error) {
        throw std::runtime_error(
            "invalid overworld layout " + path.string() + ": " +
            error.what());
    }
}

void writeOverworldLayout(
    const std::filesystem::path& path,
    const OverworldLayout& layout)
{
    OverworldLayout normalized = layout;
    normalizeLayout(normalized);
    validateBasicLayout(normalized);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot write overworld layout " + path.string());
    }
    file << layoutToJson(normalized).dump(2) << '\n';
    file.close();
    if (!file) {
        throw std::runtime_error("cannot write overworld layout " + path.string());
    }
}

OverworldMap OverworldMap::load(
    const std::filesystem::path& overworldRoot,
    std::optional<OverworldDraftOverride> draftOverride)
{
    OverworldMap map;
    map.layout_ = draftOverride && draftOverride->layout
        ? *draftOverride->layout
        : loadOverworldLayout(overworldRoot / "layout.json");
    validateBasicLayout(map.layout_);

    validateDraftOverrides(draftOverride);

    const SlotExtent slots = validateSlotSpanAndNormalize(map);

    std::optional<uint32_t> commonWaterLayer;
    bool firstScreen = true;
    std::size_t overridesUsed = 0;
    std::size_t playerTileCount = 0;
    uint32_t maximumDepth = 0;
    for (const OverworldScreenSpec& spec : map.layout_.screens) {
        const std::filesystem::path path = overworldRoot / spec.file;
        Level::Definition definition;
        const auto override = draftOverride
            ? std::ranges::find(
                  draftOverride->definitions,
                  spec.id,
                  &OverworldDefinitionOverride::screen)
            : std::vector<OverworldDefinitionOverride>::const_iterator {};
        if (draftOverride && override != draftOverride->definitions.end()) {
            definition = override->definition;
            ++overridesUsed;
        } else {
            definition = Level::loadDefinitionFromFile(path);
        }
        if (firstScreen) {
            commonWaterLayer = definition.waterLayer;
            firstScreen = false;
        }
        validateComponentDefinition(
            definition,
            spec,
            map.layout_.screenWidth,
            map.layout_.screenHeight,
            commonWaterLayer);
        for (const auto& layer : definition.layers) {
            for (const std::string& row : layer) {
                const std::size_t players = static_cast<std::size_t>(
                    std::ranges::count(
                        row, tileTypeToChar(TileType::Player)));
                if (players != 0) {
                    playerTileCount += players;
                    map.startScreen_ = spec.id;
                }
            }
        }
        maximumDepth = std::max(
            maximumDepth,
            static_cast<uint32_t>(definition.layers.size()));
        map.screens_.push_back({
            .id = spec.id,
            .file = spec.file,
            .slot = spec.slot,
            .origin = {
                static_cast<int>(
                    (static_cast<int64_t>(spec.slot.x) - slots.minimum.x) *
                    map.layout_.screenWidth),
                static_cast<int>(
                    (static_cast<int64_t>(spec.slot.y) - slots.minimum.y) *
                    map.layout_.screenHeight),
            },
            .depth = static_cast<uint32_t>(definition.layers.size()),
            .definition = std::move(definition),
        });
    }
    if (draftOverride &&
        overridesUsed != draftOverride->definitions.size()) {
        throw std::runtime_error(
            "overworld draft definition override references a missing screen");
    }
    if (playerTileCount != 1) {
        throw std::runtime_error(
            "overworld must contain exactly one Player tile; found " +
            std::to_string(playerTileCount));
    }

    const uint64_t composedWidth =
        static_cast<uint64_t>(slots.spanX) * map.layout_.screenWidth;
    const uint64_t composedHeight =
        static_cast<uint64_t>(slots.spanY) * map.layout_.screenHeight;
    const uint64_t composedCells = composedWidth * composedHeight * maximumDepth;
    if (composedWidth > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        composedHeight > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        composedCells > maxComposedCells) {
        throw std::runtime_error(
            "composed overworld exceeds the " +
            std::to_string(maxComposedCells) + "-cell safety limit");
    }

    Level::Definition composed;
    composed.waterLayer = commonWaterLayer;
    composed.layers.assign(
        maximumDepth,
        std::vector<std::string>(
            static_cast<std::size_t>(composedHeight),
            std::string(
                static_cast<std::size_t>(composedWidth),
                tileTypeToChar(TileType::Air))));
    // A frame-wide water layer would otherwise turn sparse layout holes into
    // traversable water. A solid column through every runtime layer prevents
    // entering those holes or standing above a one-layer filler. These walls
    // are runtime-only and region-aware rendering omits them.
    if (commonWaterLayer) {
        for (uint32_t z = *commonWaterLayer; z < maximumDepth; ++z) {
            std::ranges::fill(
                composed.layers[z],
                std::string(
                    static_cast<std::size_t>(composedWidth),
                    tileTypeToChar(TileType::Wall)));
        }
    }

    composeScreenGrid(map, composed, maximumDepth);

    map.level_ = Level::loadFromDefinition(composed, "composed overworld");

    map.fingerprint_ = mapFingerprint(map.layout_, map.screens_);
    return map;
}

// The slot bounding box, checked against the safety limit, and the offset that
// puts the top-left screen at the origin.
//
// A private static stage of the load() factory rather than a free function: it
// reads the layout and writes the offset, both private, and load() is building
// an OverworldMap that is not a valid object yet. The body is what was inline,
// unchanged; the four values that outlive it come back in a SlotExtent instead
// of being four loose locals in a 226-line function.
OverworldMap::SlotExtent OverworldMap::validateSlotSpanAndNormalize(
    OverworldMap& map)
{
    const int minSlotX = std::ranges::min(map.layout_.screens, {},
        [](const OverworldScreenSpec& value) { return value.slot.x; }).slot.x;
    const int maxSlotX = std::ranges::max(map.layout_.screens, {},
        [](const OverworldScreenSpec& value) { return value.slot.x; }).slot.x;
    const int minSlotY = std::ranges::min(map.layout_.screens, {},
        [](const OverworldScreenSpec& value) { return value.slot.y; }).slot.y;
    const int maxSlotY = std::ranges::max(map.layout_.screens, {},
        [](const OverworldScreenSpec& value) { return value.slot.y; }).slot.y;
    const int64_t slotSpanX =
        static_cast<int64_t>(maxSlotX) - minSlotX + 1;
    const int64_t slotSpanY =
        static_cast<int64_t>(maxSlotY) - minSlotY + 1;
    if (slotSpanX <= 0 || slotSpanY <= 0 ||
        slotSpanX > maxSlotSpan || slotSpanY > maxSlotSpan) {
        throw std::runtime_error(
            "overworld slot span exceeds the safety limit of " +
            std::to_string(maxSlotSpan));
    }

    const int64_t normalizationX =
        -static_cast<int64_t>(minSlotX) * map.layout_.screenWidth;
    const int64_t normalizationY =
        -static_cast<int64_t>(minSlotY) * map.layout_.screenHeight;
    if (normalizationX < std::numeric_limits<int>::min() ||
        normalizationX > std::numeric_limits<int>::max() ||
        normalizationY < std::numeric_limits<int>::min() ||
        normalizationY > std::numeric_limits<int>::max()) {
        throw std::runtime_error(
            "overworld slot coordinates exceed the supported range");
    }
    map.normalizationOffset_ = {
        static_cast<int>(normalizationX),
        static_cast<int>(normalizationY),
    };
    return { { minSlotX, minSlotY }, slotSpanX, slotSpanY };
}

// Copies every screen's authored layers into the composed level, caps the
// unauthored volume of short screens, translates decorations, and gives each
// selector a runtime id. Also a stage of the factory, for the same reason.
void OverworldMap::composeScreenGrid(
    OverworldMap& map, Level::Definition& composed, uint32_t maximumDepth)
{
    uint32_t nextRuntimeSelectorId = 1;
    for (const OverworldScreenRuntime& screen : map.screens_) {
        for (std::size_t z = 0; z < screen.definition.layers.size(); ++z) {
            for (uint32_t y = 0; y < map.layout_.screenHeight; ++y) {
                const std::string& source =
                    screen.definition.layers[z][static_cast<std::size_t>(y)];
                std::string& destination = composed.layers[z][
                    static_cast<std::size_t>(screen.origin.y) + y];
                std::copy(
                    source.begin(),
                    source.end(),
                    destination.begin() + screen.origin.x);
            }
        }
        // A shorter chunk must not expose supported Air in a layer that exists
        // only because another screen is taller. Fill its unauthored upper
        // volume to the composed ceiling; rendering uses authored depth and
        // does not emit this collision-only cap.
        for (uint32_t z = screen.depth; z < maximumDepth; ++z) {
            for (uint32_t y = 0; y < map.layout_.screenHeight; ++y) {
                std::string& destination = composed.layers[z][
                    static_cast<std::size_t>(screen.origin.y) + y];
                std::fill_n(
                    destination.begin() + screen.origin.x,
                    map.layout_.screenWidth,
                    tileTypeToChar(TileType::Wall));
            }
        }
        for (const Level::Decoration& authored : screen.definition.decorations) {
            Level::Decoration translated = authored;
            translated.position.x += static_cast<float>(screen.origin.x);
            translated.position.y += static_cast<float>(screen.origin.y);
            composed.decorations.push_back(std::move(translated));
        }
        for (const Level::ScreenSelector& authored : screen.definition.selectors) {
            if (nextRuntimeSelectorId == 0) {
                throw std::runtime_error("overworld contains too many selectors");
            }
            Level::ScreenSelector translated = authored;
            translated.id = nextRuntimeSelectorId++;
            translated.cell = translate(screen, authored.cell);
            composed.selectors.push_back(translated);
            map.selectors_.push_back({
                .screen = screen.id,
                .localId = authored.id,
                .runtimeId = translated.id,
                .globalCell = translated.cell,
                .target = authored.target,
            });
        }
    }
}

const OverworldScreenRuntime* OverworldMap::screen(
    OverworldScreenId id) const
{
    return findRuntimeScreen(screens_, id);
}

std::optional<OverworldScreenId> OverworldMap::screenAt(
    GridPosition3 globalCell) const
{
    if (globalCell.z < 0) {
        return std::nullopt;
    }
    for (const OverworldScreenRuntime& candidate : screens_) {
        if (globalCell.x >= candidate.origin.x &&
            globalCell.y >= candidate.origin.y &&
            globalCell.x < candidate.origin.x +
                static_cast<int>(layout_.screenWidth) &&
            globalCell.y < candidate.origin.y +
                static_cast<int>(layout_.screenHeight) &&
            globalCell.z < static_cast<int>(candidate.depth)) {
            return candidate.id;
        }
    }
    return std::nullopt;
}

std::optional<GridPosition3> OverworldMap::toGlobal(
    OverworldScreenId screenId,
    GridPosition3 localCell) const
{
    const OverworldScreenRuntime* owner = screen(screenId);
    if (!owner || !localCellInScreen(
            *owner, localCell, layout_.screenWidth, layout_.screenHeight)) {
        return std::nullopt;
    }
    return translate(*owner, localCell);
}

std::optional<GridPosition3> OverworldMap::toLocal(
    OverworldScreenId screenId,
    GridPosition3 globalCell) const
{
    const OverworldScreenRuntime* owner = screen(screenId);
    if (!owner || screenAt(globalCell) !=
            std::optional<OverworldScreenId> { screenId }) {
        return std::nullopt;
    }
    return GridPosition3 {
        globalCell.x - owner->origin.x,
        globalCell.y - owner->origin.y,
        globalCell.z,
    };
}

std::vector<OverworldScreenId> OverworldMap::visibleNeighborhood(
    OverworldScreenId activeScreen) const
{
    const OverworldScreenRuntime* active = screen(activeScreen);
    if (!active) {
        return {};
    }
    std::vector<OverworldScreenId> result;
    for (const OverworldScreenRuntime& candidate : screens_) {
        if (std::abs(candidate.slot.x - active->slot.x) <= 1 &&
            std::abs(candidate.slot.y - active->slot.y) <= 1) {
            result.push_back(candidate.id);
        }
    }
    std::ranges::sort(result);
    return result;
}

void OverworldMap::validatePuzzleSelectors(
    std::span<const int> puzzleScreenCounts,
    OverworldValidationMode mode) const
{
    std::vector<std::optional<OverworldScreenId>> ownerByLevel(
        puzzleScreenCounts.size());
    std::vector<std::vector<bool>> covered;
    covered.reserve(puzzleScreenCounts.size());
    for (int count : puzzleScreenCounts) {
        if (count < 0) {
            throw std::invalid_argument(
                "puzzle screen counts must not be negative");
        }
        covered.emplace_back(static_cast<std::size_t>(count), false);
    }

    for (const OverworldSelectorRuntime& selector : selectors_) {
        if (!selector.target) {
            // Deleting a puzzle level or screen deliberately leaves its flag
            // in place for the author to reassign or remove. It is inert in
            // gameplay and remains editable after the game starts.
            continue;
        }
        if (!levelLocationExists(puzzleScreenCounts, *selector.target)) {
            throw std::runtime_error(
                "overworld selector " + std::to_string(selector.screen) +
                ":" + std::to_string(selector.localId) +
                " targets a missing puzzle screen");
        }
        const std::size_t level =
            static_cast<std::size_t>(selector.target->level);
        if (ownerByLevel[level] &&
            *ownerByLevel[level] != selector.screen) {
            throw std::runtime_error(
                "puzzle level " + std::to_string(selector.target->level) +
                " has selectors in more than one overworld screen");
        }
        ownerByLevel[level] = selector.screen;
        covered[level][static_cast<std::size_t>(selector.target->screen)] = true;
    }

    if (mode == OverworldValidationMode::Structural) {
        return;
    }
    for (std::size_t level = 0; level < covered.size(); ++level) {
        for (std::size_t screenIndex = 0;
             screenIndex < covered[level].size();
             ++screenIndex) {
            if (!covered[level][screenIndex]) {
                throw std::runtime_error(
                    "overworld has no selector for puzzle level " +
                    std::to_string(level) + " screen " +
                    std::to_string(screenIndex));
            }
        }
    }
}

} // namespace sokoban
