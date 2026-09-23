#pragma once

#include "engine/GameplayPresentation.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/Level.hpp"

#include <optional>
#include <vector>

namespace sokoban {

// Headless per-frame bridge between semantic input, GameplaySession, and its
// presentation state. It emits domain outcomes; Application owns their
// persistence, audio, level-loading, and editor consequences.
class GameplayLoop {
public:
    struct ButtonState {
        bool pressed = false;
        bool down = false;
    };

    struct InputFrame {
        ButtonState up;
        ButtonState down;
        ButtonState left;
        ButtonState right;
        bool undoPressed = false;
        bool undoDown = false;
        bool restartPressed = false;
        bool cycleHeroPressed = false;
        // Contextual world interaction. GameplayLoop handles mirror
        // activation; Application handles transitions such as entering a
        // selector.
        bool interactPressed = false;
    };

    struct UpdateResult {
        struct TurretShotPresentation {
            rules::TurretShot shot;
            // Time from this update to the gameplay impact. The particle
            // effect starts early enough for its fast trace to arrive then.
            float impactDelaySeconds = 0.0f;
        };

        bool stateCommitted = false;
        bool screenSolved = false;
        bool draftSolved = false;
        bool mirrorActivated = false;
        bool witchSwapped = false;
        bool activeHeroChanged = false;
        std::vector<GridPosition3> mirrorSwapDestinations;
        std::vector<GridPosition3> witchSwapDestinations;
        std::vector<TurretShotPresentation> turretShots;
    };

    [[nodiscard]] static UpdateResult update(
        const Level& level,
        GameplaySession& session,
        GameplayPresentation& presentation,
        const InputFrame& input,
        float dt,
        bool playingDraft);

    [[nodiscard]] static std::optional<MoveDirection> pressedVertical(
        const InputFrame& input);
    [[nodiscard]] static std::optional<MoveDirection> pressedHorizontal(
        const InputFrame& input);
    [[nodiscard]] static std::optional<MoveDirection> heldVertical(
        const InputFrame& input);
    [[nodiscard]] static std::optional<MoveDirection> heldHorizontal(
        const InputFrame& input);
};

} // namespace sokoban
