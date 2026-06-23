#include "engine/Level.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

constexpr std::string_view layerPrefix = "@layer ";

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

size_t tileIndex(uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height)
{
    return (static_cast<size_t>(z) * height + y) * width + x;
}

} // namespace

Level Level::loadFromFile(const std::filesystem::path& path)
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

    return loadFromLines(lines, path.string());
}

Level::LayerRows Level::parseLayerRows(const std::vector<std::string>& lines, std::string_view sourceName)
{
    const std::string source(sourceName);
    if (lines.empty()) {
        throw std::runtime_error("Level is empty: " + source);
    }

    const bool layered = std::ranges::any_of(lines, [](const std::string& line) {
        return line.starts_with(layerPrefix);
    });
    if (!layered) {
        return { lines };
    }

    LayerRows layers;
    std::optional<uint32_t> currentLayer;
    for (const std::string& line : lines) {
        if (line.starts_with(layerPrefix)) {
            const std::optional<uint32_t> layer = parseLayerHeader(line);
            if (!layer || *layer != layers.size()) {
                throw std::runtime_error(
                    "Layer headers must be sequential, starting with '@layer 0': " + source);
            }
            layers.emplace_back();
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
        layers[*currentLayer].push_back(line);
    }

    if (layers.empty()) {
        throw std::runtime_error("Level contains no layers: " + source);
    }
    if (std::ranges::any_of(layers, [](const std::vector<std::string>& layer) { return layer.empty(); })) {
        throw std::runtime_error("Every layer must contain at least one row: " + source);
    }

    return layers;
}

std::vector<std::string> Level::serializeLayerRows(const LayerRows& layers)
{
    if (layers.size() == 1) {
        return layers.front();
    }

    std::vector<std::string> lines;
    for (size_t layer = 0; layer < layers.size(); ++layer) {
        if (layer > 0) {
            lines.emplace_back();
        }
        lines.push_back(std::string(layerPrefix) + std::to_string(layer));
        lines.insert(lines.end(), layers[layer].begin(), layers[layer].end());
    }
    return lines;
}

Level Level::loadFromLines(const std::vector<std::string>& lines, std::string_view sourceName)
{
    LayerRows layers = parseLayerRows(lines, sourceName);
    const bool usesLayerHeaders = std::ranges::any_of(lines, [](const std::string& line) {
        return line.starts_with(layerPrefix);
    });
    if (!usesLayerHeaders && layers.size() == 1) {
        size_t width = 0;
        for (const std::string& row : layers.front()) {
            width = std::max(width, row.size());
        }
        for (std::string& row : layers.front()) {
            row.resize(width, tileTypeToChar(TileType::Empty));
        }
    }
    return loadFromLayers(layers, sourceName);
}

Level Level::loadFromLayers(const LayerRows& sourceLayers, std::string_view sourceName)
{
    const std::string source(sourceName);
    if (sourceLayers.empty()) {
        throw std::runtime_error("Level contains no layers: " + source);
    }

    Level level;
    level.depth_ = static_cast<uint32_t>(sourceLayers.size());
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

                level.tiles_[tileIndex(x, y, z, level.width_, level.height_)] = tileTypeInitialFloor(*tile);
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

    return level;
}

TileType Level::tileAt(uint32_t x, uint32_t y, uint32_t z) const
{
    return tiles_[tileIndex(x, y, z, width_, height_)];
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
    return tile != TileType::Wall && tile != TileType::Air;
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

} // namespace sokoban
