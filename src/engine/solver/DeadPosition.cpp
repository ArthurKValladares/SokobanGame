#include "engine/solver/DeadPosition.hpp"

#include "engine/TileTypes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace sokoban::solver::detail {
namespace {

constexpr std::array directions {
    MoveDirection::Up,
    MoveDirection::Down,
    MoveDirection::Left,
    MoveDirection::Right,
};

bool characterUsesOrdinaryPushes(CharacterType character)
{
    return character == CharacterType::Rogue ||
        character == CharacterType::Knight;
}

bool tileChangesMovableReachability(TileType tile)
{
    return tile == TileType::Ice || tile == TileType::Water ||
        tile == TileType::Ladder || tileTypeIsConveyor(tile) ||
        tileTypeIsMirror(tile);
}

bool staticallySupported(const Level& level, GridPosition3 cell)
{
    if (!rules::staticCellAllowsEntity(level, cell)) {
        return false;
    }
    const std::optional<TileType> support = level.supportingTileAt(cell);
    return support && tileTypeSupportsEntity(*support);
}

bool supportsStaticAnalysis(const Level& level)
{
    if (level.pressurePlates().empty() || level.waterLayer() ||
        !level.enemyStarts().empty() || level.playerStarts().empty()) {
        return false;
    }
    const int elevation = level.pressurePlates().front().z;
    if (!std::ranges::all_of(
            level.pressurePlates(),
            [elevation](GridPosition3 cell) {
                return cell.z == elevation;
            }) ||
        !std::ranges::all_of(
            level.playerStarts(),
            [elevation](const Level::PlayerStart& player) {
                return player.position.z == elevation;
            }) ||
        !std::ranges::all_of(
            level.movableTiles(),
            [elevation](const Level::MovableTile& movable) {
                return movable.position.z == elevation;
            })) {
        return false;
    }
    if (!std::ranges::all_of(
            level.playerStarts(),
            [](const Level::PlayerStart& player) {
                return characterUsesOrdinaryPushes(player.character);
            }) ||
        !std::ranges::all_of(
            level.movableTiles(),
            [](const Level::MovableTile& movable) {
                return movable.type == TileType::Rock;
            })) {
        return false;
    }
    for (std::uint32_t z = 0; z < level.depth(); ++z) {
        for (std::uint32_t y = 0; y < level.height(); ++y) {
            for (std::uint32_t x = 0; x < level.width(); ++x) {
                if (tileChangesMovableReachability(level.tileAt(x, y, z))) {
                    return false;
                }
            }
        }
    }
    // A gap on the entity plane can drop a pushed rock to another elevation;
    // the planar reverse-push proof below deliberately does not model falls.
    for (int y = 0; y < static_cast<int>(level.height()); ++y) {
        for (int x = 0; x < static_cast<int>(level.width()); ++x) {
            const GridPosition3 cell { x, y, elevation };
            if (rules::staticCellAllowsEntity(level, cell) &&
                !staticallySupported(level, cell)) {
                return false;
            }
        }
    }
    return true;
}

GridPosition3 subtract(GridPosition3 cell, GridPosition offset)
{
    return { cell.x - offset.x, cell.y - offset.y, cell.z };
}

} // namespace

