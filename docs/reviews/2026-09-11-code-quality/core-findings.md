# Core review evidence

Reviewed on 2026-09-11. These are supporting notes for the overall code-quality review, not production changes. Two actionable correctness findings survived verification. Severity is P2 (normal priority) for each: both need correction, but neither probe demonstrates irreversible loss of saved bytes or incorrect committed puzzle state.

## CORE-01 — Completing one action resets the presentation of other running actions

**Category:** correctness; completing the concurrent-action integration.

**Source:** `src/engine/GameplayLoop.cpp:204-214`, especially `213-214`; `src/engine/GameplayPresentation.cpp:638-640`, `666-669`, and the corresponding movable/enemy synchronization below them.

`GameplayLoop::update` advances and samples every in-flight action, commits the actions that finish, then calls `presentation.finishAction(session.state())`. That function calls `syncToGameState`, which resets **all** entity visuals to committed positions and rest animations. The committed state deliberately excludes unfinished actions. When an independent player movement ends during a longer ice slide, this erases the slide's sampled position. If completion consumes the frame's remaining time exactly, the loop exits without sampling the surviving action again, and the reset position is the frame that gets rendered.

**Reproduction:** [CoreReviewProbe.cpp](core-probes/CoreReviewProbe.cpp), built and run with [run.ps1](core-probes/run.ps1). The fixture pushes an ice block, lets it slide, then moves the player perpendicularly while the block is still sliding. Action duration is `0.125` seconds and each main probe frame is `0.0625` seconds, below the application's `0.1`-second frame cap. The binary-exact intervals deliberately exercise the end-of-frame boundary without floating-point ambiguity.

Observed positions from [core-probes.txt](evidence/core-probes.txt):

| Frame | Player committed cell | Block visual X | Block committed X | Actions in flight |
| --- | --- | --- | --- | --- |
| Before the perpendicular move | (1, 0) | 3.0 | 2 | 1 |
| Halfway through the move | (1, 0) | 3.5 | 2 | 2 |
| Move completes at frame end | (1, 1) | **2.0** | 2 | 1 |
| Next frame, 0.015625 seconds later | (1, 1) | **4.125** | 2 | 1 |

The expected visual X at the completion boundary is 4.0. The committed block remaining at X=2 is correct while the slide runs; the defect is presentation snapping backward and then forward. The same whole-state synchronization also touches animated actors' rest/clip state. Do not infer a committed-state corruption finding from this evidence.

**Suggested change:** make completion reconciliation respect the entities owned by surviving actions. A narrow fix can synchronize committed entities and immediately re-sample every surviving action after synchronization, including when no frame time remains. A fuller completion API can reconcile only completed entity changes. Choose the smallest implementation that handles additions/removals, deferred actions, and animation state without creating a second ownership model.

**Acceptance criteria:** a gameplay-loop regression follows at least two actions with different durations and verifies surviving position, motion flag, and actor animation at the exact completion boundary and the next frame. Include a frame with leftover time as a control; the boundary matters. Preserve mirror additions/removals, undo behavior, final idle poses, and all existing gameplay/presentation tests.

**Implementation order:** after save-loading resilience (CORE-02), independently of renderer changes. Add the regression and fix in one focused packet.

## CORE-02 — Save artifact inspection/cleanup can prevent loading an intact primary profile

**Category:** correctness; recovery failure handling.

**Source:** `src/engine/SaveStore.cpp:179-201`: `load()` calls recovery at `195` before its normal primary read/decode path at `198-201`. The outer fallback is `312-318`; recovery of primary and backup is `322-326`; live/temporary/displaced inspection is `334-336`; valid-primary stale cleanup is `338-343`; recovery-candidate promotion is `366-368`. The usable-data-with-persistence-error contract already exists in `src/engine/SaveStore.hpp:22-25` and in migration/backup-repair branches of `SaveStore::load`.

`load()` calls `recoverInterruptedWrites()` before its normal primary read/decode-and-return path. Recovery first inspects the live, temporary, and displaced paths, removes stale artifacts for a valid live file, and then performs the same preflight for the backup. Inspection itself reads and decodes existing regular files to classify their state (`SaveStore.cpp:51-78`), but does not retain the decoded profile for `load()` to return. Any failure inspecting or removing one of those sibling artifacts escapes to `load()`'s outer catch, which returns a default profile and `StorageUnavailable`. A readable, decodable primary never reaches the normal decode-and-return branch. This couples permission to perform maintenance on all recovery artifacts to permission to use valid progress already on disk.

**Reproduction:** the same [core probe](core-probes/CoreReviewProbe.cpp) saves a profile whose current/unlocked level is 7, creates a directory at its otherwise absent `profile.backup.json` path, and calls `load()`.

```text
recovery: expectedLevel=7 actualLevel=0 disposition=8
recovery: primaryStillDecodesAs=7
```

Disposition 8 is `StorageUnavailable`; the complete status in [core-probes.txt](evidence/core-probes.txt) identifies the non-regular backup artifact. The primary bytes still decode to level 7. `inspect()` can report `PrimaryValid` for this same primary while `load()` fails, because inspection returns after decoding the primary and does not require backup maintenance. The directory is deterministic fault injection; the code path also rejects inspection or stale-cleanup I/O errors before returning usable data.

