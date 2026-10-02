#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <cstdint>

namespace sokoban {

enum class TileType {
    Air,
    Ground,
    Wall,
    End,
    PressurePlate,
    Gate,
    // Surface plates that turn whatever stands on them a quarter turn each
    // time their linked pressure plates become fully pressed.
    RotatorClockwise,
    RotatorCounterClockwise,
    // Legacy generic player start. New puzzle documents author a concrete
    // Rogue, Knight, Druid, Witch, or Bard tile; overworld documents retain
    // this tile for backwards compatibility with their single rogue.
    Player,
    Rogue,
    Knight,
    Druid,
    Witch,
    Bard,
    Rock,
    Ice,
    Water,
    Ladder,
    ConveyorUp,
    ConveyorDown,
    ConveyorRight,
    ConveyorLeft,
    MirrorNorthWest,
    MirrorNorthEast,
    MirrorSouthWest,
    MirrorSouthEast,
    Decorative,
    Enemy,
    TurretNorth,
    TurretEast,
    TurretSouth,
    TurretWest,
    // A moving platform. It is a solid block on the layer it rests on, so
    // units stand on top of it; each time its linked pressure plates become
    // fully pressed it carries itself and whatever stands on it to the next
    // stop in its authored list of layers (see Level::Elevator).
    Elevator,
    // Minecart track pieces. Each orientation is a tile type so screen files
    // stay a compact character grid while all variants share three models.
    RailStraightNorthSouth,
    RailStraightEastWest,
    RailCornerNorthEast,
    RailCornerSouthEast,
    RailCornerSouthWest,
    RailCornerNorthWest,
    RailStopNorthSouth,
    RailStopEastWest,
    // A moving platform authored on top of a Rail Stop (the stop is retained
    // in an @plate record). Units may occupy and ride in the cart's cell.
    Minecart,
    PortalNorth,
    PortalEast,
    PortalSouth,
    PortalWest,
    Count,
};

// Behavioural traits shared by families of tiles. Combine with `|`.
enum class TileProperty : uint32_t {
    None = 0,
    // A floor plate: a thin tile that units, and mirrors, may occupy. Plates
    // may be authored with something already standing on them (see the
    // `@plate` screen directive), and each kind reacts to its occupant: a
    // pressure plate drives gates and rotators, a rotator turns what stands
    // on it, and an End is where heroes finish.
    Plate = 1U << 0U,
    // Thin traversable floor geometry. Plates imply this visually; rails use
    // it without gaining plate stacking/activation semantics.
    Surface = 1U << 1U,
};

[[nodiscard]] constexpr TileProperty operator|(TileProperty left, TileProperty right)
{
    return static_cast<TileProperty>(
        static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

[[nodiscard]] constexpr bool hasProperty(TileProperty set, TileProperty property)
{
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(property)) ==
        static_cast<uint32_t>(property) &&
        property != TileProperty::None;
}

struct TileTypeDefinition {
    TileType type = TileType::Ground;
    char character = ' ';
    std::string_view name;
    Vec4 activeColor {};
    Vec4 inactiveColor {};
    TileProperty properties = TileProperty::None;
};

inline constexpr auto tileTypeCount = static_cast<std::size_t>(TileType::Count);
inline constexpr std::array<TileTypeDefinition, tileTypeCount> tileTypeDefinitionTable {
    TileTypeDefinition { TileType::Air, ' ', "Air", { 0.0f, 0.0f, 0.0f, 0.0f } },
    TileTypeDefinition { TileType::Ground, '.', "Ground", { 0.82f, 0.82f, 0.84f, 1.0f } },
    TileTypeDefinition { TileType::Wall, '#', "Wall", { 0.62f, 0.32f, 0.09f, 1.0f } },
    TileTypeDefinition { TileType::End, 'E', "End", { 1.0f, 0.05f, 0.04f, 1.0f }, { 0.38f, 0.04f, 0.04f, 1.0f }, TileProperty::Plate },
    TileTypeDefinition { TileType::PressurePlate, 'P', "Pressure", { 0.18f, 0.18f, 0.18f, 1.0f }, {}, TileProperty::Plate },
    TileTypeDefinition { TileType::Gate, 'G', "Gate", { 1.0f, 0.72f, 0.12f, 0.82f }, { 1.0f, 0.72f, 0.12f, 0.0f } },
    TileTypeDefinition { TileType::RotatorClockwise, ')', "Rotator Clockwise", { 0.24f, 0.62f, 0.92f, 1.0f }, {}, TileProperty::Plate },
    TileTypeDefinition { TileType::RotatorCounterClockwise, '(', "Rotator Counter-Clockwise", { 0.24f, 0.62f, 0.92f, 1.0f }, {}, TileProperty::Plate },
    TileTypeDefinition { TileType::Player, 'C', "Player", { 0.0f, 1.0f, 0.15f, 1.0f } },
    TileTypeDefinition { TileType::Rogue, 'Q', "Rogue", { 0.0f, 1.0f, 0.15f, 1.0f } },
    TileTypeDefinition { TileType::Knight, 'K', "Knight", { 0.25f, 0.55f, 1.0f, 1.0f } },
    TileTypeDefinition { TileType::Druid, 'U', "Druid", { 0.35f, 0.72f, 0.28f, 1.0f } },
    TileTypeDefinition { TileType::Witch, 'H', "Witch", { 0.62f, 0.25f, 0.82f, 1.0f } },
    TileTypeDefinition { TileType::Bard, 'B', "Bard", { 0.94f, 0.44f, 0.82f, 1.0f } },
    TileTypeDefinition { TileType::Rock, 'R', "Rock", { 0.20f, 0.10f, 0.04f, 1.0f } },
    TileTypeDefinition { TileType::Ice, 'I', "Ice", { 0.62f, 0.88f, 1.0f, 1.0f } },
    TileTypeDefinition { TileType::Water, 'W', "Water", { 0.08f, 0.34f, 0.78f, 1.0f } },
    TileTypeDefinition { TileType::Ladder, 'L', "Ladder", { 0.43f, 0.22f, 0.08f, 1.0f } },
    TileTypeDefinition { TileType::ConveyorUp, '^', "Conveyor Up", { 0.22f, 0.56f, 0.95f, 1.0f } },
    TileTypeDefinition { TileType::ConveyorDown, 'v', "Conveyor Down", { 0.22f, 0.56f, 0.95f, 1.0f } },
    TileTypeDefinition { TileType::ConveyorRight, '>', "Conveyor Right", { 0.22f, 0.56f, 0.95f, 1.0f } },
    TileTypeDefinition { TileType::ConveyorLeft, '<', "Conveyor Left", { 0.22f, 0.56f, 0.95f, 1.0f } },
    TileTypeDefinition { TileType::MirrorNorthWest, '1', "Mirror North-West", { 0.72f, 0.90f, 1.0f, 1.0f } },
    TileTypeDefinition { TileType::MirrorNorthEast, '2', "Mirror North-East", { 0.72f, 0.90f, 1.0f, 1.0f } },
    TileTypeDefinition { TileType::MirrorSouthWest, '3', "Mirror South-West", { 0.72f, 0.90f, 1.0f, 1.0f } },
    TileTypeDefinition { TileType::MirrorSouthEast, '4', "Mirror South-East", { 0.72f, 0.90f, 1.0f, 1.0f } },
    TileTypeDefinition { TileType::Decorative, 'D', "Decorative Block", { 0.32f, 0.58f, 0.48f, 1.0f } },
    TileTypeDefinition { TileType::Enemy, 'N', "Enemy", { 0.75f, 0.18f, 0.12f, 1.0f } },
    TileTypeDefinition { TileType::TurretNorth, 'n', "Turret North", { 0.72f, 0.48f, 0.16f, 1.0f } },
    TileTypeDefinition { TileType::TurretEast, 'e', "Turret East", { 0.72f, 0.48f, 0.16f, 1.0f } },
    TileTypeDefinition { TileType::TurretSouth, 's', "Turret South", { 0.72f, 0.48f, 0.16f, 1.0f } },
    TileTypeDefinition { TileType::TurretWest, 'w', "Turret West", { 0.72f, 0.48f, 0.16f, 1.0f } },
    TileTypeDefinition { TileType::Elevator, '=', "Elevator", { 0.86f, 0.88f, 0.92f, 1.0f } },
    TileTypeDefinition { TileType::RailStraightNorthSouth, '|', "Rail Straight North-South", { 1.0f, 1.0f, 1.0f, 1.0f }, {}, TileProperty::Surface },
    TileTypeDefinition { TileType::RailStraightEastWest, '-', "Rail Straight East-West", { 1.0f, 1.0f, 1.0f, 1.0f }, {}, TileProperty::Surface },
    TileTypeDefinition { TileType::RailCornerNorthEast, '5', "Rail Corner North-East", { 1.0f, 1.0f, 1.0f, 1.0f }, {}, TileProperty::Surface },
    TileTypeDefinition { TileType::RailCornerSouthEast, '6', "Rail Corner South-East", { 1.0f, 1.0f, 1.0f, 1.0f }, {}, TileProperty::Surface },
    TileTypeDefinition { TileType::RailCornerSouthWest, '7', "Rail Corner South-West", { 1.0f, 1.0f, 1.0f, 1.0f }, {}, TileProperty::Surface },
    TileTypeDefinition { TileType::RailCornerNorthWest, '8', "Rail Corner North-West", { 1.0f, 1.0f, 1.0f, 1.0f }, {}, TileProperty::Surface },
    TileTypeDefinition { TileType::RailStopNorthSouth, '!', "Rail Stop North-South", { 1.0f, 1.0f, 1.0f, 1.0f }, {}, TileProperty::Plate | TileProperty::Surface },
    TileTypeDefinition { TileType::RailStopEastWest, '_', "Rail Stop East-West", { 1.0f, 1.0f, 1.0f, 1.0f }, {}, TileProperty::Plate | TileProperty::Surface },
    TileTypeDefinition { TileType::Minecart, 'M', "Minecart", { 1.0f, 1.0f, 1.0f, 1.0f } },
    TileTypeDefinition { TileType::PortalNorth, 'O', "Portal North", { 0.64f, 0.30f, 1.0f, 1.0f }, {}, TileProperty::Plate },
    TileTypeDefinition { TileType::PortalEast, 'o', "Portal East", { 0.64f, 0.30f, 1.0f, 1.0f }, {}, TileProperty::Plate },
    TileTypeDefinition { TileType::PortalSouth, 'p', "Portal South", { 0.64f, 0.30f, 1.0f, 1.0f }, {}, TileProperty::Plate },
    TileTypeDefinition { TileType::PortalWest, 'q', "Portal West", { 0.64f, 0.30f, 1.0f, 1.0f }, {}, TileProperty::Plate },
};

[[nodiscard]] const std::array<TileTypeDefinition, tileTypeCount>& tileTypeDefinitions();
[[nodiscard]] char tileTypeToChar(TileType type);
[[nodiscard]] std::optional<TileType> charToTileType(char character);
[[nodiscard]] std::string_view tileTypeName(TileType type);
// Inverse of tileTypeName; empty for unknown names.
[[nodiscard]] std::optional<TileType> tileTypeFromName(std::string_view name);
[[nodiscard]] TileProperty tileTypeProperties(TileType type);
[[nodiscard]] bool tileTypeHasProperty(TileType type, TileProperty property);
// Pressure plates, rotators and Ends: see TileProperty::Plate.
[[nodiscard]] bool tileTypeIsPlate(TileType type);
// What may stand on a plate, including when a screen is authored that way:
// every unit that occupies a level cell (heroes, rocks, ice, turrets,
// enemies) and mirrors.
[[nodiscard]] bool tileTypeCanStandOnPlate(TileType type);
[[nodiscard]] bool tileTypeOccupiesLevelCell(TileType type);
[[nodiscard]] bool tileTypeIsSolidBlock(TileType type);
[[nodiscard]] bool tileTypeSupportsEntity(TileType type);
[[nodiscard]] bool tileTypeAllowsEntity(TileType type);
// Thin floor-level tiles drawn as flat slabs; currently exactly the plates.
[[nodiscard]] bool tileTypeIsSurfaceEntity(TileType type);
[[nodiscard]] bool tileTypeIsPlayerStart(TileType type);
[[nodiscard]] bool tileTypeIsConveyor(TileType type);
[[nodiscard]] bool tileTypeIsMirror(TileType type);
[[nodiscard]] bool tileTypeIsTurret(TileType type);
// Movable non-character units represented by GameState::Movable.
[[nodiscard]] bool tileTypeIsMovableObject(TileType type);
[[nodiscard]] bool tileTypeIsDecorative(TileType type);
[[nodiscard]] bool tileTypeIsRotator(TileType type);
[[nodiscard]] bool tileTypeIsElevator(TileType type);
[[nodiscard]] bool tileTypeIsRail(TileType type);
[[nodiscard]] bool tileTypeIsRailStop(TileType type);
[[nodiscard]] bool tileTypeIsMinecart(TileType type);
[[nodiscard]] constexpr bool tileTypeIsPortal(TileType type)
{
    return type >= TileType::PortalNorth && type <= TileType::PortalWest;
}
// The edge normal points out of the owning tile; the active front faces in.
[[nodiscard]] constexpr GridPosition portalEdgeOffset(TileType type)
{
    switch (type) {
    case TileType::PortalNorth: return { 0, -1 };
    case TileType::PortalEast: return { 1, 0 };
    case TileType::PortalSouth: return { 0, 1 };
    case TileType::PortalWest: return { -1, 0 };
    default: return {};
    }
}
// N/E/S/W connector bits used to validate and traverse a rail system.
inline constexpr uint8_t railNorth = 1U << 0U;
inline constexpr uint8_t railEast = 1U << 1U;
inline constexpr uint8_t railSouth = 1U << 2U;
inline constexpr uint8_t railWest = 1U << 3U;
[[nodiscard]] uint8_t railConnectionMask(TileType type);
// Clockwise turns from the north-south straight or north-east corner model.
[[nodiscard]] std::optional<uint32_t> railOrientationQuarterTurns(TileType type);
// Signed quarter turns one activation applies: +1 clockwise, -1
// counter-clockwise (seen from above). Empty for every other tile.
[[nodiscard]] std::optional<int> rotatorQuarterTurns(TileType type);
[[nodiscard]] bool tileTypeAffectsCameraFit(TileType type);
// Clockwise quarter-turns from the north-west-facing model orientation.
[[nodiscard]] std::optional<uint32_t> mirrorOrientationQuarterTurns(TileType type);
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
