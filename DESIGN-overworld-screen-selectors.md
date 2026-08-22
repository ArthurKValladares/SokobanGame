# Overworld and Screen Selector Design

Status: design only. No game code or content has been changed as part of this document.

## 1. Goal

Replace navigation between levels with a playable overworld while preserving sequential progression between screens within each level. The overworld is authored and simulated as a normal Sokoban screen: it uses the same layered board format, player movement, pushing, ice, conveyors, ladders, water, mirrors, enemies, pressure plates, undo, restart, animation, audio, camera, and checkpoint behavior.

The overworld adds one semantic object: a **Screen Selector** anchored to a board cell. When the player is standing on an available selector and presses Space, the game loads the level/screen assigned to that selector. Solving that screen records its completion and returns the player to the preserved overworld state. A selector is blue when playable and unsolved, green when solved, and red when locked. The last screen in a level uses the corresponding `flag_B_COLOR` model; all other screens use `flag_A_COLOR`.

The Debug Level Editor must be able to edit the overworld, place/delete selectors, show `Selector N` labels above them in the 3D editor view, and associate each selector with an existing level/screen.

## 2. Scope and interpretation

Each level retains its own sequential screen progression, while levels remain independent of one another. In particular:

- Entering a selector mapped to `(level 2, screen 1)` loads exactly that screen.
- Screen 0 of every level is playable immediately.
- Screen N is playable after screen N-1 in the same level has been solved.
- Completing a screen never unlocks a screen in another level, and a level does not depend on any lower-numbered level.
- Solving it does **not** automatically load screen 2 or the next level.
- The completion overlay's Continue action returns to the overworld.
- Completion, best moves, and best time are recorded per target screen.
- A solved selector can be entered again to replay its screen and improve its records.
- If two selectors point to the same screen, both display the same solved state.
- The overworld is valid only when every catalog screen has at least one selector. Overall game completion therefore means every catalog screen has been solved.

The existing level/screen directory organization, names, per-level music, screen files, and ground splat maps remain useful content organization. What changes is the player-facing progression policy.

## 3. Current architecture and why this is a major change

The current implementation encodes sequential progression in several systems:

- `CampaignSession` owns a numeric current level/screen, accumulates move/time totals across screens, advances to `screen + 1`, and then advances to the next level.
- `Application::advanceScreen()` treats a solved screen as either `ScreenAdvanced`, `LevelCompleted`, or `GameCompleted`.
- `PlayerProfile` stores current level/screen, level-level completion, reached-screen counts, level bests, and one checkpoint tied to the numeric current screen.
- The title screen and options expose a level/screen selector after unlock conditions are met.
- `LevelCompleteOverlay` assumes level-level totals and a Next Level/Level Select flow.
- Asset preloading assumes the current level and next level are the likely destinations.
- `levels/` staging, project validation, editor browsing, and runtime mirroring only know `levelN/screenN.scr` files.
- Ground splat map identity is derived from a `LevelLocation`, which currently has no overworld representation.

Therefore the feature should be implemented as a progression/navigation redesign with a first-class selector model. Treating the flags as ordinary mesh decorations would leave progression, persistence, editor identity, remapping, and completion semantics unresolved.

## 4. Core design decisions

### 4.1 Dedicated overworld file

Store the overworld at:

```text
levels/overworld.scr
```

It uses the existing `.scr` syntax and `Level` parser, extended with selector metadata. Keeping it outside `levelN/` prevents it from being counted as a puzzle screen and avoids inventing a reserved level or screen index that could leak into player records and metadata.

The source file and staged runtime file use the same relative path.

### 4.2 A selector is a first-class tile-anchored object

A selector should not be a generic `Level::Decoration`:

- Generic decorations are free-form transforms and can be moved between cells without stable identity.
- Selectors require a stable ID for editor labels and associations.
- Selectors require an exact gameplay cell.
- Their model is chosen by save progress, not authored per instance.
- Their targets must be remapped atomically when levels/screens are inserted or deleted.

A selector also should not be added to the ordinary `TileType` character grid. The flag must share a walkable cell with the player and should be offset within that cell so the character and flag do not occupy the same visual pivot. A separate tile-anchored record preserves the underlying normal tile and makes the flag a semantic decoration of that tile.

