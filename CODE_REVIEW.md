# Engineering Review: Code Quality & Refactoring Priorities

**Scope:** current `main`. No new features — this is about making what exists
cleaner, more readable, harder to break, and cheaper to run. Findings are
ordered by leverage: what a small team should spend its next quality budget on.

**Context for the reader:** the codebase is ~23.5k lines of C++20 across
engine, renderer, UI, and tools, with 17 headless test suites. The overall
architecture is genuinely good — headless gameplay/UI modules with thin
adapters, a data-driven asset manifest, a strict versioned save codec, and a
content pipeline that ships only reachable files. The problems below are
mostly the *seams between* those good modules, and the two files that have
quietly re-accreted responsibility.

---

## P0 — Structural debt that is actively producing bugs

### 1. `Application.cpp` (1,326 lines) has become the untested glue layer

The codebase's own doctrine is "headless module + thin adapter," and it has
been applied everywhere except the place where everything meets. `Application`
now owns: the SDL event loop with capture/suppression rules, shell-menu result
routing, save-slot lifecycle (marker file, store swapping, deletion,
summaries), level/screen progression, completion interception, checkpoint
scheduling, and audio gating. None of it is testable, and it shows: the most
recent shipped bug (deleted saves resurrecting) lived in `saveSlotInfos()` —
exactly the kind of two-line policy error a headless test would have caught,
in exactly the layer that has none.

**Recommendation.** Extract two headless components, in this order:

- `SaveSlotManager` — owns `saveDirectory_`, the two `AsyncSaveStore`s, the
  active-slot marker, `saveSlotInfos()`, `switchSaveSlot`, `deleteSaveSlot`,
  and the settings-merge boot logic (`loadInitialProfile`). Filesystem access
  behind the existing store abstractions; fully unit-testable with a temp dir,
  same as `SaveStore` already is. This is where the last three bugs clustered.
- `ShellFlow` — a pure state machine consuming menu results
  (`TitleScreenResult`, `OptionsMenuResult`, `LevelCompleteResult`) plus a
  few facts (`gameLoaded`, `progressEmpty`, `allLevelsCompleted`) and emitting
  commands (`LoadScreen`, `StartNewGame{slot}`, `OpenLevelSelect`, `Quit`...).
  `Application::run()`'s ~80-line result-routing if-chain becomes a dispatch
  over commands, and every menu-flow rule gets a test.

Effort: 2–3 days each. This is the highest-leverage change in the codebase.

### 2. Menu result structs are drifting toward boolean soup

`TitleScreenResult` is now seven fields (three bools, three optionals, plus
`startRequested`); nothing prevents impossible combinations, and every new
shell interaction adds a field plus a routing branch. Same pattern brewing in
`OptionsMenuResult`.

**Recommendation.** Replace result structs with
`std::optional<ShellAction>` where `ShellAction` is a `std::variant` of small
command structs (`Continue`, `NewGame{slot}`, `SwitchSlot{slot}`,
`DeleteSlot{slot}`, `StartLevel{level, screen}`, `OpenOptions`, `Quit`, ...).
One menu interaction produces at most one action — which is already true in
practice, but the types don't say so. Pairs naturally with the `ShellFlow`
extraction above. Effort: 1 day, mostly mechanical.

### 3. `rules::step()` is a 251-line function guarding the whole game

It is correct (131 rules checks say so) but it is the file people will be
most afraid to touch: three nested resolution loops, per-entity budget
arrays, contested-destination pre-passes, and slide-cancellation rules woven
through lambdas. The recent unsupported-landing fix had to be threaded
through five separate branch sites — a shape that invites the next
regression.

**Recommendation.** Extract a `MicroStepResolver` struct with named phases:
`deriveIntents()`, `markContested()`, `resolveMoves()`, `settleBlocked()`,
operating on an explicit `EntityMotion` array (player as index 0 rather than
a parallel set of `player*` variables — roughly half the function body is
player/movable duplication, including the twin `playerFallTarget` /
`movableFallTarget` functions which differ only in their occupancy predicate;
unify them behind one fall routine taking a blocker predicate). Behavior
must be regression-locked by the existing suite before and after. Effort:
2–3 days, high care, do it while the rules suite is green and fresh.

---

## P1 — Duplication and readability

### 4. The `ui/` modules re-implement the same private helpers

Measured duplication across `OptionsMenu.cpp`, `TitleScreen.cpp`,
`LevelCompleteOverlay.cpp`:

- `drawTrailingText` — 3 copies (one with a padding parameter, two without).
- `formatTimeSeconds` — 3 copies with *silently different behavior* (title
  shows `m:ss`, the overlay `m:ss.t`) — a consistency bug waiting to be
  noticed by a player comparing screens.
- `centeredColumn`/`centeredPanel`/panel-height policy — 3 variants.
- `MenuPageLayout` vs `TitlePageLayout` — same title/subtitle/divider
  scaffold, two names.
- Row-navigation (`navigateRows` + wrap + reset-on-change) — 4 copies, and
  three *different* ad-hoc schemes for optional rows (OptionsMenu computes
  shifted indexes arithmetically; TitleScreen hides a row and re-derives
  three indexes; the overlay hardcodes two enums). The optional-row
  arithmetic in `OptionsMenu::drawMain` is the most fragile code in the UI
  layer.

