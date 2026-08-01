#pragma once

#include "engine/Math.hpp"
#include "engine/TileTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class Level {
public:
    using LayerRows = std::vector<std::vector<std::string>>;

    struct Decoration {
        // Stable manifest model name. Screen files never embed source asset
        // paths or renderer ids, so manifest reordering cannot corrupt them.
        std::string model;
        // World-space pivot. A freshly placed decoration uses the centre of
        // the supporting tile at its top surface.
        Vec3 position {};
        // Euler angles in degrees, applied X then Y then Z.
        Vec3 rotationDegrees {};
        Vec3 scale { 1.0f, 1.0f, 1.0f };

        bool operator==(const Decoration& other) const
        {
            return model == other.model &&
                position.x == other.position.x &&
                position.y == other.position.y &&
                position.z == other.position.z &&
                rotationDegrees.x == other.rotationDegrees.x &&
                rotationDegrees.y == other.rotationDegrees.y &&
                rotationDegrees.z == other.rotationDegrees.z &&
                scale.x == other.scale.x &&
                scale.y == other.scale.y &&
                scale.z == other.scale.z;
        }
    };

    struct Definition {
        LayerRows layers;
        std::optional<uint32_t> waterLayer;
        std::vector<Decoration> decorations;

        bool operator==(const Definition&) const = default;
    };

    struct MovableTile {
        TileType type = TileType::Rock;
        GridPosition3 position {};
    };

    static Level loadFromFile(const std::filesystem::path& path);
    static Level loadFromLines(const std::vector<std::string>& lines, std::string_view sourceName);
    static Level loadFromDefinition(const Definition& definition, std::string_view sourceName);
    static Level loadFromLayers(
        const LayerRows& layers,
        std::string_view sourceName,
        std::optional<uint32_t> waterLayer = std::nullopt,
        const std::vector<Decoration>& decorations = {});
    [[nodiscard]] static Definition parseDefinition(
        const std::vector<std::string>& lines,
        std::string_view sourceName);
    [[nodiscard]] static std::vector<std::string> serializeDefinition(
        const Definition& definition);
    [[nodiscard]] static LayerRows parseLayerRows(const std::vector<std::string>& lines, std::string_view sourceName);
    [[nodiscard]] static std::vector<std::string> serializeLayerRows(const LayerRows& layers);

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] uint32_t depth() const { return depth_; }
    [[nodiscard]] GridPosition3 playerStart() const { return playerStart_; }
    [[nodiscard]] const std::vector<MovableTile>& movableTiles() const { return movableTiles_; }
    [[nodiscard]] const std::vector<GridPosition3>& enemyStarts() const { return enemyStarts_; }
    [[nodiscard]] const std::vector<GridPosition3>& pressurePlates() const { return pressurePlates_; }
    [[nodiscard]] std::optional<uint32_t> waterLayer() const { return waterLayer_; }
    [[nodiscard]] const std::vector<Decoration>& decorations() const { return decorations_; }
    [[nodiscard]] TileType authoredTileAt(uint32_t x, uint32_t y, uint32_t z = 0) const;
    [[nodiscard]] TileType tileAt(uint32_t x, uint32_t y, uint32_t z = 0) const;
    [[nodiscard]] std::optional<TileType> supportingTileAt(GridPosition3 position) const;
    [[nodiscard]] bool inBounds(GridPosition3 position) const;
    [[nodiscard]] bool isWalkable(GridPosition3 position) const;
    [[nodiscard]] bool isEnd(GridPosition3 position) const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t depth_ = 0;
    GridPosition3 playerStart_ {};
    std::vector<MovableTile> movableTiles_;
    std::vector<GridPosition3> enemyStarts_;
    std::vector<GridPosition3> pressurePlates_;
    std::vector<TileType> tiles_;
    std::optional<uint32_t> waterLayer_;
    std::vector<Decoration> decorations_;
};

} // namespace sokoban
