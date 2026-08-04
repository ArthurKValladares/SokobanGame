#pragma once

#include "engine/ActionPlan.hpp"
#include "engine/ActionPresentation.hpp"
#include "engine/ActionScheduler.hpp"
#include "engine/GameplayConfig.hpp"
#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace sokoban {

// Headless orchestration for one playable screen. This owns command buffering,
// authoritative state, action timing, history, undo/restart, and automatic
// world steps. GameplayLoop translates semantic input and advances the
// presentation around its actions.
class GameplaySession {
public:
    struct Controls {
        bool undoHeld = false;
        std::optional<MoveDirection> verticalMove;
        std::optional<MoveDirection> horizontalMove;
    };

    // One discrete world step (or undo/restart transition). State remains at
    // `before` until the presentation layer completes the action.
    //
    // The type lives in ActionPlan.hpp, where the pure functions that produce
    // it live too. It is an alias rather than its own struct so that the save
    // format, which persists these inside `Snapshot`, is unaffected.
    using Action = ActionPlan;

    struct Snapshot {
        GameState state;
        std::vector<Action> undoStack;
        int playerMoveCount = 0;
        bool automaticMotionPaused = false;

        bool operator==(const Snapshot&) const = default;
    };

    void reset(const Level& level);
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] bool restore(const Level& level, const Snapshot& snapshot);

    void queueMove(MoveDirection direction);
    void queueMirror();
    void queueUndo();
    void queueRestart();

    [[nodiscard]] bool tryStartNextAction(const Level& level, const Controls& controls);
    void advanceActiveAction(float dt);
    void completeActiveAction();

    [[nodiscard]] const GameState& state() const { return scheduler_.state(); }
    [[nodiscard]] bool moving() const { return !scheduler_.idle(); }

    // Several actions can be in flight at once. The accessors below still speak
    // in terms of a single "active" action, resolving to the one that has been
    // running longest, so callers that have no notion of concurrency yet -
    // `Application` and `RenderFrameBuilder` - keep working unchanged. They read
    // as a default-constructed action while idle.
    [[nodiscard]] const Action& activeAction() const;
    // The states the active action passes through, one per world step. A slide
    // resolved as a chain has several; everything else has one. The presentation
    // uses these to animate a chain tile by tile rather than interpolating once
    // from start to finish. Empty for a restored action, which already carries
    // its built timeline.
    [[nodiscard]] const std::vector<GameState>& activeActionLegs() const;
    [[nodiscard]] float activeActionDuration() const;
    [[nodiscard]] float activeActionElapsedSeconds() const;
    [[nodiscard]] float activeActionRemainingSeconds() const;
    [[nodiscard]] bool activeActionComplete() const;
    [[nodiscard]] const std::vector<ActionScheduler::InFlight>& inFlight() const
    {
        return scheduler_.inFlight();
    }
    // One action in flight, by the id `tryStart` gave it. Null once it has
    // committed.
    [[nodiscard]] const ActionScheduler::InFlight* findInFlight(
        std::size_t actionId) const;
    // How long until the next action finishes. Advancing by more than this
    // would step over a completion boundary, committing an action late and
    // planning whatever follows it from a state that never existed. Zero when
    // idle, or when something is already due.
    [[nodiscard]] float timeToNextCompletion() const;
    // Whether any action has run its full duration. `activeActionComplete`
    // asks the same of the oldest one only.
    [[nodiscard]] bool anyActionComplete() const;
    [[nodiscard]] std::size_t historySize() const { return moveHistory_.size(); }
    [[nodiscard]] std::size_t undoCount() const { return undoHistory_.size(); }
    [[nodiscard]] std::size_t mirrorActivationSequence() const
    {
        return mirrorActivationSequence_;
    }
    [[nodiscard]] const std::vector<GridPosition3>&
        lastMirrorSwapDestinations() const
    {
        return lastMirrorSwapDestinations_;
    }
    [[nodiscard]] int playerMoveCount() const { return playerMoveCount_; }
    [[nodiscard]] float stepDurationSeconds() const { return stepDurationSeconds_; }
    [[nodiscard]] const rules::StepRates& stepRates() const { return stepRates_; }

    void setStepDurationSeconds(float durationSeconds);
    void setStepRates(rules::StepRates rates) { stepRates_ = rates; }
    // Both target the oldest in-flight action. `GameplayLoop` installs a
    // timeline on the action it has just started, which is the oldest only
    // because it starts one at a time; the id overloads are what a caller
    // starting several in a frame needs.
    void setActiveActionPresentation(ActionPresentationTimeline presentation);
    void setActiveActionDuration(float durationSeconds);
    void setActionPresentation(
        std::size_t actionId, ActionPresentationTimeline presentation);
    void setActionDuration(std::size_t actionId, float durationSeconds);

