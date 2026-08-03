#include "engine/StateDelta.hpp"

#include <cstddef>

namespace sokoban {
namespace {

constexpr std::size_t notFound = static_cast<std::size_t>(-1);

// Vector position is not identity, so every lookup goes through the entity's
// resolved id. States that carry real ids match on those; hand-authored states
// without them fall back to the index, which degenerates to positional
// matching - correct as long as such a state is not also reordered.
template <EntityKind kind, typename Entity>
[[nodiscard]] std::size_t indexOf(
    const std::vector<Entity>& entities, EntityId id)
{
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (resolvedEntityId(kind, entities[index].id, index) == id) {
            return index;
        }
    }
    return notFound;
}

template <EntityKind kind, typename Entity>
void collect(
    const std::vector<Entity>& before,
    const std::vector<Entity>& after,
    std::vector<StateDelta::Change<Entity>>& changes)
{
    // `after` is walked first so that entities the action created are recorded
    // in the order it created them. Applying then appends them in that same
    // order, which is what makes the result compare equal to `after` rather
    // than merely hold the same entities.
    for (std::size_t index = 0; index < after.size(); ++index) {
        const EntityId id =
            resolvedEntityId(kind, after[index].id, index);
        const std::size_t previous = indexOf<kind>(before, id);
        if (previous == notFound) {
            changes.push_back({
                .id = id,
                .before = std::nullopt,
                .after = after[index],
            });
        } else if (!(before[previous] == after[index])) {
            changes.push_back({
                .id = id,
                .before = before[previous],
                .after = after[index],
            });
        }
    }

    for (std::size_t index = 0; index < before.size(); ++index) {
        const EntityId id =
            resolvedEntityId(kind, before[index].id, index);
        if (indexOf<kind>(after, id) == notFound) {
            changes.push_back({
                .id = id,
                .before = before[index],
                .after = std::nullopt,
            });
        }
    }
}

template <EntityKind kind, typename Entity>
void apply(
    const std::vector<StateDelta::Change<Entity>>& changes,
    std::vector<Entity>& entities)
{
    for (const StateDelta::Change<Entity>& change : changes) {
        const std::size_t found = indexOf<kind>(entities, change.id);
        if (change.after.has_value()) {
            if (found == notFound) {
                entities.push_back(*change.after);
            } else {
                entities[found] = *change.after;
            }
        } else if (found != notFound) {
            entities.erase(
                entities.begin() + static_cast<std::ptrdiff_t>(found));
        }
    }
}

template <typename Entity>
[[nodiscard]] std::vector<StateDelta::Change<Entity>> invert(
    const std::vector<StateDelta::Change<Entity>>& changes)
{
    std::vector<StateDelta::Change<Entity>> result;
    result.reserve(changes.size());
    for (const StateDelta::Change<Entity>& change : changes) {
        result.push_back({
            .id = change.id,
            .before = change.after,
            .after = change.before,
        });
    }
    return result;
}

} // namespace

StateDelta StateDelta::between(
    const GameState& before, const GameState& after)
{
    StateDelta delta;
    collect<EntityKind::Player>(before.players, after.players, delta.players);
    collect<EntityKind::Movable>(
        before.movables, after.movables, delta.movables);
    collect<EntityKind::Enemy>(before.enemies, after.enemies, delta.enemies);
    return delta;
}

void StateDelta::applyTo(GameState& state) const
{
    apply<EntityKind::Player>(players, state.players);
    apply<EntityKind::Movable>(movables, state.movables);
    apply<EntityKind::Enemy>(enemies, state.enemies);
}

StateDelta StateDelta::inverted() const
{
    return {
        .players = invert(players),
        .movables = invert(movables),
        .enemies = invert(enemies),
    };
}

bool StateDelta::empty() const
{
    return players.empty() && movables.empty() && enemies.empty();
}

} // namespace sokoban
