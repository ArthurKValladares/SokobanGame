#pragma once

#include "engine/OverworldMap.hpp"

#include <nlohmann/json.hpp>

#include <string_view>

namespace sokoban::makerDraftCodec {

// Saved and recovery drafts allow incomplete puzzles and overworlds.
void validateDraftGrid(const Level::Definition& definition);
[[nodiscard]] nlohmann::json encodeOverworld(
    const OverworldDraftOverride& draft);
[[nodiscard]] OverworldDraftOverride decodeOverworld(
    const nlohmann::json& encoded,
    std::string_view name);

} // namespace sokoban::makerDraftCodec
