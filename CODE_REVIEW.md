# Engineering Review: Code Quality & Refactoring Priorities

**Scope:** current working tree at `bd23d3d1`. No new features — this is about
making what exists cleaner, more readable, harder to break, and cheaper to
run. Findings are ordered by leverage: what the next quality budget should be
spent on.

**Context for the reader:** ~48.7k lines of C++20 across engine, renderer, UI
and tools, against ~18.1k lines of tests in 44 registered CTest suites. The
architecture is in good shape and has visibly improved: headless modules with
thin adapters, a data-driven asset manifest, a strict versioned save codec
(format 17) with forward-patch migrations, a content pipeline that ships only
reachable files, and an async categorized logger. There are zero
`TODO`/`FIXME`/`HACK` markers in the entire production tree, which is unusual
and worth preserving.

**Status of the previous review.** All ten of its findings are resolved:
`SaveSlotManager` and `ShellFlow` were extracted with suites, menu results
became `std::variant` actions, `rules::step` became `MicroStepResolver` with
the four named phases (and the twin fall-target functions were unified behind
one blocker predicate), `MenuKit`/`RowList` landed, `PlayerProfile` split into
a 126-line model plus codec and migration files, `sokoban_add_test` collapsed
the CMake boilerplate, `Log` shipped, title-open filesystem IO went from 15
`std::filesystem::exists` call sites in `Application` to 1, and the two save
workers became one channelled worker. `VulkanRenderer` also shrank from 2,334
lines to 1,147 without being explicitly asked to.

The theme of this review is different from the last one. Last time the problem
was that the glue layer had no tests and no seams. The seams now exist — and
two of them have quietly filled back up from the other side.

---

## P0 — Structural debt that is actively producing risk

### 1. `Application.cpp` is 45% debug-editor tooling

`Application.cpp` is 1,571 lines, and **700 of them sit inside
`#if SOKOBAN_ENABLE_DEBUG_UI` regions** across 15 separate conditional blocks.
The composition root did shed campaign policy, input routing, settings
projection and save-slot lifecycle exactly as the last review asked — and then
the level editor moved into the space that freed up.

What lives there now:

- `drawBrushPreview()` — 121 lines projecting a 48-segment, 12-ring disc
  through the previous frame's camera and hand-writing ImGui vertices and
  indices, including a 16-bit index-overflow guard.
- `bakeTileThumbnails()` — 102 lines driving its own two-frame render loop,
  cropping, and writing PNGs into two asset trees.
- `drawDecorationGizmo()` — 91 lines of gizmo projection and hit testing.
- `updateEditorPainting()`, `updateGroundPainting()`,
  `updateDecorationEditing()`, `drawDraftExitConfirmation()` — seven
  editor-behaviour methods declared on `Application` itself.

None of it is testable, and it is the same category of problem the last review
named: policy in the one file with no suite. It is arguably worse this time,
because the code is *geometry* — projection, winding, index arithmetic,
coverage sampling — which is precisely the kind of thing a headless test pins
cheaply and eyeballs expensively.

`ApplicationTools` (379 lines) already exists as the seam and already owns the
editor's *state*. The behaviour never followed it.

**Recommendation.** Move editor interaction into a headless
`EditorInteraction` (or `EditorToolsController`) that consumes a pointer ray,
the previous frame's camera projection and the editor document, and returns
intents plus preview geometry as plain data. `Application` executes the
intents; a thin ImGui adapter draws the returned geometry, exactly as
`LevelEditorDebugUi` already does for the rest of the editor. The brush-preview
disc and the gizmo become data-returning functions with unit tests instead of
`ImDrawList` calls. Effort: 2–3 days. Highest leverage in the codebase.

### 2. `RenderFrameBuilder::buildEditor()` is a 354-line function next to an 8-line sibling

`RenderFrameBuilder.cpp` is now the largest file in the project at 1,964
lines, and it contains both the best and the worst function in it.

```
RenderFrameData RenderFrameBuilder::buildGameplay(const GameplayInput& input)
{
    RenderFrameData frame = initializeGameplayFrame(input);
    appendGameplayWorld(frame, input);
    appendGameplayEntities(frame, input);
    appendMirrorPreview(frame, input);
    applyScrollingMaterials(frame, input);
    return frame;
}
```