DeadPositionIndex::DeadPositionIndex(const Level& level)
    : level_(level)
{
    enabled_ = supportsStaticAnalysis(level);
    if (!enabled_) {
        return;
    }

    const std::size_t cellCount =
        static_cast<std::size_t>(level.width()) * level.height() *
        (static_cast<std::size_t>(level.depth()) + 1);
    supported_.resize(cellCount);
    canReachPlate_.resize(cellCount);
    for (int z = 0; z <= static_cast<int>(level.depth()); ++z) {
        for (int y = 0; y < static_cast<int>(level.height()); ++y) {
            for (int x = 0; x < static_cast<int>(level.width()); ++x) {
                const GridPosition3 cell { x, y, z };
                supported_[index(cell)] = staticallySupported(level, cell);
            }
        }
    }

    // One reverse-pull flood per plate. Keeping the individual tables lets the
    // runtime prove that all plates can receive distinct rocks, while their
    // union identifies cells that cannot reach any plate at all.
    reachableByPlate_.reserve(level.pressurePlates().size());
    for (const GridPosition3 plate : level.pressurePlates()) {
        std::vector<bool>& reachesThisPlate =
            reachableByPlate_.emplace_back(cellCount);
        std::vector<GridPosition3> reachable;
        if (inRange(plate) && supported_[index(plate)] &&
            !reachesThisPlate[index(plate)]) {
            reachesThisPlate[index(plate)] = true;
            reachable.push_back(plate);
        }
        for (std::size_t next = 0; next < reachable.size(); ++next) {
            const GridPosition3 destination = reachable[next];
            for (const MoveDirection direction : directions) {
                const GridPosition offset = rules::directionOffset(direction);
                const GridPosition3 source = subtract(destination, offset);
                const GridPosition3 pusher = subtract(source, offset);
                if (!inRange(source) || !inRange(pusher) ||
                    !supported_[index(source)] ||
                    !supported_[index(pusher)] ||
                    reachesThisPlate[index(source)]) {
                    continue;
                }
                reachesThisPlate[index(source)] = true;
                reachable.push_back(source);
            }
        }
        for (std::size_t cell = 0; cell < cellCount; ++cell) {
            canReachPlate_[cell] =
                canReachPlate_[cell] || reachesThisPlate[cell];
        }
    }

    for (std::size_t cell = 0; cell < supported_.size(); ++cell) {
        deadCellCount_ += supported_[cell] && !canReachPlate_[cell] ? 1 : 0;
    }
}

bool DeadPositionIndex::inRange(GridPosition3 cell) const
{
    return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 &&
        cell.x < static_cast<int>(level_.width()) &&
        cell.y < static_cast<int>(level_.height()) &&
        cell.z <= static_cast<int>(level_.depth());
}

std::size_t DeadPositionIndex::index(GridPosition3 cell) const
{
    return (static_cast<std::size_t>(cell.z) * level_.height() +
               static_cast<std::size_t>(cell.y)) *
            level_.width() +
        static_cast<std::size_t>(cell.x);
}

bool DeadPositionIndex::isDeadCell(GridPosition3 cell) const
{
    return enabled_ && inRange(cell) && supported_[index(cell)] &&
        !canReachPlate_[index(cell)];
}

bool DeadPositionIndex::rejects(const GameState& state) const
{
    std::vector<GridPosition3> liveMovables;
    for (const GameState::Movable& movable : state.movables) {
        if (!movable.fallen && !movable.dead) {
            liveMovables.push_back(movable.cell);
        }
    }
    std::size_t liveEnemies = 0;
    for (const GameState::Enemy& enemy : state.enemies) {
        if (!enemy.fallen && !enemy.dead) {
            ++liveEnemies;
        }
    }
    const std::size_t plateCount = level_.pressurePlates().size();
    if (liveMovables.size() + liveEnemies < plateCount) {
        return true;
    }
    if (!enabled_) {
        return false;
    }

    // Maximum bipartite matching between plates and current rock cells. An
    // edge means the rock could reach that plate on an otherwise empty board;
    // ignoring all dynamic blockers makes failure a safe impossibility proof.
    std::vector<std::size_t> plateForMovable(
        liveMovables.size(), plateCount);
    const auto assign = [&](auto&& self,
                            std::size_t plate,
                            std::vector<bool>& visited) -> bool {
        for (std::size_t movable = 0;
             movable < liveMovables.size();
             ++movable) {
            const GridPosition3 cell = liveMovables[movable];
            if (visited[movable] || !inRange(cell) ||
                !reachableByPlate_[plate][index(cell)]) {
                continue;
            }
            visited[movable] = true;
            if (plateForMovable[movable] == plateCount ||
                self(self, plateForMovable[movable], visited)) {
                plateForMovable[movable] = plate;
                return true;
            }
        }
        return false;
    };
    for (std::size_t plate = 0; plate < plateCount; ++plate) {
        std::vector<bool> visited(liveMovables.size());
        if (!assign(assign, plate, visited)) {
            return true;
        }
    }
    return false;
}

} // namespace sokoban::solver::detail
