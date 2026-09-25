# Packet C implementation evidence

Implemented September 25, 2026, on top of packet B. Verified in the same
2-core Linux container as packets A and B (clang 18 and GCC 13, Debug, with
lavapipe under Xvfb). No Windows or GPU run was possible from there.

## What changed

- **DI-12, drag painting, stroke undo, and redo.**
  - `LevelEditor::beginStroke`/`endStroke` open and close a stroke. While
    it is open, `recordDocumentChange` counts edits instead of recording
    them, and `endStroke` writes one `EditActionRecord` from the snapshot
    taken at `beginStroke`. Undo, redo, saving, draft playback and document
    switches close an open stroke first.
  - `ApplicationTools` starts a stroke on mouse-down in the Tiles tool.
    While the button is held, it fills the gap between frames with a
    Bresenham line (`EditorInteraction::gridLine`), so a fast drag stays
    connected.
    - Each board column is edited at most once per stroke. Holding still
      does not stack tiles or erase down through layers.
    - Every column resolves its own target, the same cell a click there
      would edit. `D` and `R` held at mouse-down make the stroke delete or
      replace.
    - Shift constrains the stroke to the anchor's row or column
      (`EditorInteraction::constrainToAxis`).
    - Only the first cell may grow the board. If it does, the stroke
      closes, because growing the board shifts every coordinate.
    - If the pointer leaves the board mid-drag, the stroke resumes at the
      re-entry cell instead of drawing a line across the board.
  - Redo stack.
    - `tryUndoEdit` moves the record to the redo stack, and `tryRedoEdit`
      re-applies it. Any newly recorded edit clears the stack.
    - Redo is carried in the per-document draft cache and remapped by
      `applyScreenIdentityRemaps`, exactly like undo.
    - `setCell`, `paintCell` and `eraseCell` now return whether the
      document changed. `moveObject` uses this with a recording guard
      instead of truncating the history vector, so a failed move leaves
      redo intact.
  - Not built: the optional rectangle mode. Move stays click-twice. The
    panel has Undo and Redo buttons.
- **DI-13, shortcuts and the play/edit round trip.**
  - `InputRouter::EditorInput` gains redo, save, layer up/down, cycle tool,
    layer lock, recent-tile slot, pick modifier (Alt), and line constraint
    (Shift). `Frame` gains `toggleDraftPlaybackPressed` and
    `playDraftFromCursorPressed`.
  - Shortcuts are suppressed while ImGui owns the keyboard (a focused text
    field or panel) and while the options or title shell is open. Held
    Alt, Shift and Ctrl state reaches the input state even while a panel
    has focus. (The first version also switched letter shortcuts off
    while Ctrl was held; chord matching in the follow-up below replaces
    that.)
  - `F5` in the editor plays the draft. While a draft plays, `F5` returns
    to the editor directly. `Esc` still asks first.
  - `Shift+F5` calls `beginDraftPlayback(topology, heroStart)`. On a
    puzzle screen, playback lifts the draft's first hero in scan order,
    which is the one the level starts controlling, and places it on the
    cell a click would fill in the hovered column. The document is not
    changed. Off-board cells, occupied cells and overworld screens are
    refused with a status message. The composed overworld allows exactly
    one Player tile across all screens, so moving it would mean editing a
    different screen.
  - `Ctrl+S` (`saveLoadedDocument`) saves to the file the document was
    loaded from and refreshes the panel's Path field. A never-saved
    document reports that it needs a path. While ground painting is open,
    `Ctrl+S` saves the splat map instead.
  - Eyedropper (`pickTile`): Alt+click selects the top tile in the column,
    or the tile on the active layer when the layer is locked.
  - Recent tiles.
    - The strip holds up to nine tiles and starts with the default brush.
    - A tile keeps its slot until newer choices push it out. Number keys
      therefore stay stable while you alternate between tiles already in
      the list.
    - The strip is shown at the top of the Tiles palette.
  - `PageUp`/`PageDown` (`stepActiveLayer`), `L` (`toggleLayerLock`) and
    `Tab` (`cycleTool`) report their result in the status line.
- **Rebindable shortcuts (follow-up, same day).** The first version used
  fixed keys. At the owner's request they became rebindable, and old saves
  are no longer migrated:
  - 22 new `InputAction`s: eyedropper and straight-drag (held), redo,
    save, play/stop draft, play from cursor, layer up/down, layer lock,
    next tool, the three gizmo modes, and recent tiles 1-9. The first
    version's defaults are kept.
  - Keyboard bindings can be chords (`KeyboardBinding::modifiers`,
    Ctrl/Shift/Alt, either side). Among the bindings on one key whose
    modifiers are held, only the most specific fires. So `Ctrl+S` saves
    without moving down or switching the gizmo, and `Shift+F5` does not also
    press `F5`, while `Ctrl+Z` still undoes.
  - Capture records a chord when its key goes down. A modifier on its own
    (for example, Left Ctrl as the eyedropper) is captured on release.
    Binding rows show chords as text, such as "Ctrl+Shift+Z", because there
    is no single glyph.
  - `inputActionContext` replaces the editor-only list. Editor actions may
    share gameplay keys. Undo, Back and Play/Stop Draft are live in both
    contexts and conflict with both.
  - Options > Controls > Editor Controls has Editing, Playtest and Recent
    Tiles tabs and uses the compact Controls-page metrics.
  - Player profile format 33. `PlayerProfileMigrations.cpp` (31 steps,
    about 1,100 lines) and the pre-split settings bootstrap are gone.
    `decodePlayerProfile` throws `ObsoletePlayerProfileFormat` for older
    formats. `SaveStore` renames every older-format artifact to
    `.obsolete-format-<N>-<stamp>` before loading, and returns
    `SetAsideObsolete` with defaults. `inspect()` reports such a slot as
    empty. A save over an obsolete primary sets it aside instead of backing
    it up. Newer formats are still preserved untouched.