That is the shape the whole codebase aspires to. `buildEditor()`
(`RenderFrameBuilder.cpp:1537`) is 354 lines straight-line — the longest
function in the tree by 150 lines (`:1536`) — doing layer iteration, per-cell tile
construction, preview and pick-only cells, water resolution, camera extent
accumulation and decoration placement inline. It reuses four of the file's
helpers (`waterGridBoundsFor`, `appendLadderRungsForCell`,
`appendUnboundedWaterExterior`, `appendDecorations`) and re-implements the
rest.

`appendMirrorPreview()` (`:1232`) is a second offender at 278 lines.

This is low-risk work with an existing net: the module is Vulkan-free and
`tests/PresentationTests.cpp` already drives `buildEditor` through six
scenarios and `buildGameplay` through twenty-two. Decompose `buildEditor` into the
same named-phase shape (`initializeEditorFrame`, `appendEditorLayers`,
`appendEditorPreviews`, `appendEditorCamera`), sharing the gameplay phases
where the two genuinely agree. Regression-lock with the presentation suite
before and after, same discipline as the `MicroStepResolver` change.
Effort: 1–2 days.

---

## P1 — Duplication and coverage gaps

### 3. 33 hand-written image barriers across 8 renderer files

`VkImageMemoryBarrier2` is constructed by hand 33 times:
`VulkanSwapchainResources.cpp` (16), `VulkanSsaoPass.cpp` (6),
`VulkanFrameCapture.cpp`, `VulkanModelResources.cpp`, `VulkanShadowPass.cpp`,
`VulkanThumbnailPass.cpp`, `VulkanUiResources.cpp` (2 each) and
`VulkanSceneRecorder.cpp` (1). Every one repeats the same
`.subresourceRange`, `.srcQueueFamilyIndex`/`.dstQueueFamilyIndex` and
`VkDependencyInfo` scaffolding around two or three fields that actually vary.

Meanwhile `VulkanResourceUtils.hpp` — included in 19 translation units, and
already the designated home for shared Vulkan plumbing — exposes exactly two
functions: `vkCheck` and `destroyImage`.

**Recommendation.** Add `transitionImage(commandBuffer, image, From, To)` and
a colour/depth `subresourceRange()` helper to `VulkanResourceUtils`. Barriers
are the single most consequential thing to get right in a Vulkan renderer and
the single least interesting to read 33 times. Mechanical, no behaviour
change, validated by the existing renderer suites. Effort: half a day.

### 4. The filesystem-mutation primitives are the only untested headless code left

Every headless module has a suite except these:

- **`AtomicFile`** (67 lines) — used by `SaveStore`, `SaveSlotManager`,
  `AssetManifest{Editor}`, `AnimationCatalog`, `DecorationAssetRegistry`,
  `PngWriter` and `ApplicationTools`: seven consumers. It contains a
  displace-and-restore fallback for platforms that cannot rename over an
  existing file — a branch that only executes when the *second* rename fails,
  i.e. never in development and occasionally in the field. No suite.
- **`LevelProjectStore`** (327 lines) — staging-root mutation transactions for
  the editor, a `Mutation` callback applied against a staging tree with a
  `Result`. No suite.
- `RuntimeContent` (43 lines, the boot gate that rejects a mismatched
  `content.index`) and `BoardLayout` (59 lines of pure geometry) are smaller
  instances of the same gap.

The last review observed that the save-slot bugs "clustered" in exactly the
untested filesystem layer. That layer got a suite; the primitive underneath it
still has none. `SaveStore` is already tested against a temp directory — the
same harness covers all four. Effort: 1 day for `AtomicFile` and
`LevelProjectStore` together, including the rename-failure path via an
injected failure hook.

---

## P2 — Efficiency (measured restraint: nothing here is currently hot)

### 5. Per-frame allocation churn, carried forward and now half-done

Partially resolved since the last review. `IsoScenePreparer` fills two
renderer-owned `PreparedFrameScratch` slots whose vectors are cleared without
releasing capacity, and the UI now draws from a `FrameArena` bump allocator
into a fixed-capacity `ArenaArray`, so a UI frame is exactly one allocation.

What remains:

- `RenderFrameBuilder::buildGameplay`/`buildEditor` return `RenderFrameData`
  **by value**, and it holds four `std::vector`s (`tiles`, `waterSurfaces`,
  `isoFaces`, `particles`) rebuilt from empty every frame.
- `renderAssetRequirementsForFrame()` at `VulkanRenderer.cpp:248` allocates
  three fresh `std::vector<bool>`s per frame as a draw-time safety net.

