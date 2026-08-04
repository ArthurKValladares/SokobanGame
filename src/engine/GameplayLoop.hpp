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
        bool mirrorPressed = false;
        bool undoPressed = false;
        bool undoDown = false;
        bool restartPressed = false;
    };

    struct UpdateResult {
        bool stateCommitted = false;
        bool screenSolved = false;
        bool draftSolved = false;
        bool mirrorActivated = false;
        std::vector<GridPosition3> mirrorSwapDestinations;
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
