# Sokoban 3D handoff

Updated 2026-09-14. This file contains current operating guidance and enduring
contracts. The former chronological handoff is preserved at
[`docs/history/HANDOFF-2026-09-09.md`](docs/history/HANDOFF-2026-09-09.md).
The active code-quality assessment and its evidence are in
[`docs/reviews/2026-09-11-code-quality/README.md`](docs/reviews/2026-09-11-code-quality/README.md).

## Current status

Sokoban 3D is a C++20, SDL3, Vulkan 1.3 project. Runtime content is declared by
`assets/manifest.json`, staged by `sokoban_content`, and validated against
`content.index` at startup.

The full Windows configuration currently registers 80 CTest suites. The
Vulkan-free `headless-tests` preset registers 72; it omits seven SDK-dependent
suites, while the Windows-only shipping-package gate accounts for the eighth
difference. Do not copy these counts into new scripts. CTest is the source of
truth.

The 14 actionable findings in the September 3 code-quality review are resolved.
Subsequent maintainability work has:

- introduced typed asynchronous persistence results and documented ownership;
- centralized changed-entity interpretation, manifest texture identity, and
  numbered puzzle paths;
- added shared assertions and collision-safe scoped test directories;
- completed immediate PBR material discovery for editor-appended models;
- enabled warnings-as-errors for Linux and Windows CI; and
- removed the obsolete undefined-member probe and stale shader commentary.

The September 11 review records 13 recommendations with evidence, acceptance
criteria, and an implementation order. Packets 1 through 5 were implemented on
September 14: document/draft/selector and asset-association identity remapping,
save-loading resilience, semantic setting-choice values, completion-safe
concurrent presentation, prepared-texture invalidation, complete Vulkan
descriptor-limit accounting, bounded residency admission, and failure-safe
painted-texture replacement, plus defined water-ripple derivatives across
depth-dependent geometry boundaries. The full 80-test Debug and Release
registries pass in the warnings-as-errors configuration. Next is packet 6:
small-window settings access and failure-safe new-game slot selection.
Broader refactoring or efficiency work still requires a concrete maintenance
problem or measurement.

## Build and validation

