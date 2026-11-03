#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace sokoban {

// A character selects the rendered model and the player's base abilities.
// Rogue and Druid are the internal identities for Paladin and Thief.
enum class CharacterType : uint8_t {
    Lorekeeper,
    Rogue,
    Knight,
    Druid,
    Witch,
    Bard,
    Cleric,
    Ranger,
    CrystalDruid,
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
    case CharacterType::Ranger:
        return "ranger";
    case CharacterType::CrystalDruid:
        return "crystal-druid";
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
    if (name == "ranger") {
        return CharacterType::Ranger;
    }
    if (name == "crystal-druid") {
        return CharacterType::CrystalDruid;
    }
    return std::nullopt;
}

enum class CharacterAbility : uint16_t {
    Pull = 1 << 0,
    Swap = 1 << 1,
    MovementAura = 1 << 2,
    ProtectionAura = 1 << 3,
    AttackImmunity = 1 << 4,
    PushAlly = 1 << 5,
    PushableAlly = 1 << 6,
    PushEnemy = 1 << 7,
    Crystallize = 1 << 8,
};

struct CharacterAbilities {
    static constexpr uint8_t unlimitedPush = 255;

    uint8_t pushPower = 0;
    uint16_t flags = 0;
    static constexpr uint32_t maxPacked = (1U << 17) - 1;

    [[nodiscard]] constexpr bool has(CharacterAbility ability) const
    {
        return (flags & static_cast<uint16_t>(ability)) != 0;
    }

    constexpr void grant(CharacterAbility ability)
    {
        flags |= static_cast<uint16_t>(ability);
    }

    [[nodiscard]] constexpr uint32_t packed() const
    {
        return pushPower | (static_cast<uint32_t>(flags) << 8);
    }

    [[nodiscard]] static constexpr CharacterAbilities unpack(uint32_t value)
    {
        return { static_cast<uint8_t>(value), static_cast<uint16_t>(value >> 8) };
    }

    bool operator==(const CharacterAbilities&) const = default;
};

[[nodiscard]] constexpr CharacterAbilities baseCharacterAbilities(
    CharacterType character)
{
    CharacterAbilities abilities;
    switch (character) {
    case CharacterType::Lorekeeper:
    case CharacterType::Rogue:
    case CharacterType::Ranger:
        abilities.pushPower = 1;
        abilities.grant(CharacterAbility::PushEnemy);
        break;
    case CharacterType::Knight:
        abilities.pushPower = CharacterAbilities::unlimitedPush;
        abilities.grant(CharacterAbility::PushEnemy);
        abilities.grant(CharacterAbility::PushAlly);
        break;
    case CharacterType::Druid:
        abilities.grant(CharacterAbility::Pull);
        break;
    case CharacterType::CrystalDruid:
        abilities.grant(CharacterAbility::Crystallize);
        break;
    case CharacterType::Witch:
        abilities.pushPower = 1;
        abilities.grant(CharacterAbility::Swap);
        break;
    case CharacterType::Bard:
        abilities.pushPower = 1;
        abilities.grant(CharacterAbility::MovementAura);
        break;
    case CharacterType::Cleric:
        abilities.grant(CharacterAbility::AttackImmunity);
        abilities.grant(CharacterAbility::ProtectionAura);
        abilities.grant(CharacterAbility::PushableAlly);
        break;
    }
    return abilities;
}

} // namespace sokoban
