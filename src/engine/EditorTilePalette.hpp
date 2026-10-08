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

inline constexpr std::array<Group, 12> groups {{
    { "Ground", { TileType::Ground, TileType::GroundRock02,
                  TileType::GroundRock03, TileType::GroundRock04,
                  TileType::GroundRock05, TileType::GroundRock06,
                  TileType::GroundRock07, TileType::GroundRock08,
                  TileType::GroundRock09, TileType::GroundRock10 }, 10 },
    { "Wall", { TileType::Wall, TileType::WallStone02,
                TileType::WallStone03, TileType::WallStone04,
                TileType::WallStone05, TileType::WallStone06,
                TileType::WallStone07, TileType::WallStone08 }, wallStoneVariantCount },
    { "Conveyor", { TileType::ConveyorUp, TileType::ConveyorRight,
                    TileType::ConveyorDown, TileType::ConveyorLeft }, 4 },
    { "Mirror", { TileType::MirrorNorthWest, TileType::MirrorNorthEast,
                  TileType::MirrorSouthWest, TileType::MirrorSouthEast }, 4 },
    { "Turret", { TileType::TurretNorth, TileType::TurretEast,
                  TileType::TurretSouth, TileType::TurretWest }, 4 },
    { "Portal", { TileType::PortalNorth, TileType::PortalEast,
                  TileType::PortalSouth, TileType::PortalWest }, 4 },
    { "Lectern", { TileType::LecternNorth, TileType::LecternEast,
                   TileType::LecternSouth, TileType::LecternWest }, 4 },
    { "Rotator", { TileType::RotatorClockwise,
                   TileType::RotatorCounterClockwise }, 2 },
    { "Rail Straight", { TileType::RailStraightNorthSouth,
                         TileType::RailStraightEastWest }, 2 },
    { "Rail Corner", { TileType::RailCornerNorthEast,
                       TileType::RailCornerSouthEast,
                       TileType::RailCornerSouthWest,
                       TileType::RailCornerNorthWest }, 4 },
    { "Rail Stop", { TileType::RailStopNorthSouth,
                     TileType::RailStopEastWest }, 2 },
    { "Wardrobe", { TileType::WardrobeLorekeeper,
                     TileType::WardrobeRogue,
                     TileType::WardrobeKnight,
                     TileType::WardrobeDruid,
                     TileType::WardrobeWitch,
                     TileType::WardrobeBard }, 6 },
}};

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
