# Deterministic Actions — Design

**Status.** Live and playable. Actions are planned as pure functions over a
causal closure, run through a scheduler that holds several at once, and are
gated by entity ownership. The **claim rule** below is implemented, and the read
set, `Traversal` and the instants-versus-intervals reasoning are gone with it.
All 41 suites buildable on Linux pass — the 33 SDL-free ones and, with a
vendored SDL build, the 8 that need it, including `player_profile`.

One thing is outstanding: the `Application.cpp` changes have never been compiled,
because it is excluded from the headless build. The two Vulkan suites are in the
same position. See [What is left](#what-is-left).

---

## The guarantee

> Every action's outcome is computed in full at the moment the action starts,
> and nothing that happens during the action can change it.

Two things follow, worth stating separately because they are often conflated:

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

## What the concurrency is for

This is the part to read before touching anything else, because it bounds how
clever the rest is allowed to be.

Everything could run on one global lockstep timeline: one action at a time, the
player locked out for the length of a long slide. On that model stability is
free. The reason not to stop there is **feel**, not correctness.

**Exactly two kinds of concurrency are wanted:**

1. Actions with nothing to do with each other — the player interacting with
   something else entirely, elsewhere on the board.
2. The player entering a cell a sliding block **has already passed**.

**Stopping the player from touching anything an ongoing action has reserved is
fine** — including cells the block will reach but has not yet. That is a
deliberate choice, not a compromise, and it is what keeps the machinery small.

Anything more permissive than this is not worth complexity. If a future change
makes the system harder to reason about in exchange for admitting more, it is
the wrong trade.

## The two rules

Admission is decided by two checks, in this order.

### 1. Entity ownership — implemented

> **An action owns the entities it will move, until it lands. Nothing else may
> plan for them.**

`ActionScheduler::ownershipConflict`, checked against the delta between a plan's
endpoints, before the reservation table is consulted at all. A causal group is
exempt from itself, since a consequence exists precisely to take over the
entities its cause was moving.

This is the guarantee stated directly, and it should have been the first thing
built rather than the last. The reservation machinery is about *cells*; the
property that actually matters is about *entities*, and no amount of space-time
reasoning adds up to it.

**Why the reservation table could not catch this on its own.** Push a block onto
ice, let the push commit, then walk into the block again while its slide is
still running. The second push was admitted, and both halves were right on their
own terms:

- The **table** reasons about cells at instants. By the step the second push was
  tried, it believed the block had left that cell two steps ago — which its own
  claims say, correctly.
- The **planner** reads authoritative state, which does not advance until an
  action commits. The slide had not committed, so the block was still sitting on
  the cell, and a push for it was a perfectly sound plan.

Neither could see the contradiction, because neither was wrong. What was missing
was the plain statement that the block was already spoken for.

Pinned by `anEntityInFlightCannotBeTakenBySomethingElse`.

### 2. Cell claims — implemented

> **An action claims every cell on its path from the moment it starts until the
> instant it leaves that cell. The cell it comes to rest on is claimed
> open-ended.**

For a new action at step `S` against a path cell the block vacates at instant
`i`: refused when `S <= i` (not there yet), admitted when `S > i` (already
passed). That is exactly the two wanted cases and nothing more.

Claims were previously the other shape — cell `i` claimed at instant `i` *only* —
which is what let a second action slip into a cell the block had not yet
reached, and which is why the machinery was more elaborate than it needed to be.

`firstStep` is now always the action's own base, with exactly one exception: an
entity the action *adds* part-way through — a mirror clone — claims from the
instant it appears, because it was standing nowhere before that. Pinned by
`entitiesAnActionAddsAreClaimed`.

## What the claim rule deleted

Implementing it removed three things. The second is not obvious.

- **The read set** (`ActionReservations::reads`, `blockingCell`, and the
  `EntityTrack::slid` momentum inference that guards it). Its motivating case
  was the player standing on a slide's destination before the block arrives —
  and under the new rule the destination is claimed open-ended *from the start*,
  so a plain write-write test refuses it. The other half, an inferred read on
  the cell that stopped the slide, protected against a plan going stale when
  something vacates that cell; but the outcome is committed at plan time and is
  meant not to change, so there is nothing to protect.

- **`Traversal` and the swap check.** Two entities exchanging cells share no
  instant, which is why crossings had to be compared separately. But once claims
  begin at the action's start rather than at the instant of arrival, A's claim
  on its destination begins at instant 0 and overlaps B's claim on that same
  cell as its origin. Occupancy alone catches it, and the whole concept goes.

- **Instants-versus-intervals.** The reasoning that a claim numbers moments
  rather than spans existed so a *push* could be admitted — the block vacating a
  cell exactly as the player enters it. Under entity ownership a push is one
  action moving both entities, so it is never two actions to arbitrate. The
  problem the subtlety solved no longer exists.

`Reservation` reduced to a cell, a first step and an optional last step, and
`ActionReservations` to the one set of them — `writes` was renamed `cells`, since
the name only meant anything against the `reads` that no longer exist. The
causal-group exemption stays: a consequence still claims the cell its cause is
resting on.

## Decisions

| Question | Decision |
| --- | --- |
| Conveyor motion | Per-step actions, re-reserved each step |
| Conflicting input | Queued, not rejected |
| Undo | Only permitted when nothing is in flight |
| Undo granularity | One player action, whatever it set off |
| Causal chains | Committed in full at the first action |
| Player's own step | One step; the slide it starts is a separate action |
| Conflict detection | Entity ownership first, then cell claims |
| Cells ahead of a moving entity | Blocked — deliberately |
| Cells behind a moving entity | Free |
| Player facing | Every instance faces the last input, moved or not |
| Red rejection glow | Dropped for now |

### Why the player's step does not chain

Committing a player's push and the slide it starts as *one* action does not
release the player early, which was the original hope. An entity's final cell is
claimed **open-ended** — it is still standing there long after the action ends —
so the player's own resting cell would stay claimed for the whole chain, and
their next move, which has to write the cell they are standing on, would
conflict with it.

So `planPlayerStep` is one step and the slide is a second plan. The guarantee is
untouched: both plans are made from the same instant, so the slide's destination
is still settled at the moment of the push. What is given up is packaging — and
that is handled by causal grouping rather than by planning them together.

## Shape

- **Planners** live in `plans::` and are pure functions of their arguments, in
  the same spirit as `rules::`. `planPlayerStep`, `planSlides` and
  `planConveyorRides` plan over a causal closure; `worldStep` plans the whole
  board and is kept for save validation. All are one core function differing
  only in scope and whether they chain. Plus `fromMirrorPreview`, `restart` and
  `inverted`.
- **`rules::StepScope`** is the seam the planners are built on: which entities a
  step may move. Out of scope means scenery — still blocking, supporting and
  stopping slides, but forming no intent and never written. `rules::step` is
  exactly `scopedStep` with an empty scope and **must stay that way**, because
  saves on disk are validated by replaying through the whole-world form.
- **`StateDelta`** is what an action commits — per-entity changes keyed by id,
  never a whole `GameState`.
- **`Reservation` / `ActionReservations` / `ReservationTable`** hold the cell
  claims and answer `conflict`.
- **`ActionScheduler`** owns authoritative state, the actions in flight, their
  per-action clocks, the shared step clock, the table, and the ownership check.
  It executes plans; it does not make them.
- **`GameplaySession`** decides what to plan and in what order, and owns history,
  the undo stack and the running move total.

**Planning is per entity *set*, not per entity.** Two entities sliding from the
same instant are outside each other's scope, so each treats the other as scenery
standing where it started — and a third cell both cross on the same step looks
free to both. Hence `planSlides` / `planConveyorRides` over the whole set,
handing arbitration back to `MicroStepResolver`. Riders especially: planned
separately, a follower sees the leader as scenery in the cell it is about to
leave and never moves, so a queue on a belt would not advance.

**Nothing may plan for an entity already in flight.** Authoritative state does
not change until an action commits, so a second plan made from it is the same
motion again. `withoutEntitiesInFlight` filters both ambient motion *and* a
player step's consequences. Note this filters only what a planner is **seeded**
with — the closure can still grow into an owned entity, which is why the
ownership check in the scheduler is the real guard.

`ActionPlan` deliberately carries no `involved` or `effect` field. `Snapshot` —
including its `undoStack` of actions — is serialised into save files, so extra
fields would either bloat every save or default on load and make a restored
action compare unequal to a live one. The delta stays derived at completion, and
claims are computed at admission.

## Undo and saves under concurrency

Undo is the player's idea of what happened, not the scheduler's. One input
happened, so one undo puts back everything that followed from it, however many
actions the scheduler used. `ActionScheduler::InFlight` carries a `causalGroup`;
a consequence is started with the group of the action that caused it, and on
commit it folds into that group's undo entry — endpoints composed, presentation
timelines concatenated at an offset.

`undoGroups_` runs parallel to `undoHistory_` so a consequence folds into its own
cause rather than into whatever committed most recently.

**The save format is untouched.** `causalGroup` lives on `InFlight`, which is
transient; by the time a session is saved every action has committed.

**Validation had to change, though.** `restore` requires the undo stack to be a
linear chain, and concurrency broke it twice over:

- Two actions planned from the same instant both record `before = S`, and each
  `after` holds only its own change, so the chain breaks at the first overlap.
  Folding compounds it: composing into an earlier entry moves an endpoint that
  anything appended in between was chained to.
- An entry is a *partial* transition, so replaying the whole world steps
  bystanders the action never touched and lands elsewhere.

The fix is two-sided and both halves are in:

- An undo entry stores what its action **changed**, replayed onto the running
  chain — `recordCompletion` derives a `StateDelta` and applies it to the
  previous entry's `after`, and a fold rebases the short tail behind it
  (`rebaseUndoFrom`). The chain then holds by construction. Move counts move by
  the same reasoning: `playerMoveCount_` advances by an action's *delta*, never
  to the total the plan predicted, or an ambient action planned alongside a
  player's step would drag the count back down on commit.
- `matchesForwardTransition` tries the whole world first — unchanged, so every
  save written before the split validates exactly as it did — then a scoped
  replay, deriving the changed-entity set and running it through
  `rules::scopedStep`.

The scoped branch is **weaker validation**: replaying one entity under a scope
naming only itself is close to asking whether it could have moved at all. What
still holds it down is the chain check around it, which pins every entry to the
one before it and the last to the saved state.

**Checkpointing is no longer gated on the world being idle.** The gate assumed a
mid-action snapshot catches the world half-transitioned; it does not, because a
snapshot holds the *committed* state and the stack chained to it, and an action
in flight has contributed to neither. A reload loses the action, not the
consistency. The one cost is granularity: a slide whose push had already
committed reappears as its own undo entry rather than folded into that push.

## Queueing and starvation

A queued command is planned when it dequeues, not when it is entered, so it is
deterministic under the guarantee. The queue is bounded at two (a full queue
drops the oldest, since the most recent input is the one the player still means)
and commands older than one second are dropped. Both numbers are tuning, best
revisited against a playable build.

`StartOutcome` splits **`Refused`** (the table or ownership said no — goes back
on the front of the queue and retries) from **`Impossible`** (there was no plan
to make — dropped). Without that split a player walking into a wall would retry
a doomed command until it went stale, holding up everything behind it.

Because a belt rider releases its reservation at the end of each step and
immediately takes another, a queued player action could be shut out
indefinitely. **`tryStartNextAction` drains the command queue before starting
any new ambient action**, which is what prevents it. Pinned by
`queuedCommandsGoAheadOfAmbientMotion`.

## Chain termination

Full chain commitment means planning must provably terminate.
`plans::maxChainedSteps` caps it.

**The adversarial ice/belt loop does not appear to be constructible.** A slide
travels in a straight line and momentum never changes direction; a belt cannot
bend it, because momentum overrides the belt and `conveyorRiders` excludes
anything still sliding; a fall cancels momentum outright. The cap stays as a
guard against mechanics that do not exist yet — a teleport, a bumper, anything
that redirects.

Hitting the cap is safe, which is the more useful half: a capped plan leaves the
block still carrying momentum, and momentum is what ambient motion schedules on,
so the remainder is planned as a second action rather than lost. Pinned by
`chainPlanningStopsAtTheCap`, which reaches it the only way available — a
corridor longer than the cap.

---

## What is left

### 1. Verify `Application.cpp` on Windows

Still the open item. `Application.cpp` is excluded from the headless build (it
pulls in Vulkan), so three changes have never been compiled by any real build:

- the `projectedState` call site in `buildRenderFrame`;
- the two checkpoint gates that no longer test `moving()`.

`GameplayInput::projectedState` was made a required reference rather than an
optional field precisely so a missed call site is a compile error.

What *has* been done short of that: the `buildRenderFrame` statement was lifted
verbatim into a throwaway translation unit with the members replaced by locals of
the same types, and type-checked against the real headers — `RenderFrameBuilder`
and `GameplaySession` are both headless, so only `Application.cpp`'s own Vulkan
includes stand in the way. It compiles, including the designated-initializer
order and the reference binding to the local `projectedState`. The same was done
for the debug-UI admission counters below. The two checkpoint changes are
condition *removals*, so they carry no compile risk at all; what they carry is
behaviour risk, and that is covered by
`snapshotMidFlightRestoresFromTheCommittedState`.

So the Windows build is expected to be clean. It has not been run.

### 2. Run the two Vulkan suites

`vulkan_device_selection` and `frame_descriptor_sync` include
`<vulkan/vulkan.h>` and need the Windows/MSVC build.

The other eight that used to be listed here — the SDL-dependent ones, including
`player_profile` — now run on Linux and pass. Build SDL once as the header of
`tools/build_headless_tests.sh` describes and the script reports 41 of 43.
`player_profile` was the one flagged as most likely to have been disturbed by the
undo rebase; it round-trips history through the save format and is clean.

### 3. Read the admission counters

`ActionScheduler::AdmissionStats` counts admitted / refusedByOwnership /
refusedByReservation, exposed via `GameplaySession::admissionStats()` and now
shown in the Engine tab of the Debug UI, alongside the in-flight count.

If `refusedByReservation` stays near zero in real play, the cell machinery is
barely earning its keep even in simplified form, and the next simplification is
to drop time from claims entirely — an action locks the cells it touches for as
long as it runs. That would cost case 2 above (entering a cell the block has
passed), so it is a gameplay call, not a code one.

**There is reason to expect it will read near zero.** A refusal by claim needs
two actions with disjoint entities whose paths cross, and the commonest candidate
turns out not to be one: a player who pushes a block ends up directly behind it,
both move a tile per step, so the player can never get ahead of the slide to be
refused by a claim on a cell in front of it. The block is also still sitting in
authoritative state where the push left it until the slide commits, so a step
into *that* cell is refused by ownership, not by claims. Which is why
`playerFollowsIntoACellTheSlideHasPassed` has to walk the player down a second
row and back up to reach a cell the block has passed at all. If real play agrees,
the honest reading is that the time dimension is doing very little.

---

## Testing

Run `tools/build_headless_tests.sh` on Linux, or the normal CMake/CTest build on
Windows. The script builds `sokoban_core` and `sokoban_ui` directly from the
source lists in `CMakeLists.txt`. Without a vendored SDL build it runs 33 of the
43 suites; with SDL, 41. Only the two that include `<vulkan/vulkan.h>` need
Windows.

### Three ways the test script will lie to you

- **A failed relink does not fail the run.** `LINK FAIL` is printed, the script
  `rm -f`s the binary, and if *that* also fails the stale binary is executed and
  reports PASS. An output directory owned by another user produces exactly this.
- **An interrupted compile leaves a zero-byte `.o`** that is newer than its
  source, so `needsRebuild` accepts it and `ar` archives it. The failure
  surfaces as undefined symbols in unrelated suites. `nm` on every object
  catches it. If you are building in an environment that kills long commands,
  compile to a temp path and `mv` into place — then an interruption can only
  leave a `.tmp`.
- **The script exits 1 on complete success.** The last statement is
  `[ -n "$skipped" ] && echo ...`, which is false when nothing was skipped.

It rebuilds on header changes via a whole-tree header timestamp. The precise
`.d`-file version was tried and silently did nothing, because the checkout path
contains a space and `.d` escaping defeats naive whitespace splitting. That
failure mode produces objects built against different layouts of the same struct
and crashes that read as logic bugs, so it is worth not reintroducing.

### Suites that pin this design

`state_delta`, `action_plan`, `reservation`, `action_scheduler`,
`gameplay_session`, `gameplay_loop`, `presentation`.

**The guarantee itself** is pinned by
`outcomeSurvivesAnyChangeOutsideItsClaims` (`action_plan`): it plans a slide,
re-plans it from every state reachable by moving the player onto a cell the plan
does not claim, and requires the block to land in the same place by the same
route. Note the asymmetry — it says nothing about the claim set being *tight*. An
overstated one costs responsiveness, never correctness.

**The two wanted kinds of concurrency** have one test each, so that neither can
be lost silently. Case 1 is `playerMovesAlongsideASlideItCannotDisturb`; case 2
is `playerFollowsIntoACellTheSlideHasPassed`, which is the whole justification
for a claim carrying a last step rather than being held for the action's
duration. Delete the time dimension and that second test is what fails.
`crossingBehindTheBlockIsAllowed` states both halves at table level: behind the
block admitted, ahead of it refused.

**Concurrency and history** (`gameplay_session`):
`iceSlideIsSettledAtTheMomentOfThePush`,
`playerMovesAlongsideASlideItCannotDisturb`,
`anEntityInFlightCannotBeTakenBySomethingElse`,
`commandRefusedByAClaimIsRequeuedNotLost`, `concurrentPlayHistoryRoundTrips`,
`snapshotMidFlightRestoresFromTheCommittedState`, `goldenTraceIsReproducible`.

**Belts** (`action_plan`, `gameplay_session`):
`beltRidersFollowEachOtherDownOneBelt`, `beltCarriesARiderOneActionPerStep`,
`queuedCommandsGoAheadOfAmbientMotion`.

**Rendering** (`gameplay_loop`): `renderedPlayerNeverGoesBackwards`,
`chainedSlideIsDrawnTileByTile`. These exist because a rendering fault is
invisible to every other test — the committed state and the plan are both
correct, and only the sampled position between them is wrong.

**Golden traces** compare a whole committed `ActionPlan` sequence against a
second run of the same input script, rather than against baked-in literals. A
hard-coded expectation pins the ordering of a hundred fields nobody reads and
breaks on every unrelated change; what needs guarding is that nothing in the
scheduler depends on anything beyond the level, the state and the input.

### Belt fixtures

A movable's start tile (`R`, `I`) resolves to Air, so "a rock standing on a
conveyor" cannot be authored — the cell is either the rock or the belt, never
both. Two ways round it:

- **`placeMovables`** in `action_plan` moves authored movables onto the cells a
  test needs, giving the state a push would have produced without the push.
  `beltWithTwoRiders()` is built on it. Planner-level tests want this.
- **Push a rock on for real** in `gameplay_session`. Hand-placed state cannot be
  injected into a session — `restore` validates the snapshot against a replay
  from the opening state and rejects anything that does not chain — so a
  session-level test has to reach the belt by playing.

---

## Things learned that are not obvious from the code

- **`advance` had to split into `advanceClock` and `commitFinished`.** The caller
  works between them: the presentation is sampled at the new elapsed time before
  anything commits, and history bookkeeping hangs off the commit.
- **Elapsed time must not be clamped.** How far an action ran past its end is
  what orders the commits, and it sits *below zero* while an action is deferred.
  Readers clamp to `[0, duration]` on the way out.
- **A deferred action must not be seeked.** It is admitted and holds its claims,
  but its entities are still driven by its cause. `startPresentation` and
  `seekAllInFlight` skip it until its clock reaches zero, or it snaps the block
  to where the push is going to leave it and cuts the push's animation short.
- **`seekAction` must apply one motion track per entity.** A chained slide owns
  one per leg, and applying all of them let the last win — a track whose leg had
  not begun set the entity to *that leg's* starting cell, so a block one tile
  into a five-tile slide was drawn at its destination for the whole slide.
- **Legs serve two masters and must not be shared.** Claims need at least one leg
  or a plan claims nothing. But the presentation reads legs to decide whether to
  animate a chain tile by tile, and a synthetic leg on a single-step action sends
  it down the chain-aware path, which pairs entities positionally and cannot
  survive an action that *adds* a player — mirror activation does exactly that.
- **`collectTracks` must run past the end of `before`.** Entities an action
  *adds* — mirror clones — otherwise claim nothing at all, leaving the clone's
  cell free for anything else to walk into. They carry a `firstInstant` so the
  claim starts when they appear and not before.
- **The loop in `GameplayLoop::update` did not disappear.** It no longer
  *sequences* — what remains is catch-up: a frame longer than an action must run
  the world forward more than once, stopping at each completion rather than
  stepping over it, or an action commits late and whatever follows is planned
  from a state that never existed. Hence `timeToNextCompletion()`.
- **`reservationsFor` originally claimed every entity in the state**, so every
  plan write-claimed every bystander's resting cell and no two plans could ever
  be concurrent.
- **Player facing is global by design.** Every player instance faces the last
  movement input, including instances it did not move — mirror copies are one
  character in several places sharing one input. This must not be scoped even
  under concurrency. Ambient facing — a belt or slide turning a rider — is a
  different thing and *does* need scoping, since it is not an input.
- **`playerMoveCount_` is safe under concurrency.** Only input-driven actions
  increment it, and a second would have to move the player, whom the first
  already owns. The invariant is enforced by the ownership check, not by the
  counting code.
- **`rules::hasPendingMotion` is not a "world is settled" test.** It reports that
  an entity is *on* a conveyor, not that the conveyor can move it, so a rider
  pinned against a wall keeps it true forever. The scheduling loop terminates
  anyway, because a ride that changes nothing is no plan at all.
- **Tests must admit before advancing.** Checking `moving()` first stops dead on
  a world that is idle but still owed ambient motion — exactly a belt between
  one rider's step and the next. `runUntilIdle` uses `GameplayLoop`'s order.
