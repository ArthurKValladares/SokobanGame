#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

enum class TileType {
    Empty,
    Wall,
    End,
    PressurePlate,
    Count,
};

struct TileTypeDefinition {
    TileType type = TileType::Empty;
    char character = ' ';
    std::string_view name;
};

inline constexpr char playerStartCharacter = 'C';
inline constexpr char rockCharacter = 'R';
inline constexpr std::string_view playerStartName = "Player";
inline constexpr std::string_view rockName = "Rock";

inline constexpr auto tileTypeCount = static_cast<std::size_t>(TileType::Count);
inline constexpr std::array<TileTypeDefinition, tileTypeCount> tileTypeDefinitionTable {
    TileTypeDefinition { TileType::Empty, ' ', "Empty" },
    TileTypeDefinition { TileType::Wall, '#', "Wall" },
    TileTypeDefinition { TileType::End, 'E', "End" },
    TileTypeDefinition { TileType::PressurePlate, 'P', "Pressure" },
};

[[nodiscard]] constexpr const std::array<TileTypeDefinition, tileTypeCount>& tileTypeDefinitions()
{
    return tileTypeDefinitionTable;
}

[[nodiscard]] constexpr char tileTypeToChar(TileType type)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.type == type) {
            return definition.character;
        }
    }

    return '\0';
}

[[nodiscard]] constexpr std::optional<TileType> charToTileType(char character)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.character == character) {
            return definition.type;
        }
    }

    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view tileTypeName(TileType type)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.type == type) {
            return definition.name;
        }
    }

    return "Unknown";
}

[[nodiscard]] constexpr bool tileTypeDefinitionsAreOneToOne()
{
    std::array<bool, tileTypeCount> seenTypes {};

    for (std::size_t i = 0; i < tileTypeDefinitionTable.size(); ++i) {
        const auto typeIndex = static_cast<std::size_t>(tileTypeDefinitionTable[i].type);
        if (typeIndex >= tileTypeCount || seenTypes[typeIndex]) {
            return false;
        }
        seenTypes[typeIndex] = true;

        for (std::size_t j = i + 1; j < tileTypeDefinitionTable.size(); ++j) {
            if (tileTypeDefinitionTable[i].character == tileTypeDefinitionTable[j].character) {
                return false;
            }
        }
    }

    for (bool seenType : seenTypes) {
        if (!seenType) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr bool levelEntityCharactersDoNotCollideWithTiles()
{
    return playerStartCharacter != rockCharacter &&
        !charToTileType(playerStartCharacter).has_value() &&
        !charToTileType(rockCharacter).has_value();
}

static_assert(tileTypeDefinitionsAreOneToOne(), "TileType definitions must map one-to-one with unique characters.");
static_assert(levelEntityCharactersDoNotCollideWithTiles(), "Level entity characters must not collide with tile characters.");

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
