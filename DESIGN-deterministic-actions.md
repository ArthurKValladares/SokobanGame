# Deterministic Actions — Design

**Status.** Live. Actions are planned as pure functions over a causal closure,
carry space-time reservations, and run through a scheduler holding several at
once. `GameplaySession` plans input moves with `planPlayerStep` and the slide
they set off as a deferred consequence in the same causal group; ambient motion
is `planSlides` / `planConveyorRides`. The one-at-a-time guard is gone, refused
commands are requeued, and undo folds a consequence into its cause.

Concurrency is therefore real: a player released after their own tile can step
again while a block they pushed is still travelling. All 33 headless suites
runnable on Linux pass; the 8 SDL-dependent and 2 Vulkan suites need their
respective builds.

This document records the decisions, the reasoning behind them, and what is
still open.

## The guarantee

> Every action's outcome is computed in full at the moment the action starts,
> and nothing that happens during the action can change it.

Two things follow that are worth stating separately, because they are often
conflated:

1. **Determinism** — the outcome is a pure function of the state at start.
2. **Stability** — the outcome survives everything else that runs concurrently
   while the action plays out.

Determinism was there from the beginning: an action holds a complete `before`
and `after`, `after` is produced when the action begins, and live state stays at
`before` until the presentation finishes.

Stability was missing, and only for motion spanning more than one world step. An
ice slide used to be momentum re-evaluated every step, so its destination was
recomputed as it travelled and a second push into its path changed where it
landed. Slides now resolve as one committed chain, so that is fixed.

## Why this needs a scheduler at all

Everything originally ran on one global lockstep timeline: one action at a time,
and within a step the player, slide momentum and conveyors all advanced
together. On that model stability is free — plan the whole slide up front as one
long action and queue player input during it.

The reason not to stop there is feel, not correctness: the player is locked out
for the length of a long slide. The entire reservation system exists to buy
**responsiveness**, and it is worth re-reading that sentence before continuing,
because if the simple version turns out to feel fine, none of the rest is
needed.

## Decisions

| Question | Decision |
| --- | --- |
| Conveyor motion | Per-step actions, re-reserved each step |
| Conflicting input | Queued, not rejected |
| Undo | Only permitted when nothing is in flight |
| Undo granularity | One player action, whatever it set off |
| Causal chains | Committed in full at the first action |
| Conflict detection | Space-time read/write sets |
| Cell handoff within a step | Allowed; claims number instants, not intervals |
| Player facing | Every instance faces the last input, moved or not |
| Red rejection glow | Dropped for now |

### Chains and reservations interact in a way that matters

Full chain commitment is a choice about the **player's** mental model — "I pushed
this, so I know everything that follows" — not a correctness requirement. A
chained slide planned as a fresh action when the previous one completes would
still satisfy the guarantee.

Having chosen it, the space-time model stops being a nicety and becomes load
bearing. A chain can span most of the board but occupies each cell only briefly.
With whole-duration dirty tiles, committing a full chain would lock the player
out for as long as the chain runs. With space-time intervals the player is
released as soon as their own step's interval ends, and is blocked only from
cells the chain has not reached yet.

**The two decisions only work together. Do not implement full chain commitment
on top of whole-duration locking.**

## Conflict rule

Each plan carries two sets of `(cell, interval)` entries plus its boundary
crossings:

- **write set** — cells the action occupies, and when.
- **read set** — cells whose contents the precomputed outcome depended on.
- **moves** — the cell boundaries it crosses, and during which step.

Two plans conflict iff one's writes intersect the other's reads or writes over
overlapping intervals, or their moves cross head-on in the same step.

A write-only check is not sufficient. If the player walks onto a slide's
destination and stands there, the player writes that cell only during their own
step; by the time the block arrives, a write-write test sees nothing. The read
set catches it, because the slide's outcome depended on that cell being empty at
arrival time.

### Claims number instants, not intervals

Step `i` is the *moment* step `i` begins. A claim covers the instants at which
the entity is standing in the cell: an entity leaving during step `i` holds its
origin through instant `i`, and one arriving holds its destination from instant
`i + 1`.

This is what lets a push work — the block vacates the pushed-from cell during
the same step the player enters it, so the two never share an instant and both
are admitted. Claiming both ends of a move for the whole step instead makes
every handoff look like a collision, which is neither true to what happens nor
extensible to the other mechanics that hand cells over.

