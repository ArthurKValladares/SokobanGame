#include "engine/Level.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

constexpr std::string_view layerPrefix = "@layer ";
constexpr std::string_view waterPrefix = "@water ";
constexpr std::string_view decorationPrefix = "@decoration ";
constexpr std::string_view selectorPrefix = "@selector ";

using Json = nlohmann::json;

std::runtime_error unknownLevelCharacter(char value)
{
    return std::runtime_error(std::string("Unknown level tile character: '") + value + "'");
}

std::optional<uint32_t> parseLayerHeader(std::string_view line)
{
    if (!line.starts_with(layerPrefix)) {
        return std::nullopt;
    }

    uint32_t layer = 0;
    const char* begin = line.data() + layerPrefix.size();
    const char* end = line.data() + line.size();
    const auto result = std::from_chars(begin, end, layer);
    if (result.ec != std::errc {} || result.ptr != end) {
        return std::nullopt;
    }
    return layer;
}

std::optional<uint32_t> parseWaterHeader(std::string_view line)
{
    if (!line.starts_with(waterPrefix)) {
        return std::nullopt;
    }

    uint32_t layer = 0;
    const char* begin = line.data() + waterPrefix.size();
    const char* end = line.data() + line.size();
    const auto result = std::from_chars(begin, end, layer);
    if (result.ec != std::errc {} || result.ptr != end) {
        return std::nullopt;
    }
    return layer;
}

Vec3 parseDecorationVec3(
    const Json& object,
    std::string_view field,
    std::string_view sourceName)
{
    const auto found = object.find(field);
    if (found == object.end() || !found->is_array() || found->size() != 3) {
        throw std::runtime_error(
            "Decoration '" + std::string(field) +
            "' must be an array of three numbers: " +
            std::string(sourceName));
    }

    Vec3 value;
    float* components[] { &value.x, &value.y, &value.z };
    for (size_t i = 0; i < 3; ++i) {
        if (!(*found)[i].is_number()) {
            throw std::runtime_error(
                "Decoration '" + std::string(field) +
                "' must contain only numbers: " +
                std::string(sourceName));
        }
        *components[i] = (*found)[i].get<float>();
        if (!std::isfinite(*components[i])) {
            throw std::runtime_error(
                "Decoration '" + std::string(field) +
                "' must contain finite numbers: " +
                std::string(sourceName));
        }
    }
    return value;
}

void validateDecoration(
    const Level::Decoration& decoration,
    std::string_view sourceName)
{
    if (decoration.model.empty()) {
        throw std::runtime_error(
            "Decoration model must not be empty: " +
            std::string(sourceName));
    }
    const auto finite = [](Vec3 value) {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    };
    if (!finite(decoration.position) ||
        !finite(decoration.rotationDegrees) ||
        !finite(decoration.scale)) {
        throw std::runtime_error(
            "Decoration transforms must contain finite numbers: " +
            std::string(sourceName));
    }
    if (decoration.scale.x <= 0.0f ||
        decoration.scale.y <= 0.0f ||
        decoration.scale.z <= 0.0f) {
        throw std::runtime_error(
            "Decoration scale components must be greater than zero: " +
            std::string(sourceName));
    }
    if (decoration.pointLight) {
        const Level::Decoration::PointLight& light = *decoration.pointLight;
        if (!finite(light.offset) || !finite(light.color) ||
            !std::isfinite(light.intensity) || !std::isfinite(light.range) ||
            !std::isfinite(light.shadowBias) ||
            !std::isfinite(light.shadowOpacity)) {
            throw std::runtime_error(
                "Decoration point-light settings must contain finite values: " +
                std::string(sourceName));
        }
        if (light.color.x < 0.0f || light.color.y < 0.0f ||
            light.color.z < 0.0f || light.intensity < 0.0f ||
            light.range <= 0.0f || light.shadowBias < 0.0f ||
            light.shadowOpacity < 0.0f || light.shadowOpacity > 1.0f) {
            throw std::runtime_error(
                "Decoration point-light color, intensity, range, and shadow settings are out of range: " +
                std::string(sourceName));
        }
    }
}

