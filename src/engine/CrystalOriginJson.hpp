#pragma once

#include "engine/Rules.hpp"

#include <nlohmann/json.hpp>

#include <limits>
#include <stdexcept>

namespace sokoban {

template <typename Json>
[[nodiscard]] Json crystalOriginToJson(const GameState::CrystalOrigin& origin)
{
    Json result {
        { "character", origin.character
                ? Json(characterTypeName(*origin.character)) : Json(nullptr) },
        { "controller", origin.controller },
        { "abilityGrants", origin.abilityGrants.packed() },
        { "quarterTurns", origin.quarterTurns },
    };
    if (origin.facing) result["facing"] = static_cast<uint8_t>(*origin.facing);
    return result;
}

[[nodiscard]] inline GameState::CrystalOrigin crystalOriginFromJson(
    const nlohmann::json& value, TileType type)
{
    if (!tileTypeIsCrystal(type) || !value.is_object() ||
        value.size() != 4 + static_cast<std::size_t>(value.contains("facing")) ||
        !value.contains("character") || !value.contains("controller") ||
        !value.contains("abilityGrants") || !value.contains("quarterTurns")) {
        throw std::runtime_error("Invalid crystal origin");
    }
    const auto number = [&](const char* key, uint64_t maximum) {
        const auto& item = value.at(key);
        if (!item.is_number_integer() ||
            (!item.is_number_unsigned() && item.get<int64_t>() < 0) ||
            item.get<uint64_t>() > maximum) {
            throw std::runtime_error("Invalid crystal origin number");
        }
        return item.get<uint64_t>();
    };
    GameState::CrystalOrigin origin;
    if (!value.at("character").is_null()) {
        origin.character = characterTypeFromName(value.at("character").get<std::string>());
        if (!origin.character) throw std::runtime_error("Invalid crystal origin character");
    }
    origin.controller = number("controller", std::numeric_limits<EntityId>::max());
    origin.abilityGrants = CharacterAbilityGrants::unpack(
        static_cast<uint32_t>(number("abilityGrants", CharacterAbilityGrants::maxPacked)));
    origin.quarterTurns = static_cast<uint8_t>(number("quarterTurns", 3));
    if (value.contains("facing")) {
        origin.facing = static_cast<MoveDirection>(number("facing", static_cast<uint8_t>(MoveDirection::Right)));
    }
    if ((!origin.character && (origin.controller != invalidEntityId ||
             origin.abilityGrants.packed() != 0 || origin.facing)) ||
        (type == TileType::SkippingCrystal) != (origin.character == CharacterType::Ranger)) {
        throw std::runtime_error("Crystal origin does not match its rock");
    }
    return origin;
}

} // namespace sokoban
