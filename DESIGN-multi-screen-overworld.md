# Multi-Screen Overworld Design and Implementation

Status: implemented. The runtime composes separately authored screens into one overworld, renders the active screen and its surrounding 3x3 neighborhood, and moves the camera between screen centers during ordinary cross-screen movement. The editor can add, move, edit, delete, restore, and save screens. Screen seams are implicit: no connection records or connection-editing tools exist.

## 1. Summary

The overworld is a spatial map of separately authored, equal-sized screens. Each screen has a stable ID and occupies an integer `(x, y)` slot. Cardinally adjacent slots share a physical boundary. Diagonal screens are visible but do not share movement cells.

At load time, every screen definition is translated into one global coordinate system and composed into one normal `Level`. A move across a screen boundary is therefore an ordinary one-cell move governed by the existing rules engine. If facing cells on two cardinally adjacent screens are walkable, the boundary is traversable. If normal tile, support, entity, or movement rules block the move, it is not traversable. There is no separate connection permission or metadata layer.

The active screen is derived from the living players' global cells. When movement changes that ownership, the camera interpolates from the old screen center to the new one during the same action. Rendering includes the active screen and every existing cardinal or diagonal neighbor in its surrounding 3x3 slot region.

Puzzle selectors remain inside component screen files. All selectors for one puzzle level must be owned by one overworld screen, while different puzzle levels may be placed on different overworld screens.

## 2. Goals

- Author an arbitrarily shaped overworld from separately editable screens.
- Place screens in a two-dimensional slot layout.
- Traverse cardinal seams through normal movement and collision rules.
- Avoid authored connection records, endpoint markers, pairing tools, and special seam logic.
- Animate the camera during a cross-screen action.
- Render the active screen and all existing screens in its surrounding 3x3 neighborhood.
- Preserve ordinary movement, pushing, conveyors, ice, mirrors, undo, restart, and checkpoint behavior across boundaries.
- Keep screen identity stable when screens move or other screens are added.
- Validate that every screen is reachable from the authored start through at least one chain of implicit walkable seams.
- Enforce one overworld-screen owner per puzzle level.

## 3. Non-goals

- Direct diagonal traversal. Diagonal screens are visible only.
- Portals between nonadjacent slots.
- Rotated or mirrored screen coordinate systems.
- Screens with different horizontal dimensions.
- Independent simulation clocks for off-screen screens.
- Unbounded disk streaming in the first implementation.
- A player-facing map editor; authoring remains in the Debug Level Editor.

## 4. Core architectural choice

The runtime uses one composed `Level` and one `GameplaySession`, not one session per screen. Switching sessions at a seam would turn one move into a load/teleport operation and would complicate pushing, automatic movement, animation, undo, and snapshots. Composition lets the existing rules see the boundary cells as ordinary adjacent global cells.

Screen identity is metadata over the composed board. It is used by the camera, visibility, per-screen ground materials, persistence, selector ownership, and editor. It does not replace the normal rules grid.

## 5. Identity and layout model

```cpp
using OverworldScreenId = uint32_t;

struct OverworldSlot {
    int x = 0;
    int y = 0;
};

struct OverworldPosition {
    OverworldScreenId screen = 0;
    GridPosition3 cell;
};
```

IDs are positive and stable. Filenames derive from IDs but IDs do not need to be contiguous. Slots and referenced filenames are unique.

`LevelLocation` remains exclusively a puzzle `(level, screen)` identity. A selector's overworld owner is the stable screen ID of the component containing it.

## 6. Layout format

`levels/overworld/layout.json` uses strict format 2:

```json
{
  "format": 2,
  "screenSize": [12, 8],
  "start": {
    "screen": 1,
    "cell": [6, 4, 1]
  },
  "screens": [
    { "id": 1, "file": "screen1.scr", "slot": [0, 0] },
    { "id": 2, "file": "screen2.scr", "slot": [0, -1] },
    { "id": 3, "file": "screen3.scr", "slot": [1, 0] },
    { "id": 4, "file": "screen4.scr", "slot": [0, 1] },
    { "id": 5, "file": "screen5.scr", "slot": [-1, 0] }
  ]
}
```

There is deliberately no `connections` property. Strict parsing rejects it as unknown, and format 1 layouts are rejected rather than ambiguously reinterpreted. Serialization writes screens in ascending ID order, rejects unknown properties and unsafe paths, and uses safe integer coordinates.

The start cell is local to its screen and is the only cell-level topology metadata. Component files do not contain a Player or End tile; the loader injects the single player at the translated start.

