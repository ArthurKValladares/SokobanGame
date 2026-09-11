# Content and editor review evidence

Reviewed on 2026-09-11. These supporting notes retain two executable reproductions and one independently traced consequence of level/screen renumbering. They recommend changes; no production behavior was changed during this review.

## CONTENT-01 — Renumbering screens leaves cached drafts attached to the wrong document

**Priority:** P1, because an ordinary editor operation can silently restore one screen's unsaved contents into another screen. **Category:** correctness and authoring data integrity.

**Source:** `src/engine/LevelEditor.cpp:384-407` keys cached drafts by normalized path; `1470-1504` restores a matching path before reading disk; `1898-1934` inserts a screen by renaming the existing files and loading the inserted screen. The common mutation wrapper at `2426-2447` publishes the filesystem operation without remapping editor drafts. Related renumbering paths are `addLevelAt` at `1768-1811`, `deleteLevel` at `1840-1882`, and `deleteScreen` at `1970-2032`.

**Reproduction:** [content-editor-probe.cpp](content-editor-probe.cpp), built/run using [run-content-editor-probe.cmd](run-content-editor-probe.cmd), creates two blank screens, edits a wall into screen 1, opens screen 0 to cache that unsaved draft, and inserts a new screen at index 0. The original screen 1 moves to screen 2 on disk. Its draft remains keyed to screen 1, now occupied by the original screen 0. Opening screen 1 restores that unrelated wall instead of the file's actual contents.

The retained [probe output](evidence/content-editor-probes.txt) reports:

```text
after_insert_cached_draft_old_path=1
after_insert_cached_draft_new_path=0
wrong_occupant_disk_wall=0
wrong_occupant_restored_draft_wall=1
```

**Impact and limits:** the probe demonstrates incorrect document identity and contents in the editor after a successful insertion. It deliberately does not save the misidentified document. A subsequent ordinary save uses the newly opened path, so the mismatch exposes that occupant to being overwritten with the other screen's draft. The disk renaming transaction itself succeeds; improving filesystem rollback alone will not fix this in-memory identity problem.

**Suggested change:** represent each insertion/deletion as an explicit mapping from old screen paths/locations to new paths/locations, plus deleted identities. Apply that mapping to cached draft keys and each draft's stored document paths, and reconcile the active document and its history, only when the filesystem transaction commits. Define what happens to a deleted dirty draft: preserve it as a clearly detached/recoverable document or require an explicit discard within the editor flow. Do not silently drop all cached drafts as the fix. Keep the existing path-based document model if a well-scoped remapping operation suffices; stable IDs throughout all content are not a prerequisite.

**Acceptance criteria:** retain a regression for the exact insertion sequence above, asserting both document contents and identity. Cover insertion before/after cached dirty screens, deleting before a cached draft, deleting the draft's own screen, level insertion/deletion, and an active dirty document. Undo/redo must remain attached to the original document. On a rejected or successfully rolled-back structural transaction, assert that draft keys, active paths, histories, and original source/runtime files remain unchanged. If a publication API explicitly commits source while deferring a failed runtime mirror, assert that in-memory identities follow committed source and that the mirror remains visibly stale and retryable; never remap back to old source identities after source has committed. See the distinct existing save/structural transaction contracts below.

**Implementation order:** first editor correction. Introduce a small reusable renumbering result here, then use the same mapping for CONTENT-03; do not duplicate index-shift rules in each consumer.

## CONTENT-02 — Saving a painted splat map leaves the old compressed runtime texture authoritative

**Priority:** P2. **Category:** correctness and incomplete integration between editing and packaged texture loading.

**Source:** `src/engine/SplatPainter.cpp:336-383`, specifically runtime PNG publication and index refresh at `362-369`; `src/engine/render/TextureSourceLoader.cpp:95-119` selects any existing matching BC7 artifact, and `179-188` prefers that artifact to the raw source. `src/engine/render/CompressedTextureArtifact.cpp:244-254` derives the artifact path from texture-source identity, not the changed image bytes.

The painter writes the new source PNG and runtime PNG, refreshes `content.index`, clears its dirty flag, and reports success. It does not regenerate or invalidate the compiled texture for that PNG. A subsequent load on a BC7-capable device finds the previous artifact and uses its previous pixels. Refreshing the package index inventories the mixed old/new files successfully; it does not make the artifact correspond to the newly painted source.

**Reproduction:** the same [probe](content-editor-probe.cpp) creates an all-black splat PNG and its valid BC7 artifact, paints the entire map white, saves through `SplatPainter`, validates the refreshed package, and loads through the production prepared-texture loader with BC7 enabled.

```text
saved_raw_first_pixel=255
restart_uses_compressed=1
restart_compressed_is_old_black=1
refreshed_package_validation_passed=1
```

**Impact and limits:** saved PNG bytes are correct, but the next prepared texture load uses the old paint on the compressed path. The retained probe exercises the exact loader used for restart/resource loading; it does not launch a second game process or capture a GPU frame. Devices without BC7 support take the raw fallback and do not show this particular stale-artifact behavior.

**Suggested change:** publish an edited texture together with its derived-artifact update. Either regenerate the matching BC7 artifact, or remove/invalidate every derived variant of the changed source so the loader takes the supported raw path until content compilation regenerates it. Perform artifact handling before declaring successful runtime publication/index refresh, and preserve the current retryable dirty state on failure. Prefer a shared, explicit texture-publication operation over duplicating artifact-path policy inside each editor. If multiple manifest entries can reference the edited source with different interpretations, invalidate/update all those variants rather than only the painter's selected name.

