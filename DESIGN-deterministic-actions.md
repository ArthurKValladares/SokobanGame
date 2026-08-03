# Deterministic Actions — Design

Status: **design agreed, not implemented.** This document is the spec to build
from. It records the decisions, the reasoning behind them, and the parts that
are still genuinely open.

## The guarantee

> Every action's outcome is computed in full at the moment the action starts,
> and nothing that happens during the action can change it.

Two things follow that are worth stating separately, because they are often
conflated:

1. **Determinism** — the outcome is a pure function of the state at start.
2. **Stability** — the outcome survives everything else that runs concurrently
   while the action plays out.

The current code already has (1). `GameplaySession::Action` holds a complete
`before` and `after` `GameState`, `after` is produced by `rules::step` when the
action begins, and `state_` stays at `before` until the presentation finishes.
Nothing during the animation can alter it.

What is missing is (2), and only for motion that spans more than one world
step. An ice slide is not an action today — it is momentum stored in
`GameState::sliding`, re-evaluated every step. So the destination is recomputed
as it travels, and a second push into its path changes where it lands.

## Why this needs a scheduler at all

Everything today runs on one global lockstep timeline: one action at a time,
and within a step the player, slide momentum and conveyors all advance
together. On that model, stability is free — you would simply plan the whole
slide up front as one long action and queue player input during it.

The reason not to do that is feel, not correctness: the player would be locked
out for the length of a long slide. The entire reservation system below exists
to buy **responsiveness**, and it is worth re-reading that sentence before
starting work, because if the simple version turns out to feel fine, none of
the rest of this is needed.

## Decisions

| Question | Decision |
| --- | --- |
| Conveyor motion | Per-step actions, re-reserved each step |
| Conflicting input | Queued, not rejected |
| Undo | Only permitted when nothing is in flight |
| Causal chains | Committed in full at the first action |
| Conflict detection | Space-time read/write sets |
| Red rejection glow | Dropped for now |

### Chains and reservations interact in a way that matters

An earlier version of this reasoning claimed that the guarantee *forces* full
chain commitment. That was wrong. If a chained slide is planned as a new action
when the previous one completes, its outcome is still fully known when it
starts, which satisfies the guarantee as stated. Full chain commitment is a
choice about the **player's** mental model — "I pushed this, so I know
everything that follows" — not a correctness requirement.

Having chosen it, the space-time model stops being a nicety and becomes load
bearing. A chain can span most of the board, but it occupies each cell only
during a short interval. With plain whole-duration dirty tiles, committing a
full chain would lock the player out for as long as the chain runs. With
space-time intervals the player is released as soon as their own step's
interval ends, and is only blocked from cells the chain has not reached yet.

**The two decisions only work together. Do not implement full chain commitment
on top of whole-duration locking.**

## Conflict rule

Each plan carries two sets of `(cell, step-interval)` entries:

- **write set** — cells the action will occupy or modify, and when.
- **read set** — cells whose contents the precomputed outcome depended on, and
  when. A slide reads every cell along its path (it had to check they were
  clear) and the cell that stops it.

Two plans conflict iff one's write set intersects the other's read **or** write
set, over overlapping intervals.

A write-only check is not sufficient. If the player walks onto the slide's
destination and stands there, the player writes that cell only during their own
step; by the time the block arrives, a write-write test sees nothing. The read
set is what catches it, because the slide's outcome depended on that cell being
empty at arrival time.

## Conveyors

Belt riders plan one step at a time and re-reserve on each step, so their locks
are one cell and one interval long. This keeps ambient motion from holding
long-lived reservations, which would otherwise make the area around any belt
permanently unusable and stop two riders from following each other down the
same belt.

`rules::hasPendingMotion` already identifies when the world should keep
stepping; that is the natural trigger for scheduling the next belt action.

### Starvation

Because a belt rider releases its reservation at the end of each step and
immediately takes another, a queued player action waiting on a cell in the
belt's path can be shut out indefinitely.

**At each scheduling point, admit queued actions before starting new ambient
actions.** This has to be designed in from the start; it is awkward to retrofit.

## Queueing

A queued command is planned when it dequeues, not when it is entered, so it is
deterministic under the guarantee — its outcome is known the instant it starts.

Two policies are needed:

- **Bounded depth.** `pendingCommands_` is currently an unbounded `std::deque`.
  If a player mashes a direction during a long slide, they should not get eight
  moves spooling out afterwards. Small bound, likely 1–2.
- **Staleness.** A command entered before a chain resolved may no longer be
  what the player wanted. Drop commands older than a short threshold.

Both are tuning decisions best made against a playable build.

## Undo

Permitted only when nothing is in flight. This keeps `undoHistory_` a linear
stack of invertible whole-world transitions and sidesteps the fact that
overlapping actions make history a DAG rather than a sequence.

Practically: `queueUndo` and `queueRestart` should refuse to start while the
scheduler is non-empty, and either drop or hold the request.

## Chain termination

Full chain commitment means planning must provably terminate. Three hazards:

- **Cycles.** A block sliding onto a belt that carries it back onto ice can
  loop. Planning needs a visited set over `(entity, cell, direction)` and a
  hard cap on total planned steps.
