#include "engine/TileTypes.hpp"

namespace sokoban {

const std::array<TileTypeDefinition, tileTypeCount>& tileTypeDefinitions()
{
    return tileTypeDefinitionTable;
}

char tileTypeToChar(TileType type)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.type == type) {
            return definition.character;
        }
    }

    return '\0';
}

std::optional<TileType> charToTileType(char character)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.character == character) {
            return definition.type;
        }
    }

    return std::nullopt;
}

std::string_view tileTypeName(TileType type)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.type == type) {
            return definition.name;
        }
    }

    return "Unknown";
}

bool tileTypeOccupiesLevelCell(TileType type)
{
    return type == TileType::Player || type == TileType::Rock;
}

TileType tileTypeInitialFloor(TileType type)
{
    return tileTypeOccupiesLevelCell(type) ? TileType::Empty : type;
}

Vec4 tileColor(TileType type, bool endUnlocked)
{
    switch (type) {
    case TileType::Wall:
        return { 0.62f, 0.32f, 0.09f, 1.0f };
    case TileType::End:
        return endUnlocked ? Vec4 { 1.0f, 0.05f, 0.04f, 1.0f } : Vec4 { 0.38f, 0.04f, 0.04f, 1.0f };
    case TileType::PressurePlate:
        return { 0.18f, 0.18f, 0.18f, 1.0f };
    case TileType::Player:
        return { 0.0f, 1.0f, 0.15f, 1.0f };
    case TileType::Rock:
        return { 0.20f, 0.10f, 0.04f, 1.0f };
    case TileType::Empty:
        return { 0.82f, 0.82f, 0.84f, 1.0f };
    case TileType::Count:
        return { 1.0f, 0.0f, 1.0f, 1.0f };
    }

    return { 1.0f, 0.0f, 1.0f, 1.0f };
}

} // namespace sokoban
