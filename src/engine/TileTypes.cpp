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

std::optional<TileType> tileTypeFromName(std::string_view name)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.name == name) {
            return definition.type;
        }
    }

    return std::nullopt;
}

TileProperty tileTypeProperties(TileType type)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitionTable) {
        if (definition.type == type) {
            return definition.properties;
        }
    }

    return TileProperty::None;
}

bool tileTypeHasProperty(TileType type, TileProperty property)
{
    return hasProperty(tileTypeProperties(type), property);
}

bool tileTypeIsPlate(TileType type)
{
    return tileTypeHasProperty(type, TileProperty::Plate);
}

bool tileTypeCanStandOnPlate(TileType type)
{
    return tileTypeOccupiesLevelCell(type) || tileTypeIsMirror(type);
}

bool tileTypeOccupiesLevelCell(TileType type)
{
    return tileTypeIsPlayerStart(type) || type == TileType::Rock ||
        type == TileType::Ice || type == TileType::Enemy ||
        tileTypeIsTurret(type);
}

bool tileTypeIsSolidBlock(TileType type)
{
    // An elevator is solid wherever its platform currently rests. The level
    // grid only knows where it was authored; rules ask about the live
    // position (see rules::elevatorPlatformAt).
    return type == TileType::Ground || type == TileType::Wall ||
        type == TileType::Elevator;
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
        tileTypeIsPlate(type);
}

bool tileTypeIsSurfaceEntity(TileType type)
{
    return tileTypeIsPlate(type);
}

bool tileTypeIsPlayerStart(TileType type)
{
    return type == TileType::Player || type == TileType::Rogue ||
        type == TileType::Knight || type == TileType::Druid ||
        type == TileType::Witch || type == TileType::Bard;
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

bool tileTypeIsTurret(TileType type)
{
    return type == TileType::TurretNorth ||
        type == TileType::TurretEast ||
        type == TileType::TurretSouth ||
        type == TileType::TurretWest;
}

bool tileTypeIsDecorative(TileType type)
{
    return type == TileType::Decorative;
}

bool tileTypeIsRotator(TileType type)
{
    return type == TileType::RotatorClockwise ||
        type == TileType::RotatorCounterClockwise;
}

bool tileTypeIsElevator(TileType type)
{
    return type == TileType::Elevator;
}

std::optional<int> rotatorQuarterTurns(TileType type)
{
    switch (type) {
    case TileType::RotatorClockwise: return 1;
    case TileType::RotatorCounterClockwise: return -1;
    default: return std::nullopt;
    }
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
