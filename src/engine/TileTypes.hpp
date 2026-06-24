#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace sokoban {

enum class TileType {
    Air,
    Ground,
    Wall,
    End,
    PressurePlate,
    Player,
    Rock,
    Ice,
    Water,
    Count,
};

struct TileTypeDefinition {
    TileType type = TileType::Ground;
    char character = ' ';
    std::string_view name;
    Vec4 activeColor {};
    Vec4 inactiveColor {};
};

inline constexpr auto tileTypeCount = static_cast<std::size_t>(TileType::Count);
inline constexpr std::array<TileTypeDefinition, tileTypeCount> tileTypeDefinitionTable {
    TileTypeDefinition { TileType::Air, ' ', "Air", { 0.0f, 0.0f, 0.0f, 0.0f } },
    TileTypeDefinition { TileType::Ground, '.', "Ground", { 0.82f, 0.82f, 0.84f, 1.0f } },
    TileTypeDefinition { TileType::Wall, '#', "Wall", { 0.62f, 0.32f, 0.09f, 1.0f } },
    TileTypeDefinition { TileType::End, 'E', "End", { 1.0f, 0.05f, 0.04f, 1.0f }, { 0.38f, 0.04f, 0.04f, 1.0f } },
    TileTypeDefinition { TileType::PressurePlate, 'P', "Pressure", { 0.18f, 0.18f, 0.18f, 1.0f } },
    TileTypeDefinition { TileType::Player, 'C', "Player", { 0.0f, 1.0f, 0.15f, 1.0f } },
    TileTypeDefinition { TileType::Rock, 'R', "Rock", { 0.20f, 0.10f, 0.04f, 1.0f } },
    TileTypeDefinition { TileType::Ice, 'I', "Ice", { 0.62f, 0.88f, 1.0f, 1.0f } },
    TileTypeDefinition { TileType::Water, 'W', "Water", { 0.08f, 0.34f, 0.78f, 1.0f } },
};

[[nodiscard]] const std::array<TileTypeDefinition, tileTypeCount>& tileTypeDefinitions();
[[nodiscard]] char tileTypeToChar(TileType type);
[[nodiscard]] std::optional<TileType> charToTileType(char character);
[[nodiscard]] std::string_view tileTypeName(TileType type);
[[nodiscard]] bool tileTypeOccupiesLevelCell(TileType type);
[[nodiscard]] bool tileTypeIsSolidBlock(TileType type);
[[nodiscard]] bool tileTypeSupportsEntity(TileType type);
[[nodiscard]] bool tileTypeAllowsEntity(TileType type);
[[nodiscard]] Vec4 tileColor(TileType type, bool isActive = true);

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
