# Ownership boundary evidence

Captured 2026-09-10 for MQ-05.

## Prepared asset publication

Model, texture, and animation slots previously repeated five related fields:
the load state, decode future, decoded payload, decoded byte count, and stored
failure. Their publication paths changed those fields independently during
queueing, decode collection, residency refusal, upload, failure, and
retirement. The scheduler behavior was correct, but its most important
ownership rule depended on each caller remembering which fields to preserve or
clear.

`PreparedAssetPublication<Payload>` now owns those fields and exposes checked
transitions for the shared lifecycle. A residency-admission refusal performs no
transition, so the exact decoded object remains `CpuReady` for a later attempt.
Beginning an upload or publishing an animation consumes the decoded object and
clears its byte accounting once. Upload completion produces `Ready`; retirement
returns a resident slot to `Unrequested`. Failure captures the active exception,
releases CPU ownership, marks the slot failed before a blocking caller receives
the error, and preserves the background logging behavior.

The owner depends on no Vulkan type. GPU allocations and asset-specific upload
logic remain in their typed resource slots. Focused tests cover the model-style
decode/upload/retirement lifecycle, retry identity, animation publication,
queued cancellation, asynchronous and blocking failures, estimate failure,
and invalid transitions.

## Scene recording inputs

`VulkanSceneRecorder::record` previously accepted a preview frame pointer and a
prepared-preview pointer independently. The recorder handled a mismatched pair
in several different ways: some statistics counted the frame, other statistics
counted the prepared scene, and preview rendering required both.

The recorder now accepts an immutable `FrameInputs` object. Its required game
scene and optional preview each pair `RenderFrameData` with the corresponding
`PreparedRenderScene`, so an API caller cannot express half of a preview. The
renderer also checks its prepared-frame scratch invariant before constructing
the input. The per-call recording session borrows persistent scratch, cache,
configuration, and telemetry state directly from the recorder owner instead of
receiving those members as fourteen unrelated constructor arguments.

The editor publication path was left intact because its transaction result
already distinguishes source commit, runtime mirror, and index publication.
Application flow was also left intact because its persistent subsystems already
own their lifecycles; another aggregate context would have obscured those
owners without removing a concrete ambiguity.

Verification:

- Focused Debug `asset_load_state` and validation-backed `vulkan_smoke` tests.
- Full Debug warning-as-error build and CTest registry.
- Full Release warning-as-error build and CTest registry.

MQ-05 is complete.
