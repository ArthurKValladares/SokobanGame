#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace sokoban {

enum class TileType {
    Empty,
    Wall,
    End,
    PressurePlate,
    Player,
    Rock,
    Count,
};

struct TileTypeDefinition {
    TileType type = TileType::Empty;
    char character = ' ';
    std::string_view name;
};

inline constexpr auto tileTypeCount = static_cast<std::size_t>(TileType::Count);
inline constexpr std::array<TileTypeDefinition, tileTypeCount> tileTypeDefinitionTable {
    TileTypeDefinition { TileType::Empty, ' ', "Empty" },
    TileTypeDefinition { TileType::Wall, '#', "Wall" },
    TileTypeDefinition { TileType::End, 'E', "End" },
    TileTypeDefinition { TileType::PressurePlate, 'P', "Pressure" },
    TileTypeDefinition { TileType::Player, 'C', "Player" },
    TileTypeDefinition { TileType::Rock, 'R', "Rock" },
};

[[nodiscard]] const std::array<TileTypeDefinition, tileTypeCount>& tileTypeDefinitions();
[[nodiscard]] char tileTypeToChar(TileType type);
[[nodiscard]] std::optional<TileType> charToTileType(char character);
[[nodiscard]] std::string_view tileTypeName(TileType type);
[[nodiscard]] bool tileTypeOccupiesLevelCell(TileType type);
[[nodiscard]] TileType tileTypeInitialFloor(TileType type);
[[nodiscard]] Vec4 tileColor(TileType type, bool endUnlocked);

[[nodiscard]] constexpr bool tileTypeDefinitionsContainCharacter(char character)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.character == character) {
            return true;
        }
    }

    return false;
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

static_assert(tileTypeDefinitionsAreOneToOne(), "TileType definitions must map one-to-one with unique characters.");

} // namespace sokoban
