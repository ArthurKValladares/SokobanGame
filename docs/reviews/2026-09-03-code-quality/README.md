# Code quality roadmap

Updated 2026-09-10. This is the active, forward-looking code-quality backlog
for Sokoban 3D. It contains only remaining work; Git history and the archived
handoff retain historical context. Remove an item when its acceptance criteria
are met.

The roadmap covers first-party C++ code, shaders, tests, build configuration,
content tooling, packaging, and engineering documentation. Third-party source
internals and art-asset quality or licensing remain outside its scope.

## Working rules

- Preserve behavior unless an item defines an intentional behavior change.
- Tie each refactor to an ownership, lifecycle, correctness, or measured-cost
  improvement. File length alone is not a reason to split code.
- Change one analyzer category or one runtime boundary per reviewable packet.
- Add tests for a meaningful invariant or failure mode. Avoid tests that only
  repeat an implementation.
- Measure performance and memory before changing architecture, and record the
  baseline, workload, target, and result together.
- Treat CTest as the source of truth for the registered suite. Do not copy test
  counts into scripts.

## Priority queue

| ID | Priority | Work | Exit criteria |
| --- | --- | --- | --- |
| MQ-03 | Next | Quantify profile and undo snapshot copying | Benchmark save-request latency, serialization time, and peak memory late in a long level; optimize only after preserving the agreed undo-persistence contract |
| MQ-04 | Later | Separate startup inspection cost from decode and upload | Record manifest inspection, glTF dependency discovery, decode, upload, and first-playable-frame timing; introduce cached metadata only with reliable invalidation |
| MQ-05 | Later | Extract cohesive ownership boundaries from large modules | Each extraction must reduce a specific lifetime or state-transition ambiguity and keep dependencies narrower than the source module |
| MQ-06 | Ongoing | Expand platform and device evidence | Run the Linux toolchain, installer, controller, audio-device, and broader Vulkan device matrix described below and retain actionable failure diagnostics |

## Analyzer gate maintenance

The gate covers 104 correctness- and cost-oriented analyzer checks. One hundred
and two are enabled. No actionable analyzer category remains excluded.

Two exclusions are policy exceptions rather than an automatic rewrite queue:

- `bugprone-easily-swappable-parameters` cannot reliably distinguish a
  confusable API from deliberate same-type coordinates or vector components.
  Review multi-boolean and semantically ambiguous APIs directly, using enums or
  option structures where combinations have meaning.
- `bugprone-unchecked-optional-access` currently reports guarded accesses whose
  short-circuit or early-exit control flow it does not model. Re-evaluate it
  when the analyzer improves or when optional-heavy code is substantially
  changed.

When a newly available check or a future finding requires gate work:

1. Use a compiler that supports the installed standard library.
2. Generate a compile database from the real target graph.
3. Scan every first-party translation unit with only the target category and
   its required dependencies enabled.
4. Search the complete report for parser errors separately from category
   diagnostics.
5. Inspect every finding in context. Prefer range checks, size-based algorithms,
   ownership changes, or clearer control flow over suppression casts.
6. Run warning-as-error builds and focused tests. Run the full Debug and Release
   registries when behavior, public interfaces, or cross-cutting headers change.
7. Remove any temporary exclusion and update this roadmap in the same commit.

The Linux CI analyzer remains the portable authority. A Windows scan may
disable MSVC STL vectorized implementations during analysis and omit unsupported
reproducible-path flags, but it must otherwise preserve the real compile
definitions and include graph.

## MQ-03 and MQ-04: remaining efficiency experiments

| Area | Experiment | Implementation threshold |
| --- | --- | --- |
| Profile and undo snapshots | Measure request latency and peak memory for long undo histories across deferred and urgent saves | Change representation only if the cost is user-visible or breaches a defined memory target |
| Startup inspection | Separate document parsing and dependency discovery from decode, upload, and first playable frame | Add cached metadata only when inspection is material and cache invalidation can be proven from source identity |

Retain the existing frame arenas, scratch reuse, suballocators, draw sorting,
shadow caching, compressed artifacts, upload scheduling, and residency tracking
unless equivalent measurements identify a concrete regression.

## MQ-05: ownership-focused refactoring

Candidate boundaries, in preferred order:

1. **Prepared asset publication:** make CPU-ready ownership, admission, upload,
   retry, and retirement transitions visible in one small state machine.
2. **Scene recording inputs:** replace long frame/pass argument lists with
   cohesive immutable contexts whose lifetimes do not outlive the frame.
3. **Editor publication orchestration:** isolate transaction coordination only
   if it makes source commit, runtime mirror, and index publication outcomes
   easier to prove.
4. **Application flow:** extract persistent state only when a component can own
   its lifecycle and tests without becoming a general-purpose application
   context.

Do not split renderer or model-resource files mechanically. An extraction is
successful when state ownership is more explicit, the new interface exposes
fewer unrelated dependencies, failure handling remains local, and focused
tests cover the moved invariant.

Prefer named option types where call sites contain several booleans with
meaningful combinations. Keep independent booleans when their purpose is
obvious at the call site.

## MQ-06: coverage expansion

| Environment | Required evidence |
| --- | --- |
| Linux GCC/Clang | Configure and build the supported presets with warnings as errors; run the headless registry and the validation-backed render job |
| Sanitizers | Run address and undefined-behavior sanitizers on the headless suite; triage every first-party report |
| Shipping package | Validate a fresh extraction with the bounded package gate, then complete the [manual release checklist](../../../packaging/ReleaseValidation.md) |
| Controllers | Exercise keyboard-to-controller capture, cancellation, held inputs, axis neutralization, disconnects, and at least two controller families |
| Audio | Test no-device startup, device loss, format negotiation, mute/volume changes, and shutdown on real hardware |
| Vulkan devices | Cover at least one integrated and one discrete GPU, validation enabled, resize/reconfigure cycles, residency pressure, and teardown |
| Editor workflow | Exercise stage, edit, publish, restart, and recover-from-failure flows against a disposable project copy |

Automated runs must capture logs and return nonzero on failure. Interactive
checks need the device, driver, package identity, steps, and observed result.

## Invariants every future change must preserve

- A valid decoded profile remains usable when migration, promotion, or saving
  cannot persist it; future-format data is never overwritten by an older build.
- Save deletion commits before cleanup and prevents recovery artifacts from
  reviving the deleted slot.
- Puzzle source commits, runtime mirrors, and package-index publication expose
  distinct outcomes and remain retryable where appropriate.
- Content publication validates complete GLTF or GLB dependencies before
  changing a live manifest or index.
- Binding capture keeps physical held state current while suppressing action
  edges caused during capture.
- Scheduler admission denial retains prepared mesh data for retry without
  another decode.
- Static loading and CPU/GPU skinning use the same position, normal, tangent,
  and handedness transform rules.
- Renderer teardown finishes before final validation status and logging
  shutdown are evaluated.
- Task-system construction failure stops and joins every worker already
  created.

## Execution order

Keep the analyzer gate green as checks and first-party code evolve.

1. Measure profile snapshots and startup inspection in that order.
2. Perform ownership extractions only when the preceding analyzer or
   measurement work exposes a concrete boundary to improve.
3. Expand platform and device validation alongside the code packets it covers.

A roadmap item is complete only when its acceptance criteria, relevant tests,
warning gate, and documentation all describe the resulting current state.
