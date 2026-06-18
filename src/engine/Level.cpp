#include "engine/Level.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

TileType parseTile(char value, bool& foundPlayer, bool& foundRock)
{
    switch (value) {
    case '#':
        return TileType::Wall;
    case 'E':
        return TileType::End;
    case ' ':
        return TileType::Empty;
    case 'C':
        foundPlayer = true;
        return TileType::Empty;
    case 'R':
        foundRock = true;
        return TileType::Empty;
    case 'P':
        return TileType::PressurePlate;
    default:
        throw std::runtime_error(std::string("Unknown level tile character: '") + value + "'");
    }
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

Level Level::loadFromLines(const std::vector<std::string>& lines, std::string_view sourceName)
{
    const std::string source(sourceName);
    if (lines.empty()) {
        throw std::runtime_error("Level is empty: " + source);
    }

    Level level;
    level.height_ = static_cast<uint32_t>(lines.size());
    for (const auto& row : lines) {
        level.width_ = std::max(level.width_, static_cast<uint32_t>(row.size()));
    }

    if (level.width_ == 0) {
        throw std::runtime_error("Level has no tiles: " + source);
    }

    level.tiles_.assign(static_cast<size_t>(level.width_) * level.height_, TileType::Empty);

    bool hasPlayer = false;
    for (uint32_t y = 0; y < level.height_; ++y) {
        for (uint32_t x = 0; x < static_cast<uint32_t>(lines[y].size()); ++x) {
            bool foundPlayerHere = false;
            bool foundRockHere = false;
            level.tiles_[static_cast<size_t>(y) * level.width_ + x] = parseTile(lines[y][x], foundPlayerHere, foundRockHere);
            if (foundPlayerHere) {
                if (hasPlayer) {
                    throw std::runtime_error("Level has more than one player start: " + source);
                }
                hasPlayer = true;
                level.playerStart_ = { static_cast<int>(x), static_cast<int>(y) };
            }
            if (foundRockHere) {
                level.rocks_.push_back({ static_cast<int>(x), static_cast<int>(y) });
            }
            if (level.tiles_[static_cast<size_t>(y) * level.width_ + x] == TileType::PressurePlate) {
                level.pressurePlates_.push_back({ static_cast<int>(x), static_cast<int>(y) });
            }
        }
    }

    if (!hasPlayer) {
        throw std::runtime_error("Level is missing a player start tile 'C': " + source);
    }

    return level;
}

TileType Level::tileAt(uint32_t x, uint32_t y) const
{
    return tiles_[static_cast<size_t>(y) * width_ + x];
}

bool Level::inBounds(GridPosition position) const
{
    return position.x >= 0 &&
        position.y >= 0 &&
        position.x < static_cast<int>(width_) &&
        position.y < static_cast<int>(height_);
}

bool Level::isWalkable(GridPosition position) const
{
    if (!inBounds(position)) {
        return false;
    }

    return tileAt(static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.y)) != TileType::Wall;
}

bool Level::isEnd(GridPosition position) const
{
    if (!inBounds(position)) {
        return false;
    }

    return tileAt(static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.y)) == TileType::End;
}

} // namespace sokoban
