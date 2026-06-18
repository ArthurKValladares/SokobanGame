#pragma once

#include "engine/Math.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sokoban {

enum class TileType {
    Empty,
    Wall,
    End,
};

class Level {
public:
    static Level loadFromFile(const std::filesystem::path& path);

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] Vec2 playerStart() const { return playerStart_; }
    [[nodiscard]] TileType tileAt(uint32_t x, uint32_t y) const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    Vec2 playerStart_ {};
    std::vector<TileType> tiles_;
};

} // namespace sokoban
