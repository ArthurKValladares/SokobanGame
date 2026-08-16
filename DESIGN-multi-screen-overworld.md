# Multi-Screen Connected Overworld Design

Status: implementation in progress. Topology/composition, format-20 persistence/campaign state, runtime action admission, composed-map application loading, committed screen changes, synchronized fixed-3x3 camera movement, neighborhood-gated frame construction, per-screen splat materials, production staging, the initial content migration, and the core editor topology phase are implemented. The editor now authors start/connection cells directly in the 3D view and Play Draft composes both unsaved topology and the unsaved active component. Authoring production multi-screen content and full visual crossing validation remain.

Implemented foundation (2026-08-16):

- `OverworldMap.*` defines stable screen IDs, slots, endpoints, connections, strict versioned layout JSON, canonical serialization, topology limits, and selector ownership validation.
- Separately authored component definitions compose into one normal `Level`, with translated decorations/selectors, a single injected start, global/local coordinate lookups, visible-neighborhood queries, and a deterministic content fingerprint.
- Validation covers common dimensions/water metadata, forbidden component `C`/`E` tiles, cardinal endpoint geometry, unique endpoint use, reachability, supported walkable endpoints, and undeclared traversable seams.
- `OverworldMapTests.cpp` exercises canonical I/O, composition, real `GameplaySession` movement/undo across a seam, negative slot normalization, invalid topology, fingerprint stability, selector coverage, and the one-overworld-screen-per-puzzle-level invariant.
- Player profile format 20 replaces the untyped single-overworld snapshot with a checkpoint containing the topology fingerprint, stable active overworld-screen ID, and exact gameplay snapshot. Format-19 migration preserves puzzle progress/settings and deliberately discards only the unsafe legacy overworld snapshot.
- `CampaignSession` owns configured overworld topology identity and active-screen state, validates checkpoints against current screen IDs/fingerprint, restores stale checkpoints to the authored start, persists typed checkpoints at normal save boundaries, and exposes shared-player-screen/committed-transition helpers for the upcoming runtime crossing path.
- `GameplaySession` accepts an optional projected-state admission invariant. The composed runtime rejects a player action, automatic movement, mirror, undo, or restart result that would leave living players split across authored screens, before the scheduler owns the action.
- `OverworldView.*` derives a fixed three-screen-by-three-screen camera extent, source/destination neighborhood union, and sub-cell camera translation from committed/projected player ownership and the same interpolated player position used by presentation. `RenderFrameData::cameraOffset` carries that translation through `RenderFrameBuilder` into `IsoScenePreparer` without integer snapping or zoom pumping.
- `RenderFrameBuilder::GameplayInput::visibleCell` gates composed static tiles, water cells/edges, ladder faces, decorations, selectors, and gameplay actors to the settled 3x3 neighborhood or the source/destination union while moving.
- `RenderFrameData` carries a bounded ground-splat region table. `RenderFrameBuilder` resolves stable-ID texture names for visible overworld screens, and `VulkanSceneRecorder` selects a region per ground face and supplies region-local splat coordinates while preserving global material-layer UV continuity.
- `Application` automatically loads and composes `levels/overworld/layout.json` when present, configures the real topology fingerprint and screen catalog, installs action admission, restores the composed checkpoint, updates the active screen after an action commits, and feeds the overworld view to rendering. When the layout is absent, transitional fingerprint `0`/screen `1` keeps the legacy `levels/overworld.scr` path and existing saves working.
- `ContentPipeline` validates and stages `levels/overworld/layout.json` plus every referenced component, applies production selector ownership/coverage validation, and retains the root-file path only as a compatibility fallback. `make_ground_textures.py` discovers stable overworld screen IDs and checks their manifest entries.
- Production content now uses a one-screen composed layout (`screen ID 1`) with the former start moved into layout metadata and the existing selectors unchanged. Its dedicated 9x7 splat map is `GroundSplatMapOverworld1`.
- `OverworldMapEditor.*` owns the in-memory topology draft, selection, stable-ID connected-screen creation, disconnected moves, start/connection editing, soft delete/restore, undo/redo, and rollback-capable saves. Its Debug UI presents a spatial slot canvas, connection lines, screen cards, and the corresponding commands.
- `LevelProjectStore` validates structural composed-map rules and selector targets before atomically replacing source/runtime trees. Puzzle renumbering rewrites selectors across every component, while runtime mirroring includes only the active layout and referenced screens—not soft-deleted authoring files.
- `LevelEditor` recognizes only layout-referenced component paths, locks shared dimensions, blocks `C`/`E`, protects start/connection cells and their supports, rejects puzzle levels owned by another overworld screen, previews/paints stable-ID splat maps, and exposes start/connection overlays. Component saves validate the entire map before touching source or runtime.
- Draft play composes the complete in-memory `OverworldMapEditor` layout/component set, substitutes the unsaved active `LevelEditor` component, and uses the real action-admission, fixed-camera, visible-neighborhood, and per-screen-splat runtime path while selector activation remains disabled. Brand-new screens can therefore be crossed in draft play before their files are saved.
- `OverworldMapTests.cpp` also covers pre-scheduler rejection, living-player ownership, fixed 3x3 framing, half-transition camera progress, and actual isometric camera translation. The full 49-test Debug suite passes, production staging emits 167 reachable files, and an application startup smoke test reaches normal Vulkan initialization with the composed map and editor integration.

The shipped/staged content now exercises the composed path by default, but contains only one overworld screen. Real cardinal crossings and diagonal-neighbor presentation still require additional authored screens before they can be visually validated end to end.

This design builds on the existing playable overworld and screen-selector system described in `DESIGN-overworld-screen-selectors.md`. In this document, **overworld screen** means one spatial chunk of the overworld. **Puzzle screen** means a playable `levelN/screenM.scr` selected by a flag. Keeping those terms distinct is important because both are currently called “screens” in parts of the codebase.

## 1. Summary

Replace the single `levels/overworld.scr` board with a connected map of separately authored overworld screens. Each overworld screen occupies one cell in a two-dimensional layout. A screen can have cardinal connections to screens in the north, east, south, and west layout cells. While a screen is active, it and every existing screen in the surrounding 3x3 layout neighborhood are rendered, including diagonal neighbors.