- **Handoff to ambient motion.** Conveyors are per-step by decision. A chain
  that runs onto a belt should *end* there and hand off, rather than trying to
  plan an unbounded ride. This is the natural chain boundary.
- **Growth.** The involved-entity set is the causal closure, not just the
  entities the player touched. Planning must accumulate it as it goes.

## The delta refactor

This is the largest single piece of work and it is unavoidable.

`Action` carries whole-world `GameState`s, and `completeActiveAction` does
`state_ = activeAction_.after`. With two concurrent actions in flight,
whichever finishes last overwrites the other's effect completely.

Actions must carry **deltas** — per-entity changes — rather than whole states.
That ripples into `invertAction`, `undoHistory_`, `Snapshot`, and every test
that compares `GameState` values. Budget accordingly, and do it first: nothing
else can be correct until it lands.

## Shape

`rules::previewMirrorActivation` is already close to the API needed — it
returns `{after, entities}` plus beam segments, computed purely. Generalise
that shape:

```cpp
struct Reservation {
    GridPosition3 cell {};
    int firstStep = 0;   // inclusive
    int lastStep = 0;    // inclusive
};

struct ActionPlan {
    std::vector<EntityId> involved;   // causal closure, not just what was touched
    std::vector<Reservation> writes;
    std::vector<Reservation> reads;
    StateDelta effect;                // never a whole GameState
    float durationSeconds = 0.0f;
    ActionPresentationTimeline presentation;
};
```

- **Planners**, one per action kind — `planMove`, `planPush`, `planSlide`,
  `planMirrorSwap`, `planConveyorRide` — each a pure
  `(Level, GameState, params) -> std::optional<ActionPlan>`. Keeping them pure
  matches the existing `rules::` style and keeps them trivially testable.
- **`ReservationTable`** holding in-flight plans, with
  `conflict(plan) -> std::optional<Conflict>` returning the offending cells and
  entities.
- **`ActionScheduler`** replacing the single `activeAction_`: in-flight plans
  each with their own elapsed time, applying deltas on completion, admitting
  queued actions ahead of ambient ones.

## Suggested sequence

1. **Done.** `StateDelta` (`src/engine/StateDelta.hpp`) plus delta application
   in `GameplaySession::completeActiveAction`. No behaviour change.

   Smaller than this document originally implied, and deliberately so. The
   substance of the step is that completing an action writes only what that
   action changed, and that undo runs the same machinery backwards — both of
   which fall out of one call site. `Action` still carries `before`/`after`,
   because `GameplayPresentation` genuinely needs paired whole states to
   animate from, and the delta is derived from them. When step 2 introduces
   `ActionPlan`, the stored `effect` belongs there rather than on `Action`.

   Known gap, deferred to step 4: `GameplayPresentation::beginAction` calls
   `syncToGameState(action.before)`, which is a stale whole-world snapshot.
   Harmless with one action in flight; wrong once actions overlap.
2. **Done.** `ActionPlan` (`src/engine/ActionPlan.hpp`) plus pure `plans::`
   functions for the existing action kinds — `worldStep`, `fromMirrorPreview`,
   `restart`, `inverted`. `GameplaySession` keeps only timing, history and the
   running move total. No behaviour change.

   `GameplaySession::Action` is now an **alias** for `ActionPlan` rather than
   its own struct, and `ActionPlan` deliberately carries no new fields. The
   reason is that `Snapshot` — including its `undoStack` of actions — is
   serialised into save files by `PlayerProfileCodec`. Adding `effect` there
   would either bloat every save with a delta per undo entry, or default on
   load and make a restored action compare unequal to a live one. The delta
   therefore stays derived at completion, and the `effect` field belongs on
   whatever the scheduler holds in step 4, not on the persisted record.

   Move counts are left at zero by the planners: only the session knows the
   running total.
3. Full-chain planning for slides. Behaviour changes here: slides become
   stable. Still single-timeline, so no reservations needed yet.
4. `ReservationTable` and concurrent execution, with queueing and admission
   priority.
5. Conveyors as per-step actions.

Steps 1 and 2 are the risky, wide-reaching ones and produce no visible change;
steps 3–5 are where behaviour moves. Keeping that split means most of the churn
is verifiable by existing tests.

## Testing

- Golden traces: a recorded input sequence produces an exact `ActionPlan`
  sequence. `Action` already has `operator==`, and `GameState` is cheap to copy
  and compare, so this is close to free.
- Stability property: for any plan, replaying it against a state mutated only
  outside its read set yields the identical outcome. This is the guarantee
  stated as an executable test, and it is the single most valuable test here.
- Conflict-rule unit tests, especially the stationary-blocker case that
  write-only detection misses.
- Termination: adversarial ice/belt loops must hit the cap rather than hang.
- Starvation: a queued player action adjacent to a running belt must be
  admitted within a bounded number of steps.

## Open risks

- **Concurrency may be rarer than it looks.** Full chain commitment reserves a
  lot. It is worth instrumenting how often a queued action actually waits, on
  real levels, before investing further in permissiveness.
- **Replay determinism is a free byproduct** if the action log is designed for
  it, and it makes debugging concurrency far easier. Worth keeping in view
  while shaping `ActionPlan`.
- **The red glow was dropped, not resolved.** If playtesting shows queued
  actions read as dropped input, some quiet indicator will be needed.
