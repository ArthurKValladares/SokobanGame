# Skinned-mesh packing

Captured 2026-09-10 for MQ-02.

## Finding

Skinned publication converted the retained `SkinnedMeshData` into complete GPU
vertex and index vectors before asking the residency budget for permission.
Every refused publication discarded both vectors. A later retry repeated the
conversion and both allocations before making the same admission request.

The 32-model prepared-asset pressure workload established the cost. Each Rogue
model produces 655,880 bytes of packed GPU geometry in two temporary vector
allocations. Before this change, the number of packing passes was exactly the
32 successful publications plus the recorded residency-refusal attempts:

| Release run | Residency refusals | Derived packing passes | Derived allocations | Derived temporary bytes | Upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 15 | 47 | 94 | 30,826,360 B | 20,988,160 B |
| 2 | 18 | 50 | 100 | 32,794,000 B | 20,988,160 B |
| 3 | 16 | 48 | 96 | 31,482,240 B | 20,988,160 B |

These packing totals are derived from the observed refusal count and the old
control flow. The old code packed once immediately before every admission
attempt and used one vertex-vector allocation and one index-vector allocation
per pass. The upload total is the unchanged 32-model payload.

## Change

`inspectGpuSkinnedMeshLayout` now validates palette limits, base and attachment
indices, attachment nodes, and 32-bit draw-count limits while calculating exact
vertex and index upload sizes without allocating packed vectors. Publication
uses that layout for residency admission. Only an admitted model calls
`packGpuSkinnedMesh`, which reserves the exact two output counts, converts the
vertices and indices together, and passes the paired result to the geometry
uploader.

`VulkanModelResources::LoadingStats` now exposes cumulative packing passes,
temporary-vector allocations, allocated temporary bytes, peak temporary bytes,
and successfully submitted skinned upload bytes. The debug panel and smoke log
show the same totals. Transient-memory peak sampling includes packed capacity
while the decoded source and upload-ring reservation are also live.

The residency-retry smoke path requires zero packing work while admission is
denied and exactly one pass with two allocations after admission succeeds. The
pressure workload requires one pass per published model regardless of refusal
count.

## Result

Three Release runs on an NVIDIA GeForce RTX 4060 Laptop GPU produced:

| Run | Residency refusals | Packing passes | Allocations | Temporary bytes | Temporary peak | Upload bytes | Workload time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 20 | 32 | 64 | 20,988,160 B | 655,880 B | 20,988,160 B | 82,165 us |
| 2 | 15 | 32 | 64 | 20,988,160 B | 655,880 B | 20,988,160 B | 82,642 us |
| 3 | 23 | 32 | 64 | 20,988,160 B | 655,880 B | 20,988,160 B | 83,523 us |

Packing is now independent of residency retries. Against the three baseline
runs, it removes 15–18 passes, 30–36 temporary-vector allocations, and
9,838,200–11,805,840 bytes of allocation churn, a 31.9–36.0 percent reduction.
The 20,988,160 upload bytes are unchanged.

The reported transient peak rises from 4,484,770 bytes to 5,140,650 bytes
because the counter now includes the 655,880-byte packed temporary that was
present but uncounted before this work. This is a telemetry correction rather
than an increase in the runtime's live allocations.

Verification:

- GPU-skinning unit suite: 86 checks passed in Debug and Release.
- The Vulkan residency-retry and 32-model pressure paths passed in Debug and
  Release.
- Full Debug and Release warning-as-error builds.
- Debug CTest registry: 80 of 80 passed.
- Release CTest registry: 80 of 80 passed.

MQ-02 is complete. The forward-looking roadmap begins with MQ-03.
