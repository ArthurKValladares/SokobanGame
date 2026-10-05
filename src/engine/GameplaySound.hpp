#pragma once

#include <string_view>

namespace sokoban {

enum class GameplaySound {
    PortalTravel,
    MinecartGateOpen,
    GateOpen,
    GateClose,
    RotatorTurn,
    PressurePlatePress,
    PressurePlateRelease,
    ButtonPress,
};

[[nodiscard]] constexpr std::string_view soundSetName(GameplaySound sound)
{
    switch (sound) {
    case GameplaySound::PortalTravel: return "portal-travel";
    case GameplaySound::MinecartGateOpen: return "minecart-gate-open";
    case GameplaySound::GateOpen: return "gate-open";
    case GameplaySound::GateClose: return "gate-close";
    case GameplaySound::RotatorTurn: return "rotator-turn";
    case GameplaySound::PressurePlatePress: return "pressure-plate-press";
    case GameplaySound::PressurePlateRelease: return "pressure-plate-release";
    case GameplaySound::ButtonPress: return "button-press";
    }
    return {};
}

// Transient, action-local timing. Neither cues nor their delivery state enter
// undo history or checkpoints, so loading and rewinding cannot replay effects.
struct GameplaySoundCue {
    GameplaySound sound;
    float triggerSeconds = 0.0f;
    bool emitted = false;

    bool operator==(const GameplaySoundCue&) const = default;
};

} // namespace sokoban