Every component has the declared width and height. Depth may differ; absent higher layers compose as Air. All screens must agree on water-layer metadata.

## 7. Spatial model and implicit seams

For a screen at slot `(sx, sy)`, its unnormalized global origin is:

```text
origin.x = sx * screenWidth
origin.y = sy * screenHeight
```

The loader applies one runtime-only normalization offset so composed coordinates are non-negative. Moving the whole authored map does not rewrite component content.

Two screens can be traversed directly only when their slots are cardinally adjacent. Their facing boundary cells then become Manhattan-adjacent in the composed level. Each matching orthogonal coordinate and Z layer is an independent potential crossing.

A boundary pair is an implicit seam exactly when both cells are walkable under the composed level's ordinary static walkability query. Gameplay still applies the complete rules when an entity attempts the move, so temporary entities and action-specific constraints remain authoritative. Multiple walkable pairs naturally create multiple crossings. Walls, unsupported Air, water, and other non-walkable geometry close individual portions of a boundary.

No endpoint uniqueness, colored pairing, connection ID, or undeclared-seam check exists. Editing ordinary boundary tiles is how authors open or close a seam.

Topology rules:

- screen IDs, slots, and active filenames are unique;
- referenced files exist and parse;
- every screen is reachable from the start screen through implicit walkable boundary pairs;
- diagonal adjacency is visual only;
- sparse layouts and holes are valid;
- cardinal neighbors may have a fully blocked shared edge, provided both remain reachable through another route;
- configured span, depth, and total-cell limits bound composition allocations.

## 8. Loading and composition

Loading proceeds as follows:

1. Strictly parse format-2 `layout.json` and validate identities, paths, slots, dimensions, and start metadata.
2. Parse every referenced `.scr` as a `Level::Definition`.
3. Require common dimensions and water metadata; reject component Player and End tiles.
4. Compute origins, map bounds, and the normalization offset.
5. Translate tiles, decorations, selectors, and water behavior into one composed definition.
6. Inject the Player at the translated start and construct one normal `Level`.
7. Build slot-to-screen and global-cell ownership lookups.
8. Inspect cardinal neighbors' facing cells using `Level::isWalkable` and build the derived reachability graph.
9. Reject a map if any screen is unreachable from the start through that graph.
10. Compute a deterministic fingerprint from format, dimensions, start, screen IDs/slots/files, and normalized component definitions.

The derived seam graph is validation/navigation metadata only. It does not intercept gameplay moves; `GameplaySession` and the regular rules remain authoritative.

## 9. Runtime behavior

The campaign loads the composed level into the existing session. Before admitting a projected action, the overworld policy requires every living player to remain owned by the same screen. Dead players do not affect ownership. This preserves the single-active-screen camera and visibility invariant without duplicating movement rules.

For a crossing action:

- the source screen comes from the committed state;
- the destination screen comes from the projected state;
- the camera center interpolates between those screen centers using the action's presentation progress;
- the visible region is the union of source and destination 3x3 neighborhoods while movement is in flight;
- after commit, the destination becomes active.

Camera framing uses a fixed 3x3-screen footprint so adding or removing neighbors does not change zoom. Missing slots contribute no geometry but still occupy their expected framing space.

## 10. Rendering

Frame construction emits geometry only for screens in the visible neighborhood, while simulation retains the whole composed level. World coordinates remain global, so adjacent surfaces meet without a portal effect.

Each screen keeps its own ground splat map. Render data associates a stable screen ID and global bounds with that screen's resolved textures. Material UVs use region-local coverage while world-space material sampling remains continuous.

Water and shoreline queries use composed tile geometry. Compatible water across an implicit seam has no artificial internal shoreline. Asset requirements include the source/destination neighborhood union during a transition.

## 11. Persistence

The profile stores a typed overworld checkpoint:

```cpp
struct OverworldCheckpoint {
    uint64_t topologyFingerprint = 0;
    OverworldScreenId activeScreen = 0;
    GameplaySession::Snapshot session;
};
```

The fingerprint includes screen layout, start, and component content. Because implicit seam state comes from ordinary component geometry, changing a boundary tile changes the fingerprint without needing separate topology metadata.

If the fingerprint, snapshot, active screen, or living-player ownership is invalid, only the overworld checkpoint is discarded and rebuilt from the authored start. Puzzle progress and settings remain intact.

## 12. Puzzle-selector ownership

After selector targets and production coverage are validated, group selectors by target puzzle level. Every puzzle level must have exactly one owning overworld screen:

```text
owners[level] = set of overworld screen IDs containing selectors for level
require owners[level].size() == 1
```