The recommended runtime model is **one composed overworld**, not a sequence of unrelated `GameplaySession`s. The individual files are authoring chunks. At load time they are translated into a shared global tile coordinate system and composed into one `Level` and one `GameplaySession`. This gives boundary movement the same semantics as an ordinary one-tile move and preserves normal pushing, conveyors, ice, automatic steps, animation, undo, restart, and the overworld checkpoint without a second rules engine.

An explicit connection joins one boundary occupancy cell on a screen to the immediately adjacent boundary occupancy cell on a cardinal neighbor. For example, moving south from the marked connection cell on the central screen enters the matching north-edge cell of the red screen. That move changes the active overworld screen. During the same movement animation, the camera translates from the source screen’s center to the destination screen’s center. Once settled, the destination is active and its surrounding 3x3 neighborhood is the visible set.

Puzzle selectors remain authored inside overworld screen files. Production validation adds this invariant:

> Every puzzle screen belonging to one puzzle level must be selected from one and only one overworld screen.

Different puzzle levels may be assigned to different overworld screens, and more than one puzzle level may share an overworld screen. Existing coverage remains required: every puzzle screen in the catalog must have at least one selector.

## 2. Goals

- Author an arbitrarily shaped overworld from multiple separately editable screens.
- Place screens north, east, south, west, and diagonally around one another in a spatial layout.
- Traverse cardinal connections with a normal-looking one-cell movement.
- Translate the camera during the crossing movement and make the destination screen active.
- Render the active screen plus all existing cardinal and diagonal neighbors.
- Preserve normal overworld rules, animation, undo, restart, checkpoints, selectors, and puzzle return behavior.
- Let the Debug Level Editor create, position, open, edit, connect, disconnect, and delete overworld screens.
- Keep each overworld screen’s splat map and decorations independent.
- Validate topology, connections, selector coverage, and the one-overworld-screen-per-puzzle-level rule in both project transactions and production staging.
- Migrate the current single overworld into a one-screen map without changing its selector targets.

## 3. Non-goals for the first implementation

- Direct diagonal traversal. Diagonal screens are visible, but movement connections are cardinal only.
- Arbitrary rotation or mirroring of an overworld screen. Every screen shares the world axes.
- Screens with different horizontal dimensions. A map has one width and height used by every screen.
- Portals between non-neighboring layout positions. A connection is a physical seam, not teleportation.
- Streaming an unbounded world from disk. The first implementation loads and simulates the whole composed overworld; rendering is restricted to a neighborhood.
- Independent simulation clocks for off-screen screens.
- A polished player-facing map editor. The existing Debug ImGui editor remains the authoring UI.
- Entering puzzle selectors during an unsaved overworld draft. Draft play can test movement and camera connections without mutating player progression.

These constraints deliberately make the first version spatially coherent and testable. Non-cardinal portals, rotated chunks, and streaming can be added later without changing the authored screen identity model.

## 4. Current architecture and the largest challenges

The existing implementation already has many of the required pieces, but almost all assume exactly one loaded `Level`:

- `Application::overworldPath()` resolves one `levels/overworld.scr` file.
- `Application::loadCurrentScreen()` loads either that file or one puzzle screen into `level_`.
- `GameplaySession` owns one `GameState`, action scheduler, undo chain, and snapshot for that `Level`.
- `RenderFrameBuilder::GameplayInput` accepts one `Level`, one state, one frame-wide ground splat selection, and one integer camera-fit extent.
- `IsoScenePreparer` derives a camera immediately from that extent. Camera pitch is animated, but camera position and fit are not stateful transition tracks.
- `PlayerProfile::overworldSession` stores one raw `GameplaySession::Snapshot` with no map identity, active overworld screen, or topology fingerprint.
- `CampaignSession` aggregates selector targets from one level and has no spatial overworld location.
- `LevelEditor::editingOverworld()` recognizes the exact `overworld.scr` filename.
- `LevelProjectStore` and `ContentPipeline` special-case one root overworld file.

The most difficult parts are therefore not file enumeration. They are the boundaries between systems.

### 4.1 One movement must cross two authored files

Resetting `GameplaySession` at the seam would split one movement into save/load/teleport operations, clear or splice undo history, reset automatic motion, and require a special presentation bridge. It also makes pushing a movable through a connection, undoing back across it, or sliding across it fundamentally different from ordinary rules.

The design avoids that by composing authored chunks before gameplay. The rules see one global board and the two connection cells are ordinary adjacent cells.

### 4.2 The camera needs an explicit animated view

The current integer `CameraExtent` is an authored fit box, not an animation state. Replacing it with the destination extent on the crossing frame would snap. Expanding it to both screens would zoom rather than translate.

The renderer needs an explicit floating-point camera view containing a center and a stable fitted footprint. The overworld camera controller animates only the center between fixed screen-slot centers. Puzzle screens continue using the current camera-fit path.

### 4.3 Rendering neighbors conflicts with a frame-wide splat map

Ground splat selection is currently frame-wide. A multi-screen frame can contain up to nine independently painted overworld screens, so ground instances need a screen-local splat binding and local UV bounds. Reusing one global map would either stretch one screen’s paint over the neighborhood or require baking a large atlas every time the layout changes.

The recommended change is per-ground-region material data, not an offline atlas.

### 4.4 Authored chunks are not independently valid gameplay levels

`Level::loadFromDefinition()` requires exactly one `C` player start. A component screen should not add a new runtime player. The map has one global start cell stored in its layout metadata; component `.scr` files contain no `C`.

`Level::parseDefinition()` already separates syntax parsing from construction of a valid playable `Level`. The overworld loader should expose a file-backed definition loader, parse all chunks as definitions, inject one player at the map start, and validate only the composed result as a gameplay `Level`.

### 4.5 Stable identity cannot depend on file renumbering

The active overworld screen is persisted and connections reference screens. Renumbering `screen2.scr` because a new screen was inserted before it would rewrite layout references and invalidate saves for no semantic reason.

Overworld screens therefore use stable positive IDs. Their files are named from those IDs and are not made contiguous. Puzzle screen numbering remains unchanged.

### 4.6 Multiple players can make “active screen” ambiguous

Mirrors can create several authoritative players. The existing selector rule already requires every living player to occupy one selector. For spatial transitions, an action that would leave living players in different overworld screens must be rejected. A screen transition commits only when every living player’s projected destination belongs to the same destination screen.