Level::Decoration parseDecoration(
    std::string_view payload,
    std::string_view sourceName)
{
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object()) {
            throw std::runtime_error("decoration payload is not an object");
        }
        const auto model = object.find("model");
        if (model == object.end() || !model->is_string()) {
            throw std::runtime_error("decoration 'model' must be a string");
        }
        Level::Decoration decoration {
            .model = model->get<std::string>(),
            .position = parseDecorationVec3(
                object, "position", sourceName),
            .rotationDegrees = parseDecorationVec3(
                object, "rotation", sourceName),
            .scale = parseDecorationVec3(
                object, "scale", sourceName),
        };
        if (const auto light = object.find("light");
            light != object.end() && !light->is_null()) {
            if (!light->is_object()) {
                throw std::runtime_error(
                    "decoration 'light' must be an object or null");
            }
            const auto number = [&](std::string_view field) {
                const auto value = light->find(field);
                if (value == light->end() || !value->is_number()) {
                    throw std::runtime_error(
                        "decoration light '" + std::string(field) +
                        "' must be a number");
                }
                return value->get<float>();
            };
            const auto castsShadows = light->find("castsShadows");
            if (castsShadows == light->end() || !castsShadows->is_boolean()) {
                throw std::runtime_error(
                    "decoration light 'castsShadows' must be a boolean");
            }
            decoration.pointLight = Level::Decoration::PointLight {
                .offset = parseDecorationVec3(*light, "offset", sourceName),
                .color = parseDecorationVec3(*light, "color", sourceName),
                .intensity = number("intensity"),
                .range = number("range"),
                .castsShadows = castsShadows->get<bool>(),
                .shadowBias = number("shadowBias"),
                .shadowOpacity = number("shadowOpacity"),
            };
        }
        validateDecoration(decoration, sourceName);
        return decoration;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "Invalid decoration JSON in " + std::string(sourceName) +
            ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "Invalid decoration in " + std::string(sourceName) +
            ": " + error.what());
    }
}

std::string serializeDecoration(const Level::Decoration& decoration)
{
    validateDecoration(decoration, "serialized level");
    Json object {
        { "model", decoration.model },
        { "position", {
              decoration.position.x,
              decoration.position.y,
              decoration.position.z,
          } },
        { "rotation", {
              decoration.rotationDegrees.x,
              decoration.rotationDegrees.y,
              decoration.rotationDegrees.z,
          } },
        { "scale", {
              decoration.scale.x,
              decoration.scale.y,
              decoration.scale.z,
        } },
    };
    if (decoration.pointLight) {
        const Level::Decoration::PointLight& light = *decoration.pointLight;
        object["light"] = {
            { "offset", { light.offset.x, light.offset.y, light.offset.z } },
            { "color", { light.color.x, light.color.y, light.color.z } },
            { "intensity", light.intensity },
            { "range", light.range },
            { "castsShadows", light.castsShadows },
            { "shadowBias", light.shadowBias },
            { "shadowOpacity", light.shadowOpacity },
        };
    }
    return std::string(decorationPrefix) + object.dump();
}

int parseSelectorInteger(
    const Json& object,
    std::string_view field,
    std::string_view sourceName)
{
    const auto found = object.find(field);
    if (found == object.end() || !found->is_number_integer()) {
        throw std::runtime_error(
            "Selector '" + std::string(field) +
            "' must be an integer: " + std::string(sourceName));
    }
    const int value = found->get<int>();
    if (value < 0) {
        throw std::runtime_error(
            "Selector '" + std::string(field) +
            "' must not be negative: " + std::string(sourceName));
    }
    return value;
}

GridPosition3 parseSelectorCell(
    const Json& object,
    std::string_view sourceName)
{
    const auto found = object.find("cell");
    if (found == object.end() || !found->is_array() || found->size() != 3) {
        throw std::runtime_error(
            "Selector 'cell' must be an array of three integers: " +
            std::string(sourceName));
    }
    GridPosition3 cell;
    int* components[] { &cell.x, &cell.y, &cell.z };
    for (size_t i = 0; i < 3; ++i) {
        if (!(*found)[i].is_number_integer()) {
            throw std::runtime_error(
                "Selector 'cell' must contain only integers: " +
                std::string(sourceName));
        }
        *components[i] = (*found)[i].get<int>();
        if (*components[i] < 0) {
            throw std::runtime_error(
                "Selector cell coordinates must not be negative: " +
                std::string(sourceName));
        }
    }
    return cell;
}