private:
    enum class CommandType {
        Move,
        Mirror,
        Undo,
        Restart,
    };

    struct Command {
        CommandType type = CommandType::Move;
        MoveDirection direction = MoveDirection::Up;
        // When it was entered, on the session clock, so a command the player
        // has visibly outlived can be dropped rather than played back.
        float queuedAtSeconds = 0.0f;
    };

    void enqueue(Command command);
    [[nodiscard]] bool isStale(const Command& command) const;

    [[nodiscard]] bool tryStartHeldMove(const Level& level, const Controls& controls);
    [[nodiscard]] bool tryStartWorldStep(const Level& level, std::optional<MoveDirection> playerInput);
    [[nodiscard]] bool tryStartMirrorAction(const Level& level);
    [[nodiscard]] bool tryStartUndoMove();
    [[nodiscard]] bool tryStartRestart(const Level& level);
    [[nodiscard]] bool tryStartHeldDirection(
        const Level& level,
        MoveDirection direction,
        std::optional<MoveDirection> queuedDirection);
    [[nodiscard]] bool hasPendingMove(MoveDirection direction) const;
    // Hands a plan to the scheduler, which admits it only if nothing already
    // running would be disturbed. Returns false when it was refused.
    [[nodiscard]] bool beginAction(Action action, std::vector<GameState> legs = {});
    // Bookkeeping for one action that has just committed: history, the undo
    // stack, and the running move total.
    void recordCompletion(const Action& action);

    // Buffered input is a courtesy, not a recording. Once slides resolve as one
    // long action, an unbounded queue turns a moment of mashing during a slide
    // into a burst of moves afterwards that the player is no longer asking for.
    //
    // Two is enough for the input this game actually produces: a move plus the
    // perpendicular one that `tryStartHeldDirection` buffers for diagonals.
    static constexpr std::size_t maxQueuedCommands = 2;
    // Long enough to survive a normal step, short enough that a command does
    // not outlive the situation it was meant for.
    static constexpr float commandStalenessSeconds = 1.0f;

    // Owns the authoritative state, every action in flight, their per-action
    // clocks, the shared step clock, and the reservations that decide whether a
    // new action may join them. The session decides *what* to plan and in what
    // order; the scheduler only executes.
    ActionScheduler scheduler_;
    std::deque<Command> pendingCommands_;
    // Completed forward actions that can still be undone. Reversed actions
    // remain in moveHistory_ for diagnostics but never become undoable again.
    std::vector<Action> undoHistory_;
    std::vector<Action> moveHistory_;
    float stepDurationSeconds_ = config::stepDurationSeconds;
    int playerMoveCount_ = 0;
    // Transient event sequence; intentionally excluded from save snapshots.
    std::size_t mirrorActivationSequence_ = 0;
    std::vector<GridPosition3> lastMirrorSwapDestinations_;
    rules::StepRates stepRates_ {};
    // Rewinding freezes pending slides and conveyors until the next
    // input-driven step.
    bool autoMotionPaused_ = false;
};

} // namespace sokoban