This keeps camera ownership, visible-neighborhood selection, selector interaction, and the saved active screen deterministic.

## 5. Content layout and identity

Store the new overworld under a dedicated directory:

```text
levels/
  overworld/
    layout.json
    screen1.scr
    screen2.scr
    screen7.scr
  level0/
    screen0.scr
    screen1.scr
  level1/
    screen0.scr
```

Overworld screen IDs are positive `uint32_t` values and are stable for the lifetime of the screen. Gaps are valid. A new screen receives `max(existing IDs) + 1`; deleting screen 2 does not rename screen 7. A restored soft-deleted screen keeps its ID when it is still available and receives a new one only on collision.

Introduce explicit types rather than reusing puzzle `LevelLocation`:

```cpp
using OverworldScreenId = uint32_t;

struct OverworldSlot {
    int x = 0;
    int y = 0;
    bool operator==(const OverworldSlot&) const = default;
};

struct OverworldScreenLocation {
    OverworldScreenId screen = 0;
};

struct OverworldSelectorId {
    OverworldScreenId screen = 0;
    uint32_t localSelector = 0;
};
```

`LevelLocation` remains exclusively `(puzzle level, puzzle screen)`. Avoid a variant in hot rules code; the composed overworld is still a `Level`. `OverworldScreenId` is navigation/render/editor metadata layered over it.

## 6. Layout file

Use strict versioned JSON in `levels/overworld/layout.json`:

```json
{
  "format": 1,
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
  ],
  "connections": [
    {
      "a": { "screen": 1, "cell": [11, 3, 1] },
      "b": { "screen": 3, "cell": [0, 3, 1] }
    },
    {
      "a": { "screen": 1, "cell": [5, 7, 1] },
      "b": { "screen": 4, "cell": [5, 0, 1] }
    }
  ]
}
```

Coordinates inside a connection and the start record are local authored occupancy cells. `z` is the entity cell, consistent with `GameState::Player::cell` and selector cells.

Serialization rules:

- write screens in ascending ID order;
- write connections by the ordered pair of screen IDs and endpoint cells;
- reject unknown properties;
- require `format == 1`;
- require positive IDs and safe integer coordinates;
- require a relative filename with no parent traversal;
- normalize each undirected connection so its lower screen ID is `a` for deterministic diffs.

The file declares one common width and height. Every screen definition must have exactly those horizontal dimensions on every layer. Screen depths may differ; absent higher layers compose as Air.

## 7. Spatial model and connections

### 7.1 Global coordinates

Each screen’s unshifted global origin is:

```text
origin.x = slot.x * screenWidth
origin.y = slot.y * screenHeight
```

The loader adds one map-wide normalization offset so all composed coordinates are non-negative, because `Level` stores rectangular dimensions and many tile queries accept unsigned indices. The normalization offset is runtime-only and never serialized. Moving the entire authored map therefore does not rewrite content.

The global cell for a local cell is `origin + normalizationOffset + localCell` in X/Y and retains local Z.

### 7.2 Valid connection geometry

A connection is valid only when:

- its screens occupy cardinally adjacent slots;
- each endpoint is on the facing boundary;
- the orthogonal coordinate and Z are equal;
- translating both endpoints to global coordinates makes them Manhattan-adjacent;
- both cells are valid entity occupancy cells under the normal level rules;
- each endpoint is used by at most one connection;
- both endpoints have compatible support and traversal semantics;
- neither screen contains an End tile.

Examples for a `W x H` screen:

- east connection: `a.x == W - 1`, `b.x == 0`, `a.y == b.y`, `a.z == b.z`;
- south connection: `a.y == H - 1`, `b.y == 0`, `a.x == b.x`, `a.z == b.z`.

Multiple connections along one shared edge are allowed, as long as their endpoints are unique.

### 7.3 Preventing accidental crossings

Because rectangles in adjacent slots touch along a complete edge, two undeclared boundary occupancy cells could otherwise form a legal move. Strict runtime and production validation must inspect every pair of facing boundary columns and reject any undeclared pair across which a player or movable could legally step under the static board geometry.

This makes the connection list authoritative without adding a special collision branch to `Rules`. Authors close unused portions of a seam with walls, unsupported Air, water, or other normal non-traversable geometry.

Dynamic changes do not create new connections. A movable cannot build an undeclared bridge across a seam because the static destination occupancy/support pair is rejected when it could become traversable. If later mechanics can change static support, seam permission should move into a general world-adjacency query rather than accumulating mechanic-specific validators.

### 7.4 Topology rules

- Screen IDs are unique.
- Layout slots are unique.
- Files are unique and must exist.
- Every screen is reachable from the start screen through declared connections.
- A connection may join only cardinal neighbors.
- Diagonal adjacency is visual only.
- Sparse layouts and holes are valid.
- A screen may have no connection on one or more sides.
- Two screens may be adjacent visually without a connection if their entire shared seam is non-traversable.
- The global composed rectangle is bounded by configurable safety limits for total cells, slot span, and layer count to prevent malformed content from allocating excessive memory.

### 7.5 Connection markers

The colored endpoint tiles in the concept image represent a matched connection pair; they do not need to become a new `TileType`. Store the connection as metadata anchored to an ordinary valid occupancy cell, just as a selector is metadata anchored to a cell without replacing its underlying tile.

The editor should draw matching colored endpoint overlays and direction arrows so authors can see and pair connections. Normal gameplay should render the physically adjacent authored surfaces without a portal effect in the first implementation. If playtesting shows that exits need stronger signposting, add a renderer-only decal/model chosen from the connection ID; do not encode topology by comparing tile colors or assets.

## 8. Loading and composing the overworld

Add a headless `OverworldMap`/`OverworldMapLoader` subsystem. It should own authored topology and produce a runtime composition:

```cpp
struct ComposedOverworld {
    Level level;
    uint64_t topologyFingerprint = 0;
    OverworldScreenId startScreen = 0;
    std::vector<OverworldScreenRuntime> screens;
    std::vector<OverworldConnectionRuntime> connections;
    std::vector<LevelLocation> selectorTargets;

    std::optional<OverworldScreenId> screenAt(GridPosition3 globalCell) const;
    GridPosition3 toGlobal(OverworldScreenId, GridPosition3 localCell) const;
    GridPosition3 toLocal(OverworldScreenId, GridPosition3 globalCell) const;
    std::vector<OverworldScreenId> visibleNeighborhood(OverworldScreenId) const;
};
```

