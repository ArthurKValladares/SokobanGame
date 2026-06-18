#pragma once

#include "engine/Math.hpp"
#include "engine/TileTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class Level {
public:
    static Level loadFromFile(const std::filesystem::path& path);
    static Level loadFromLines(const std::vector<std::string>& lines, std::string_view sourceName);

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] GridPosition playerStart() const { return playerStart_; }
    [[nodiscard]] const std::vector<GridPosition>& rocks() const { return rocks_; }
    [[nodiscard]] const std::vector<GridPosition>& pressurePlates() const { return pressurePlates_; }
    [[nodiscard]] TileType tileAt(uint32_t x, uint32_t y) const;
    [[nodiscard]] bool inBounds(GridPosition position) const;
    [[nodiscard]] bool isWalkable(GridPosition position) const;
    [[nodiscard]] bool isEnd(GridPosition position) const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    GridPosition playerStart_ {};
    std::vector<GridPosition> rocks_;
    std::vector<GridPosition> pressurePlates_;
    std::vector<TileType> tiles_;
};

} // namespace sokoban
