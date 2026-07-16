#pragma once

#include "engine/Config.hpp"
#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace sokoban {

// Headless orchestration for one playable screen. This owns command buffering,
// authoritative state, action timing, history, undo/restart, and automatic
// world steps while Application owns input translation and presentation.
class GameplaySession {
public:
    struct Controls {
        bool undoHeld = false;
        std::optional<MoveDirection> verticalMove;
        std::optional<MoveDirection> horizontalMove;
    };

    // One discrete world step (or undo/restart transition). State remains at
    // `before` until the presentation layer completes the action.
    struct Action {
        GameState before;
        GameState after;
        float durationSeconds = config::stepDurationSeconds;
        bool playerPushing = false;
        bool reversed = false;
        std::optional<MoveDirection> facingDirection;
    };

    void reset(const Level& level);

    void queueMove(MoveDirection direction);
    void queueUndo();
    void queueRestart();

    [[nodiscard]] bool tryStartNextAction(const Level& level, const Controls& controls);
    void advanceActiveAction(float dt);
    void completeActiveAction();

    [[nodiscard]] const GameState& state() const { return state_; }
    [[nodiscard]] bool moving() const { return moving_; }
    [[nodiscard]] const Action& activeAction() const { return activeAction_; }
    [[nodiscard]] float activeActionDuration() const { return activeAction_.durationSeconds; }
    [[nodiscard]] float activeActionRemainingSeconds() const;
    [[nodiscard]] bool activeActionComplete() const;
    [[nodiscard]] std::size_t historySize() const { return moveHistory_.size(); }
    [[nodiscard]] float stepDurationSeconds() const { return stepDurationSeconds_; }
    [[nodiscard]] const rules::StepRates& stepRates() const { return stepRates_; }

    void setStepDurationSeconds(float durationSeconds) { stepDurationSeconds_ = durationSeconds; }
    void setStepRates(rules::StepRates rates) { stepRates_ = rates; }

private:
    enum class CommandType {
        Move,
        Undo,
        Restart,
    };

    struct Command {
        CommandType type = CommandType::Move;
        MoveDirection direction = MoveDirection::Up;
    };

    [[nodiscard]] bool tryStartHeldMove(const Level& level, const Controls& controls);
    [[nodiscard]] bool tryStartWorldStep(const Level& level, std::optional<MoveDirection> playerInput);
    [[nodiscard]] bool tryStartUndoMove();
    [[nodiscard]] bool tryStartRestart(const Level& level);
    [[nodiscard]] bool tryStartHeldDirection(
        const Level& level,
        MoveDirection direction,
        std::optional<MoveDirection> queuedDirection);
    [[nodiscard]] bool hasPendingMove(MoveDirection direction) const;
    [[nodiscard]] Action invertAction(const Action& action) const;
    void beginAction(Action action);

    GameState state_;
    std::deque<Command> pendingCommands_;
    // Completed forward actions that can still be undone. Reversed actions
    // remain in moveHistory_ for diagnostics but never become undoable again.
    std::vector<Action> undoHistory_;
    std::vector<Action> moveHistory_;
    Action activeAction_;
    float moveElapsed_ = 0.0f;
    float stepDurationSeconds_ = config::stepDurationSeconds;
    rules::StepRates stepRates_ {};
    bool moving_ = false;
    // Rewinding freezes pending slides and conveyors until the next
    // input-driven step.
    bool autoMotionPaused_ = false;
};

} // namespace sokoban
