#pragma once

#include "engine/ScreenSelectorState.hpp"

#include <array>
#include <string_view>

namespace sokoban::selectorRender {

inline constexpr std::string_view aPlayableModelName =
    "ScreenSelectorAPlayable";
inline constexpr std::string_view aSolvedModelName =
    "ScreenSelectorASolved";
inline constexpr std::string_view aUnavailableModelName =
    "ScreenSelectorAUnavailable";
inline constexpr std::string_view bPlayableModelName =
    "ScreenSelectorBPlayable";
inline constexpr std::string_view bSolvedModelName =
    "ScreenSelectorBSolved";
inline constexpr std::string_view bUnavailableModelName =
    "ScreenSelectorBUnavailable";

inline constexpr std::array modelNames {
    aPlayableModelName,
    aSolvedModelName,
    aUnavailableModelName,
    bPlayableModelName,
    bSolvedModelName,
    bUnavailableModelName,
};

[[nodiscard]] constexpr std::string_view modelName(
    ScreenSelectorViewState state)
{
    if (state.lastScreenInLevel) {
        switch (state.status) {
        case ScreenSelectorStatus::Playable: return bPlayableModelName;
        case ScreenSelectorStatus::Solved: return bSolvedModelName;
        case ScreenSelectorStatus::Unavailable: return bUnavailableModelName;
        }
    }
    switch (state.status) {
    case ScreenSelectorStatus::Playable: return aPlayableModelName;
    case ScreenSelectorStatus::Solved: return aSolvedModelName;
    case ScreenSelectorStatus::Unavailable: return aUnavailableModelName;
    }
    return aUnavailableModelName;
}

} // namespace sokoban::selectorRender
