#include "engine/solver/StateKey.hpp"

#include <optional>
#include <type_traits>

namespace sokoban::solver::detail {
namespace {

static_assert(static_cast<std::uint32_t>(CharacterType::Bard) < 0xffU);
static_assert(static_cast<std::uint32_t>(MoveDirection::Right) < 0xffU);
static_assert(static_cast<std::uint32_t>(TileType::Count) <= 0x100U);

std::uint64_t packPair(int first, int second)
{
    static_assert(sizeof(int) <= sizeof(std::uint32_t));
    return static_cast<std::uint32_t>(first) |
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(second)) << 32);
}

std::uint64_t packZAndFlags(int z, std::uint32_t flags)
{
    return static_cast<std::uint32_t>(z) |
        (static_cast<std::uint64_t>(flags) << 32);
}

template <typename Enum>
std::uint32_t enumCode(Enum value)
{
    static_assert(std::is_enum_v<Enum>);
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<std::uint32_t>(static_cast<Underlying>(value));
}

template <typename Enum>
std::uint32_t optionalEnumCode(std::optional<Enum> value)
{
    return value ? 1U + enumCode(*value) : 0U;
}

} // namespace

PackedStateKey makePackedStateKey(
    const GameState& state, EntityId activeController)
{
    std::vector<std::uint64_t> words;
    words.reserve(4 + state.players.size() * 4 +
        state.movables.size() * 3 + state.enemies.size() * 3);

    words.push_back(activeController);
    words.push_back(state.players.size());
    for (const GameState::Player& player : state.players) {
        std::uint32_t flags = optionalEnumCode(player.character);
        flags |= optionalEnumCode(player.sliding) << 8;
        flags |= static_cast<std::uint32_t>(player.dead) << 16;
        flags |= static_cast<std::uint32_t>(player.drowned) << 17;
        flags |= static_cast<std::uint32_t>(player.quarterTurns) << 18;
        words.push_back(player.id);
        words.push_back(player.controller);
        words.push_back(packPair(player.cell.x, player.cell.y));
        words.push_back(packZAndFlags(player.cell.z, flags));
    }

    words.push_back(state.movables.size());
    for (const GameState::Movable& movable : state.movables) {
        std::uint32_t flags = enumCode(movable.type);
        flags |= optionalEnumCode(movable.sliding) << 8;
        flags |= static_cast<std::uint32_t>(movable.fallen) << 16;
        flags |= static_cast<std::uint32_t>(movable.dead) << 17;
        flags |= static_cast<std::uint32_t>(movable.quarterTurns) << 18;
        words.push_back(movable.id);
        words.push_back(packPair(movable.cell.x, movable.cell.y));
        words.push_back(packZAndFlags(movable.cell.z, flags));
    }

    words.push_back(state.enemies.size());
    for (const GameState::Enemy& enemy : state.enemies) {
        std::uint32_t flags = optionalEnumCode(enemy.sliding);
        flags |= static_cast<std::uint32_t>(enemy.fallen) << 8;
        flags |= static_cast<std::uint32_t>(enemy.dead) << 9;
        flags |= static_cast<std::uint32_t>(enemy.quarterTurns) << 10;
        words.push_back(enemy.id);
        words.push_back(packPair(enemy.cell.x, enemy.cell.y));
        words.push_back(packZAndFlags(enemy.cell.z, flags));
    }

    // Appended only when present, so keys for screens without turned mirrors
    // are unchanged; the counted sections above keep the tail unambiguous.
    if (!state.turnedMirrors.empty()) {
        words.push_back(state.turnedMirrors.size());
        for (const GameState::TurnedMirror& mirror : state.turnedMirrors) {
            words.push_back(packPair(mirror.cell.x, mirror.cell.y));
            words.push_back(packZAndFlags(
                mirror.cell.z, static_cast<std::uint32_t>(mirror.quarterTurns)));
        }
    }
    // Elevators likewise: appended only on screens that have them, after a
    // marker that keeps the tail unambiguous from the mirror section.
    if (!state.elevators.empty()) {
        words.push_back(0xE1E7A702U);
        words.push_back(state.elevators.size());
        for (const GameState::Elevator& elevator : state.elevators) {
            words.push_back(packZAndFlags(
                elevator.cell.z, static_cast<std::uint32_t>(elevator.phase)));
        }
    }
    if (!state.minecarts.empty()) {
        words.push_back(0xCA471702U);
        words.push_back(state.minecarts.size());
        for (const GameState::Minecart& minecart : state.minecarts) {
            words.push_back(packPair(minecart.cell.x, minecart.cell.y));
            words.push_back(packZAndFlags(
                minecart.cell.z, static_cast<std::uint32_t>(minecart.phase)));
        }
    }
    if (!state.wardrobes.empty()) {
        words.push_back(0xA4D20BE2U);
        words.push_back(state.wardrobes.size());
        for (const GameState::Wardrobe& wardrobe : state.wardrobes) {
            words.push_back(packPair(wardrobe.cell.x, wardrobe.cell.y));
            words.push_back(packZAndFlags(
                wardrobe.cell.z, enumCode(wardrobe.character)));
        }
    }
    if (!state.activeButtons.empty()) {
        words.push_back(0xB0770A02U);
        words.push_back(state.activeButtons.size());
        for (const GridPosition3 button : state.activeButtons) {
            words.push_back(packPair(button.x, button.y));
            words.push_back(packZAndFlags(button.z, 0));
        }
    }
    return PackedStateKey(std::move(words));
}

std::size_t PackedStateKeyHash::operator()(const PackedStateKey& key) const
{
    // SplitMix64's avalanche step gives well-distributed hashes even though
    // adjacent coordinates and entity ids tend to differ by only one bit.
    std::uint64_t hash = 0x9e3779b97f4a7c15ULL;
    for (std::uint64_t word : key.words_) {
        word += 0x9e3779b97f4a7c15ULL;
        word = (word ^ (word >> 30)) * 0xbf58476d1ce4e5b9ULL;
        word = (word ^ (word >> 27)) * 0x94d049bb133111ebULL;
        word ^= word >> 31;
        hash ^= word + 0x9e3779b97f4a7c15ULL +
            (hash << 6) + (hash >> 2);
    }
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        hash ^= hash >> 32;
    }
    return static_cast<std::size_t>(hash);
}

} // namespace sokoban::solver::detail
