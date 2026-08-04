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
| Player facing | Every instance faces the last input, moved or not |

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
3. **Done.** `plans::worldStep` resolves the whole chain: the first step applies
   the player's input, then the world keeps stepping while anything carries
   slide momentum, and the run becomes one action whose duration covers every
   leg. Slides are now stable — the destination is settled before the block has
   moved a tile, and nothing can start while the action runs.

   Consequences, all intended but worth knowing:

   - **The player is locked out for the whole chain.** Single timeline, so a
     long slide is a long wait. This is the regression step 4 removes: with
     space-time reservations the player is released as soon as their own leg's
     interval ends.
   - **One undo reverses the entire slide**, not one tile of it, since the
     chain is one history entry.
   - Conveyor riders stop after one step, as decided. `plans::anySlideMomentum`
     is what distinguishes them from sliders; `rules::hasPendingMotion` reports
     both and is the wrong test for chaining.

   Two things needed handling that the plan above did not anticipate:

   - `matchesForwardTransition` replays a single `rules::step` to validate a
     save, so it rejected chained actions outright. It now replays the chain —
     and still accepts a single step, so saves written before this change load
     unchanged.
   - Motion has to be staggered per leg, or a block that only starts moving on
     the fourth step would set off immediately. `GameplayPresentation` gained a
     leg-aware overload; the session carries the legs transiently
     (`activeActionLegs()`), never persisting them.

   Known gap: an animated event occurring in a *later* leg — a player killed
   part-way through a slide — has correct motion but no clip, because
   animations are built from the first leg alone. Fixing it means building a
   timeline per leg and merging them.