Two consequences:

- **A resting cell is claimed open-ended.** An entity that steps onto a cell and
  stays writes it once, but is still standing there ten steps later.
  `Reservation::lastStep` is therefore optional; unset means "until something
  else moves it".
- **Instants alone would let two entities swap places.** Each ends where the
  other began, so they share no instant, yet they cross mid-step and pass
  through one another. `Traversal` records the crossings and is compared
  separately.

### The read set is approximate

It is derived from states rather than by instrumenting the rules to report every
cell they consulted. That can reject concurrency which would have been safe; it
cannot admit concurrency which is not.

One refinement is load-bearing: the inferred *stopping* read applies only to
entities that carried slide momentum. Anything that stopped was assumed to have
been blocked, so a one-tile input step claimed a read on the cell beyond it —
exactly the cell the block it had just pushed was moving into, which refused
every push. Only something still travelling can be said to have been stopped.

## Conveyors

Belt riders plan one step at a time and re-reserve each step, so their locks are
one cell and one interval long. This keeps ambient motion from holding
long-lived reservations, which would make the area around any belt permanently
unusable and stop two riders following each other down the same belt.

`rules::hasPendingMotion` identifies when the world should keep stepping; that
is the natural trigger for scheduling the next belt action.

### Starvation

Because a belt rider releases its reservation at the end of each step and
immediately takes another, a queued player action waiting on a cell in the
belt's path can be shut out indefinitely.

**At each scheduling point, admit queued actions before starting new ambient
actions.** `tryStartNextAction` already drains the command queue before falling
through to ambient motion, so the ordering is right; it just now matters.

## Queueing

A queued command is planned when it dequeues, not when it is entered, so it is
deterministic under the guarantee — its outcome is known the instant it starts.

Both policies are in place: the queue is bounded at two (a full queue drops the
oldest, since the most recent input is the one the player still means), and
commands older than one second are dropped rather than played back. Both matter
more since slides became chains, which are long enough for a burst of mashing to
spool out behind them. The exact numbers are tuning, best revisited against a
playable build.

## Undo

Permitted only when nothing is in flight. This keeps the undo stack a linear
sequence of invertible whole-world transitions and sidesteps the fact that
overlapping actions make history a DAG. `queueUndo` and `queueRestart` refuse
while the scheduler is non-empty.

## Chain termination

Full chain commitment means planning must provably terminate. Three hazards:

- **Cycles.** A block sliding onto a belt that carries it back onto ice can
  loop. `plans::maxChainedSteps` caps it; a visited set over
  `(entity, cell, direction)` would be tighter.
- **Handoff to ambient motion.** A chain that runs onto a belt *ends* there and
  hands off rather than planning an unbounded ride. `plans::anySlideMomentum` is
  what distinguishes a slider from a rider; `rules::hasPendingMotion` reports
  both and is the wrong test for chaining.
- **Growth.** The involved-entity set is the causal closure, not just what the
  player touched. Planning accumulates it as it goes.

## Shape

- **Planners** live in `plans::` and are pure functions of their arguments, in
  the same spirit as `rules::`. `planPlayerStep`, `planSlide` and
  `planConveyorRide` plan over a causal closure; `worldStep` plans the whole
  board and is kept for save validation. All four are one core function
  differing only in scope and whether they chain. Plus `fromMirrorPreview`,
  `restart` and `inverted`.
- **`rules::StepScope`** is the seam the planners are built on: which entities a
  step may move. Out of scope means scenery — blocks and supports as ever, but
  forms no intent and is never written.
- **`StateDelta`** (`StateDelta.hpp`) is what an action commits — per-entity
  changes keyed by id, never a whole `GameState`.
- **`Reservation` / `ActionReservations` / `ReservationTable`**
  (`Reservation.hpp`) hold the claims and answer `conflict`.
- **`ActionScheduler`** (`ActionScheduler.hpp`) owns authoritative state, the
  actions in flight, their per-action clocks, the shared step clock and the
  table. It executes plans; it does not make them.
- **`GameplaySession`** decides what to plan and in what order, and owns
  history, the undo stack and the running move total.

`ActionPlan` deliberately carries no `involved` or `effect` field. `Snapshot` —
including its `undoStack` of actions — is serialised into save files, so extra
fields would either bloat every save or default on load and make a restored
action compare unequal to a live one. The delta stays derived at completion, and
reservations are computed at admission.