Selectors for different levels may share an overworld screen or live on separate screens. Duplicate selectors for puzzle screens remain allowed as long as the whole puzzle level has one owner. Historical progress remains keyed by puzzle `LevelLocation`, so moving a level's selectors together does not change progression identity.

## 13. Editor design

`OverworldMapEditor` owns the complete in-memory layout and component draft, selection, stable-ID allocation, screen add/move/delete/restore, start-cell editing, topology undo/redo, validation, and transactional save. `LevelEditor` continues to edit one selected component's tiles, decorations, and selectors.

The Overworld Map panel provides:

- a scrollable spatial slot canvas with one card per screen;
- selected-screen `+N`, `+E`, `+S`, and `+W` controls;
- immediate creation of an all-Ground layer 0 with Air above;
- screen open, move, delete, restore, undo, redo, and validated save actions;
- start-screen markers and selector/diagnostic summaries.

Adding a cardinal neighbor is immediate. There is no connection picker or confirmation step. The new screen can be opened and edited at once, including before its first save. The default ground boundary is walkable, so adjacent newly created screens are reachable immediately.

While editing an overworld component:

- dimensions are locked to the map;
- Player and End tiles are disabled;
- selectors and decorations remain screen-local;
- only the map start is protected and shown as a 3D metadata overlay;
- ordinary edge tile edits directly open or close implicit crossings;
- a component save validates the complete composed draft before replacing source or runtime files.

Draft play composes all in-memory screens plus the unsaved active component and uses the real movement, camera, neighborhood, and material paths. Selector activation is disabled so playtesting cannot modify campaign progress.

## 14. Validation

Structural validation shared by runtime loading, editor transactions, and content staging requires:

- strict format-2 layout syntax with no legacy connection property;
- unique identities, slots, and paths;
- referenced, parseable, dimension-compatible components;
- valid start metadata and no component Player/End tiles;
- common water metadata;
- complete reachability through derived walkable seam pairs;
- valid selectors, decorations, and allocation limits;
- successful construction of the composed `Level`.

Production validation additionally requires assigned selector coverage and the one-overworld-screen-per-puzzle-level invariant.

Useful diagnostics identify the screen and cause, for example: `screen 9 is unreachable: no path of facing walkable boundary cells from the start`.

## 15. Implementation phases

1. **Topology and composition:** strict layout types, coordinate translation, ownership queries, selector identity, implicit seam reachability, and tests.
2. **Persistence and campaign state:** topology fingerprints, typed checkpoints, migration, and active-screen restoration.
3. **Movement and camera:** projected-action ownership, crossing transitions, fixed 3x3 framing, and visibility union.
4. **Rendering and assets:** neighborhood culling, per-screen splat regions, water seam handling, and production staging.
5. **Content migration:** convert the old single overworld to a one-screen format-2 layout.
6. **Editor topology:** spatial canvas, immediate cardinal add controls, all-ground defaults, move/delete/restore, start picker, draft composition, and transactional saves.
7. **Polish and content authoring:** build the intended multi-screen production world and playtest visual readability at crossings.

## 16. Test plan

Automated coverage includes:

- canonical format-2 layout round trips;
- rejection of format 1 and the legacy `connections` property;
- global/local translation with negative slots;
- composition and ordinary gameplay movement across an implicit seam;
- blocked or diagonal boundaries not creating reachability;
- action admission and undo across screens;
- camera interpolation and fixed neighborhood framing;
- selector coverage and one-screen ownership per puzzle level;
- editor add/move/delete/restore/start/undo/redo flows;
- all-Ground/Air defaults for new screens;
- unsaved topology and component draft play;
- transactional failure leaving source and staged content unchanged;
- production staging of only referenced components and assets.

## 17. Acceptance criteria

The feature is complete when:

- production loads through `layout.json` and one composed session;
- adjacent walkable boundary cells can be crossed using normal game mechanics without connection metadata;
- blocked boundary cells cannot be crossed;
- the camera transitions during the same action and the destination becomes active;
- cardinal and diagonal neighbors are visible;
- checkpoints restore safely by stable screen ID and content fingerprint;
- the editor can independently create, open, edit, position, delete, restore, and save screens;
- new screens default to Ground with Air above;
- authors open and close seams solely by editing ordinary edge tiles;
- one puzzle level cannot be split across overworld screens;
- the complete automated suite and production-content stage pass.

The guiding principle is: **screens are authoring and presentation regions; the overworld is one physical rules space, and its seams are implicit consequences of ordinary walkability.**