**Impact and limits:** users can be prevented from loading otherwise valid progress by a broken backup or stale artifact. `SaveSlotManager` deliberately refuses a `StorageUnavailable` load; the probe does not show it silently starting a new game or overwriting level 7. Keep that distinction in the final review.

**Suggested change:** separate selecting/decoding an authoritative usable profile from best-effort repair and cleanup. Preserve deletion-marker authority and future-format protection. Once valid primary data is established, failure to inspect/clean non-authoritative siblings should yield that data with a maintenance/persistence error, consistent with `LoadedWithPersistenceError`. When the primary is missing or invalid, retain recovery-candidate ordering, but do not discard decoded backup/recovery data merely because promotion or cleanup fails. Keep enough detail in the result for later retry and diagnostics.

**Acceptance criteria:** cover valid primary plus inaccessible/non-regular backup; valid primary plus failing stale-artifact cleanup; usable backup/recovery candidate plus failed promotion; and controls for deletion markers, unsupported future formats, corrupt primary data, and wholly unavailable storage. Tests must assert both profile contents and preserved source files, not just a success enum. Existing legacy-migration/backup-repair tests cover failures after decoding but miss this pre-decode recovery boundary.

**Implementation order:** first of the two core findings, because it prevents access to valid saved progress. It can reuse the existing load-result vocabulary rather than introducing a parallel persistence protocol.

## Breadth, validation, and deliberate non-findings

This resumed core pass traced the gameplay-loop/presentation completion boundary, action admission and commit order, StateDelta application/identity semantics, undo rebasing and snapshot restore, campaign checkpoint/elapsed-time flows, SaveStore recovery/inspection/deletion, asynchronous coalescing/flush/retry, save-slot switch rollback, input routing/capture, task exception/lifetime handling, frame timing, shared math conventions, and bounded logging. Review depth was greatest at the two reproduced cross-component failures. Supporting source/test inspection was risk-based; this is not a claim of formal verification of every input or exhaustive device coverage.

The retained fresh Debug and Release logs each report **80/80 CTest suites passing**: [Debug](evidence/ctest-debug.txt), [Release](evidence/ctest-release.txt). Relevant suites include `rules`, `gameplay_session`, `gameplay_loop`, `action_plan`, `action_scheduler`, `reservation`, `state_delta`, `presentation`, `campaign_session`, `player_profile`, `save_slots`, `atomic_file`, `input`, `input_router`, `tasks`, `logging`, `math`, `geometry`, and `frame_pacing`. The standalone core probe was subsequently rebuilt against the Debug libraries and reproduced the two missing cases. Probe sources/scripts and text evidence are retained under this review; new binary/object/data outputs are under `out/code-quality-review/probes/core`.

- **Do not restore closed historical backlog items.** The September 3 roadmap was updated September 10 and identifies only the continuing MQ-06 platform/device evidence work. Preserve its explicit invariants instead of treating its archived proposals as current defects.
- **ActionPlan's changed-entity collector is not a separate required refactor.** `ActionPlan.cpp:53-81` uses positional comparison to grow a scoped slide closure; `StateDelta.cpp:27-66` matches identity and supports insertion/removal. The planner documents that its particular step path never inserts/removes entities. This is overlapping logic, but no live mismatch was established. Replacing it with construction of full deltas for every leg adds work and allocation. If the step invariant changes, share a narrowly scoped ID visitor or adopt the canonical delta then; do not count it as a present correctness bug or cleanup requirement solely for reducing duplicate-looking code.
- **Async save destruction is not shown to throw on ordinary filesystem failures.** `SaveStore::save` catches those failures and returns false; `AsyncSaveStore::flush` reports typed retryable results. A speculative allocation-failure exception during destructor bookkeeping is not the ordinary I/O failure claimed by the rejected candidate.
- **The selector-transition “durable” wording is inaccurate, but a separate demonstrated data-loss defect was not established.** Saves are queued, and later queued profiles retain the overworld checkpoint. Correct that word if touching the code; do not inflate the backlog with a second save-loss claim.
- **Keep historical migrations and compatibility adapters when they preserve supported saves/callers.** Age or length alone is not evidence that a migration or adapter is dead. Similarly, `Math.hpp`'s named arithmetic wrappers call the same operator implementation and explicitly document their compatibility purpose.
- **Do not expand the task system speculatively.** It propagates packaged-task/chunk exceptions, waits for helpers before rethrow, drains shutdown work, and rolls back partial worker construction. Tasks are documented as independent, with no nested waiting; current model-loading tasks do not call the CPU-skinning `parallelFor` path. No current nested-wait deadlock was established.
- **Keep measured optimizations.** No measured first-party bottleneck from this core pass justifies a new scheduler, indexing structure, state representation, or logging architecture. Focused correctness changes should retain the current ownership and persistence contracts.

Remaining environmental evidence is the existing Linux/compiler/sanitizer and real controller/audio/Vulkan-device matrix. A passing local Windows registry and deterministic fault probes do not substitute for those environments.