## What is built

Each step below was behaviour-preserving unless noted.

**Verification.** All 41 headless suites pass against the tree as it stands,
freshly linked. The caveat that used to sit here — 31 suites never relinked
after the step 4 change — has been discharged.

**Two ways the test script will lie to you**, both worth knowing before you
trust a green run:

- **A failed relink does not fail the run.** `LINK FAIL` is printed, the script
  `rm -f`s the binary, and if *that* also fails the stale binary is executed and
  reports PASS. An output directory owned by another user produces exactly this:
  31 suites "passing" from binaries built against different source.
- **An interrupted compile leaves a zero-byte `.o`** that is newer than its
  source, so `needsRebuild` accepts it and `ar` archives it. The failure surfaces
  as undefined symbols in unrelated suites. `nm` on every object catches it.

The script also exits 1 on complete success: the last statement is
`[ -n "$skipped" ] && echo ...`, which is false when nothing was skipped.

1. **`StateDelta` and delta application.** Completing an action writes only what
   that action changed; undo runs the same machinery backwards.
2. **`ActionPlan` and pure `plans::` functions.** `GameplaySession::Action` is an
   alias for `ActionPlan`. Move counts are left at zero by planners — only the
   session knows the running total.
3. **Chain resolution in `worldStep`.** The first step applies input, then the
   world keeps stepping while anything carries slide momentum, and the run
   becomes one action. Slides are stable. Consequences, all intended: the player
   is locked out for the whole chain, and one undo reverses the entire slide.
   `matchesForwardTransition` replays the chain to validate a save and still
   accepts a single step, so older saves load unchanged. Motion is staggered per
   leg so a block that only moves on the fourth step does not set off
   immediately.
4. **Reservations, scheduler, presentation, and the wiring.**
   - `Reservation`, `ActionReservations`, `ReservationTable`,
     `plans::reservationsFor`.
   - `ActionScheduler`: conflict-gated admission, per-action elapsed time,
     commit-as-delta on completion, deterministic commit order.
   - Presentation made per-entity rather than per-world: structural sync is
     driven by the session's current state rather than an action's `before`, and
     `seekAction` clears only the targets in its own timeline. Three of the four
     `trackedTimeline*` members turned out to be written every action and never
     read, and are gone.
   - `GameplaySession` holds an `ActionScheduler` instead of its own state and
     single-action fields. The single-action accessors remain and resolve to the
     oldest action in flight, so `Application` and `RenderFrameBuilder` needed no
     changes.
   - `GameplayLoop::update` no longer gates starting an action on
     `!session.moving()`: it admits what it can, advances everything, commits
     what finished.
5. **Scoped stepping and the per-entity planners.** `rules::StepScope` and
   `rules::scopedStep`; `plans::planPlayerStep`, `planSlide`,
   `planConveyorRide`, `slidingEntities`, `conveyorRiders`. Behaviour-preserving:
   `rules::step` and `plans::worldStep` are the same functions with an empty
   scope, and every suite passes unchanged. Pinned by seven new cases in `rules`
   and six in `action_plan`, including the composition property — a player step
   followed by the slides it leaves behind lands everything exactly where the
   whole-world step would have.

### Things learned along the way that are not obvious from the code

- **`advance` had to split into `advanceClock` and `commitFinished`.** The
  caller works between them: the presentation is sampled at the new elapsed time
  before anything commits, and history bookkeeping hangs off the commit.
- **Elapsed time must not be clamped to the duration.** How far an action ran
  past its end is what orders the commits. Readers clamp on the way out.
- **Legs serve two masters and must not be shared.** Reservations need at least
  one leg or a plan claims nothing and conflicts with nothing. But the
  presentation reads legs to decide whether to animate a chain tile by tile, and
  a synthetic leg on a single-step action sends it down the chain-aware path,
  which pairs entities positionally and cannot survive an action that *adds* a
  player — mirror activation does exactly that. `beginAction` computes the claim
  from a local copy and stores the legs the caller meant.
- **The loop in `GameplayLoop::update` did not disappear.** It no longer
  *sequences* — it need not finish one action before starting the next. What
  remains is catch-up: a frame longer than an action must run the world forward
  more than once, stopping at each completion rather than stepping over it, or an
  action commits late and whatever follows is planned from a state that never
  existed. Hence `timeToNextCompletion()`.