Loading proceeds in this order:

1. Strictly parse `layout.json` and validate identity, paths, slots, and graph references.
2. Parse every `.scr` as a `Level::Definition`, not as an independently playable `Level`.
3. Require exact common width/height, reject `C` and `E` tiles in chunks, and validate decorations/selectors in local coordinates.
4. Validate start and connection occupancy cells against their local definitions.
5. Compute global origins and a normalization offset.
6. Allocate a rectangular composed definition large enough for the occupied slot bounds and maximum depth.
7. Translate non-Air authored tiles, water behavior, decorations, selectors, and local metadata into the composed definition.
8. Inject one `C` at the translated map start cell.
9. Assign runtime selector IDs while preserving compound `(overworld screen ID, local selector ID)` source identity. Local selector IDs need only be unique inside their file.
10. Construct one normal `Level` from the composed definition.
11. Build fast cell-to-screen ownership and slot-to-screen lookup tables.
12. Compute a deterministic topology/content fingerprint from the layout and normalized serialized screen definitions.

Water needs deliberate treatment. The current `Level` has one optional frame-wide water layer. The first implementation should require every screen either to use no water layer or to use the same water-layer index; the composed level then keeps that index. Supporting different water layers would require changing `Level` from one optional index to region-based water metadata and is not necessary for this feature.

## 9. Runtime state and movement

### 9.1 One gameplay session

When the campaign context is overworld, `Application` loads the `ComposedOverworld::level` into the existing `GameplaySession`. There is still one authoritative state, scheduler, move count, presentation, undo chain, and snapshot.

The active overworld screen is metadata maintained by a small `OverworldSession` or by a focused extension of `CampaignSession`; it is not a second rules state.

### 9.2 Detecting a crossing

When a player action is planned, compare the owning screen of every living player before and in the projected result:

- if all remain in the active screen, the action is ordinary;
- if all end in one cardinally connected destination screen, attach a screen transition to that action;
- if living players would be split across screens, reject the action before scheduling;
- if a destination cell has no screen owner or crosses an undeclared seam, reject it as blocked.

The connection validator makes ordinary rules adjacency correct, but the multi-player admission check must occur before the action commits.

Movables and enemies may cross a declared seam and remain in another screen. Their global state persists even while off camera. Only living players determine the active screen. Mirror beams and other world queries operate on the composed board and can cross an open declared seam naturally.

### 9.3 Crossing animation and undo

The player animation already moves between global adjacent cells. A camera track is attached to the same action presentation transaction:

```cpp
struct OverworldCameraTrack {
    OverworldScreenId from;
    OverworldScreenId to;
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
};
```

Forward playback interpolates source center to destination center. Reversed playback uses the existing transaction reversal behavior, so undo animates the camera and player back together. A conveyor or ice action with several legs can contain several camera legs if it crosses more than one seam; each camera leg uses the corresponding motion-track interval.

The active screen changes logically when the crossing action commits. The presentation controller may target the destination earlier for interpolation, but persistence and selector admission continue to use committed state.

### 9.4 Restart and undo policy

- Undo may cross overworld screen boundaries because the overworld is one session.
- Restart resets the entire overworld to its authored initial state, matching current overworld restart behavior.
- Returning from a puzzle restores the entire preserved composed overworld state and active screen.
- Overworld move count remains irrelevant to puzzle best records.
- Automatic overworld mechanics continue to run across the whole composed map on the existing gameplay step clock.

Resetting only the active chunk would produce a state that may not belong to the global undo chain and is intentionally excluded from the first version.

### 9.5 Selector interaction

Selector lookup uses the translated global selector cell. Existing interaction policy remains:

- the world is idle;
- every player is alive;
- every living player is on the same selector;
- the target exists and is playable;
- draft play cannot activate it.

Before entering a puzzle, the full composed overworld checkpoint and active overworld screen ID are saved immediately. Returning from completion restores that checkpoint, refreshes flag states, and centers the camera on the saved active screen without an artificial map transition.

## 10. Camera and visible neighborhood

### 10.1 Settled view

At rest, the camera centers on the active screen’s slot. It uses a fixed virtual footprint of three screen widths by three screen heights, plus the existing presentation padding. This guarantees that all existing screens in the surrounding Chebyshev-distance-1 neighborhood can be visible:

```text
NW  N  NE
 W  A   E
SW  S  SE
```

`A` is active. Missing slots render empty. The fixed footprint prevents zoom pumping when moving between an edge screen with few neighbors and a dense central screen.

The screen map should be authored with this viewing scale in mind. If later playtesting finds a full 3x3 view too distant, the camera footprint can become a presentation setting, but reducing it would no longer satisfy the requirement that diagonal neighboring screens be visible.

### 10.2 Transition view

During a cardinal crossing:

- camera center interpolates exactly one screen width or height;
- fit size, pitch, yaw, and distance remain stable;
- render eligibility is the union of the source and destination 3x3 neighborhoods, preventing a row or column of screens from popping halfway through the move;
- at completion, eligibility contracts to the destination 3x3 set;
- the source and destination sets are prepared before the action begins.

Use the same movement easing as the player’s grid interpolation unless visual testing demonstrates motion sickness or lag. Camera shake is applied after the base center interpolation so it does not alter the logical target.

### 10.3 Renderer API

Add an optional explicit view to `RenderFrameData`, for example:

```cpp
struct CameraView {
    Vec3 center;
    Vec3 fittedSize;
};

std::optional<CameraView> cameraView;
```

`IsoScenePreparer` uses `cameraView` when present and otherwise retains the current `cameraExtent` behavior. This isolates multi-screen changes from puzzle camera fitting, editor picking, thumbnail baking, and top-down pitch animation.

The explicit view must be used consistently by rendering, world-to-pixel projection, picking, shadows, particles, SSAO depth ranges, and editor overlays. Camera code has historically been sensitive to mismatched “prepared frame” state, so no subsystem should re-derive a second overworld camera.

## 11. Rendering composed screens

### 11.1 Visibility

