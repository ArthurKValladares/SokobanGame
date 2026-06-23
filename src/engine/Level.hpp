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
    using LayerRows = std::vector<std::vector<std::string>>;

    struct MovableTile {
        TileType type = TileType::Rock;
        GridPosition3 position {};
    };

    static Level loadFromFile(const std::filesystem::path& path);
    static Level loadFromLines(const std::vector<std::string>& lines, std::string_view sourceName);
    static Level loadFromLayers(const LayerRows& layers, std::string_view sourceName);
    [[nodiscard]] static LayerRows parseLayerRows(const std::vector<std::string>& lines, std::string_view sourceName);
    [[nodiscard]] static std::vector<std::string> serializeLayerRows(const LayerRows& layers);

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] uint32_t depth() const { return depth_; }
    [[nodiscard]] GridPosition3 playerStart() const { return playerStart_; }
    [[nodiscard]] const std::vector<MovableTile>& movableTiles() const { return movableTiles_; }
    [[nodiscard]] const std::vector<GridPosition3>& pressurePlates() const { return pressurePlates_; }
    [[nodiscard]] TileType tileAt(uint32_t x, uint32_t y, uint32_t z = 0) const;
    [[nodiscard]] bool inBounds(GridPosition3 position) const;
    [[nodiscard]] bool isWalkable(GridPosition3 position) const;
    [[nodiscard]] bool isEnd(GridPosition3 position) const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t depth_ = 0;
    GridPosition3 playerStart_ {};
    std::vector<MovableTile> movableTiles_;
    std::vector<GridPosition3> pressurePlates_;
    std::vector<TileType> tiles_;
};

} // namespace sokoban
