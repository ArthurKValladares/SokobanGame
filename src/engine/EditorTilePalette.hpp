#pragma once

#include "engine/TileTypes.hpp"

#include <array>
#include <span>
#include <string_view>

namespace sokoban::editorTilePalette {

// Only the palette is grouped. Brushes and screen files still use the exact
// directional tile type chosen in the picker or with the eyedropper.
struct Group {
    std::string_view name;
    std::array<TileType, 4> tiles;
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

inline constexpr std::array<Group, 8> groups {{
    { "Conveyor", { TileType::ConveyorUp, TileType::ConveyorRight,
                    TileType::ConveyorDown, TileType::ConveyorLeft }, 4 },
    { "Mirror", { TileType::MirrorNorthWest, TileType::MirrorNorthEast,
                  TileType::MirrorSouthWest, TileType::MirrorSouthEast }, 4 },
    { "Turret", { TileType::TurretNorth, TileType::TurretEast,
                  TileType::TurretSouth, TileType::TurretWest }, 4 },
    { "Portal", { TileType::PortalNorth, TileType::PortalEast,
                  TileType::PortalSouth, TileType::PortalWest }, 4 },
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