void validateSelectorRecord(
    const Level::ScreenSelector& selector,
    std::string_view sourceName)
{
    if (selector.id == 0) {
        throw std::runtime_error(
            "Selector id must be greater than zero: " +
            std::string(sourceName));
    }
    if (selector.cell.x < 0 || selector.cell.y < 0 || selector.cell.z < 0) {
        throw std::runtime_error(
            "Selector cell coordinates must not be negative: " +
            std::string(sourceName));
    }
    if (selector.target &&
        (selector.target->level < 0 || selector.target->screen < 0)) {
        throw std::runtime_error(
            "Selector target coordinates must not be negative: " +
            std::string(sourceName));
    }
}

Level::ScreenSelector parseSelector(
    std::string_view payload,
    std::string_view sourceName)
{
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object()) {
            throw std::runtime_error("selector payload is not an object");
        }
        const int id = parseSelectorInteger(object, "id", sourceName);
        if (id == 0) {
            throw std::runtime_error("selector 'id' must be greater than zero");
        }
        const auto target = object.find("target");
        if (target == object.end()) {
            throw std::runtime_error("selector 'target' is required");
        }
        std::optional<LevelLocation> location;
        if (!target->is_null()) {
            if (!target->is_object()) {
                throw std::runtime_error(
                    "selector 'target' must be an object or null");
            }
            location = LevelLocation {
                .level = parseSelectorInteger(*target, "level", sourceName),
                .screen = parseSelectorInteger(*target, "screen", sourceName),
            };
        }
        Level::ScreenSelector selector {
            .id = static_cast<uint32_t>(id),
            .cell = parseSelectorCell(object, sourceName),
            .target = location,
        };
        validateSelectorRecord(selector, sourceName);
        return selector;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "Invalid selector JSON in " + std::string(sourceName) +
            ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "Invalid selector in " + std::string(sourceName) +
            ": " + error.what());
    }
}

std::string serializeSelector(const Level::ScreenSelector& selector)
{
    validateSelectorRecord(selector, "serialized level");
    Json target = nullptr;
    if (selector.target) {
        target = {
            { "level", selector.target->level },
            { "screen", selector.target->screen },
        };
    }
    const Json object {
        { "id", selector.id },
        { "cell", {
              selector.cell.x,
              selector.cell.y,
              selector.cell.z,
          } },
        { "target", std::move(target) },
    };
    return std::string(selectorPrefix) + object.dump();
}

size_t tileIndex(uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height)
{
    return (static_cast<size_t>(z) * height + y) * width + x;
}

bool hasAdjacentGround(const Level& level, GridPosition3 position)
{
    constexpr std::array<GridPosition, 4> offsets {
        GridPosition { 0, -1 },
        GridPosition { 1, 0 },
        GridPosition { 0, 1 },
        GridPosition { -1, 0 },
    };

    for (GridPosition offset : offsets) {
        const GridPosition3 neighbor {
            position.x + offset.x,
            position.y + offset.y,
            position.z,
        };
        if (!level.inBounds(neighbor)) {
            continue;
        }
        if (level.tileAt(
                static_cast<uint32_t>(neighbor.x),
                static_cast<uint32_t>(neighbor.y),
                static_cast<uint32_t>(neighbor.z)) == TileType::Ground) {
            return true;
        }
    }

    return false;
}

} // namespace

Level Level::loadFromFile(const std::filesystem::path& path)
{
    return loadFromDefinition(loadDefinitionFromFile(path), path.string());
}

Level::Definition Level::loadDefinitionFromFile(
    const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open level file: " + path.string());
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }

    return parseDefinition(lines, path.string());
}