Proposed level model:

```cpp
struct ScreenSelector {
    uint32_t id;
    GridPosition3 cell;
    std::optional<LevelLocation> target;
};
```

`Level::Definition` and the loaded `Level` gain a selector collection and lookup helpers such as `selectorAt(cell)`.

Rules for selector records:

- IDs are positive and unique within the overworld.
- Cells are unique within the overworld.
- The cell is an entity-occupancy cell, just like the player's `GameState::Player::cell`.
- The cell must be statically walkable and supported by the board.
- A selector does not block players, movables, enemies, beams, or camera fit.
- A target may temporarily be absent while authoring. A production content build rejects unassigned selectors.
- Selector IDs are stable. Deleting Selector 2 does not rename Selector 3. A new selector receives `max(existing IDs) + 1`.

### 4.3 Selector file syntax

Extend `.scr` metadata with one JSON directive per selector, before `@layer 0`, following the existing `@decoration` precedent:

```text
@selector {"id":1,"cell":[4,3,2],"target":{"level":0,"screen":0}}
@selector {"id":2,"cell":[8,3,2],"target":{"level":1,"screen":0}}
@selector {"id":3,"cell":[12,3,2],"target":null}

@layer 0
...
```

The parser must reject duplicate IDs, duplicate cells, negative coordinates, malformed targets, and out-of-bounds/non-walkable cells after the full level has been constructed. Serialization is deterministic: selectors are written in ascending ID order before free-form decorations and layers.

Although the parser can understand selector metadata in any `.scr` for round-trip simplicity, project/content validation must reject selectors in ordinary puzzle screens. Only `levels/overworld.scr` may contain them.

### 4.4 World identity

Introduce a navigation-level identity distinct from `LevelLocation`:

```cpp
using WorldLocation = std::variant<OverworldLocation, LevelLocation>;
```

`LevelLocation` remains the identity of a puzzle and continues to drive level/screen metadata, music, and screen-specific splat maps. `OverworldLocation` identifies the hub without a fake numeric pair.

Any subsystem that currently assumes every rendered world has a `LevelLocation` must explicitly support the variant, especially ground splat naming, logs, checkpoints, and editor path parsing.

## 5. Runtime behavior

### 5.1 Starting and continuing

- New Game resets progression and starts a fresh `overworld.scr` session.
- Continue restores whichever context was active when last saved:
  - an in-progress selected puzzle resumes that puzzle;
  - otherwise the overworld checkpoint resumes the overworld.
- If a checkpoint no longer validates against edited content, discard only that invalid checkpoint, load the authored initial state, log the recovery, and immediately save the repaired state. This follows the current invalid-screen-checkpoint behavior.

### 5.2 Moving around the overworld

The overworld runs through the normal `Level`, `GameplaySession`, `GameplayLoop`, `Rules`, and `GameplayPresentation` path. Input commands, action scheduling, undo history, restart, ambient motion, and autosave cadence must not be duplicated in an overworld-specific movement engine.

Overworld moves do not contribute to puzzle best-move or best-time records.

### 5.3 Activating a selector

Space is the keyboard default for the remappable `MenuConfirm` action. Expose that same semantic action as `interactPressed` in the gameplay routing context so it drives selectors and mirror activation without a separate serialized Mirror action. The Controls UI label is **Confirm / Interact**; Space is its sole keyboard default.

Activation is accepted only when all of the following are true:

- the loaded world is the overworld;
- the press is a new edge, not a held repeat;
- no gameplay action is in flight and no automatic movement is pending;
- the completion overlay, title, options, editor document mode, and draft-exit modal are closed;
- at least one living player exists;
- the selector has a valid target that exists in the current catalog;
- the target is solved, is screen 0, or its immediately preceding screen in the same level has been solved.

Because mirrors can create multiple players, selector activation follows a deterministic rule consistent with the existing end-tile rule: every living player must stand on the same selector cell. A dead player prevents activation just as death prevents screen completion. This avoids silently abandoning a clone elsewhere on the overworld.

On activation:

