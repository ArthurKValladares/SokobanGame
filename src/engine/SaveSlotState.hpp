#pragma once

namespace sokoban {

enum class SaveSlotState {
    Empty,
    Ready,
    Recoverable,
    Corrupt,
    Unavailable,
};

[[nodiscard]] constexpr bool saveSlotCanLoad(SaveSlotState state)
{
    return state != SaveSlotState::Corrupt &&
        state != SaveSlotState::Unavailable;
}

[[nodiscard]] constexpr bool saveSlotHasStoredData(SaveSlotState state)
{
    return state != SaveSlotState::Empty;
}

} // namespace sokoban