**Acceptance criteria:** start with an existing compressed black image, save white paint, and assert that both BC7-enabled and raw loads represent the newly saved image. Include board-resize saves, source paths with multiple texture interpretations, and failure during derived-artifact publication/index refresh. A package-validation assertion by itself is insufficient: verify the representation selected by `loadPreparedTextureSource` and its content. Retain correct source bytes and a retryable status if runtime publication fails.

**Implementation order:** after the draft-identity fix, independently of the broader renderer refactors. Other independent core/UI corrections can proceed in parallel; the main review's packet table sets the global order.

## CONTENT-03 — Level/screen renumbering does not move their authored splat or music associations

**Priority:** P2. **Category:** correctness and missing cross-component ownership.

**Source:** the renumbering mutations in `src/engine/LevelEditor.cpp:1781-1803`, `1850-1878`, `1898-1927`, and `1989-2025` change level/screen files, screen-name metadata, and overworld selector targets. They pass only level project/runtime roots through `2426-2438` to `src/engine/LevelProjectStore.cpp:332-364`; this transaction does not include the asset manifest. Splat lookup constructs `GroundSplatMap<level>_<screen>` at `src/engine/render/RenderTypes.hpp:101-108`. Music selection compares the numeric level in `src/engine/AudioSystem.cpp:272-283`.

**Evidence:** this is a direct source/data trace, distinct from the executable draft reproduction. The shipped `assets/manifest.json:132-203` contains named splat entries for existing numeric locations, and `658-674` maps levels 0 through 3 to different music tracks. Inserting screen 0 changes old screen 0's path to screen 1, while the manifest still associates its original map with `GroundSplatMap0_0`; the moved board now resolves `GroundSplatMap0_1`, if present, or the shared fallback. Inserting level 0 likewise shifts boards/selectors but leaves the old level-number music assignments untouched. Deleted-level restoration appends the level at a new index (`LevelEditor.cpp:2045-2063`) and also lacks association remapping.

**Why change it:** screen names and selector destinations already follow the logical board during renumbering; its painted terrain should follow it too. Otherwise inserting or deleting an earlier item changes the appearance of untouched boards and can pair them with differently sized maps. Per-level music has the same positional-key coupling. Existing manifest content makes this relevant to authored content, rather than a speculative empty-feature concern.

**Suggested change:** extend the shared renumbering mapping from CONTENT-01 to update manifest associations alongside the level project. Preserve referenced image files when only names/associations must change. Define deletion/restoration ownership explicitly, so restored boards recover their own paint and music without taking another board's entries. Keep authoritative source level files and source associations consistent at their commit boundary. Runtime publication must either participate in the existing structural rollback contract or explicitly return a committed-source/stale-mirror result with retry; an undifferentiated failure must not leave callers guessing which identities committed. Include relevant manifest, derived-artifact, and index handling in that publication contract. Do not introduce a new global asset registry merely to fix these specific associations.

**Acceptance criteria:** fixtures with distinguishable per-screen maps and per-level tracks must preserve those associations across screen/level insertion, deletion, and deleted-level restoration; the inserted board receives only its intended default. Validate source and runtime manifests, selector targets, screen names, texture lookup, and index consistency. Inject a source transaction failure and assert that level files and associations retain their original meaning. For a runtime publication failure, assert the documented outcome: rollback under the current structural contract, or consistently committed source/in-memory identities plus an explicit retryable stale mirror under a deliberately separated publication contract.

**Implementation order:** include association remapping with CONTENT-01 once its shared location mapping exists; the main review groups both as CQ-01. Renaming associations while retaining their image paths does not require new texture bytes. Any operation that also changes texture source paths or bytes must coordinate with CONTENT-02's derived-texture publication policy (CQ-03).

## Scope and deliberate exclusions

These notes finish the previously captured content/editor evidence. The two probe findings use production editor/loader APIs and disposable fixture roots, with their exact outputs retained. CONTENT-03 is established by source and shipped data, and is not represented as a separate executed renderer/audio probe.

- The package-index refresh succeeding in CONTENT-02 is evidence of stale derived publication. The index validates file inventory/sizes; it does not establish a source-to-artifact freshness relationship.
- The current transaction contracts are intentionally different. Ordinary `LevelEditor::saveDocument` retains saved source on runtime failure and returns typed stale-mirror/index outcomes (`LevelEditor.cpp:1710-1754`). Structural `LevelProjectStore::transact` stages and validates both roots and attempts to roll both back on runtime/index failure (`LevelProjectStore.hpp:17-19`, `LevelProjectStore.cpp:375-407`). Respect the actual committed result when reconciling in-memory identities. Extend ownership where needed, but do not rewrite these contracts solely to fix the findings.
- No separate source-equals-runtime index defect is included without a supported workflow and focused reproduction.
- The length of `LevelEditor.cpp` alone is not a sufficient reason to split it. The useful extraction identified here is a cohesive location-remapping/publication operation with explicit commit semantics, because multiple concrete bugs cross that boundary.
- Do not remove migrations, content-version handling, or supported legacy formats merely because they are old. No evidence here establishes that they are dead.