1. Write an immediate overworld checkpoint containing the complete `GameplaySession::Snapshot`.
2. Set the current context to the target puzzle.
3. Restore that puzzle's active checkpoint if it is the one currently saved; otherwise start from its authored state.
4. Load target assets, reset presentation/particles, switch music using the target level, and immediately checkpoint the puzzle.

An unassigned or now-invalid selector remains visible in Debug builds, but Space does nothing and a warning is shown in the editor/status log. Production content validation prevents shipping that state.

### 5.4 Solving a selected screen

When `GameplayLoop` reports `screenSolved` in a selected puzzle:

1. Capture that screen's move count and elapsed puzzle time.
2. Record per-screen completion and bests (unless the debug solve command was used).
3. Clear the active puzzle checkpoint.
4. Mark the saved current context as overworld.
5. Persist immediately before opening a completion overlay.
6. Open a **Screen Complete** overlay showing the target's level/screen name, moves, time, and best comparisons.
7. On Continue, restore the preserved overworld snapshot. The selector flag is now green and the following screen in that level is now blue because rendering reads the updated profile.

The completion overlay's To Title action records the same completion first, then opens the title. Continue from the title returns to the overworld. It must not accidentally reload the completed puzzle.

When the solve completes the last distinct target used by the overworld, open the game-complete variant. Its primary action is **Return to Overworld**, not Level Select. The overworld remains playable and solved screens remain replayable.

### 5.5 Debug solve

The existing Engine-tab debug solve command should continue to work only in a selected puzzle, not in the overworld. It marks the screen complete but does not create best records, matching current debug semantics.

### 5.6 Draft play

Ordinary and overworld drafts continue to exercise all normal mechanics. Selector activation should be disabled during unsaved draft playback so a draft cannot mutate profile progression or transition into staged content while retaining an in-memory return world. The selector panel and labels still allow the author to validate assignments. A saved overworld can be tested through the normal game flow.

Supporting cross-screen transitions from an unsaved draft could be added later as a separate editor feature with an isolated, non-persistent test profile.

## 6. Progress and save data

### 6.1 Per-screen progress

The flag state and completion overlay require screen-level records. Extend each `LevelProgress` with screen entries or add a top-level sorted list keyed by `LevelLocation`:

```cpp
struct ScreenProgress {
    int level;
    int screen;
    bool completed;
    optional<int> bestMoves;
    optional<double> bestTimeSeconds;
};
```

Required profile queries include:

- `progressForScreen(LevelLocation)`
- `screenCompleted(LevelLocation)`
- `recordScreenCompletion(LevelLocation, moves, time, recordBests)`
- `allTargetsCompleted(span<LevelLocation>)`

Store screen records in sorted level/screen order and reject duplicates during decoding.

The old `unlockedLevel`, `currentLevel`, `currentScreen`, `reachedScreens`, level completion, and aggregate level-best fields are no longer authoritative for navigation. They may be retained for one migration version and old-save compatibility, then removed in a later cleanup.

### 6.2 Checkpoints

The profile needs two distinct concepts:

- `overworld`: the latest overworld `GameplaySession::Snapshot`;
- `activePuzzle`: an optional target location, elapsed time, and `GameplaySession::Snapshot` for the currently active puzzle.

It also needs an explicit current context (`overworld` or `puzzle`). The old single `activeScreen` field cannot preserve the overworld while a puzzle is active.

Only the current puzzle needs a resumable checkpoint. Completed and abandoned historical puzzle sessions do not need to be retained. The preserved overworld checkpoint remains until New Game/reset.

The profile normalizer must validate internal consistency without requiring filesystem access:

- puzzle context requires a matching `activePuzzle` location;
- overworld context clears stale `activePuzzle` after a completed transition;
- times and moves are finite/non-negative;
- screen progress keys are non-negative and unique;
- settings remain independent from progress reset as they are now.

Catalog-dependent validation happens after the level catalog and overworld are loaded.

### 6.3 Format migration

Bump `currentPlayerProfileFormat` from 17 to 18 and add a strict migration.

Old sequential saves should preserve earned completion as closely as possible:

- For every old level record, create screen-completion records for indices `0 .. reachedScreens - 1` when the level was completed.
- Old completion necessarily meant every then-existing screen in that level had been traversed sequentially. Retain a temporary `legacyLevelCompleted` marker (or the existing level `completed` field) so old formats whose `reachedScreens` was zero can still make every selector targeting that completed level green after the runtime catalog is available.
- Old level aggregate best moves/time cannot be truthfully assigned to an individual screen. Preserve them as legacy aggregate statistics or omit them from screen bests; never copy the aggregate to every screen.
- An old in-progress `activeScreen` becomes the active puzzle checkpoint for its saved location.
- Since old saves have no overworld snapshot, returning from that resumed puzzle starts a fresh overworld. If the old save has no active screen, it starts directly in a fresh overworld.
- `unlockedLevel` does not gate selectors after migration.

Migration is automatically rewritten by `SaveStore`, so codec, migration, backup-recovery, slot-summary, and round-trip tests must all move together.

### 6.4 Save-slot summaries

Replace the title's current-level/completed-level summary with selector-target progress:

- completed distinct targets / total distinct valid targets;
- complete when those counts match and total is nonzero;
- current context may be shown as `Overworld` or the active puzzle name.

Slot summaries require the overworld target catalog, not only `levelCount`. Cache invalidation should use a target-catalog revision/fingerprint so changing selector assignments invalidates non-active slot summaries.

## 7. Rendering and assets

### 7.1 Manifest entries

Register the supplied texture and six model variants explicitly in `assets/manifest.json`:

- texture: `KayKit Board Game Bits 1.0/Assets/gltf/boardgame_bits_texture.png`;
- normal-screen models: `flag_A_blue.gltf`, `flag_A_green.gltf`, and `flag_A_red.gltf`;
- final-screen models: `flag_B_blue.gltf`, `flag_B_green.gltf`, and `flag_B_red.gltf`.

The glTFs reference their adjacent `.bin` and texture; the content pipeline's existing glTF dependency scanner stages those once the models and texture are in the manifest.

The models have identical bounds and a height of approximately 1.43 authored units. During implementation, use one shared selector transform policy and visually verify scale, axis orientation, bottom pivot, flag-pole corner offset, shadows, and player overlap. Do not expose per-selector transforms in content; all selectors must render consistently.

### 7.2 Dynamic model choice

Gameplay overworld frame construction receives a read-only selector-state query. For each selector:

- choose blue for playable but incomplete, green for solved, and red for unavailable or invalid;
- choose the B model when the target is the last screen in its level, otherwise choose A.

Editor frame construction should use the active save slot's progress so the author sees the same state as normal gameplay. An unassigned selector is red.

All six flag models must be included in overworld asset requirements even if the current frame only uses one variant. A screen can change state and return to the already-loaded overworld without a blocking model load. The per-frame requirements assertion must therefore also accept each dynamic change.

Selectors do not affect camera fit, are not generic-decoration pick targets, and do not contribute collision geometry. They may cast/receive shadows using the normal model path.

### 7.3 Editor labels

Labels are Debug-editor overlays, not world geometry and not serialized content.

Follow the existing brush/gizmo overlay pattern:

1. Project a world point above each selector flag using `VulkanRenderer::projectToPixels` and the prepared frame.
2. Build testable label placement data in `EditorInteraction` (text, pixel anchor, selector ID).
3. Draw `Selector N` with `ImGui::GetBackgroundDrawList()` so labels appear over the 3D scene and behind Debug windows.
4. Center the text, add a dark outline or translucent backing for readability, and skip points outside/behind the camera.

Labels render whenever the loaded editor document is `overworld.scr`, regardless of whether the Tiles, Mesh Decorations, or Screen Selectors tool is active.

### 7.4 Overworld ground splat map

Extend ground-splat identity with an overworld-specific name/path, for example:

```text
GroundSplatMapOverworld
assets/custom/textures/ground_splat_overworld.png
```

The editor's Paint Ground/Create Splat Map workflow and loaded-document path resolution must recognize `levels/overworld.scr`. A missing dedicated map should continue to fall back to the global splat map, matching ordinary screens.

## 8. Level Editor workflow

### 8.1 Opening the overworld