`RenderFrameBuilder` receives the composed map metadata and an eligible screen-ID set. It appends only static tiles, decorations, selectors, water, and dynamic entities owned by those screens. It uses global translated coordinates, so depth sorting, shadows, picking, particles, and interpolation remain in one coordinate space.

An entity moving across a seam is included if either its current or projected cell belongs to an eligible screen. A particle remains visible while its position lies inside the eligible global bounds even if its source screen has just left the settled neighborhood.

The simulation still owns off-screen entities. Rendering omission must never mutate state or affect rules.

### 11.2 Per-screen ground splat maps

Give each overworld screen an independent stable-ID-based map:

```text
GroundSplatMapOverworld1
assets/custom/textures/ground_splat_overworld_1.png
```

Add a render-frame ground region table:

```cpp
struct GroundSplatRegion {
    OverworldScreenId screen;
    GridRect globalBounds;
    GroundSplatTextures textures;
};
```

The render frame stores each region’s global bounds and resolved textures. The recorder finds the containing region for each ground face and supplies its region-local origin to the shader. The shader derives coverage from the splat texture dimensions while using the face’s global vertices for continuous grass/rock material UVs. A missing dedicated map falls back to the shared ground splat map for that region only.

This avoids rebuilding an atlas when a screen moves, preserves existing per-screen painting resolution, and makes editor paint identity stable across layout changes.

### 11.3 Water and seams

Unbounded water exterior currently expands around one authored board. In the composed overworld, water and shoreline tests must operate against the composed tile query but emit only for eligible screen regions. A connected seam must not draw a shoreline wall between two compatible water cells. A non-connected shared edge may draw its normal closed boundary.

### 11.4 Asset residency

The first implementation preloads the active screen’s visible 3x3 neighborhood. Frame-derived requirements cover the source/destination neighborhood union while a crossing is in flight, providing a draw-time safety net for every splat region actually emitted.

If memory measurements later require streaming:

- keep active-distance-1 assets resident for rendering;
- preload distance-2 screens so crossing to any cardinal neighbor can immediately reveal its new 3x3 neighborhood;
- never begin a crossing whose destination neighborhood is not resident;
- keep common tile, entity, selector, animation, and audio assets permanently resident.

Streaming is an optimization phase, not part of initial correctness.

## 12. Persistence and migration

### 12.1 Checkpoint shape

Replace the raw overworld snapshot with a typed checkpoint:

```cpp
struct OverworldCheckpoint {
    uint64_t topologyFingerprint = 0;
    OverworldScreenId activeScreen = 0;
    GameplaySession::Snapshot session;
};

std::optional<OverworldCheckpoint> overworld;
```

The fingerprint must cover connection topology, screen IDs/slots, start cell, and normalized screen definitions. It prevents a structurally valid snapshot from being restored onto a different layout whose global coordinates happen to pass lower-level checks.

Restore policy:

- fingerprint matches and snapshot validates: restore it;
- fingerprint differs or snapshot validation fails: discard only the overworld checkpoint, start from authored state, log the repair, and immediately persist the repaired state;
- active screen does not own every living player after restore: reject the checkpoint;
- saved active screen is absent: reject the checkpoint.

Puzzle checkpoints and per-puzzle-screen progress are unchanged.

### 12.2 Save format

The current profile format is 19. This feature should bump it to 20, migrate `overworldSession` to the typed checkpoint property, and preserve all puzzle-screen progress and settings.

A format-19 save has a snapshot for the old single `overworld.scr` but no layout fingerprint or stable screen ID. The safe migration is:

- preserve puzzle progress and active puzzle checkpoints;
- set the new overworld checkpoint to null;
- if context is overworld, start at the new layout’s authored start;
- if context is puzzle, resume the puzzle normally and return to a fresh overworld after completion.

Attempting to reinterpret old global positions is not reliable once content has been split and should not be done implicitly.

### 12.3 Checkpoint timing

In addition to existing deferred overworld saves, request an immediate checkpoint:

- after a screen-crossing action commits;
- before entering a puzzle selector;
- after returning from a puzzle;
- on clean shutdown and save-slot switch as today.

This bounds crash recovery to a committed active screen and prevents the saved camera location from disagreeing with the saved player state.

## 13. Puzzle-selector ownership rule

For every selector in every overworld screen, define its owner as the stable ID of the file containing it. After target existence and complete catalog coverage are validated, group selectors by target puzzle level:

```text
owners[L] = set of overworld screen IDs containing a selector
            whose target.level == L
```

Production validity requires `owners[L].size() == 1` for every puzzle level `L`.

Consequences:

- Level 0 screen 0 and Level 0 screen 4 cannot have selectors in different overworld screens.
- Duplicate selectors for the same puzzle screen are still allowed, but all duplicates for that puzzle level must remain in its one owner overworld screen.
- Level 0 and Level 1 may be on different overworld screens.
- Level 0 and Level 1 may also share one overworld screen.
- An unassigned selector is allowed as temporary editor state but rejected by production staging.
- Historical completion remains keyed by `LevelLocation`, so moving a whole level’s selectors to another overworld screen does not alter player progress.

The editor should compute the same owner map live. When a level is already owned by another overworld screen, the target picker disables that level in the current screen and explains where it is assigned. This prevents most invalid states before save while the validator remains authoritative.

## 14. Level Editor design

### 14.1 Separation of responsibilities

Keep `LevelEditor` focused on one tile document. Add a headless `OverworldMapEditor` for project-level topology. The ImGui adapter coordinates them but never mutates either model directly.

`OverworldMapEditor` owns:

- parsed layout and selected overworld screen ID;
- add/move/delete/restore screen commands;
- start-cell editing;
- connection creation/deletion;
- topology validation diagnostics;
- topology-level undo/redo records;
- source/runtime transactional save requests.

`LevelEditor` continues to own the loaded screen definition, tile/decorations/selectors, document history, picking, and save/load.

### 14.2 Browser and layout view

Replace the single Overworld file row with an **Overworld Map** tab containing:

- a 2D layout canvas showing one card per slot;
- cardinal connection lines and endpoint counts;
- the start-screen marker;
- per-screen diagnostics, selector count, and puzzle-level ownership summary;
- controls to add a screen in any empty cardinal or diagonal slot;
- controls to move a disconnected screen to an empty slot;
- Open Screen, Duplicate Screen, Delete Screen, and Set Start actions.