- **`reservationsFor` originally claimed every entity in the state**, not the
  ones the action involves, so every plan write-claimed every bystander's
  resting cell and no two plans could ever be concurrent. An entity another
  action depends on staying put is that action's *read* set to declare.
- **Player facing is global by design.** Every player instance faces the last
  movement input, including instances it did not move. Mirror copies are one
  character in several places; they share one input, so they share one facing.
  This must not be scoped to the entities an action moves, even under
  concurrency. Ambient facing — a belt or slide turning a rider, the
  `!playerInput` branch of `worldStep` — is a different thing and *does* need
  scoping, since it is not an input and would fight another action.
- **`playerMoveCount_` is safe under concurrency, and it is worth knowing why.**
  Only input-driven actions increment it, and a second one would have to move
  the player, whose cells the first already claims. The invariant is enforced by
  the reservation table, not by anything in the counting code.

## What is left

### 1. Split the whole-world planner — **done**

The split turned out to belong one level lower than this section assumed. The
obstacle was not `plans::worldStep` but `rules::step` underneath it: every
entity derives an intent inside `MicroStepResolver`, so there was no way to
express "resolve a step in which only this player acts". Building the planners
on top of the old `rules::step` would have meant a second copy of collision
arbitration, pushing, falls and attacks, free to drift from the first.

So the seam is `rules::scopedStep`, taking a `StepScope` — the entities allowed
to act. Everything else is scenery: still blocking, supporting, occluding and
stopping slides, but deriving no intent and never written. `rules::step` is
exactly `scopedStep` with an empty scope and must stay that way, because
`matchesForwardTransition` validates every save on disk by replaying it through
the whole-world form.

**The scope grows during resolution.** The causal closure is not knowable
beforehand — whether a move turns out to be a push, and what that push sets off,
is decided while resolving it. Seed an action with one player and it may write
three entities: the player pushes a block, the block shoves an enemy, the enemy
kills a bystander who was never named.

**Scoping enemy attacks is the subtle part.** `resolveEnemyAttacks` swept every
player unconditionally. Adjacency is a *standing* fact about the board, so under
a scope that sweep has any action anywhere kill a player who was already next to
an enemy before it began — writing an entity far outside the closure and
attributing the death to the wrong action. An action is answerable for a death
only when it caused the adjacency: it moved the player, or it shoved the enemy
into place. The second case pulls the victim into the closure.

The planners are `planPlayerStep`, `planSlide` and `planConveyorRide` — the same
core with a different scope and a different answer to whether it chains.
`worldStep` is that core with an empty scope. There is no `planPush`: whether a
move is a push is for the rules to decide, not the caller, and a separate entry
point could only disagree with them. Players plan as one action rather than one
each, because mirror copies are one character sharing one input.

#### Why the player's step does not chain