- **Packet B follow-up.** New workspace tabs such as Tuning and Shaders
  were meant to dock beside the existing tabs. With an `imgui.ini` from
  before packet B, the first frame still created them floating over the
  game view, because no window existed yet to copy a dock from. The dock
  is now read from the loaded settings as well. A window already saved as
  floating stays where it was saved (**Layouts > Reset** docks it).

## Measurements

| Scenario | Before | After |
| --- | --- | --- |
| Paint a wall of eight tiles | 8 clicks, 8 undo records, no redo | One drag; one `Z` removed all eight and one `Y` restored them (live run) |
| Test the level being edited | Play Draft button, then Esc, modal, Stop Testing | `F5`, then `F5` (live run; no modal) |
| Test one spot of a large level | Play, then walk there | `Shift+F5` over the spot (live run; hero started on the hovered cell, document unchanged and still clean) |
| Save | Mouse to the panel, Save button | `Ctrl+S` (live run; file on disk updated, panel shows "clean") |

Live runs used the Debug game on lavapipe under Xvfb, driven with
`xdotool`. Software rendering there takes about two seconds per frame, so
the pointer had to dwell on each cell. At normal frame rates the per-frame
line fill is what keeps fast drags connected; the `editor_interaction`
tests cover it.

## Validation

| Check | Result |
| --- | --- |
| GCC 13 Debug, `-DSOKOBAN_WARNINGS_AS_ERRORS=ON` | Built; 83/83 CTest |
| clang 18 Debug, full Vulkan | Built with no warnings; 83/83 CTest |
| `headless-tests` preset | 76/76 CTest |
| GCC 13 Release game (developer tools compiled out) | Built |
| `level_editor` | 482 → 598 checks. New tests: a stroke is one undo step and redo replays it; an empty stroke records nothing; undo mid-stroke; a new edit abandons redo; redo survives draft switching and a failed move; redo follows screen renumbering; eyedropper, recent tiles and tool cycling; play from cursor (moves the first hero, stacks on walls, refuses off-board cells, leaves the document untouched); `Ctrl+S` and layer stepping and lock |
| `input_router` | 80 → 121 checks in the first version. Every shortcut, keyboard-capture suppression, F5 and Shift+F5 in edit and play, modifier keys passing panel capture |
| `editor_interaction` | 3486 → 3541 checks. Line continuity and inclusivity in all octants; axis constraint |
| Live game run | Drag, stroke undo and redo, `Ctrl+S`, Alt+click, `F5` both ways, `Shift+F5`, `PageDown`, `Tab`. Tuning and Shaders docked on first launch with an old `imgui.ini` |
| clang-tidy 18 on changed files | No findings in new code; the remaining findings are pre-existing |
| Follow-up: rebindable shortcuts | GCC `-Werror` and clang builds, 83/83 each; `headless-tests` 76/76. `input` adds chord specificity, capture, and context tests. `input_router` adds rebinding tests. `ui` covers the Editor Controls tabs and chord capture. `player_profile` replaces the migration tests with chord round-trip and obsolete-save tests. `save_slot_manager` checks that obsolete slots and settings start fresh |
| Follow-up, live run | Editor Controls page rendered all three tabs. Save was rebound to `Ctrl+D` through the menu, written to `settings.json` as `{"control":"D","modifiers":["ctrl"]}` (format 33), and `Ctrl+D` then saved the level. Launching over format-32 files logged "Player profile from an older build (format 32) was set aside; starting fresh." and left both files renamed |

## Not verified here

- Windows/MSVC builds of these changes, and the shortcuts on a Windows
  keyboard layout. The code uses SDL scancodes, which are positional.
- Drag feel at normal frame rates on a GPU.

## Noticed, not changed

- `iso_scene_preparer` fails about 1 run in 30 under parallel CTest:
  `IsoScenePreparerTests.cpp:207` checks `executedTaskCount() == 1` right
  after the work finishes. `TaskSystem::workerLoop` increments that
  counter after the task body returns. The task has already signalled
  completion by then, so the check can observe 0. The code is unchanged
  since the review baseline. Counting before `task()` runs, or having the
  test wait for the counter, would fix it.