Double-clicking a card opens its `.scr` in the existing 3D editor. The selected card and loaded document are separate concepts, following the existing browser-selection versus loaded-document rule.

### 14.3 Editing individual screens

`LevelEditor::editingOverworld()` becomes path-aware: any loaded file referenced by the active `overworld/layout.json` is an overworld screen. Do not infer this only from a parent directory name, because scratch or deleted files can share names.

While editing an overworld screen:

- selector tools are enabled;
- End and Player tiles are disabled;
- dimensions are locked to the map width/height;
- adding a layer is allowed subject to the common water-layer rule;
- screen-local selector IDs remain stable and local;
- the active map start and connection endpoints render as editor overlays, not serialized tile types;
- connection endpoints remain selectable even when a different tool is active.

Changing or deleting tiles under a start/connection/selector endpoint must be one atomic validated edit. The command either updates/clears the affected metadata in the same transaction with an explicit warning or refuses the edit. It may not leave a runtime mirror whose topology points at an invalid cell.

### 14.4 Connection workflow

Add a **Connections** tool:

1. Choose a cardinal neighbor from the map panel.
2. Click a valid boundary occupancy cell in the loaded screen.
3. The editor computes the only matching cell on the neighbor.
4. Preview both endpoints and the connection direction.
5. Confirm to save one undirected connection in `layout.json`.

Because screen dimensions and slots determine the matching endpoint, the author should not manually choose two arbitrary cells. This removes mismatched Y/Z and wrong-edge errors. If the neighbor’s matching cell is not traversable, show why and do not create the connection.

Disconnect is an explicit undoable command. Moving a screen with connections is blocked until those connections are removed; silently retargeting spatial edges would be surprising.

### 14.5 Start cell

The map has one start screen and start cell. A **Set Overworld Start** tool writes the layout record after validating the clicked occupancy cell. The start appears as a ghost player in editor rendering but is not written as `C` into the component screen.

### 14.6 Draft play

Playing an overworld draft composes the entire source map, substituting the unsaved in-memory definition for the currently edited screen. It then uses the real movement, screen-transition, camera, and neighbor-render path with a non-persistent temporary session.

Puzzle selector activation is disabled in this mode so draft testing cannot modify the active save or jump from source content into staged runtime puzzle content. Selector models and status previews still render.

### 14.7 Project mutations

Puzzle level/screen insertion and deletion must continue remapping selector `LevelLocation`s, now across every overworld screen file in the same `LevelProjectStore` transaction.

- insert puzzle level N: increment target levels `>= N` in all overworld screens;
- delete puzzle level N: clear exact targets and decrement later target levels;
- insert puzzle screen N: increment later targets in that level;
- delete puzzle screen N: clear exact targets and decrement later targets;
- restore a puzzle level: apply the insertion remap across the map before commit.

Topology edits update `layout.json` and affected overworld files atomically in both source and runtime roots. Soft-deleted overworld screens should be stored outside the active layout and include enough metadata to offer restoration without colliding with active IDs or slots.

## 15. Validation tiers

Use one shared headless validator with explicit modes so the editor, project store, content tool, and application do not drift.

### 15.1 Structural authoring validation

Required before mirroring runnable content:

- layout JSON is syntactically and semantically valid;
- screen IDs, filenames, and slots are unique;
- referenced files parse and have common dimensions;
- no component contains `C` or `E`;
- the start is valid;
- connection references and geometry are valid;
- all screens are reachable;
- no undeclared traversable seam exists;
- selectors are locally valid and target only existing puzzle screens when assigned;
- decorations and asset names are valid under existing rules;
- composed allocation safety limits are satisfied;
- the composed definition constructs as a valid `Level`.

Unassigned selectors and incomplete selector coverage may remain editor warnings so a map can be authored incrementally. If the current project-store policy continues to require complete selector assignment, creation commands must add screens and assignments in a transaction large enough to stay valid; the recommended policy is to reserve completeness for production validation.

### 15.2 Production validation

Adds:

- at least one overworld screen and at least one selector exist;
- every selector is assigned;
- every selector target exists;
- every puzzle screen has at least one selector;
- every puzzle level’s selectors are contained in exactly one overworld screen;
- every required selector model and per-screen render asset can be staged;
- no unexpected active files exist in the overworld directory;
- layout and screen serialization are canonical enough for a stable fingerprint.

`CampaignSession::setOverworldTargets()` should continue defense-in-depth catalog coverage validation, but the application should receive targets from the validated `ComposedOverworld` rather than reloading and scanning one file.

## 16. Content pipeline and tools

`ContentPipeline::addLevels()` now prefers the composed layout and:

1. add and validate `levels/overworld/layout.json`;
2. add every referenced screen file;
3. reject unreferenced active `.scr` files in that directory;
4. validate each screen’s decorations and selectors;
5. apply strict topology and selector ownership validation;
6. stage every screen-local splat map that is declared in the manifest;
7. emit the layout and screen files under the same runtime paths.

Update `tools/make_ground_textures.py`, `SplatPainter`, render asset requirements, content indexes, and ground-map cleanup to use stable overworld screen IDs. Moving a screen to another slot must not rename or regenerate its splat map.

Content diagnostics should name both the stable overworld screen and source path, for example:

```text
level 2 selectors span overworld screens 4 and 9
connection 4:(11,3,1) -> 9:(0,3,1) joins non-adjacent slots
overworld screen 7 has a traversable undeclared east seam at local (11,5,1)
```

## 17. Application, campaign, UI, and audio changes

### 17.1 Application

- Replace `overworldPath()` with `overworldRoot()` and a cached validated `ComposedOverworld`.
- Build the puzzle catalog, then load/validate the map once and pass its targets to `CampaignSession`.
- Apply the composed `Level` when entering overworld context.
- Pass map metadata, active/transition screen IDs, and explicit camera view to frame construction.
- Refresh the map after relevant Debug editor saves; reject or recover invalid checkpoints using the fingerprint.
- Preload merged overworld requirements.

### 17.2 Campaign state

Add queries and transitions for:

- current active overworld screen;
- committed overworld screen crossing;
- typed overworld checkpoint creation/restoration;
- current topology fingerprint;
- selector target owner screen for diagnostics.

