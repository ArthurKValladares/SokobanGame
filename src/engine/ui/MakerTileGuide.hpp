#pragma once

#include "engine/EditorTilePalette.hpp"
#include "engine/ui/UiTextEdit.hpp"

#include <array>
#include <string_view>

namespace sokoban::makerTileGuide {

struct Entry {
    std::string_view description;
    std::string_view placementHint;
    std::string_view aliases;
};

[[nodiscard]] inline std::string_view name(TileType tile)
{
    if (tile == TileType::PressurePlate) return "Pressure Plate";
    if (const auto* group = editorTilePalette::groupFor(tile); group && group->rotationOnly) {
        return group->name;
    }
    return tileTypeDisplayName(tile);
}

[[nodiscard]] inline Entry forTile(TileType tile)
{
    if (const auto* group = editorTilePalette::groupFor(tile); group && group->rotationOnly) {
        tile = group->variants().front();
    }
    if (tileTypeIsGround(tile)) {
        return { "A solid floor that supports tiles above it.",
            "Paint on the floor layer to build a walking surface.", "terrain base platform" };
    }
    if (tileTypeIsCliffWall(tile)) {
        return { "A solid cliff with a walkable top.",
            "Place next to other cliffs to form a raised ledge.", "terrain ledge cliffside elevation" };
    }
    if (tileTypeIsWall(tile)) {
        return { "A solid barrier that blocks movement and sight.",
            "Paint above the floor to block a path.", "obstacle barrier stone" };
    }
    if (tileTypeIsWardrobe(tile)) {
        return { "Changes a hero to the character shown on the wardrobe.",
            "Place on a supported path a hero can walk through.", "costume class transform switch hero" };
    }

    constexpr std::string_view heroStart = "Place on a supported tile where a hero should start.";
    switch (tile) {
    case TileType::Air:
        return { "Empty space.", "Erase a tile to remove it from the cell.", "erase remove void" };
    case TileType::End:
        return { "Finish by putting every hero on a goal and filling every goal.",
            "Place on a supported cell the hero can reach.", "exit finish win target objective" };
    case TileType::Player:
    case TileType::Rogue:
        return { "A hero who can push one obstacle at a time.", heroStart, "paladin player rogue start spawn" };
    case TileType::Knight:
        return { "A hero who can push a whole chain of obstacles.",
            "Leave room at the far end of the chain.", "heavy strength shove player start spawn" };
    case TileType::Druid:
        return { "A hero who pulls the object directly behind them as they move.",
            "Leave room for the Thief to walk past the object and pull it.", "drag player start spawn" };
    case TileType::Witch:
        return { "A hero who swaps with the nearest visible movable unit when moving toward it.",
            "Leave a straight, unobstructed path to a swap target.", "teleport spell exchange player start spawn" };
    case TileType::Bard:
        return { "When the Bard moves, nearby objects and enemies try to move with them.",
            "Place near movable units to use the Bard's aura.", "music aura player start spawn" };
    case TileType::Cleric:
        return { "A hero immune to attacks who also protects adjacent allies.",
            "Keep allies beside the Cleric on the same layer.", "healer shield immunity protection aura player start spawn" };
    case TileType::Ranger:
        return { "A hero who pushes one obstacle and becomes a skipping crystal when crystallized.",
            heroStart, "player start spawn" };
    case TileType::CrystalDruid:
        return { "Bumps heroes and enemies into crystal form, and bumps crystals to restore them.",
            "Leave space for the Druid to approach a hero or enemy.", "crystallize transform petrify player start spawn" };
    case TileType::Rock:
        return { "A pushable block that can hold a pressure plate down.",
            "Leave room for a push. Use Connect to make two stones move together.", "box crate boulder weight sokoban synchronized" };
    case TileType::RoundRock:
        return { "A pushable stone that skips across water and can become a stepping stone.",
            "Place on shore with open water in the direction of the push.", "boulder rolling skipping stone" };
    case TileType::Crystal:
        return { "A pushable crystal block.",
            "Place above a solid surface with room for a push.", "crystal rock block" };
    case TileType::SkippingCrystal:
        return { "A pushable crystal that skips across water.",
            "Place on shore and leave a straight path across the water.", "crystal round rock stepping stone" };
    case TileType::Water:
        return { "A hazard that can drown heroes. Floating pads and sunken rocks can provide a crossing.",
            "Paint on the floor layer to make a water gap.", "pool river lake hazard drown" };
    case TileType::StreamUp:
        return { "A current that carries lily pads and their riders.",
            "Paint on existing water. Rotate to set the flow direction.", "current river drift flow" };
    case TileType::LilyPad:
        return { "A pushable floating platform that carries riders.",
            "Paint in an empty cell one layer above water.", "leaf raft float ferry platform" };
    case TileType::Ladder:
        return { "Lets heroes climb between layers.",
            "Paint next to ground on the same layer.", "stairs vertical ramp climb" };
    case TileType::Decorative:
        return { "A visual prop that characters can walk through.",
            "Place where it will dress up the board without hiding puzzle pieces.", "decoration scenery prop art" };
    case TileType::Ice:
        return { "A pushable block that makes units slide across its top.",
            "Leave a clear run for sliding and room to push the block.", "slippery frozen momentum" };
    case TileType::MirrorNorthWest:
        return { "Use Activate to reflect units between the mirror's two rays.",
            "Rotate to aim both rays and leave the reflected cell clear.", "reflection clone copy duplicate swap" };
    case TileType::RobotNorth:
        return { "A pushable platform that patrols and reverses when blocked.",
            "Rotate to set its starting direction and leave a straight lane.", "patrol rover moving platform rider" };
    case TileType::Enemy:
        return { "Faces nearby heroes and attacks adjacent heroes.",
            "Place on a supported cell near the route you want it to guard.", "guard monster hazard combat" };
    case TileType::PressurePlate:
        return { "Sends a signal while a unit or movable object stands on it.",
            "Place where a hero or object can stand. Use Connect to wire it to a device.", "weight switch trigger sensor pressure" };
    case TileType::Gate:
        return { "A solid barrier that normally opens when all linked sources are active.",
            "Place in a passage. Use Connect to choose a plate, button, or lever.", "door bridge trapdoor switch barrier" };
    case TileType::EnemyGate:
        return { "A barrier that normally opens once all enemies and turrets are defeated.",
            "Place in a passage you want to lock until the hazards are cleared. Opens automatically.", "door barrier combat clear defeat" };
    case TileType::LockPlate:
        return { "Holds any unit standing on it in place while enabled.",
            "Place on the route of a unit you want to hold. A linked signal controls the lock.", "freeze trap stop hold immobilize" };
    case TileType::ButtonNorth:
        return { "Sends a brief signal when a hero stands on it and uses Activate.",
            "Rotate to choose its edge. Use Connect to wire it to a device.", "switch pulse trigger interact" };
    case TileType::LeverNorth:
        return { "Toggles a persistent signal when a hero stands on it and uses Activate.",
            "Rotate to choose its edge. Use Connect to wire it to a device.", "switch toggle trigger interact" };
    case TileType::ConveyorUp:
        return { "Automatically carries heroes and movable units in one direction.",
            "Place above the floor and rotate to aim the belt.", "belt automatic motion transport" };
    case TileType::RotatorClockwise:
        return { "Turns the unit on it clockwise when its linked sources activate.",
            "Place where a unit can stand. Use Connect to choose its signal sources.", "rotate turn spin pivot cog" };
    case TileType::RotatorCounterClockwise:
        return { "Turns the unit on it counter-clockwise when its linked sources activate.",
            "Place where a unit can stand. Use Connect to choose its signal sources.", "rotate turn spin pivot cog" };
    case TileType::Elevator:
        return { "Carries riders between configured layers when its linked sources activate.",
            "Leave the shaft clear. In Connect, choose another layer and add a stop.", "lift platform moving vertical stops" };
    case TileType::PortalNorth:
        return { "Teleports a unit crossing its edge to its paired portal.",
            "Rotate to choose its edge. Use Connect to pair it with another portal.", "warp teleport doorway transport pair" };
    case TileType::TurretNorth:
        return { "A pushable turret that fires when a hero or enemy moves into its line of sight.",
            "Rotate to aim along a clear row or column.", "laser hazard gun shoot cannon" };
    case TileType::RailStraightNorthSouth:
        return { "A straight piece of minecart track.",
            "Rotate to connect its ends to neighboring track.", "train railway rails track straight" };
    case TileType::RailCornerNorthEast:
        return { "A curved piece of minecart track.",
            "Rotate the bend so both ends meet neighboring track.", "train railway rails track bend curve" };
    case TileType::RailStopNorthSouth:
        return { "A track section where a minecart can start and stop.",
            "Connect it to track and paint the cart in the same cell.", "train railway rails track station" };
    case TileType::Minecart:
        return { "Carries riders along connected track when its linked sources activate.",
            "Paint on an existing Rail Stop and connect it to another stop.", "train wagon railway rails track cart" };
    case TileType::MinecartGate:
        return { "A rail barrier that lets empty carts and carts carrying characters pass.",
            "Paint over a rail. The barrier lifts automatically as a cart passes.", "train railway rails track barrier crossing" };
    case TileType::LecternNorth:
        return { "A stand that opens its authored text when a hero walks into it.",
            "Place on an accessible path and rotate to choose its facing.", "book sign text message instructions" };
    default:
        return {};
    }
}

[[nodiscard]] inline bool matches(TileType tile, std::string_view group, std::string_view query)
{
    const Entry entry = forTile(tile);
    const std::array fields { name(tile), group, entry.description, entry.placementHint, entry.aliases };
    return matchesSearchTerms(fields, query);
}

} // namespace sokoban::makerTileGuide