Normal Windows development build:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure --no-tests=error --timeout 120 -j 4
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure --no-tests=error --timeout 120 -j 4
```

Vulkan-free build using the same production libraries and test declarations:

```powershell
cmake --preset headless-tests
cmake --build --preset headless-tests
ctest --preset headless-tests
```

Use `-DSOKOBAN_WARNINGS_AS_ERRORS=ON` for a local warning gate. CI enables it
for both GCC/Clang and MSVC. The Linux Debug job performs the standalone
240-frame render under Vulkan validation. Linux Release runs the registered
CTest device smoke without the separate validation-frame command. Hosted
Windows runners build and run the SDK-independent and packaging tests but omit
device smoke because they do not provide a Vulkan ICD.

For shipping artifacts and human GPU acceptance, follow
[`packaging/ReleaseValidation.md`](packaging/ReleaseValidation.md). A package is
acceptable only after the bounded package gate succeeds from a fresh extraction
and the required real-device checks are recorded.

## Persistence and authoring contracts

- `SaveStore` owns primary, backup, temporary, displaced, and deletion-marker
  artifacts. A deletion marker commits deletion before cleanup and prevents old
  recovery candidates from reviving a slot.
- `AsyncSaveStore` management operations are owner-thread operations. Producers
  may request snapshots through the documented synchronized boundary. Flush and
  channel replacement return revisions and a typed durable/retryable outcome;
  an empty queue is not evidence that bytes reached storage.
- A decoded valid profile remains usable if migration or promotion cannot be
  persisted. Unsupported future formats remain preserved and do not fall
  through to an older writer.
- Puzzle source writes use durable atomic replacement. Runtime mirrors and
  package-index refresh have explicit outcomes, and a committed source with a
  stale mirror stays dirty and retryable.
- Successful authoring publication refreshes and validates `content.index`
  after the runtime tree is complete. Levels, overworld transactions, splat
  maps, manifests, decoration imports, animation catalogs, and thumbnail baking
  use this boundary.
- Structural level publication commits the level tree and its splat/music
  manifest associations together across source and runtime. Deleted levels keep
  a private association archive so restoration preserves their map paths and
  soundtrack at the newly assigned index.
- Overwriting an external texture invalidates all prepared interpretations of
  that source before `content.index` is refreshed. A failed invalidation leaves
  the authoring session dirty and retryable.
- Numbered puzzle paths are interpreted and constructed through `LevelCatalog`.
  Callers remain responsible for source/runtime root containment.
- Decoration registration resolves structured GLTF/GLB dependencies, validates
  external files before manifest mutation, and copies the complete dependency
  set before package-index publication.

## Gameplay and input contracts

- `StateDelta` owns canonical player, movable, and enemy entity order, append,
  and overlap interpretation. Scheduling and gameplay validation must use it
  instead of rebuilding changed-ID lists.
- Completing one scheduled action synchronizes committed/structural presentation
  state, then re-samples every surviving action at its existing elapsed time.
  This must also happen when completion consumes the frame's remaining time.
- Functional undo history is separate from the scalar completed-action
  diagnostic counter. Do not retain completed world-state actions for telemetry.
- Binding capture continues forwarding physical state changes while suppressing
  action edges. Releases and axis neutralization during capture must remain
  visible when capture completes or is cancelled.
- Static loading, CPU skinning, and GPU skinning share the source-to-model
  transform. Positions and tangents use the forward linear transform; normals
  use its inverse transpose. Tangent frames remain normalized, orthogonal, and
  preserve handedness.

## Renderer contracts

- The scene target is floating-point linear light. Tonemapping writes linear
  values to the sRGB display attachment, which performs the only display encode.
  Player-facing UI is composed after tonemapping.
- Scene alpha stores the ambient-to-total-lit ratio for opaque pixels so SSAO
  attenuates ambient contribution without darkening direct or emissive light.
  Blended scene pipelines preserve the opaque mask behind them.
- Set 0 contains scene/frame resources. Set 1 contains the variable-count
  sampled-texture heap and is the only binding in that set.
- Manifest textures own stable low descriptor indices. Discovered glTF textures
  own stable high indices. Appended manifest entries grow upward; newly
  discovered maps claim free high slots downward. Existing descriptors never
  move, and the two ranges must not overlap.
- Editor-appended models run the same material resolver and binding remapper as
  startup. Normal, metallic-roughness, emissive, and occlusion maps must work
  immediately without a restart.
- Descriptor updates are frame-local and occur only after the corresponding
  fence. Every allocated texture slot has a fallback descriptor. Evicted GPU
  resources remain alive until all submitted frames that reference them retire.
- Material-buffer entry zero is the fallback. Published model ranges remain
  stable while live and return to the allocator only after fence-owned
  retirement.
- Prepared skinned meshes retain their source payload when residency admission
  is deferred. Ownership moves only after admission succeeds, so retry does not
  require decoding again.
- Combined-image-sampler heap capacity is the minimum remaining capacity across
  per-stage and aggregate sampled-image and sampler limits after scene bindings.
  Every device-selection caller must provide all four descriptor requirements.
- A blocking asset wait retries retained CPU-ready payloads. It throws when
  queued work is blocked by retained data and no active operation can change
  admission; it must never poll that terminal state indefinitely.
- Resized painted textures publish transactionally. A failed replacement keeps
  the old handles and residency accounting live, and the paint revision remains
  pending until the renderer reports a successful update.
- Projected water-ripple screen derivatives execute before the depth-dependent
  geometry branch. Conditional cellular searches may consume an analytically
  propagated footprint, but must not invoke implicit derivatives themselves.
- One-shot command-buffer and fence lifetime belongs to
  `vulkanResources::beginOneShotCommands` and `submitOneShotCommands`. Preserve
  their cleanup behavior and diagnostic labels.
- `sokoban_core` must remain Vulkan-free. Shared arithmetic belongs in an
  SDK-independent header; `tools/check_core_is_vulkan_free.sh` enforces the
  boundary in CI.

## Decisions that require new evidence

Do not split `VulkanModelResources` merely to reduce file length. Its texture,
model, residency, publication, and retirement state has interleaved lifetimes;
extract a component only when the new owner makes a transition easier to prove.

Do not add a transfer queue, secondary command buffers, update-after-bind
descriptors, or a new task/allocator architecture without a measured workload
showing the current design is the bottleneck. Prior captures found the frame
GPU-bound and publication work bounded to startup. Historical measurements and
reproduction commands are in the archived handoff and `docs/render-evidence/`.

Keep point-shadow sampling policy deliberate: the scene and ground select
different tap counts from `shaders/include/PointShadow.glsl` because their cost
scales differently. Validate changes with the point-light evidence modes.

## Documentation rule

Update this file when an enduring contract, supported command, or immediate
next step changes. Put dated measurements and completed implementation history
under `docs/` and link them here. Do not append another chronological roadmap
to the root handoff.
