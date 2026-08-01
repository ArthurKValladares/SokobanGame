#pragma once

#include <cstddef>
#include <cstdint>

namespace sokoban {

using EntityId = uint64_t;
inline constexpr EntityId invalidEntityId = 0;

enum class EntityKind : uint8_t {
    Player,
    Movable,
    Enemy,
};

struct EntityTarget {
    EntityKind kind = EntityKind::Player;
    EntityId id = invalidEntityId;

    bool operator==(const EntityTarget&) const = default;
};

// Hand-authored tests and editor previews may construct state without ids.
// Production state always owns explicit ids; this fallback keeps those
// transient states addressable without making vector position the persisted
// identity of an entity.
[[nodiscard]] constexpr EntityId resolvedEntityId(
    EntityKind kind,
    EntityId id,
    std::size_t index)
{
    if (id != invalidEntityId) {
        return id;
    }
    return (static_cast<EntityId>(kind) + 1ULL) << 56 |
        static_cast<EntityId>(index + 1);
}

} // namespace sokoban
