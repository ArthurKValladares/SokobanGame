# Prepared asset memory instrumentation

Captured 2026-09-09 for MQ-01; budget enforcement completed 2026-09-10.

`VulkanModelResources::LoadingStats` exposes the asset publication pipeline by
model, texture, and animation class. Each class reports queued, decoding,
CPU-ready, uploading, resident, and failed counts. Models and textures also
report logical decoded payload bytes, upload-ring reservation bytes, and GPU
resident bytes. The aggregate transient counter is decoded payload plus upload
staging, and its high-water mark is sampled at state transitions so it includes
the short interval where both copies coexist.

Encoded source sizes are cached once when resource slots are created and are
reported for queued and active decode stages. Model estimates include the main
document and declared attachment documents. Texture source totals use a native
BC7 artifact when one will load, an external source file, the containing
glTF/GLB document for a buffer-view image, or the data-URI length. Missing
sources report zero and still fail through the normal loader. Queueing,
reprioritizing, cancelling, and re-requesting therefore perform no size-related
filesystem I/O.

The decoded measurement counts retained dynamic payload content: vertex,
index, material, skeleton, attachment, animation-keyframe, image, and compressed
mip data. It intentionally excludes allocator metadata, spare vector capacity,
future/task bookkeeping, and the temporary packed skinned-mesh vectors tracked
by MQ-02. It is therefore a stable workload metric and a lower bound on process
memory, rather than a claim about total heap consumption.

Model and texture residency refusals report attempt counts, the number of
assets currently deferred, and cumulative microseconds from first refusal to
successful admission or failure. Active intervals are included when statistics
are sampled, so a stalled capture remains diagnostic.

The Rendering Stats debug panel displays all stage counts and byte totals. The
hidden-surface Vulkan smoke test drives a skinned model through a forced
residency refusal and verifies that:

- the model remains CPU-ready with a nonzero retained payload;
- retry starts an upload without another decode;
- CPU-ready bytes fall to zero as upload staging becomes live;
- the transient high-water mark includes decoded and staging bytes at their
  ownership handoff; and
- source estimates survive queue cancellation and re-request.

The same executable provides `--benchmark-prepared-assets`, a repeatable
pressure workload that prefetches 32 independent instances of the same skinned
source with two CPU jobs and one publication per iteration. Its original
unbounded baseline held model residency admission until all decodes were
CPU-ready. Releasing the hold published all 32 without a failure or second
decode.

Three Release runs on an NVIDIA GeForce RTX 4060 Laptop GPU produced:

| Run | Queued model source | Retained model payload | Transient peak | Deferral attempts | Summed deferred time | Workload time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 13,094,016 B | 20,307,264 B | 20,984,422 B | 277 | 1,858,701 us | 120,998 us |
| 2 | 13,094,016 B | 20,307,264 B | 20,984,422 B | 288 | 2,159,192 us | 133,581 us |
| 3 | 13,094,016 B | 20,307,264 B | 20,984,422 B | 289 | 2,176,910 us | 136,561 us |

The decoded model payload is 1.551 times the encoded source total in every run.
The source metric remains useful for queue visibility but does not reserve the
prepared-memory budget.

Model and animation reservations now come from the same glTF document parse
that discovers runtime material dependencies. Accessor, material, skeleton,
joint, node-name, animation-channel, and keyframe counts are expanded through
the engine's actual decoded C++ layouts without opening buffer payloads.
Attachment reservations include the retained attachment object and the second
material copy merged into the owning skinned mesh. Arithmetic saturates at the
largest `uint64_t` value, so hostile counts cannot wrap into a small admission
request.

The runtime payload counters and startup estimates share
`preparedPayloadBytes` as their definition. Structural tests derive static,
skinned, skeleton, and animation totals from a document whose external buffer
is deliberately absent. The production-catalog test requires a nonzero model
reservation for every shipped model and compares every selected animation
reservation with its decoded clip. Each distinct model, attachment, or
animation document is parsed once per catalog collection; request and frame
paths remain free of metadata I/O.

Texture reservations inspect the exact representation selected after Vulkan
device capability is known. If a valid BC7 artifact is available, its mip
records and compressed byte arrays determine the reservation. Otherwise the
external file, glTF buffer view, or data URI is inspected for the exact RGBA8
payload size. The inspector and loader share artifact selection and validation,
and tests require every supported source form to match its decoded payload.

The production scheduler uses a 32 MiB prepared-memory budget. The baseline's
32 retained models used 20,307,264 bytes, leaving about 11.7 MiB of headroom for
the normal two-job decode concurrency. Upload staging is bounded separately by
the 64 MiB upload ring and remains included in transient-memory telemetry.

Before starting a decode, the scheduler admits its cached logical payload only
when retained prepared data plus active decode reservations remains within the
budget. A zero or unavailable estimate reserves the full budget and therefore
runs alone. An asset whose known size exceeds the entire limit also runs alone
when no prepared data or decode reservation is live; an oversized-start counter
makes this escape path visible. Priority and FIFO order are preserved when the
head request is budget-blocked. Decode completion releases the reservation as
the actual payload becomes retained, and publication or failure releases that
payload. A decoded payload larger than a nonzero reservation fails explicitly
instead of silently exceeding the limit.

The enforced pressure workload uses a deliberately small 4 MiB test budget. It
holds residency admission until prepared data fills the budget, requires
additional queued decodes to pause with no job active, then releases admission
and requires all 32 models to publish. It also verifies that every held
CPU-ready model is residency-deferred and that the retained prepared total does
not exceed the limit. Runtime and debug-panel telemetry report retained bytes,
active decode reservations, the configured limit, budget deferrals, and
oversized starts.

Three Release runs of the enforced workload on the same GPU produced:

| Run | Limit | Held payload | Held ready / queued | Budget deferrals | Transient peak | Workload time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 4,194,304 B | 3,807,612 B | 6 / 26 | 5 | 4,484,770 B | 81,244 us |
| 2 | 4,194,304 B | 3,807,612 B | 6 / 26 | 5 | 4,484,770 B | 81,907 us |
| 3 | 4,194,304 B | 3,807,612 B | 6 / 26 | 5 | 4,484,770 B | 87,090 us |

Verification:

- Full Debug warning-as-error build.
- Full Release warning-as-error build.
- Debug CTest registry: 80 of 80 passed.
- Release CTest registry: 80 of 80 passed.

MQ-01 is complete. The forward-looking roadmap begins with MQ-02.