`FrameArena` and `ArenaArray` now exist and are tested
(`sokoban_frame_arena_tests`), so this is no longer speculative infrastructure
work — it is pointing the existing tool at its second consumer. Two
constraints to respect: `FrameArena` is deliberately unsynchronized, so
anything reached from `TaskSystem` workers needs a per-worker arena; and
`ArenaArray` requires trivially destructible elements, which `Tile`,
`WaterSurface`, `IsoFace` and `Particle` all satisfy today. Effort:
1 day. Still genuinely optional — at current scene sizes this is noise.

### 6. Small shared-vocabulary duplication

Seven files parse JSON (`AssetManifest`, `AnimationCatalog`,
`AssetManifestEditor`, `DecorationAssetRegistry`, `Level`,
`PlayerProfileCodec`, `PlayerProfileMigrations`) and each brings its own
validation vocabulary: `requireObject` exists in both `AssetManifest.cpp:26`
and `PlayerProfileCodec.cpp:38`, `rejectUnknown` only in
`AnimationCatalog.cpp`. `lowercase` is duplicated between
`DecorationAssetRegistry.cpp:20` and `DecorationMeshCatalog.cpp:11`.

This is much smaller than the UI duplication the last review found, and the
strict-parsing behaviour is genuinely per-schema. Worth a shared
`JsonRead.hpp` only when the next JSON-backed format lands — do not go
looking for it now. Effort: opportunistic.

---

## Latent — not broken, not guarded

- **`TaskSystem`'s "tasks must not block on tasks" rule holds by luck of call
  graph.** `parallelFor` is reached only through `skinWithPoses` ←
  `skinGltfMeshBlended` ← `SkinnedMeshUpdater.cpp:91`, which runs on the render
  thread; the enqueued asset loads (`VulkanModelResources.cpp:310/345/362`)
  call `loadGltfSkinnedMesh`/`loadRgbaImage`/`loadGltfAnimationClip`, none of
  which reach it. Nothing enforces this. A debug-only check in `parallelFor`
  that the caller is not a worker thread is ~10 lines and converts a future
  deadlock into an assertion.

---

## Explicit non-goals (reviewed and rejected)

- **Replacing the custom GLTF loader.** 1,647 lines that load exactly our
  assets with clear failure modes. A general library is more code, not less,
  until asset complexity actually grows.
- **Task-graph dependencies in `TaskSystem`.** No current consumer needs
  ordering; adding it speculatively is how task systems get scary. See the
  latent note above for the cheap guardrail instead.
- **Further splitting `VulkanRenderer`.** Down to 1,147 lines from 2,334 and
  now a genuine orchestrator over its focused components. Done.
- **Splitting `GltfMesh.cpp` or `VulkanSceneRecorder.cpp`.** Both are large
  (1,647 and 1,620) but they are cohesive: one loader, one command encoder.
  Size alone is not the finding — `buildEditor` is, because it has a
  well-factored sibling proving the decomposition is available.
- **Asset cache eviction.** The staged tree is ~4 MB.
- **Micro-optimizing `rules::step`.** Entity counts are single digits.
- **Testing the Vulkan-facing classes.** `VulkanSwapchainResources`,
  `VulkanSceneDescriptors`, `VulkanPipelineFactory` and friends need a device;
  the Vulkan-free policy around them (`RendererReconfiguration`,
  `FrameResourceTracker`, `RenderResolution`, `IsoScenePreparer`) is already
  extracted and tested, which is the right line.

## Suggested sequencing

1. **`AtomicFile` + `LevelProjectStore` suites (item 4)** — one day, no
   production change, and it closes the last headless coverage gap before
   anything else moves.
2. **`buildEditor` decomposition (item 2)** — the presentation suite is green
   and covers both paths; do it while that is true.
3. **`transitionImage` helper (item 3)** — half a day of mechanical work, best
   done before the next render pass lands and adds barrier number 34.
4. **`EditorInteraction` extraction (item 1)** — the big one. Sequenced after
   items 2 and 3 because both reduce the surface it has to move through, and
   after item 1 `Application.cpp` should be back under ~900 lines.
5. **Arena-backed render frames (item 5)** — whenever the render path is next
   open, not on its own account.
6. **Shared JSON vocabulary (item 6)** — only alongside the next new format.

Each step leaves the build green and ships independently; none blocks feature
work. The theme this time: the headless-module discipline is now applied
almost everywhere, and the two places it is not — the debug editor's
interaction code and the editor render path — are the two places the editor
grew fastest. Apply the same rule to the editor that the game already follows.