**Recommendation.** Add `ui/MenuKit.{hpp,cpp}` (or fold into `UiControls`):
`trailingText`, `formatDuration(seconds, precision)`, `centeredColumn`,
`MenuHeader` layout scaffold, and — most importantly — a tiny `RowList`
builder: rows are appended conditionally, each returns its index, navigation
and focus queries go through it. That deletes every hand-maintained index
constant and makes "insert a row" a one-line change instead of an index
audit. Effort: 1–2 days including migrating the three menus and their tests.

### 5. `PlayerProfile.cpp` (1,147 lines) mixes model, codec, and 8 migrations

The format chain works, but each `parseFormatN` deep-copies the JSON root and
erases fields before delegating, so decode cost and file length grow linearly
with format count — and we are at format 8. The file also now contains a
quiet data trap: slot files still *carry* settings sections that are written
but ignored (settings.json is authoritative). A future reader will
absolutely be confused by that.

**Recommendation.**
- Split into `PlayerProfile.cpp` (model: normalize, records, settings
  helpers) and `PlayerProfileCodec.cpp` (serialize/decode/migrations). Pure
  file move, no behavior change. Effort: half a day.
- Restructure migrations as forward patches on the JSON (`migrate1to2(json)`
  ... `migrate7to8(json)`) followed by a single strict format-8 parse,
  instead of parse-chains that re-copy the root at each hop. Effort: 1 day,
  fully covered by existing migration tests.
- Schedule format 9: drop settings sections from slot files (write
  progress-only; accept-and-ignore on read for 8). Do this *before* more
  settings land. Effort: half a day.

### 6. CMake test boilerplate

Twelve near-identical `add_executable` / `target_include_directories` /
per-compiler warnings / `add_test` blocks (~180 lines). One
`sokoban_add_test(name LIBS ... SOURCES ...)` function collapses this and
ends the copy-paste drift (half the targets got `sokoban_stb`, half didn't
need it, one links SDL — make that declarative). Effort: 2 hours.

### 7. Logging is `std::cerr` scattered across 16 call sites

Four different modules log directly to stderr with no levels, no timestamps,
and nothing captured in shipped builds — while the save system carefully
archives corrupt files for diagnosis, its own status strings vanish. A
20-line `Log.hpp` (level + sink, default stderr, optional file next to the
profiles) makes ship-build diagnostics real. Do it before the next
"worked on my machine" save bug. Effort: half a day.

---

## P2 — Efficiency (measured restraint: nothing here is currently hot)

### 8. Title-screen construction does filesystem IO every open

`openTitleScreen()` → `titleLevelInfos()` walks `screenExists()` per
level×screen (15 `std::filesystem::exists` call sites in `Application`
overall) and `saveSlotInfos()` re-reads and fully decodes the *other two
slots' JSON* — including their embedded gameplay-session snapshots — on
every title open, including every exit-to-title. The content pipeline
already writes `content.index` with the authoritative file list;
`RuntimeContent` should expose a level/screen table parsed once at boot, and
slot summaries should be cached and invalidated only by slot writes/deletes
(the app performs every mutation itself, so invalidation is trivial).
Effort: 1 day. Also removes the "level count" re-scan in `advanceScreen`,
`allLevelsCompleted`, and `saveSlotInfos`.

### 9. Per-frame allocation churn in the render path

`VulkanRenderer` rebuilds and sorts the iso-face vector, and
`RenderFrameBuilder` rebuilds full tile vectors, every frame; frame
requirements allocate fresh `vector<bool>`s. At current scene sizes this is
noise — but it is also free to fix opportunistically: keep persistent scratch
vectors (`clear()` not reallocate) in the renderer and builder, and
`reserve()` from last frame's counts. Do not restructure anything for this;
just stop re-allocating. Effort: opportunistic.

### 10. Two `AsyncSaveStore` worker threads

Settings and slot stores each own a worker. Harmless, but one keyed worker
(`requestSave(storeId, profile)`) is simpler to reason about at shutdown and
halves the synchronization surface. Fold into the `SaveSlotManager`
extraction rather than doing it standalone. Effort: included above.

---

## Explicit non-goals (reviewed and rejected)

- **Replacing the custom GLTF loader.** It is 1,536 lines that load exactly
  our assets, with clear failure modes. A general library is more code, not
  less, until asset complexity actually grows.
- **Task-graph dependencies in `TaskSystem`.** No current consumer needs
  ordering; adding it speculatively is how task systems get scary.
- **Further splitting `VulkanRenderer` (2,334 lines).** It is an orchestrator
  over nine focused components now; the remaining bulk is scene traversal
  that would not improve by relocation. Revisit only alongside a concrete
  need (e.g. a second scene pass).
- **Asset cache eviction.** The staged tree is ~4 MB.
- **Micro-optimizing `rules::step`'s O(n²) contested pre-pass.** Entity
  counts are single digits; clarity wins (see item 3, which is about shape,
  not speed).

## Suggested sequencing

1. `SaveSlotManager` extraction + tests (item 1) — highest bug density.
2. `ShellAction` variant + `ShellFlow` (items 1, 2) — locks the menu flows.
3. `MenuKit`/`RowList` (item 4) — deletes the index arithmetic before the
   next menu row lands.
4. Profile codec split + forward-patch migrations + format 9 (item 5).
5. `Log` module (item 7) and CMake function (item 6) as gap-fillers.
6. `MicroStepResolver` (item 3) as a deliberate, isolated change with the
   rules suite as the safety net.
7. Content-table caching (item 8) whenever title-open latency is next
   touched.

Each step leaves the build green and ships independently; none blocks
feature work. The theme across all of it: the headless-module discipline
that made the *inner* systems excellent now needs to be applied to the glue.