This corrects [Chains and reservations](#chains-and-reservations-interact-in-a-way-that-matters)
above, and it is why the planners are carved up the way they are.

Committing a player's push and the slide it starts as *one* action does not in
fact release the player early. The reason is in `collectTracks`: an entity's
final cell is claimed **open-ended**, because it is still standing there long
after the action ends. So the player's own resting cell stays claimed for the
whole chain, and their next move — which has to write the cell they are standing
on — conflicts with it. Space-time intervals release the player from the cells
the *block* travels through, but never from their own.

So `planPlayerStep` is one step and the slide is a second plan. The guarantee is
untouched: both plans are made from the same instant, so the slide's destination
is still settled at the moment of the push, which is all determinism and
stability actually require. What is given up is packaging — one undo reverses
the push and the slide separately rather than together.

Once wired, per-entity planners also know exactly which cells they consulted, so
the read set can be declared rather than inferred — removing the last of the
guesswork described above.

### 2. Wire the planners through — **done**

`tryStartPlayerStep` plans the step and the slide it sets off as one batch,
admitted through `ActionScheduler::tryStartAll` — all or none, because taking
the push and finding the slide refused would leave a block with momentum and no
action to spend it, breaking the promise the push made. `tryStartAmbientMotion`
plans sliders and riders.

Four things this needed that the section above did not anticipate:

- **Planning per entity is not enough; it has to be per entity *set*.** Two
  entities sliding from the same instant are outside each other's scope, so each
  treats the other as scenery standing where it started — and a third cell both
  cross on the same step looks free to both. Hence `planSlides` /
  `planConveyorRides` over the whole set, handing arbitration back to
  `MicroStepResolver`. Riders especially: planned separately, a follower sees the
  leader as scenery in the cell it is about to leave and never moves, so a queue
  on a belt would not advance.
- **A consequence always collides with its own cause.** The cause claims its
  entities' final cells open-ended; the consequence is exactly what moves them
  out again. `ReservationTable::conflict` takes an `exemptGroup`. Only the group
  is exempt — a third action still sees the cause's claims.
- **Nothing may plan for an entity already in flight.** Authoritative state does
  not change until an action commits, so a second plan made from it is the same
  motion again and collides with the copy running. `withoutEntitiesInFlight`
  filters both ambient motion *and* a player step's consequences — a block still
  sliding is visible in the state that step produces and would otherwise be
  handed a second slide.
- **A deferred action must not be seeked.** It is admitted and holds its claims,
  but its entities are still being driven by its cause. `elapsedSeconds` counts
  up from negative; `startPresentation` and `seekAllInFlight` skip it until zero,
  or it would snap the block to where the push is going to leave it and cut the
  push's animation short.

#### Undo is grouped by cause — **built, not yet exercised**

Undo is the player's idea of what happened, not the scheduler's. One input
happened, so one undo puts back everything that followed from it, however many
actions the scheduler used to do it.

Splitting a push from its slide is therefore a scheduling decision only.
`ActionScheduler::InFlight` carries a `causalGroup`; a consequence is started
with the group of the action that caused it, and on commit it **folds into**
that group's undo entry instead of pushing its own — endpoints composed,
presentation timelines concatenated at an offset.

`undoGroups_` runs parallel to `undoHistory_` so a consequence folds into its
own cause rather than into whatever committed most recently — ambient motion can
interleave between the two.

**The save format is untouched** — no format 18, and the reason `ActionPlan`
carries no extra fields still holds. `causalGroup` lives on `InFlight`, which is
transient; by the time a session is saved every action has committed and every
group is closed.

#### What this section originally claimed, and why it was wrong

It said a folded entry is exactly the whole-world transition `worldStep` would
have produced, so `matchesForwardTransition` validates it unchanged. The format
survived; the validation did not. Two separate breakages:

- **The chain.** `restore` requires entry *k*'s `before` to equal entry *k−1*'s
  `after`. Two actions planned from the same instant both record `before = S`
  and each `after` holds only its own change, so the chain breaks at the first
  overlap. Folding compounds it: composing into an earlier entry moves an
  endpoint that anything appended in between was chained to.
- **The replay.** An entry is now a *partial* transition. Replaying the whole
  world steps bystanders the action never touched and lands elsewhere.

The fix is two-sided, and both halves are in:

- An undo entry stores what its action **changed**, replayed onto the running
  chain — `recordCompletion` derives a `StateDelta` and applies it to the
  previous entry's `after`, and a fold rebases the short tail behind it
  (`rebaseUndoFrom`). The chain then holds by construction, whatever order
  things committed in. Move counts move by the same reasoning: `playerMoveCount_`
  advances by an action's *delta*, never to the total the plan predicted, or an
  ambient action planned alongside a player's step would drag the count back
  down on commit.
- `matchesForwardTransition` tries the whole world first — unchanged, so every
  save written before the split validates exactly as it did — and then a scoped
  replay, deriving the changed-entity set from the entry and running it through
  `rules::scopedStep`.

The scoped branch is **weaker validation**, and that is the price of
concurrency: replaying one entity under a scope naming only itself is close to
asking whether it could have moved at all. What still holds it down is the chain
check around it, which pins every entry to the one before it and the last to the
saved state.

Pinned by `concurrentPlayHistoryRoundTrips`, which is the case the parallel
group vector exists for: a push whose slide commits after an unrelated step,
folding into an entry that something else was already chained to.

### 3. Requeue deferred commands — **done**

`tryStartNextAction` pops a command off the queue before trying it. Under the
one-at-a-time guard a pop was always followed by an admission; once admission
can be refused by the reservation table, a conflicting command is consumed and
lost. The decision is *queued, not rejected*, so it goes back on the front of
the queue and retries on a later frame — staleness is what stops it retrying
forever.

The bool the try-helpers returned could not express this, since it conflated
"the table said no" with "there was nothing to do". `StartOutcome` splits them:
`Refused` is requeued, `Impossible` is dropped. Without that split a player
walking into a wall would retry the same doomed command until it went stale,
holding up everything behind it.

Note that the admit loop in `GameplayLoop` terminates for a reason worth
preserving: authoritative state does not change until an action commits, so
re-planning against it produces the same plan, whose claims collide with the
copy in flight. A planner that produced a *different* plan from an unchanged
state would spin.

### 4. Conveyors as per-step actions — **done**

`tryStartAmbientMotion` plans every free rider as one never-chaining action per
step, after sliders, and after queued commands — the starvation ordering.

**Untested.** A rock can only reach a belt by being pushed there, so a belt
scenario cannot be authored directly in a level fixture the way an ice slide
can, and no test yet covers two riders following each other down one belt. That
is the case `planConveyorRides` exists for and it is the first test to write.

### 5. Smaller known gaps — **all four closed**

- **Animations were built from the first leg only**, so an event in a later leg —
  a player killed part-way through a slide — had correct motion and no clip at
  all. Each leg is now resolved as its own transaction and the results laid end
  to end by `concatenateTimelines`, which `GameplaySession` was already using to
  fold an undo entry and which now lives in `PresentationTransactionBuilder`.
  `playerPushing` stays confined to leg zero: a push is something input does,
  and carrying the flag through would play the push animation for the whole
  length of the slide.
- **`collectTracks` walked `before`-sized ranges positionally into each leg**, so
  entities a leg *adds* — mirror clones — claimed nothing at all. That was a
  silent hole rather than a visible refusal: the clone's destination was left
  free for anything else to walk into. Tracks now run past the end of `before`
  and carry a `firstInstant`, so a clone claims its cell from the instant it
  appears and not before. Pinned by `entitiesAnActionAddsAreClaimed`.
- **`RenderFrameBuilder` read `activeAction.before`/`.after` as whole states.**
  Replaced by `GameplaySession::projectedState()` — the live state with every
  in-flight delta applied, which is the honest version of the same idea. No
  single action's `after` answers "where is this going" any more: each is a
  snapshot taken when it started and blind to the others.
- **Checkpointing was gated on `!moving()`.** Dropped. The gate assumed a
  snapshot taken mid-action would catch the world half-way through a
  transition; it does not, because a snapshot holds the *committed* state and
  the stack chained to it, and an action in flight has contributed to neither.
  What a reload loses is the action, not the consistency — the state it was
  planned from is on disk with its momentum, so ambient motion plans it again.
  The one cost is granularity: a slide whose push had already committed
  reappears as its own undo entry rather than folded into that push. Pinned by
  `snapshotMidFlightRestoresFromTheCommittedState`.

## Testing

Run `tools/build_headless_tests.sh` on Linux, or the normal CMake/CTest build on
Windows. The script builds `sokoban_core` and `sokoban_ui` directly from the
source lists in `CMakeLists.txt` and runs 41 of the 43 suites; only the two that
include `<vulkan/vulkan.h>` need the Windows build. It is incremental, and
rebuilds on header changes via a whole-tree header timestamp — the precise
`.d`-file version was tried and silently did nothing, because the checkout path
contains a space and `.d` escaping defeats naive whitespace splitting. That
failure mode produces objects built against different layouts of the same struct
and crashes that read as logic bugs, so it is worth not reintroducing.

Suites that pin this design: `state_delta`, `action_plan`, `reservation`,
`action_scheduler`, `gameplay_session`, `gameplay_loop`, `presentation`.

Pinning the wiring, all in `gameplay_session`:

- `iceSlideIsSettledAtTheMomentOfThePush` — two actions in one causal group from
  one instant, the slide's destination already final, the slide deferred, and
  one undo taking back both.
- `playerMovesAlongsideASlideItCannotDisturb` — the point of the whole thing: a
  step admitted while a slide is still travelling.
- `commandRefusedByAClaimIsRequeuedNotLost` — a step into the block's path waits
  and then runs, rather than vanishing.
- `concurrentPlayHistoryRoundTrips` — a fold into an entry something else was
  chained to, the rebase that follows, and the round trip through `restore`.

The guarantee itself is pinned by `outcomeSurvivesAnyChangeOutsideTheReadSet`
in `action_plan`: it plans a slide, then re-plans it from every state reachable
by moving the player onto a cell the plan neither reads nor writes, and requires
the block to land in the same place by the same route. If that ever fails,
either the read set is understated or a planner is consulting something it never
declared — and the table would then be admitting concurrency that can change a
committed outcome. Note the asymmetry: it says nothing about the read set being
*tight*. An overstated one costs responsiveness, never correctness.

### Belt fixtures

A movable's start tile (`R`, `I`) resolves to Air, so "a rock standing on a
conveyor" cannot be authored — the cell is either the rock or the belt, never
both. That blocked every belt test for a while. Two ways round it, and which one
to use depends on the layer being tested:

- **`placeMovables`** in `action_plan` moves authored movables onto the cells a
  test needs, giving the state a push would have produced without the push.
  `beltWithTwoRiders()` is built on it. Planner-level tests want this: the setup
  would otherwise be longer than the test and would drag the push's own
  mechanics into a test about belts.
- **Push a rock on for real** in `gameplay_session`. Hand-placed state cannot be
  injected into a session — `restore` validates the snapshot against a replay
  from the opening state and rejects anything that does not chain — so a
  session-level test has to reach the belt by playing.

Belt coverage now:

- `beltRidersFollowEachOtherDownOneBelt` (`action_plan`) — both riders advance in
  one scoped step, *and* the single-entity planner refuses the follower, which is
  the bug the set-based planner exists to avoid.
- `beltCarriesARiderOneActionPerStep` (`gameplay_session`) — a ride is one action
  per step, never chained, one undo entry each.
- `queuedCommandsGoAheadOfAmbientMotion` (`gameplay_session`) — the starvation
  ordering: with a rider owed a step and a command queued, the player's action is
  the one admitted.

One thing that surfaced writing these: `rules::hasPendingMotion` reports that an
entity is *on* a conveyor, not that the conveyor can move it, so a rider pinned
against a wall keeps it true forever. The scheduling loop terminates anyway,
because a ride that changes nothing is no plan at all. Worth knowing before
trusting `hasPendingMotion` as a "world is settled" test — it is not one.

### Golden traces

`goldenTraceIsReproducible` plays a fixed input script and compares the whole
committed `ActionPlan` sequence against a second run of the same script, rather
than against literals baked into the test.

The literal version was the original plan and is worse. A hard-coded expectation
pins the ordering of a hundred fields nobody reads and breaks on every unrelated
change. What actually needs guarding is that nothing in the scheduler has become
dependent on anything beyond the level, the state and the input — and
concurrency is exactly where that creeps in, through commit order, clock
rounding, or iteration over the reservation table. None of it shows up in the
final state alone, which is why the comparison is over the plan sequence. The
test carries its own non-vacuity guards, and finishes by feeding the trace to
`restore`, since a trace is only worth recording if it is also a valid history.

### Termination, and why the cap cannot be provoked

`chainPlanningStopsAtTheCap` reaches `plans::maxChainedSteps` the only way it
can: a corridor longer than the cap.

The adversarial ice/belt loop this section used to ask for **does not appear to
be constructible**. A slide travels in a straight line and momentum never
changes direction; a belt cannot bend it, because momentum overrides the belt
and `conveyorRiders` excludes anything still sliding; a fall cancels momentum
outright. So there is no arrangement of the current mechanics that turns a chain
back on itself. The cap stays as a guard against mechanics that do not exist
yet — a teleport, a bumper, anything that redirects — and the test pins that it
engages cleanly rather than that a cycle is caught.

Hitting it is safe, and that is the more useful half of the test: a capped plan
leaves the block still carrying momentum, and momentum is what ambient motion
schedules on, so the remainder is planned as a second action rather than lost.

Still to write: nothing outstanding from this design. Remaining coverage gaps
are the ordinary kind — the 8 SDL and 2 Vulkan suites cannot run on Linux, and
`Application.cpp` is excluded from the headless build entirely, so its
`projectedState` call site and the checkpoint gating need a Windows build.

- **Golden traces**: a recorded input sequence produces an exact `ActionPlan`
  sequence. `ActionPlan` has `operator==` and `GameState` is cheap to compare,
  so this is close to free.
- **Stability property**: for any plan, replaying it against a state mutated
  only outside its read set yields the identical outcome. This is the guarantee
  stated as an executable test, and it is the single most valuable test here.
- **Termination**: adversarial ice/belt loops must hit the cap rather than hang.
- **Starvation**: a queued player action adjacent to a running belt must be
  admitted within a bounded number of steps.

## Entity ownership — the rule the reservations could not express

**An action owns the entities it will move, until it lands. Nothing else may
plan for them.**

Checked in `ActionScheduler::ownershipConflict` against the delta between a
plan's endpoints, before the reservation table is consulted at all. A causal
group is exempt from itself, since a consequence exists precisely to take over
the entities its cause was moving.

This is the guarantee stated directly, and it should probably have been the
first thing built rather than the last. The elaborate machinery below is about
*cells*; the property that actually matters is about *entities*, and no amount
of space-time reasoning adds up to it.

### Why the reservation table could not catch it

Push a block onto ice, let the push commit, then walk into the block again while
its slide is still running. The second push was admitted, and both halves of the
system were right on their own terms:

- The **table** reasons about cells at instants. By the step the second push was
  tried, it believed the block had left that cell two steps ago — which its own
  claims say, correctly.
- The **planner** reads authoritative state, which does not advance until an
  action commits. The slide had not committed, so the block was still sitting
  on the cell, and a push for it was a perfectly sound plan.

Neither could see the contradiction, because neither was wrong. What was missing
was the simple statement that the block was already spoken for.

Pinned by `anEntityInFlightCannotBeTakenBySomethingElse`.

### Is the elaborate half still earning its keep?

`ActionScheduler::AdmissionStats` counts admissions, ownership refusals and
reservation refusals, exposed through `GameplaySession::admissionStats()`.

With ownership in place, the only thing space-time intervals still buy is
letting the player cross the *path* of a sliding block — cells it will reach but
has not yet. If `refusedByReservation` stays near zero in real play while
ownership carries every refusal, then `Reservation`'s step ranges, `Traversal`,
the instants-versus-intervals reasoning, the inferred read set and the causal
group exemption are all dead weight and should be collapsed into "an action
locks the cells it touches for as long as it runs".

That is the measurement this section exists to prompt. It has not been taken
yet, and until it has, the machinery below stays.

## Superseded: an action can steal an entity another action already owns

Reproduced deterministically: push a block onto ice, wait for the push to
commit, then walk into the block again while its slide is still running. A
second action is planned that moves the same block, and it is admitted.

```
f26 n=1 block=4.000 | [id2 grp1 el=0.300 dur=0.750 b2->a7]
f27 n=2 block=2.111 | [id2 grp1 ...] [id3 grp30 el=0.017 b2->a3]   <== snaps back
```

`withoutEntitiesInFlight` was supposed to prevent exactly this, and it does not,
because it only filters the entities a planner is *seeded* with. The scope grows
during resolution — that is the whole point of the causal closure — so
`planPlayerStep` seeded with only the player still pulls the block in as a push,
and nothing consults the in-flight set again.

The reservation table does not catch it either, and its reasoning is sound in
isolation: the slide claims the block's cell only at the instant it passes
through, and by the step the new push is admitted at, the block is claimed to
be four tiles further on. The flaw is that **authoritative state has not caught
up** — the slide has not committed, so `planPlayerStep` planned against a state
where the block is still on the cell it left long ago, and produced a plan whose
claims are honest about a world that no longer matches the plan.

Two candidate fixes, neither obviously right:

- **Give `rules::scopedStep` a set of entities it may not write**, so the
  closure cannot grow into one. The push would then fail to resolve and the
  command would be refused and requeued, which is the existing vocabulary. The
  cost is that the rules gain a concept that is really about scheduling.
- **Plan against the projected state rather than the committed one** — the
  world once everything in flight has committed, which `projectedState()`
  already computes for rendering. That matches what the reservation table
  already assumes, and would have planned the push against a board where the
  block has gone. The cost is that a plan's `before` would no longer be a state
  the world was ever in, which the undo rebase and `matchesForwardTransition`
  both currently rely on.

The second is more principled and considerably more invasive. Worth deciding
before any further concurrency work, because everything else assumes an action
owns its entities outright.

## Open risks

- **Concurrency may be rarer than it looks.** Full chain commitment reserves a
  lot. Worth instrumenting how often a queued action actually waits, on real
  levels, before investing further in permissiveness.
- **Replay determinism is a free byproduct** if the action log is designed for
  it, and it makes debugging concurrency far easier. Worth keeping in view.
- **The red glow was dropped, not resolved.** If playtesting shows queued
  actions read as dropped input, some quiet indicator will be needed.
