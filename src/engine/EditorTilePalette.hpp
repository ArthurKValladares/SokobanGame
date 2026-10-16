#pragma once

#include "engine/TileTypes.hpp"

#include <array>
#include <span>
#include <string_view>

namespace sokoban::editorTilePalette {

// Only the palette is grouped. Brushes and screen files still use the exact
// variant tile type chosen in the picker or with the eyedropper.
struct Group {
    std::string_view name;
    std::array<TileType, groundRockVariantCount> tiles;
    std::size_t count;
    // Direction variants remain available to saved screens and the eyedropper,
    // but share one selectable brush in the palette. Rotate changes direction.
    bool rotationOnly = false;

    [[nodiscard]] constexpr std::span<const TileType> variants() const
    {
        return { tiles.data(), count };
    }

    [[nodiscard]] constexpr bool contains(TileType tile) const
    {
        for (TileType variant : variants()) {
            if (variant == tile) {
                return true;
            }
        }
        return false;
    }
};

inline constexpr std::array<Group, 16> groups {{
    { "Ground", { TileType::Ground, TileType::GroundRock02,
                  TileType::GroundRock03, TileType::GroundRock04,
                  TileType::GroundRock05, TileType::GroundRock06,
                  TileType::GroundRock07, TileType::GroundRock08,
                  TileType::GroundRock09, TileType::GroundRock10 }, 10 },
    { "Wall", { TileType::Wall, TileType::WallStone02,
                TileType::WallStone03, TileType::WallStone04,
                TileType::WallStone05, TileType::WallStone06,
                TileType::WallStone07, TileType::WallStone08 }, wallStoneVariantCount },
    { "Cliff Wall", { TileType::CliffWall, TileType::CliffWall02 }, cliffWallVariantCount },
    { "Conveyor", { TileType::ConveyorUp, TileType::ConveyorRight,
                    TileType::ConveyorDown, TileType::ConveyorLeft }, 4, true },
    { "Mirror", { TileType::MirrorNorthWest, TileType::MirrorNorthEast,
                  TileType::MirrorSouthWest, TileType::MirrorSouthEast }, 4, true },
    { "Turret", { TileType::TurretNorth, TileType::TurretEast,
                  TileType::TurretSouth, TileType::TurretWest }, 4, true },
    { "Robot", { TileType::RobotNorth, TileType::RobotEast,
                  TileType::RobotSouth, TileType::RobotWest }, 4, true },
    { "Portal", { TileType::PortalNorth, TileType::PortalEast,
                  TileType::PortalSouth, TileType::PortalWest }, 4, true },
    { "Button", { TileType::ButtonNorth, TileType::ButtonEast,
                  TileType::ButtonSouth, TileType::ButtonWest }, 4, true },
    { "Lever", { TileType::LeverNorth, TileType::LeverEast,
                 TileType::LeverSouth, TileType::LeverWest }, 4, true },
    { "Lectern", { TileType::LecternNorth, TileType::LecternEast,
                   TileType::LecternSouth, TileType::LecternWest }, 4, true },
    { "Rotator", { TileType::RotatorClockwise,
                   TileType::RotatorCounterClockwise }, 2 },
    { "Rail Straight", { TileType::RailStraightNorthSouth,
                         TileType::RailStraightEastWest }, 2, true },
    { "Rail Corner", { TileType::RailCornerNorthEast,
                       TileType::RailCornerSouthEast,
                       TileType::RailCornerSouthWest,
                       TileType::RailCornerNorthWest }, 4, true },
    { "Rail Stop", { TileType::RailStopNorthSouth,
                     TileType::RailStopEastWest }, 2, true },
    { "Wardrobe", { TileType::WardrobeLorekeeper,
                     TileType::WardrobeRogue,
                     TileType::WardrobeKnight,
                     TileType::WardrobeDruid,
                     TileType::WardrobeWitch,
                     TileType::WardrobeBard }, 6 },
}};

// These families use neighboring tiles to choose their visible terrain faces.
// Quarry walls keep their separate authored models in the ordinary palette.
[[nodiscard]] constexpr bool isTerrain(TileType tile)
{
    return tileTypeIsGround(tile) || tileTypeIsCliffWall(tile);
}

[[nodiscard]] constexpr const Group* groupFor(TileType tile)
{
    for (const Group& group : groups) {
        if (group.contains(tile)) {
            return &group;
        }
    }
    return nullptr;
}

} // namespace sokoban::editorTilePalette
