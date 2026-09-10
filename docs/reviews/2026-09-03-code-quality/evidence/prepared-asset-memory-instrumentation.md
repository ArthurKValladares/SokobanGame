# Prepared asset memory instrumentation

Captured 2026-09-09 for MQ-01.

`VulkanModelResources::LoadingStats` exposes the asset publication pipeline by
model, texture, and animation class. Each class reports queued, decoding,
CPU-ready, uploading, resident, and failed counts. Models and textures also
report logical decoded payload bytes, upload-ring reservation bytes, and GPU
resident bytes. The aggregate transient counter is decoded payload plus upload
staging, and its high-water mark is sampled at state transitions so it includes
the short interval where both copies coexist.

The decoded measurement counts retained dynamic payload content: vertex,
index, material, skeleton, attachment, animation-keyframe, image, and compressed
mip data. It intentionally excludes allocator metadata, spare vector capacity,
future/task bookkeeping, and the temporary packed skinned-mesh vectors tracked
by MQ-02. It is therefore a stable workload metric and a lower bound on process
memory, rather than a claim about total heap consumption.

The Rendering Stats debug panel displays all stage counts and byte totals. The
hidden-surface Vulkan smoke test drives a skinned model through a forced
residency refusal and verifies that:

- the model remains CPU-ready with a nonzero retained payload;
- retry starts an upload without another decode;
- CPU-ready bytes fall to zero as upload staging becomes live; and
- the transient high-water mark includes decoded and staging bytes at their
  ownership handoff.

Verification:

- Debug build: `sokoban`, `sokoban_ui_tests`, and
  `sokoban_vulkan_smoke_tests`.
- Release build: the same targets.
- Debug CTest registry: 80 of 80 passed.
- Release CTest registry: 80 of 80 passed.

The remaining MQ-01 measurement needs request-time size estimates for queued
and active decode work, publication-deferral duration, and a repeatable
large-prefetch workload. Those values will determine the budget and its
admission policy.
