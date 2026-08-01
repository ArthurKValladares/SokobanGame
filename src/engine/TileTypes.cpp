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
    return type == TileType::Player || type == TileType::Rock ||
        type == TileType::Ice || type == TileType::Enemy;
}

bool tileTypeIsSolidBlock(TileType type)
{
    return type == TileType::Ground || type == TileType::Wall;
}

bool tileTypeSupportsEntity(TileType type)
{
    return tileTypeIsSolidBlock(type) || type == TileType::Water;
}

bool tileTypeAllowsEntity(TileType type)
{
    return type == TileType::Air ||
        type == TileType::Decorative ||
        type == TileType::Ladder ||
        tileTypeIsConveyor(type) ||
        type == TileType::End ||
        type == TileType::PressurePlate;
}

bool tileTypeIsSurfaceEntity(TileType type)
{
    return type == TileType::End || type == TileType::PressurePlate;
}

bool tileTypeIsConveyor(TileType type)
{
    return type == TileType::ConveyorUp ||
        type == TileType::ConveyorDown ||
        type == TileType::ConveyorRight ||
        type == TileType::ConveyorLeft;
}

bool tileTypeIsMirror(TileType type)
{
    return type == TileType::MirrorNorthWest ||
        type == TileType::MirrorNorthEast ||
        type == TileType::MirrorSouthWest ||
        type == TileType::MirrorSouthEast;
}

bool tileTypeIsDecorative(TileType type)
{
    return type == TileType::Decorative;
}

bool tileTypeAffectsCameraFit(TileType type)
{
    return type != TileType::Air &&
        type != TileType::Water &&
        !tileTypeIsDecorative(type);
}

std::optional<uint32_t> mirrorOrientationQuarterTurns(TileType type)
{
    switch (type) {
    case TileType::MirrorNorthWest: return 0;
    case TileType::MirrorNorthEast: return 1;
    case TileType::MirrorSouthEast: return 2;
    case TileType::MirrorSouthWest: return 3;
    default: return std::nullopt;
    }
}

Vec4 tileColor(TileType type, bool isActive)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.type == type) {
            return isActive ? definition.activeColor : definition.inactiveColor;
        }
    }

    return { 1.0f, 0.0f, 1.0f, 1.0f };
}

} // namespace sokoban
