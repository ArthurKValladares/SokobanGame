#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace sokoban {

// The character selected by a level. Gameplay abilities and the rendered
// model both key off this value, while animations remain shared for now.
// Rogue and Druid are the internal identities for Paladin and Thief.
enum class CharacterType : uint8_t {
    Lorekeeper,
    Rogue,
    Knight,
    Druid,
    Witch,
    Bard,
    Cleric,
};

[[nodiscard]] constexpr std::string_view characterTypeName(
    CharacterType character)
{
    switch (character) {
    case CharacterType::Lorekeeper:
        return "lorekeeper";
    case CharacterType::Rogue:
        return "paladin";
    case CharacterType::Knight:
        return "knight";
    case CharacterType::Druid:
        return "thief";
    case CharacterType::Witch:
        return "witch";
    case CharacterType::Bard:
        return "bard";
    case CharacterType::Cleric:
        return "cleric";
    }
    return "paladin";
}

[[nodiscard]] constexpr std::optional<CharacterType> characterTypeFromName(
    std::string_view name)
{
    if (name == "lorekeeper") {
        return CharacterType::Lorekeeper;
    }
    if (name == "paladin" || name == "rogue") {
        return CharacterType::Rogue;
    }
    if (name == "knight") {
        return CharacterType::Knight;
    }
    if (name == "thief" || name == "druid") {
        return CharacterType::Druid;
    }
    if (name == "witch") {
        return CharacterType::Witch;
    }
    if (name == "bard") {
        return CharacterType::Bard;
    }
    if (name == "cleric") {
        return CharacterType::Cleric;
    }
    return std::nullopt;
}

// Lorekeeper is the overworld avatar. It intentionally inherits the Paladin's
// established rule set; the distinct value exists so rendering, saves and
// wardrobes can retain the selected appearance without inventing a new
// gameplay ability.
[[nodiscard]] constexpr CharacterType characterBehavior(
    CharacterType character)
{
    return character == CharacterType::Lorekeeper
        ? CharacterType::Rogue
        : character;
}

} // namespace sokoban