Add an **Overworld** tab alongside the existing **Levels** and **Deleted** tabs in the Level Editor browser. It loads `sourceLevelRoot/overworld.scr` as a normal editable document and mirrors saves to `runtimeLevelRoot/overworld.scr`.

Creating a brand-new generic document does not implicitly replace the overworld. If the overworld file is missing, the Overworld tab offers **Create Overworld** using the normal default screen scaffold.

### 8.2 Screen Selectors tool

Add a third editor tool next to Tiles and Mesh Decorations: **Screen Selectors**.

Behavior:

- clicking a valid board surface adds a selector at the corresponding entity cell;
- clicking an existing selector selects it;
- D + click or a Delete button removes it;
- duplicate-cell placement is rejected;
- placing, deleting, moving due to document expansion/layer insertion, and association edits each produce an undoable `DocumentSnapshot` change;
- selectors cannot be arbitrarily translated, rotated, or scaled;
- resizing that clips a selector removes it as part of the same undo record and reports the removal count;
- prepending rows/columns shifts selector X/Y exactly as it shifts decorations;
- inserting/deleting layers shifts selector Z consistently; deleting the selector's occupancy/support layer must either remove it or reject the edit with a clear status. The recommended policy is remove invalidated selectors within the same undoable transaction.

`Document`, `DocumentSnapshot`, equality/change detection, load, save, undo, and draft construction must all include selectors.

### 8.3 Association panel

When `overworld.scr` is loaded, show a **Screen Selector Associations** section:

```text
Selector 1   Level [Easy Plains]   Screen [Screen 1]
Selector 2   Level [Level 2]       Screen [Icy River]
Selector 3   [Unassigned]
```

Requirements:

- rows are sorted by stable selector ID;
- level and screen combos use `metadata.json` names with numbered fallbacks;
- choosing a level resets the screen to the first valid screen in that level;
- include Select/Focus and Delete actions;
- show warnings for unassigned targets, missing targets, duplicate targets, and selectors whose cell is no longer walkable;
- duplicate targets are allowed but explicitly identified because they share completion state;
- association changes mark the document dirty and are undoable;
- the panel is hidden or read-only for ordinary puzzle screens.

### 8.4 Level/screen insertion and deletion

Selector targets are numeric `LevelLocation`s, so existing project mutations must rewrite `overworld.scr` in the same `LevelProjectStore` transaction:

- insert level N: increment target levels `>= N`;
- delete level N: clear targets equal to N and decrement target levels `> N`;
- insert screen N in level L: increment target screens in L that are `>= N`;
- delete screen N in level L: clear exact targets and decrement later target screens in L;
- rename level/screen: no target rewrite;
- restore deleted level: apply the same insertion remap before restoring its numeric location.

Clearing a deleted target is safer than silently redirecting it to the screen that inherits the old number. The transaction validates the rewritten overworld and mirrors it with the renumbered project, so source and runtime cannot disagree.

## 9. Campaign, shell, and UI changes

### 9.1 Campaign/session policy

Refactor `CampaignSession` around `WorldLocation` and explicit transitions:

- restore/start overworld;
- enter selector target;
- complete active puzzle;
- resolve completion back to overworld;
- checkpoint the current context;
- query target completion/all-target completion.

Remove sequential-only state and outcomes:

- `completedLevelMoveCount_`;
- `levelRunFromStart_`;
- `pendingNextLevel_`;
- `ScreenAdvanced` and next-level selection;
- automatic `current_.screen++` and `current_.level++`.

The replacement completion result describes the completed `LevelLocation`, per-screen stats, and whether all overworld targets are now complete.

### 9.2 Title and options

The player-facing Level Select becomes redundant and bypasses the overworld, so remove it from the title/options flow. Direct level/screen launching remains available through Debug tools for development.

The title continues to provide New Game, Continue, Save Slots, Options, and Quit. Continue loads the saved world context. New Game enters the overworld.

If retaining Level Select is desired as an accessibility shortcut, it must use the same per-screen records and cannot be an unlock-gated alternative progression system. That would be a separate product decision; the recommended initial implementation removes it.

### 9.3 Completion overlays

Rename level-oriented types/text to screen-oriented equivalents. The normal overlay shows one screen's metrics. The game-complete overlay aggregates distinct selector targets rather than level totals. Remove Next Level, Back To Start, and To Level Select actions; replace them with Return to Overworld and To Title.

