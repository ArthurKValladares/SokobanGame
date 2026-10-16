#include "engine/TileTypes.hpp"

namespace sokoban {

TileType rotateTileType(TileType type, int quarterTurns)
{
    const int turns = (quarterTurns % 4 + 4) % 4;
    const auto rotated = [type, turns]<std::size_t Count>(
                             const std::array<TileType, Count>& variants)
        -> std::optional<TileType> {
        for (std::size_t index = 0; index < Count; ++index) {
            if (variants[index] == type) {
                return variants[(index + static_cast<std::size_t>(turns)) % Count];
            }
        }
        return std::nullopt;
    };
    for (const auto& variants : {
             std::array { TileType::ConveyorUp, TileType::ConveyorRight,
                 TileType::ConveyorDown, TileType::ConveyorLeft },
             std::array { TileType::MirrorNorthWest, TileType::MirrorNorthEast,
                 TileType::MirrorSouthEast, TileType::MirrorSouthWest },
             std::array { TileType::TurretNorth, TileType::TurretEast,
                 TileType::TurretSouth, TileType::TurretWest },
             std::array { TileType::RobotNorth, TileType::RobotEast,
                 TileType::RobotSouth, TileType::RobotWest },
             std::array { TileType::ButtonNorth, TileType::ButtonEast,
                 TileType::ButtonSouth, TileType::ButtonWest },
             std::array { TileType::LeverNorth, TileType::LeverEast,
                 TileType::LeverSouth, TileType::LeverWest },
             std::array { TileType::LecternSouth, TileType::LecternWest,
                 TileType::LecternNorth, TileType::LecternEast },
             std::array { TileType::PortalNorth, TileType::PortalEast,
                 TileType::PortalSouth, TileType::PortalWest },
             std::array { TileType::RailCornerNorthWest, TileType::RailCornerNorthEast,
                 TileType::RailCornerSouthEast, TileType::RailCornerSouthWest } }) {
        if (const auto result = rotated(variants)) return *result;
    }
    for (const auto& variants : {
             std::array { TileType::RailStraightNorthSouth, TileType::RailStraightEastWest },
             std::array { TileType::RailStopNorthSouth, TileType::RailStopEastWest } }) {
        if (const auto result = rotated(variants)) return *result;
    }
    return type;
}

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
    if (name == "Portal") {
        return TileType::PortalNorth;
    }
    if (name == "Lectern") {
        return TileType::LecternSouth;
    }
    if (name == "Button") {
        return TileType::ButtonNorth;
    }
    if (name == "Lever") {
        return TileType::LeverNorth;
    }
    if (name == "Robot") {
        return TileType::RobotNorth;
    }
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

std::optional<uint32_t> buttonOrientationQuarterTurns(TileType type)
{
    switch (type) {
    case TileType::ButtonNorth: return 0;
    case TileType::ButtonEast: return 1;
    case TileType::ButtonSouth: return 2;
    case TileType::ButtonWest: return 3;
    default: return std::nullopt;
    }
}

std::optional<uint32_t> leverOrientationQuarterTurns(TileType type)
{
    switch (type) {
    case TileType::LeverNorth: return 0;
    case TileType::LeverEast: return 1;
    case TileType::LeverSouth: return 2;
    case TileType::LeverWest: return 3;
    default: return std::nullopt;
    }
}

bool tileTypeCanStandOnPlate(TileType type)
{
    return tileTypeOccupiesLevelCell(type) || tileTypeIsMinecart(type);
}

bool tileTypeCanCoverSurface(TileType occupant, TileType surface)
{
    if (occupant == TileType::MinecartGate) {
        return tileTypeIsRail(surface);
    }
    if (tileTypeIsMinecart(occupant)) {
        return tileTypeIsRailStop(surface);
    }
    return tileTypeOccupiesLevelCell(occupant) && tileTypeIsPlate(surface);
}

bool tileTypeOccupiesLevelCell(TileType type)
{
    return tileTypeIsPlayerStart(type) || type == TileType::Rock ||
        type == TileType::Ice || type == TileType::Enemy ||
        tileTypeIsTurret(type) || tileTypeIsMirror(type) || tileTypeIsRobot(type);
}

bool tileTypeIsSolidBlock(TileType type)
{
    // An elevator is solid wherever its platform currently rests. The level
    // grid only knows where it was authored; rules ask about the live
    // position (see rules::elevatorPlatformAt).
    return tileTypeIsGround(type) || tileTypeIsWall(type) ||
        type == TileType::Elevator || type == TileType::Minecart ||
        tileTypeIsLectern(type);
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
        tileTypeIsWardrobe(type) ||
        tileTypeIsConveyor(type) ||
        tileTypeIsPlate(type) || tileTypeIsRail(type);
}

bool tileTypeIsSurfaceEntity(TileType type)
{
    return tileTypeIsPlate(type) ||
        tileTypeHasProperty(type, TileProperty::Surface);
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

std::optional<uint32_t> robotOrientationQuarterTurns(TileType type)
{
    return tileTypeIsRobot(type)
        ? std::optional<uint32_t>(static_cast<uint32_t>(type) -
              static_cast<uint32_t>(TileType::RobotNorth))
        : std::nullopt;
}

std::optional<uint32_t> lecternOrientationQuarterTurns(TileType type)
{
    switch (type) {
    case TileType::LecternSouth: return 0;
    case TileType::LecternWest: return 1;
    case TileType::LecternNorth: return 2;
    case TileType::LecternEast: return 3;
    default: return std::nullopt;
    }
}

bool tileTypeIsMovableObject(TileType type)
{
    return type == TileType::Rock || type == TileType::Ice ||
        tileTypeIsTurret(type) || tileTypeIsMirror(type) || tileTypeIsRobot(type);
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

bool tileTypeIsRail(TileType type)
{
    return type == TileType::RailStraightNorthSouth ||
        type == TileType::RailStraightEastWest ||
        type == TileType::RailCornerNorthEast ||
        type == TileType::RailCornerSouthEast ||
        type == TileType::RailCornerSouthWest ||
        type == TileType::RailCornerNorthWest ||
        type == TileType::RailStopNorthSouth ||
        type == TileType::RailStopEastWest;
}

bool tileTypeIsRailStop(TileType type)
{
    return type == TileType::RailStopNorthSouth ||
        type == TileType::RailStopEastWest;
}

bool tileTypeIsMinecart(TileType type)
{
    return type == TileType::Minecart;
}

uint8_t railConnectionMask(TileType type)
{
    switch (type) {
    case TileType::RailStraightNorthSouth:
    case TileType::RailStopNorthSouth:
        return railNorth | railSouth;
    case TileType::RailStraightEastWest:
    case TileType::RailStopEastWest:
        return railEast | railWest;
    case TileType::RailCornerNorthEast: return railNorth | railEast;
    case TileType::RailCornerSouthEast: return railSouth | railEast;
    case TileType::RailCornerSouthWest: return railSouth | railWest;
    case TileType::RailCornerNorthWest: return railNorth | railWest;
    default: return 0;
    }
}

std::optional<uint32_t> railOrientationQuarterTurns(TileType type)
{
    switch (type) {
    case TileType::RailStraightNorthSouth:
    case TileType::RailStopNorthSouth:
    case TileType::RailCornerNorthWest:
        return 0;
    case TileType::RailStraightEastWest:
    case TileType::RailStopEastWest:
    case TileType::RailCornerNorthEast:
        return 1;
    case TileType::RailCornerSouthEast: return 2;
    case TileType::RailCornerSouthWest: return 3;
    default: return std::nullopt;
    }
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
