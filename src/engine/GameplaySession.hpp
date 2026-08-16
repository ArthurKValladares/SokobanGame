#pragma once

#include "engine/ActionPlan.hpp"
#include "engine/ActionPresentation.hpp"
#include "engine/ActionScheduler.hpp"
#include "engine/GameplayConfig.hpp"
#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
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
    // The world as it will stand once everything in flight has committed.
    //
    // What rendering needs when it wants to know where an action is going: no
    // single action's `after` answers that any more, since each is a snapshot
    // taken when it started and blind to the others. Applying every in-flight
    // delta to the live state does, and the deltas are disjoint by construction
    // so the order they are applied in does not matter.
    [[nodiscard]] GameState projectedState() const;

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
    // How admissions have gone this screen, for deciding whether the
    // reservation machinery is earning its keep. See `AdmissionStats`.
    [[nodiscard]] const ActionScheduler::AdmissionStats& admissionStats() const
    {
        return scheduler_.admissionStats();
    }
    [[nodiscard]] float stepDurationSeconds() const { return stepDurationSeconds_; }
    [[nodiscard]] const rules::StepRates& stepRates() const { return stepRates_; }

    void setStepDurationSeconds(float durationSeconds);
    void setStepRates(rules::StepRates rates) { stepRates_ = rates; }
    // Optional world-level invariant checked against the state projected after
    // each newly planned action. Composed overworlds use this to prevent one
    // input from leaving living players owned by different screens.
    //
    // The policy is runtime configuration, not checkpoint state. Reset and
    // restore deliberately preserve it so Application can install the map
    // invariant once for the currently loaded world.
    using ActionAdmissionPolicy = std::function<bool(const GameState&)>;
    void setActionAdmissionPolicy(ActionAdmissionPolicy policy)
    {
        actionAdmissionPolicy_ = std::move(policy);
    }
    void clearActionAdmissionPolicy() { actionAdmissionPolicy_ = {}; }
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

    // Why an attempt to start something did or did not produce an action.
    //
    // The distinction exists for the command queue. Under the one-at-a-time
    // guard a pop was always followed by an admission, so a plain bool was
    // enough; once the reservation table can refuse, a command that was refused
    // has to go back on the queue and one that was impossible must not, or a
    // player walking into a wall would retry it until it went stale.
    enum class StartOutcome {
        // Admitted, and now in flight.
        Started,
        // Nothing to do. There was no plan to make - the move is into a wall,
        // there is no undo history, the mirror activation is invalid. Retrying
        // cannot help, because only a committed action changes the state this
        // was judged against, and by then the command is the player's stale
        // idea of a world that has moved on.
        Impossible,
        // Planned, but something already in flight holds a cell it needs. The
        // decision is queued, not rejected: it goes back on the front of the
        // queue and is retried on a later frame, and staleness is what stops it
        // retrying forever.
        Refused,
    };

    void enqueue(Command command);
    [[nodiscard]] bool isStale(const Command& command) const;

    [[nodiscard]] StartOutcome tryStartHeldMove(
        const Level& level, const Controls& controls);
    // One input-driven step for every living player, together with the slide it
    // sets off, admitted as one causal group.
    //
    // The consequence is planned here rather than when the step commits. Both
    // plans are therefore made from the same instant, which is what settles the
    // slide's destination at the moment of the push; planning it on commit
    // would settle it against a state that arrives a step late, and that is
    // precisely the stability bug this whole design exists to remove.
    [[nodiscard]] StartOutcome tryStartPlayerStep(
        const Level& level, MoveDirection input);
    // Motion nobody asked for: entities still carrying momentum, and entities
    // standing on a belt.
    //
    // Sliders and riders are two actions, not one. A slide is committed to its
    // end as a chain, while a ride is one step and re-planned - belts never
    // terminate, so a chained ride would be an action that never ends.
    // Entities an action already owns are left out, or the same motion would be
    // planned twice and the copy refused by the claims of the original.
    [[nodiscard]] StartOutcome tryStartAmbientMotion(const Level& level);
    [[nodiscard]] StartOutcome tryStartMirrorAction(const Level& level);
    [[nodiscard]] StartOutcome tryStartUndoMove();
    [[nodiscard]] StartOutcome tryStartRestart(const Level& level);
    [[nodiscard]] StartOutcome tryStartHeldDirection(
        const Level& level,
        MoveDirection direction,
        std::optional<MoveDirection> queuedDirection);
    // Runs one queued command. `Impossible` also covers a command this context
    // ignores entirely, such as anything but undo while a player is dead.
    [[nodiscard]] StartOutcome runCommand(
        const Level& level, const Command& command, const Controls& controls);
    [[nodiscard]] bool hasPendingMove(MoveDirection direction) const;
    // Hands a plan to the scheduler, which admits it only if nothing already
    // running would be disturbed. Returns false when it was refused.
    //
    // `causalGroup` is the player action this one belongs to; zero opens a new
    // one. Consequences of an action - the slide a push starts - pass the group
    // of the action that caused them, so that they undo together.
    //
    // `deferral` holds an action back that was planned now but begins later -
    // the slide a push sets off. It is admitted immediately, so its cells are
    // held from this moment and nothing can invalidate the outcome already
    // promised for it.
    [[nodiscard]] bool beginAction(
        Action action,
        std::vector<GameState> legs = {},
        std::size_t causalGroup = 0,
        ActionDeferral deferral = {});
    [[nodiscard]] bool actionAdmissionAllows(const Action& action) const;
    [[nodiscard]] bool actionAdmissionAllows(
        const std::vector<ActionScheduler::Pending>& actions) const;
    // Everything the scheduler needs to admit one plan, including the claims
    // derived from the cells its entities pass through.
    [[nodiscard]] ActionScheduler::Pending makePending(
        const Action& action,
        const std::vector<GameState>& legs,
        ActionDeferral deferral) const;
    // Drops entities some action in flight is already moving.
    //
    // Nothing may plan for them: authoritative state does not change until an
    // action commits, so a second plan made from it would be the same motion
    // over again, and its claims would collide with the copy already running.
    // This applies as much to the consequences of a player's step as to ambient
    // motion - a block still sliding is visible in the state that step produces
    // and would otherwise be handed a second slide.
    [[nodiscard]] std::vector<EntityId> withoutEntitiesInFlight(
        std::vector<EntityId> candidates) const;
    // Bookkeeping for one action that has just committed: history, the undo
    // stack, and the running move total.
    void recordCompletion(const Action& action, std::size_t causalGroup);
    // Where the undo stack starts from, which is the last entry's `after`, or
    // the level's opening state when there are none left to pop.
    [[nodiscard]] const GameState& undoBaseState() const;
    // Re-derives entries from `index` onward so each begins where the previous
    // one ended.
    //
    // An entry records what its action *changed*, not the state that action was
    // planned against - under concurrency those differ, because another action
    // may have committed in between and written entities of its own. Storing
    // the change and replaying it onto the running chain keeps `restore`'s
    // linear-chain invariant true by construction, whatever order things
    // actually committed in.
    //
    // Needed on a fold: composing a consequence into the entry its cause opened
    // moves that entry's endpoint, and anything appended after it has to
    // follow. The tail is short - only what committed between the two - so this
    // is not the whole stack.
    void rebaseUndoFrom(std::size_t index);

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
    //
    // One entry per *player* action, not per scheduled action. A push and the
    // slide it sets off are two actions to the scheduler and one entry here:
    // the consequence folds into the entry its cause created, so the folded
    // entry is exactly the whole-world transition `worldStep` would have
    // produced. That is what keeps the save format and `matchesForwardTransition`
    // untouched by the planner split.
    std::vector<Action> undoHistory_;
    // Which causal group each undo entry belongs to, so a consequence folds
    // into its own cause rather than into whatever committed most recently -
    // ambient motion can interleave. Parallel to `undoHistory_`, never
    // persisted: a restored session has no action in flight, so every group is
    // closed and each entry is its own.
    std::vector<std::size_t> undoGroups_;
    std::size_t nextCausalGroup_ = 1;
    std::vector<Action> moveHistory_;
    // The level's opening state, which is where the undo chain is anchored.
    // `restore` validates the stack by replaying from exactly this.
    GameState undoBaseState_;
    float stepDurationSeconds_ = config::stepDurationSeconds;
    int playerMoveCount_ = 0;
    // Transient event sequence; intentionally excluded from save snapshots.
    std::size_t mirrorActivationSequence_ = 0;
    std::vector<GridPosition3> lastMirrorSwapDestinations_;
    rules::StepRates stepRates_ {};
    // Rewinding freezes pending slides and conveyors until the next
    // input-driven step.
    bool autoMotionPaused_ = false;
    ActionAdmissionPolicy actionAdmissionPolicy_;
};

} // namespace sokoban