### 9.4 Preloading and music

- While in the overworld, preload all six flag models and the assets for selector targets opportunistically. To control memory, preload the overworld plus the nearest/hovered selector later if needed; the first implementation may merge all target requirements if the current content size is acceptable.
- While in a puzzle, preload the overworld requirements because it is the guaranteed next destination.
- Selected puzzles continue using `playMusicForLevel(target.level)`.
- Until a dedicated overworld music role is requested, the overworld should use the existing level 0 music/fallback rather than adding an unrelated new asset requirement.

## 10. Content pipeline and validation

`ContentPipeline::addLevels()` must explicitly stage and validate `levels/overworld.scr` in addition to contiguous `levelN/screenN.scr` content.

Production validation fails when:

- `overworld.scr` is missing;
- it contains no valid player start;
- selector IDs or cells are duplicated;
- a selector is unassigned;
- a selector target does not exist;
- a selector cell is out of bounds, unsupported, or not walkable;
- an ordinary puzzle screen contains selectors;
- any catalog screen has no selector in the overworld;
- any flag model/texture is absent from the manifest or its files/dependencies are missing.

The pipeline permits multiple selectors targeting the same screen. A screen with no selector is a production validation error.

`LevelProjectStore` must include the root overworld file when cloning, validating, preparing a runtime mirror, committing, and rolling back. Current behavior copies the root file into the source staging tree but omits it from the runtime mirror, so this must be changed deliberately.

Asset requirement tests that iterate all levels/screens must add the overworld and all six possible flag variants.

## 11. Error handling and edge cases

- **Missing overworld at startup:** log a content error and remain at the title; do not fall back to `level0/screen0`, because that silently restores the old progression model.
- **Missing selector target after a debug edit:** keep the overworld playable, render the selector red, disable activation, and surface the error in Debug UI/logs. Production staging rejects it.
- **Duplicate target:** allowed; all copies show the same state and overall completion counts the target once.
- **Solved screen removed from content:** retain its historical save record but ignore it for current all-target completion. This avoids destructive save rewriting.
- **New selector/target added after a save:** it reflects its target's sequential state and makes the game incomplete until solved.
- **Target changed after being solved:** the selector immediately reflects the new target's progress; completion is keyed to the screen, not selector ID.
- **Selector moved:** its ID and target remain stable.
- **Selector deleted:** its target's progress remains in the profile in case another selector points to it or it is restored later.
- **Player standing on selector during restart:** restart returns to the authored overworld player start like a normal screen.
- **Space pressed while moving/sliding/on a conveyor:** ignore the edge; require a new press once the world is idle. Do not queue a delayed transition that surprises the player later.
- **Several players on different selectors:** no activation. Every living player must occupy the same selector cell.
- **Screen solves on the same frame as other input:** completion wins; input does not leak into the restored overworld.
- **Save failure during entry/completion:** use the current save diagnostics behavior, but keep the in-memory transition valid. Immediate-save requests must occur at both boundaries to minimize rollback after a crash.
- **Editor draft:** selector transitions are disabled and never mutate progress.

## 12. Confirmed overworld-specific rules

The overworld is a normal rules board, but an authored End tile has no navigation destination. The confirmed policy is:

- all mechanics used to unlock and traverse an End tile continue to work;
- project/content validation rejects End tiles in `overworld.scr` so the overworld itself cannot emit a normal screen-solved event.

This keeps selector activation as the overworld's only exit and avoids an undefined “complete the overworld” transition.

The multi-player activation policy in section 5.3 is also confirmed: every living player must stand on the same selector cell before Space can activate it. The primary character alone cannot trigger a transition while another authoritative player is elsewhere.

## 13. Implementation sequence

Implement in vertical, testable stages:

1. **Selector data model and file format**
   - add `ScreenSelector`, parsing, validation, serialization, lookups, and Level tests;
   - create an initial `levels/overworld.scr` only after its map design is decided.

2. **Content and assets**
   - add manifest texture/models;
   - stage the overworld and validate selector targets;
   - extend asset requirements and content/manifest tests.

