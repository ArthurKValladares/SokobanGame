#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace sokoban {

// The character selected by a level. Gameplay abilities and the rendered
// model both key off this value, while animations remain shared for now.
enum class CharacterType : uint8_t {
    Lorekeeper,
    Rogue,
    Knight,
    Druid,
    Witch,
    Bard,
};

[[nodiscard]] constexpr std::string_view characterTypeName(
    CharacterType character)
{
    switch (character) {
    case CharacterType::Lorekeeper:
        return "lorekeeper";
    case CharacterType::Rogue:
        return "rogue";
    case CharacterType::Knight:
        return "knight";
    case CharacterType::Druid:
        return "druid";
    case CharacterType::Witch:
        return "witch";
    case CharacterType::Bard:
        return "bard";
    }
    return "rogue";
}

[[nodiscard]] constexpr std::optional<CharacterType> characterTypeFromName(
    std::string_view name)
{
    if (name == "lorekeeper") {
        return CharacterType::Lorekeeper;
    }
    if (name == "rogue") {
        return CharacterType::Rogue;
    }
    if (name == "knight") {
        return CharacterType::Knight;
    }
    if (name == "druid") {
        return CharacterType::Druid;
    }
    if (name == "witch") {
        return CharacterType::Witch;
    }
    if (name == "bard") {
        return CharacterType::Bard;
    }
    return std::nullopt;
}

// Lorekeeper is the overworld avatar. It intentionally inherits the Rogue's
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