Level::Definition Level::parseDefinition(
    const std::vector<std::string>& lines,
    std::string_view sourceName)
{
    const std::string source(sourceName);
    if (lines.empty()) {
        throw std::runtime_error("Level is empty: " + source);
    }

    const bool layered = std::ranges::any_of(lines, [](const std::string& line) {
        return line.starts_with(layerPrefix);
    });
    if (!layered) {
        if (std::ranges::any_of(lines, [](const std::string& line) {
                return line.starts_with(waterPrefix) ||
                    line.starts_with(decorationPrefix) ||
                    line.starts_with(selectorPrefix);
            })) {
            throw std::runtime_error(
                "Level metadata requires explicit '@layer 0' sections: " + source);
        }
        return { .layers = { lines } };
    }

    Definition definition;
    std::optional<uint32_t> currentLayer;
    for (const std::string& line : lines) {
        if (line.starts_with(selectorPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Selector metadata must appear before '@layer 0': " + source);
            }
            definition.selectors.push_back(parseSelector(
                std::string_view(line).substr(selectorPrefix.size()),
                sourceName));
            continue;
        }

        if (line.starts_with(decorationPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Decoration metadata must appear before '@layer 0': " + source);
            }
            definition.decorations.push_back(parseDecoration(
                std::string_view(line).substr(decorationPrefix.size()),
                sourceName));
            continue;
        }

        if (line.starts_with(waterPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Water metadata must appear before '@layer 0': " + source);
            }
            const std::optional<uint32_t> waterLayer =
                parseWaterHeader(line);
            if (!waterLayer) {
                throw std::runtime_error(
                    "Invalid water layer metadata; expected '@water N': " + source);
            }
            if (definition.waterLayer) {
                throw std::runtime_error(
                    "Level contains more than one '@water' directive: " + source);
            }
            definition.waterLayer = waterLayer;
            continue;
        }

        if (line.starts_with(layerPrefix)) {
            const std::optional<uint32_t> layer = parseLayerHeader(line);
            if (!layer || *layer != definition.layers.size()) {
                throw std::runtime_error(
                    "Layer headers must be sequential, starting with '@layer 0': " + source);
            }
            definition.layers.emplace_back();
            currentLayer = *layer;
            continue;
        }

        if (!currentLayer) {
            if (line.empty()) {
                continue;
            }
            throw std::runtime_error("Level data appears before '@layer 0': " + source);
        }

        if (line.empty()) {
            continue;
        }
        definition.layers[*currentLayer].push_back(line);
    }

    if (definition.layers.empty()) {
        throw std::runtime_error("Level contains no layers: " + source);
    }
    if (std::ranges::any_of(
            definition.layers,
            [](const std::vector<std::string>& layer) {
                return layer.empty();
            })) {
        throw std::runtime_error("Every layer must contain at least one row: " + source);
    }
    if (definition.waterLayer &&
        *definition.waterLayer >= definition.layers.size()) {
        throw std::runtime_error(
            "Water layer must refer to an existing layer: " + source);
    }

    std::ranges::sort(
        definition.selectors, {}, &Level::ScreenSelector::id);
    for (size_t i = 0; i < definition.selectors.size(); ++i) {
        const ScreenSelector& selector = definition.selectors[i];
        validateSelectorRecord(selector, sourceName);
        if (i > 0 && definition.selectors[i - 1].id == selector.id) {
            throw std::runtime_error(
                "Level contains duplicate selector id " +
                std::to_string(selector.id) + ": " + source);
        }
        for (size_t j = 0; j < i; ++j) {
            if (definition.selectors[j].cell == selector.cell) {
                throw std::runtime_error(
                    "Level contains more than one selector at cell " +
                    std::to_string(selector.cell.x) + "," +
                    std::to_string(selector.cell.y) + "," +
                    std::to_string(selector.cell.z) + ": " + source);
            }
        }
    }

    return definition;
}

Level::LayerRows Level::parseLayerRows(
    const std::vector<std::string>& lines,
    std::string_view sourceName)
{
    return parseDefinition(lines, sourceName).layers;
}