3. **Editor model**
   - include selectors in documents/snapshots/undo/save/load;
   - implement placement/deletion and coordinate maintenance;
   - implement atomic target remapping for project mutations.

4. **Editor UI and labels**
   - add the Overworld browser entry, Screen Selectors tool, association panel, warnings, and projected labels;
   - add headless interaction geometry tests and UI state tests where practical.

5. **Profile format and migration**
   - introduce screen progress, overworld/puzzle checkpoints, current context, format 18 migration, and slot-summary updates;
   - land codec/migration/store tests before changing runtime progression.

6. **Campaign and application flow**
   - replace sequential advance outcomes with enter/complete/return transitions;
   - preserve and restore overworld state;
   - gate selector interaction on an idle world;
   - update checkpointing, music, loading, logs, and preloading.

7. **Player-facing UI**
   - expose Confirm/Interact input in gameplay;
   - update title/options and completion overlays;
   - remove or deliberately redefine Level Select.

8. **Integration and regression pass**
   - add end-to-end tests for new game, selector entry, independent level chains, locked selectors, crash/resume in puzzle, solve/return, flag states, replay, duplicate targets, complete coverage, all-target completion, invalid edits, and old-save migration;
   - run the complete Debug build/test suite and visually verify all flag states and editor labels.

This order avoids a half-migrated runtime that can write a new save before the new codec and recovery paths are tested.

## 14. Expected code areas

The implementation is expected to touch at least:

- `src/engine/Level.*`
- `src/engine/CampaignSession.*`
- `src/engine/PlayerProfile.*`, `PlayerProfileCodec.cpp`, and `PlayerProfileMigrations.*`
- `src/engine/Application.*`
- `src/engine/InputRouter.*` and input/control labels
- `src/engine/RenderFrameBuilder.*`
- `src/engine/render/RenderAssetRequirements.*`
- `src/engine/LevelEditor.*`
- `src/engine/LevelEditorDebugUi.*`
- `src/engine/ApplicationTools.*` and `EditorInteraction.*`
- `src/engine/LevelProjectStore.*`
- `src/engine/ContentPipeline.*`
- `src/engine/ShellFlow.*`
- `src/engine/ui/TitleScreen.*`, `OptionsMenu.*`, and `LevelCompleteOverlay.*`
- `src/engine/SaveSlotManager.*`
- `assets/manifest.json`
- the corresponding CMake test targets and unit/integration tests

This list is intentionally broad: the current sequential assumption is distributed, and leaving one old path active can bypass or corrupt overworld progression.

## 15. Acceptance criteria

The change is complete when all of the following are true:

1. New Game starts in `overworld.scr`; Continue restores the saved overworld or active selected puzzle.
2. The overworld uses the normal gameplay/session/rules path and supports the same mechanics, undo, restart, animation, and checkpoints.
3. A selector has a stable ID, exact cell, and optional level/screen assignment stored in the overworld file.
4. Standing on a valid, available selector in an idle overworld and pressing Space loads exactly its assigned screen; unavailable selectors do nothing.
5. Solving that screen records per-screen progress, shows screen-complete UI, and returns to the preserved overworld instead of advancing sequentially.
6. Each level unlocks sequentially from screen 0, independently of every other level.
7. Playable, solved, and unavailable selectors use blue, green, and red respectively; the last screen of a level uses flag B and all others use flag A.
8. All six flag assets and dependencies are staged and preloaded correctly.
9. The Level Editor can open/save/mirror the overworld, place/delete/undo selectors, show `Selector N` labels, and edit assignments using named level/screen choices.
10. Inserting/deleting/restoring levels or screens remaps selector targets atomically and never silently retargets a selector to different content.
11. Old format-17 saves migrate without corruption; historical completions remain recognized; an old active screen can resume.
12. Save slots summarize selector-target completion and all-target completion correctly.
13. Missing catalog-screen coverage and other invalid selectors fail production content validation and degrade safely during Debug editing.
14. Player-facing Level Select no longer bypasses the overworld unless an explicit alternative design is approved.
15. Unit/integration tests cover parsing, editor operations, remapping, rendering choice, input gating, navigation, persistence, migration, and completion.