4. Split into three, because the presentation half is the largest part.

   An earlier note here claimed `PresentationTests` could not be run outside
   Windows because it needed `std::ranges` algorithms GCC 11 lacks. That is not
   true — it compiles and passes under GCC 11. See `tools/build_headless_tests.sh`,
   which builds and runs 41 of the 43 suites without Vulkan or a display. Only
   `vulkan_device_selection` and `frame_descriptor_sync` need the Windows build.
   Nothing in step 4 is unverifiable.

   **4a — done.** `Reservation`, `ActionReservations` and `ReservationTable`
   (`src/engine/Reservation.hpp`), plus `plans::reservationsFor`. Pure and
   headless; nothing consumes it yet, so no behaviour change.

   Two details settled here that the sketch above left open:

   - **Step numbering.** During step *i* an entity travels from `cells[i]` to
     `cells[i + 1]`, so it claims both for that step. Numbering by state index
     instead is off by one and lets a claim start a step late.
   - **A resting cell is claimed open-ended.** This is what makes the read/write
     distinction actually bite. An entity that steps onto a cell and stays
     writes it once; a bounded interval says it is free ten steps later, when
     the entity is plainly still standing there. `Reservation::lastStep` is
     therefore optional, and unset means "until something else moves it".

   The read set is deliberately conservative — approximated as the cell that
   stopped each entity, rather than derived by instrumenting `rules::step` to
   report everything it consulted. That can reject concurrency which would have
   been safe; it cannot admit concurrency which is not.

   **4b — done.** `ActionScheduler` (`src/engine/ActionScheduler.hpp`) runs
   several actions at once: conflict-gated admission, per-action elapsed time,
   and commit-as-delta on completion. Built standalone and tested rather than
   wired in, because wiring it in *is* 4c — the presentation cannot cope with
   two actions yet.

   Also landed: the command queue is now bounded (two) and drops commands the
   world has outlived (one second). Both matter more than they did before step
   3, since a chained slide is long enough for a burst of mashing to spool out
   behind it.

   Decisions worth recording:

   - **The scheduler executes plans; it does not make them.** Choosing what to
     plan, and in what order, stays with the session. Admission priority —
     queued player commands ahead of ambient belt motion — therefore lives in
     `tryStartNextAction`, which already drains the queue before falling
     through to `rules::hasPendingMotion`. That ordering was already correct;
     it just now matters.
   - **A full queue drops the oldest, not the newest.** The most recent input
     is the one the player still means.
   - **`baseStep` rounds down.** An action starting part-way through a step gets
     claims that begin fractionally early, which can only make the conflict
     check stricter. Rounding the other way would let two actions slip past
     each other by a fraction of a step.
   - **Completion order is sorted, not incidental.** Effects are disjoint by
     construction so order should not matter, but a non-deterministic commit
     order would be impossible to reproduce from a bug report.

   **4c — in progress.** Presentation compositing, and wiring the scheduler
   into `GameplaySession`.

   `PresentationTests` is the suite that decides whether this step is right, and
   it runs headlessly like everything else.

   ### What actually blocks concurrency

   The presentation owns *world* state, not *per-action* state:

   - `beginAction` calls `syncToGameState(action.before)`, rebuilding every
     visual from one action's snapshot. A second action beginning mid-slide
     would snap the first action's moving entities back to where they started.
   - `trackedTimeline_`, `trackedTimelineSeconds_`, `reverseSourceStartSeconds_`
     and `trackedTimelineReversed_` are single-valued. Two actions need two.
   - `seekAction` clears `moving` on every visual before applying its timeline,
     so whichever action seeks last wins and the other appears frozen.
   - `beginAction` turns *every* player to `action.facingDirection`, including
     ones the action does not touch.

   ### The decomposition that makes it work

   Concurrent actions have disjoint entity sets. That is not an assumption but a
   consequence of 4a: an entity occupies cells, so two actions moving the same
   entity would claim the same cells and conflict. Every entity is therefore
   driven by at most one action at a time, which means the presentation can be
   split per entity rather than per world.

   1. **Structural sync separates from action start.** `syncToGameState` keeps
      its job of creating and removing visuals (mirror reflections appear,
      undo removes them) but is driven by the session's current state, not by
      an action's `before`. Note these are the same value today — `state_` is
      still `before` when `beginAction` runs — so this is behaviour-preserving
      for one action and correct for several.
   2. **Per-action timeline records.** Replace the four `trackedTimeline*`
      members with a small record keyed by action id, one per action in flight.
   3. **`seekAction` clears only its own targets**, taken from its timeline's
      motion tracks, instead of every visual.
   4. **Input facing applies to every player, and stays that way.** See the
      decision below; this item was originally the opposite and was wrong.

   ### Progress

   **Steps 1-3 are done**, all behaviour-preserving with a single action and
   verified against the full suite.

   - `beginAction` now takes the session's current state for structural sync.
   - Three of the four `trackedTimeline*` members turned out to be **written on
     every action and never read**. They are gone. The presentation's entire
     per-action state is now `reverseSourceStartSeconds_`, and since undo is the
     only reversed action and only runs when nothing else is in flight, one
     value suffices even under concurrency. The per-action record the plan
     called for is not needed.
   - `seekAction` clears `moving` only on the targets in its own timeline.

   **Step 4 (facing) is decided: every player instance faces the last movement
   input, including instances that input did not move.** The earlier note here
   had this backwards, calling the whole-player behaviour an obstacle to
   concurrency that should be narrowed. It is not incidental — it is the mirror
   mechanic. The copies are one character the player is controlling in several
   places, not several characters; they share one input, so they share one
   facing. A copy held against a wall while its siblings walk right still turns
   right, or the set stops reading as one body.

   It follows that facing is not per-action state and must not be scoped to the
   entities an action moves, even when actions overlap.

   `playerCopiesShareTheInputFacing` in `PresentationTests` pins it, covering
   both the copy that cannot move and the input that moves nobody at all. It was
   checked against the scoped-to-moved-players variant and fails three checks
   there, so unlike the rest of the suite it does discriminate.

   **What does need scoping is ambient facing**, which is a different thing that
   the old note conflated with this one. The `!playerInput` branch of
   `plans::worldStep` faces players from `firstPlayerMovementDirection` — a belt
   or slide turning a rider to face travel. That is not an input, and once
   actions overlap it would let an ambient action turn players another action is
   driving. Narrow that one; leave input facing global.

   ### Remaining

   **The scheduler is wired in.** `GameplaySession` no longer holds
   `activeAction_`/`activeActionLegs_`/`moveElapsed_`/`moving_`/`clockSeconds_`
   or its own `GameState`; it holds an `ActionScheduler`, which owns the
   authoritative state, every action in flight, their clocks, the shared step
   clock and the reservation table. The single-action accessors remain and
   resolve to the oldest action in flight, so `Application`, `RenderFrameBuilder`
   and both test suites are unchanged. Reservations are computed and checked on
   every action already; `tryStartNextAction` still refuses to start a second
   one, so behaviour is identical. Undo and restart now refuse while anything is
   in flight, per the decision above.

   Three things the sketch above did not anticipate:

   - **`advance` had to split into `advanceClock` and `commitFinished`.** The
     caller does work between them - the presentation is sampled at the new
     elapsed time before anything commits, and history bookkeeping hangs off the
     commit. `advance` remains as the composition of the two.
   - **Elapsed time must not be clamped to the duration.** How far an action ran
     past its end is what orders the commits; clamping erases it. Readers that
     want a sampling time clamp on the way out.
   - **Legs serve two masters and must not be shared.** Reservations need at
     least one leg or a plan claims nothing and conflicts with nothing - the
     leg-less-plan gap noted above. But the presentation reads legs to decide
     whether to animate a chain tile by tile, and handing a single-step action a
     synthetic leg sends it down the chain-aware path, which pairs entities
     positionally between legs and cannot survive an action that adds a player.
     Mirror activation does exactly that, and it crashes. `beginAction` therefore
     computes the claim from a local copy and stores the legs the caller meant.

   **`GameplayLoop::update` is restructured.** Starting an action is no longer
   gated on `!session.moving()`: the frame admits whatever it can, advances
   everything, and commits whatever finished. Two helpers do the per-action
   work - `startPresentation` installs a timeline by action id (the newest
   action is not the one the single-action setters target) and `seekAllInFlight`
   samples each action at its own elapsed time.

   The loop itself did not disappear, and the note above that said it would was
   too strong. What it no longer does is *sequence* - it does not have to finish
   one action before starting the next. What remains is **catch-up**: a frame
   longer than an action still has to run the world forward more than once, and
   it must stop at each completion rather than step over it, or an action
   commits late and whatever follows is planned against a state that never
   existed. Hence `timeToNextCompletion()`. A regression check confirms one
   0.35s frame lands identically to seven 0.05s frames.

   ### Remaining: admit a second action

   Deleting the `if (moving()) return false;` guard in `tryStartNextAction` is
   the whole change, but two things have to be settled with it:

   - **Rejected commands are currently swallowed.** `tryStartNextAction` pops a
     command off `pendingCommands_` before trying it. Under the guard a pop was
     always followed by an admission, so this never mattered; once admission can
     be refused by the reservation table, a conflicting command is consumed and
     lost. The decision at the top of this document is *queued, not rejected*,
     so it has to go back on the front of the queue and retry on a later frame -
     bearing in mind the staleness rule, which is what stops it retrying
     forever.
   - **Termination of the admit loop.** `GameplayLoop` now calls
     `tryStartNextAction` until it returns false. That terminates for the right
     reason and it is worth knowing why: authoritative state does not change
     until an action commits, so re-planning against it produces the same plan,
     whose claims collide with the copy already in flight. Ambient motion is
     self-limiting for the same reason. This is load-bearing - a planner that
     produced a *different* plan from an unchanged state would spin.

   **A bug in 4a had to be fixed before any of this could work, and it is the
   real reason step 3 was not simply a matter of deleting the guard.**

   `plans::reservationsFor` built a track for *every entity in `before`*, not
   for the ones the action involves. A bystander's resting cell was therefore
   write-claimed open-ended by an action that never touched it. Since every plan
   is computed from the same whole world, every plan claimed every stationary
   entity, so **no two plans could ever be admitted together** - the table would
   refuse a player step on the far side of the board from a sliding block.
   Deleting the guard would have changed nothing observable.

   Neither existing suite caught it. `ReservationTests` checked one plan's
   claims in isolation; `ActionSchedulerTests` hand-builds minimal claims rather
   than calling `reservationsFor`, so the two halves were never used together.
   `uninvolvedEntitiesAreNotClaimed` now pins it, and is the first test that
   exercises both.

   An entity another action depends on staying put is that action's *read* set
   to declare, not this one's to hold. That is what the read/write split was
   for, and the write side was quietly doing the read side's job.

   ### What is genuinely left

   With that fixed, a plan that moves only a block admits a player step beside
   it. But `plans::worldStep` is a whole-world planner: given the player's
   input it steps *everything*, so the plan for a player's move during a slide
   still re-plans the slide and collides with the copy in flight. The per-entity
   planners the Shape section calls for - `planMove`, `planPush`, `planSlide`,
   `planConveyorRide` - do not exist; only `worldStep` does. Splitting the
   whole-world planner into those is the remaining work, and it is what releases
   the player as soon as their own leg ends rather than at the end of the chain.

   **Same-step handoff is now representable, and that question is settled.**
   Claims number instants rather than step intervals: an entity leaving a cell
   during step `i` holds it through instant `i`, one arriving holds it from
   instant `i + 1`. A push hands the cell over within the step and both halves
   are admitted. Two further pieces were needed to make that sound:

   - `Traversal` claims (the boundary crossings) reject two entities swapping
     places, which occupancy-at-instants alone would allow - they share no
     instant yet pass through each other mid-step.
   - The inferred stopping read is limited to entities that actually carried
     slide momentum. `blockingCell` assumed anything that stopped was blocked,
     so a one-tile input step claimed a read on the cell beyond it - precisely
     the cell the block it had just pushed was moving into, which refused every
     push. Only something still travelling can be said to have been stopped.

   ### The remaining work, concretely

   `plans::worldStep` is still the only planner, and it plans the whole world:
   given the player's input it steps every entity. So a player move planned
   while a slide is in flight re-plans that slide and collides with the copy
   already running. Until it is split, deleting the one-at-a-time guard in
   `tryStartNextAction` changes nothing in play.

   The split wants `planMove`, `planPush`, `planSlide` and `planConveyorRide`,
   each a pure `(Level, GameState, params) -> std::optional<ActionPlan>` over
   the causal closure of the entities it actually moves - not over the world.
   `worldStep` then becomes a composition of them for the paths that still want
   whole-world semantics (save validation in `matchesForwardTransition` depends
   on replaying a chain, so it cannot simply be deleted).

   Two things to carry into that work:

   - **Deferred commands are still dropped.** `tryStartNextAction` pops a
     command before trying it, so once admission can be refused the command is
     lost. It needs to go back on the front of the queue; staleness retires it.
   - **The read set will need to come from the planners.** Momentum makes the
     inferred stopping read correct for the cases the game has today, but a
     per-entity planner knows exactly which cells it consulted, and declaring
     them is what removes the last of the guesswork. `GameplaySession` keeps its current single-action
   accessors, returning the oldest in-flight action, so `Application` and
   `RenderFrameBuilder` need no changes in the same pass.

   Shape:

   ```cpp
   struct InFlightAction {
       std::size_t id = 0;
       Action action;
       std::vector<GameState> legs;   // transient, for the presentation
       ActionReservations claims;
       float elapsedSeconds = 0.0f;
   };
   std::vector<InFlightAction> inFlight_;
   ```

   `moving()` becomes `!inFlight_.empty()`; `activeAction()` returns
   `inFlight_.front().action`, or a default when idle.

   Two things found while sizing this that are not obvious from the code:

   - **`GameplayLoop`'s loop gets simpler, not harder.** Its
     `while (remainingTime > 0.0f)` structure exists because actions are
     strictly sequential — it must finish one before it can start the next
     within a frame. Concurrent actions do not need that: admit whatever can be
     admitted, advance everything by `dt`, complete whatever finished. The
     nested time-slicing goes away.
   - **`playerMoveCount_` is safe, and it is worth knowing why.** Each plan
     carries `playerMoveCountBefore/After` computed at planning time, so two
     concurrent actions both incrementing it would clash. They cannot: only
     input-driven actions increment, and a second one would have to move the
     player, whose cells the first already claims. Ambient actions never
     increment. The invariant is enforced by the reservation table, not by
     anything in the counting code, so a comment there is warranted.

   `setActiveActionPresentation` and `setActiveActionDuration` currently target
   "the" action implicitly and will need an id, since `GameplayLoop` builds a
   timeline per action.

   Undo and restart must refuse while `!inFlight_.empty()`, per the decision
   above.

   Two gaps in 4a that only bite once the scheduler is wired in:

   - **`plans::reservationsFor` returns nothing when `legs` is empty.** Mirror
     activation, restart and undo all produce leg-less `ActionPlan`s. Undo and
     restart are gated to idle so they are fine, but a mirror activation is an
     ordinary concurrent action and would claim no cells at all, conflicting
     with nothing. It needs a one-leg `PlannedAction` wrapper before it can be
     scheduled.
   - **`collectTracks` walks `before`-sized ranges positionally into each leg**,
     so entities a leg *adds* — mirror clones — are never claimed.

   `RenderFrameBuilder` also reads `activeAction.before`/`.after` as whole
   states for the water-fill and mirror-preview visuals. Once "the" active
   action is merely the oldest of several, those two read a world that no
   longer exists. Cosmetic, but it will look wrong.
5. Conveyors as per-step actions.

Steps 1 and 2 are the risky, wide-reaching ones and produce no visible change;
steps 3–5 are where behaviour moves. Keeping that split means most of the churn
is verifiable by existing tests.

## Testing

Run `tools/build_headless_tests.sh` on Linux, or the normal CMake/CTest build on
Windows. The script builds `sokoban_core` and `sokoban_ui` directly from the
source lists in `CMakeLists.txt` and runs 41 of the 43 suites; only the two that
include `<vulkan/vulkan.h>` need the Windows build.

Still to write:

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