Puzzle entry, completion, screen progress, independent per-level unlock order, and game-complete policy remain unchanged.

### 17.3 Player-facing UI

No new menu is required. Debug UI gains active overworld screen ID/slot and transition diagnostics. Existing completion and title flows still return to the saved overworld context.

If a topology edit invalidates a checkpoint, the recovery log should say that the overworld changed and the player was returned to its authored start, rather than reporting a generic corrupt save.

### 17.4 Audio and particles

Crossing an overworld seam does not restart music or reset particles. It is movement inside one world. Puzzle entry/return keeps the current music transition behavior. Screen-specific ambient audio is out of scope; overworld music remains one role.

## 18. Implementation sequence

Implement as vertical, independently tested stages.

### Phase 1: Topology types, parser, and validator — complete

- Add stable screen, slot, endpoint, connection, and layout types.
- Add strict layout JSON load/write.
- Expose file-backed `Level::Definition` parsing.
- Validate screen files, common dimensions, graph connectivity, start, connections, seams, water-layer compatibility, and allocation limits.
- Add focused `OverworldMapTests` before runtime integration.

### Phase 2: Composition — complete

- Translate definitions into a global composed definition.
- Inject the global player start.
- Translate decorations and compound selector identities.
- Build ownership lookups, visible-neighborhood queries, targets, and fingerprint.
- Prove that a normal `GameplaySession` can move, push, auto-move, undo, and restore across a seam in headless tests.

### Phase 3: Persistence and campaign metadata — complete

- Add `OverworldCheckpoint`, active screen, and fingerprint validation.
- Bump player profile format 19 to 20 and migrate old overworld checkpoints to null.
- Add immediate saves at committed crossings.
- Update codec, migrations, save slots, backup recovery, and campaign tests.

Land this before runtime writes the new checkpoint format.

### Phase 4: Camera transition and neighborhood rendering — substantially complete

- Add explicit floating-point camera view support to render-frame preparation. **Done:** a fixed integer fit extent plus floating camera offset preserves the footprint while translating smoothly.
- Add overworld camera tracks synchronized to action timelines and undo. **Done for current movement actions:** camera progress is sampled from the primary player's presentation position against committed/projected cells, so forward and reversed movement share the same clock.
- Filter rendering by source/destination neighborhood union. **Done for composed world geometry, authored instances, selectors, gameplay actors, and per-screen material regions.**
- Add per-screen splat regions and shader instance data. **Done:** stable-ID texture selection is stored in a frame region table and resolved per ground face with local splat UVs.
- Update water, particles, projection, shadows, and picking to use the same view.
- Add headless layout/projection/render-frame tests and visual captures.

### Phase 5: Application integration — core runtime complete

- Replace single-file loading with the composed map. **Done when `layout.json` exists, with legacy fallback.**
- Aggregate selectors and asset requirements across screens. **Done, including visible per-screen splat requirements.**
- Detect/admit committed screen crossings and multi-player restrictions. **Done.**
- Preserve selector entry/return, music, checkpointing, and profile repair. **Done in the runtime path; multi-screen content still needs end-to-end playtesting.**
- Update Debug Engine diagnostics.

### Phase 6: Editor model and topology UI — complete

- Add `OverworldMapEditor` and project-level history. **Done, including undo/redo and rollback-capable save.**
- Add the map canvas, stable screen CRUD, start tool, and connection tool. **Done. Start and connection commands arm a headless cell-picking mode, open the source component, preview the hovered cell in 3D, validate the click, and remain armed after an invalid click for retry or explicit cancellation.**
- Make `LevelEditor` edit referenced overworld definitions without `C`/`E`. **Done, including fixed dimensions and protected topology cells.**
- Add whole-map draft composition with the in-memory document override. **Done for the complete unsaved topology draft plus the active unsaved component, including newly created component definitions that do not yet exist on disk.**
- Update selector target ownership diagnostics and cross-file remapping. **Done.**
- Extend source/runtime transaction and soft-delete behavior. **Done; deleted component files remain source-only and keep their stable IDs for restoration.**

### Phase 7: Initial content and migration — complete

- Add `levels/overworld/layout.json` and move the current overworld into its first stable screen file. **Done.**
- Move the old player start into layout metadata and remove `C` from the component file. **Done.**
- Update content staging and ground texture tools. **Done.**
- Keep all existing selectors and puzzle targets unchanged. **Done.**
- Remove the old root-file special cases only after migration tests pass. **The production asset is removed; the loader/stager fallback remains intentionally for old installations and fixture coverage.**

### Phase 8: Regression and performance pass

- Run the full Debug and Release test suites.
- Measure composed board memory, rules-step time, frame-build time, visible instance counts, asset residency, and checkpoint size with a large synthetic map.
- Visually test cardinal crossings in both directions, undo, ice/conveyor crossings, all 3x3 neighborhood patterns, camera pitch toggle, resize/aspect changes, shadows, water seams, particles, and splat boundaries.
- Add streaming only if measured memory requires it.

## 19. Test plan

### Topology and composition

- One-screen map composes identically to its authored definition except for injected start.
- Cross-shaped and sparse maps produce correct global origins.
- Negative authored slots normalize without changing relative placement.
- Duplicate IDs, files, and slots are rejected.
- Missing/unreferenced files are rejected in the appropriate validation mode.
- Disconnected graphs are rejected.
- Cardinal endpoints translate to Manhattan-adjacent global cells.
- Diagonal or non-neighbor connections are rejected.
- Mismatched boundary, orthogonal coordinate, or Z is rejected.
- Multiple valid doorways on one edge are accepted.
- Undeclared traversable seams are rejected.
- Common dimension, water layer, allocation, and no-`C`/`E` constraints are enforced.
- Decorations, selectors, and splat regions retain local placement after translation.

### Gameplay and camera

- A normal move crosses each cardinal direction and changes active screen once.
- Camera center moves by exactly one screen footprint over the action duration.
- Reverse/undo crosses back with the reversed camera track.
- Push, ice, conveyor, enemy, and movable behavior across a declared seam matches an equivalent single-board level.
- A multi-leg action can cross more than one seam without snapping.
- Split-player outcomes are rejected; all-player crossings are accepted.
- Selector interaction after a crossing saves/restores the correct active screen.
- Restart and invalid checkpoint behavior match the policy.
- Settled visible set is exactly existing Chebyshev-distance-1 screens.
- Transition visible set is the source/destination neighborhood union.
- Neighbor assets and splat maps do not change the active screen’s local UVs.