std::vector<std::string> Level::serializeDefinition(
    const Definition& definition)
{
    if (definition.layers.size() == 1 &&
        !definition.waterLayer &&
        definition.decorations.empty() &&
        definition.selectors.empty()) {
        return definition.layers.front();
    }

    std::vector<std::string> lines;
    if (definition.waterLayer) {
        lines.push_back(
            std::string(waterPrefix) +
            std::to_string(*definition.waterLayer));
    }
    std::vector<ScreenSelector> selectors = definition.selectors;
    std::ranges::sort(selectors, {}, &ScreenSelector::id);
    for (const ScreenSelector& selector : selectors) {
        lines.push_back(serializeSelector(selector));
    }
    for (const Decoration& decoration : definition.decorations) {
        lines.push_back(serializeDecoration(decoration));
    }
    if (definition.waterLayer || !definition.decorations.empty() ||
        !definition.selectors.empty()) {
        lines.emplace_back();
    }
    for (size_t layer = 0; layer < definition.layers.size(); ++layer) {
        if (layer > 0) {
            lines.emplace_back();
        }
        lines.push_back(std::string(layerPrefix) + std::to_string(layer));
        lines.insert(
            lines.end(),
            definition.layers[layer].begin(),
            definition.layers[layer].end());
    }
    return lines;
}

std::vector<std::string> Level::serializeLayerRows(const LayerRows& layers)
{
    return serializeDefinition({ .layers = layers });
}

Level Level::loadFromLines(const std::vector<std::string>& lines, std::string_view sourceName)
{
    return loadFromDefinition(parseDefinition(lines, sourceName), sourceName);
}

Level Level::loadFromDefinition(
    const Definition& definition,
    std::string_view sourceName)
{
    return loadFromLayers(
        definition.layers,
        sourceName,
        definition.waterLayer,
        definition.decorations,
        definition.selectors);
}

Level Level::loadFromLayers(
    const LayerRows& sourceLayers,
    std::string_view sourceName,
    std::optional<uint32_t> waterLayer,
    const std::vector<Decoration>& decorations,
    const std::vector<ScreenSelector>& selectors)
{
    const std::string source(sourceName);
    if (sourceLayers.empty()) {
        throw std::runtime_error("Level contains no layers: " + source);
    }

    Level level;
    level.depth_ = static_cast<uint32_t>(sourceLayers.size());
    if (waterLayer && *waterLayer >= level.depth_) {
        throw std::runtime_error(
            "Water layer must refer to an existing layer: " + source);
    }
    level.waterLayer_ = waterLayer;
    for (const Decoration& decoration : decorations) {
        validateDecoration(decoration, sourceName);
    }
    level.decorations_ = decorations;
    level.selectors_ = selectors;
    std::ranges::sort(level.selectors_, {}, &ScreenSelector::id);
    for (size_t i = 0; i < level.selectors_.size(); ++i) {
        validateSelectorRecord(level.selectors_[i], sourceName);
        if (i > 0 &&
            level.selectors_[i - 1].id == level.selectors_[i].id) {
            throw std::runtime_error(
                "Level contains duplicate selector id " +
                std::to_string(level.selectors_[i].id) + ": " + source);
        }
        for (size_t j = 0; j < i; ++j) {
            if (level.selectors_[j].cell == level.selectors_[i].cell) {
                throw std::runtime_error(
                    "Level contains more than one selector at the same cell: " +
                    source);
            }
        }
    }
    for (const auto& layer : sourceLayers) {
        level.height_ = std::max(level.height_, static_cast<uint32_t>(layer.size()));
        for (const std::string& row : layer) {
            level.width_ = std::max(level.width_, static_cast<uint32_t>(row.size()));
        }
    }

    if (level.width_ == 0 || level.height_ == 0) {
        throw std::runtime_error("Level has no tiles: " + source);
    }

    level.tiles_.assign(
        static_cast<size_t>(level.width_) * level.height_ * level.depth_,
        TileType::Air);

    bool hasPlayer = false;
    for (uint32_t z = 0; z < level.depth_; ++z) {
        const auto& layer = sourceLayers[z];
        for (uint32_t y = 0; y < static_cast<uint32_t>(layer.size()); ++y) {
            for (uint32_t x = 0; x < static_cast<uint32_t>(layer[y].size()); ++x) {
                const char character = layer[y][x];
                const GridPosition3 position {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };

                const std::optional<TileType> tile = charToTileType(character);
                if (!tile) {
                    throw unknownLevelCharacter(character);
                }

                if (*tile == TileType::Player) {
                    if (hasPlayer) {
                        throw std::runtime_error("Level has more than one player start: " + source);
                    }
                    hasPlayer = true;
                    level.playerStart_ = position;
                }

                if (*tile == TileType::Rock || *tile == TileType::Ice) {
                    level.movableTiles_.push_back({
                        .type = *tile,
                        .position = position,
                    });
                }

                if (*tile == TileType::Enemy) {
                    level.enemyStarts_.push_back(position);
                }

                level.tiles_[tileIndex(x, y, z, level.width_, level.height_)] =
                    tileTypeOccupiesLevelCell(*tile) ? TileType::Air : *tile;
                if (*tile == TileType::PressurePlate) {
                    level.pressurePlates_.push_back(position);
                }
            }
        }
    }

    if (!hasPlayer) {
        throw std::runtime_error(std::string("Level is missing a player start tile '") +
            tileTypeToChar(TileType::Player) + "': " + source);
    }

    for (const ScreenSelector& selector : level.selectors_) {
        if (!level.isWalkable(selector.cell)) {
            throw std::runtime_error(
                "Selector " + std::to_string(selector.id) +
                " must be placed on a supported walkable cell: " + source);
        }
    }

    for (uint32_t z = 0; z < level.depth_; ++z) {
        for (uint32_t y = 0; y < level.height_; ++y) {
            for (uint32_t x = 0; x < level.width_; ++x) {
                if (level.tileAt(x, y, z) != TileType::Ladder) {
                    continue;
                }
                const GridPosition3 position {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };
                if (!hasAdjacentGround(level, position)) {
                    throw std::runtime_error(
                        "Ladder tile 'L' must be next to a ground tile on the same layer: " + source);
                }
            }
        }
    }

    return level;
}