### Selector validation

- Complete coverage in one owner screen per puzzle level passes.
- Two puzzle levels in different overworld screens pass.
- Two puzzle levels in one overworld screen pass.
- One puzzle level split across two overworld screens fails.
- Duplicate target selectors in the same owner screen pass.
- A duplicate in another owner screen fails.
- Insert/delete/restore remaps every overworld file atomically.

### Persistence

- Format-20 checkpoint round-trips active screen, fingerprint, state, and undo chain.
- Fingerprint mismatch discards only overworld state.
- Missing active screen and player/active-screen disagreement are rejected.
- Format-19 migration preserves puzzle progress/settings and clears old overworld snapshot.
- Puzzle-context migration still resumes its active puzzle.
- A committed crossing requests an immediate save.

### Editor and pipeline

- Stable IDs survive add/delete/restore and file browsing.
- Layout move is blocked while connected.
- Connection preview computes the matching endpoint.
- Invalid endpoint edits cannot create an invalid runtime mirror.
- Whole-map draft substitutes unsaved active document content.
- Whole-map draft substitutes unsaved layout, connections, start metadata, added/deleted screens, and the active document together.
- Source/runtime commit rollback remains atomic on failure.
- Production inventory includes layout, every referenced screen, dependencies, and per-screen splat maps.
- Production rejects incomplete coverage and cross-screen puzzle-level ownership.

## 20. Risks and mitigations

### Dense composed level size

A sparse map with far-apart slots can create a large rectangular `Level`. Enforce slot-span and total-cell limits immediately. If authored maps need to exceed them, refactor `Level` queries behind a sparse board abstraction before relaxing the limits; do not silently allocate based on unchecked content.

### Whole-map rules cost

Existing rules may scan the complete rectangular board on each world step. Benchmark a synthetic target-sized map in Phase 8. Safe optimizations include occupied-cell indexes and chunk-aware iteration, but changing simulation scope based on camera visibility would alter gameplay and is not an acceptable optimization.

### Render-frame and descriptor growth

Nine screens can contain significantly more geometry and splat textures than one. Reserve from measured eligible counts, keep only visible instances in the frame, and verify descriptor limits at content validation time.

### Camera scale

Framing a complete 3x3 neighborhood makes the active board smaller than today. This is a direct consequence of the visibility requirement and needs early visual validation with representative screen dimensions. It should not be “fixed” by cropping diagonal neighbors after implementation.

### Cross-file editor consistency

A layout endpoint can become invalid because of a tile edit in a different document. Route saves and topology mutations through one shared validator and transaction layer. Do not duplicate a weaker UI-only validation.

### Checkpoint compatibility during content iteration

Moving any screen changes global coordinates. The fingerprint intentionally invalidates the overworld checkpoint. Debug logs and UI diagnostics should make this expected recovery visible to authors.

## 21. Expected code areas

New or substantially changed areas are expected to include:

- new `src/engine/OverworldMap.*`, `OverworldMapValidator.*`, and `OverworldMapEditor.*`;
- `src/engine/Level.*` for file-backed definition parsing/composition support;
- `src/engine/CampaignSession.*` and `Application.*`;
- `src/engine/GameplaySession.*`, rules admission, and presentation timelines for screen/camera transition metadata;
- `src/engine/PlayerProfile.*`, `PlayerProfileCodec.cpp`, and `PlayerProfileMigrations.*`;
- `src/engine/RenderFrameBuilder.*` and `src/engine/render/RenderTypes.hpp`;
- `src/engine/render/IsoScenePreparer.*`, scene instance data, descriptors, and the ground shader;
- `src/engine/render/RenderAssetRequirements.*`;
- `src/engine/LevelEditor.*`, `LevelEditorDebugUi.*`, `ApplicationTools.*`, and editor interaction overlays;
- `src/engine/LevelProjectStore.*` and `ContentPipeline.*`;
- `src/engine/SplatPainter.*` and `tools/make_ground_textures.py`;
- `assets/manifest.json`, `levels/overworld/`, CMake test targets, README, and HANDOFF documentation.

## 22. Acceptance criteria

The feature is complete when all of the following are true:

1. The game loads a versioned overworld layout containing at least two separately authored screens.
2. A normal player movement crosses a declared cardinal seam without a load screen, teleport, presentation reset, or rules discontinuity.
3. The camera translates during that same movement and settles with the destination as active.
4. At rest, every existing cardinal and diagonal neighbor in the active screen’s 3x3 layout neighborhood is rendered.
5. Undo can reverse a screen crossing with synchronized player and camera motion.
6. Normal overworld mechanics and persisted dynamic state continue to work across connections.
7. Puzzle entry and return preserve the full overworld and its active screen.
8. Every overworld screen is separately editable, with stable identity, independent decorations/selectors/splat map, and project-level topology controls.
9. Authors can add, place, connect, disconnect, move, delete, restore, and draft-play overworld screens without manually editing JSON.
10. Production validation rejects invalid spatial topology, invalid seams, incomplete selector coverage, and any puzzle level whose selectors span more than one overworld screen.
11. Different puzzle levels can validly live on different overworld screens.
12. Existing format-19 saves preserve puzzle progress and recover safely to the new authored overworld start.
13. The complete automated suite passes, and visual verification covers all cardinal directions, diagonal-neighbor visibility, splat boundaries, water seams, aspect ratios, undo, and automatic movement.

## 23. Decisions to keep stable during implementation

The following are the load-bearing decisions in this plan:

- authored screens are chunks of one composed gameplay world;
- screen identity is stable and independent of file ordering;
- layout positions use a common screen width/height;
- connections are explicit, undirected, physical, and cardinal;
- diagonal screens are visible but not directly traversed;
- the settled camera frames a fixed 3x3 footprint;
- one puzzle level belongs to exactly one overworld screen;
- the composed overworld has one checkpoint and one undo chain;
- component screens contain no runtime player or End tile;
- initial correctness loads the full map; streaming follows measurement.

Changing any of these mid-implementation affects the file format, validator, save model, camera, or rules boundary and should trigger an explicit design revision rather than an incidental code workaround.