TileType Level::tileAt(uint32_t x, uint32_t y, uint32_t z) const
{
    const TileType authored = authoredTileAt(x, y, z);
    if (authored == TileType::Air &&
        waterLayer_ &&
        z == *waterLayer_) {
        return TileType::Water;
    }
    return authored;
}

TileType Level::authoredTileAt(uint32_t x, uint32_t y, uint32_t z) const
{
    return tiles_[tileIndex(x, y, z, width_, height_)];
}

std::optional<TileType> Level::supportingTileAt(GridPosition3 position) const
{
    const GridPosition3 support {
        position.x,
        position.y,
        position.z - 1,
    };
    if (!inBounds(support)) {
        return std::nullopt;
    }

    return tileAt(
        static_cast<uint32_t>(support.x),
        static_cast<uint32_t>(support.y),
        static_cast<uint32_t>(support.z));
}

bool Level::inBounds(GridPosition3 position) const
{
    return position.x >= 0 &&
        position.y >= 0 &&
        position.z >= 0 &&
        position.x < static_cast<int>(width_) &&
        position.y < static_cast<int>(height_) &&
        position.z < static_cast<int>(depth_);
}

bool Level::isWalkable(GridPosition3 position) const
{
    if (!inBounds(position)) {
        return false;
    }

    const TileType tile = tileAt(
        static_cast<uint32_t>(position.x),
        static_cast<uint32_t>(position.y),
        static_cast<uint32_t>(position.z));
    const std::optional<TileType> support = supportingTileAt(position);
    return tileTypeAllowsEntity(tile) &&
        support &&
        tileTypeSupportsEntity(*support);
}

bool Level::isEnd(GridPosition3 position) const
{
    if (!inBounds(position)) {
        return false;
    }

    return tileAt(
        static_cast<uint32_t>(position.x),
        static_cast<uint32_t>(position.y),
        static_cast<uint32_t>(position.z)) == TileType::End;
}

const Level::ScreenSelector* Level::selectorAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(selectors_, cell, &ScreenSelector::cell);
    return found == selectors_.end() ? nullptr : &*found;
}

} // namespace sokoban
