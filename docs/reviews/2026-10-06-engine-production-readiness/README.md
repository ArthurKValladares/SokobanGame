# Engine production readiness assessment

Assessed October 6, 2026, at commit 2aa898266a50a9e66593b375a001b225e467e208.

The engine is a capable, carefully engineered foundation for this Sokoban game. Its strongest areas are deterministic gameplay, transactional persistence and authoring, Vulkan resource lifetime management, and performance diagnostics. Its weakest existing areas are general audio, international text and accessibility, animation authoring, and reusable scene tooling. The asset pipeline and execution architecture are the largest constraints on expanding the engine because improvements to many other systems depend on them.

Rendering has a substantial distance to travel toward a broadly capable AAA renderer, but it is further developed than the least mature systems. Adding visual effects alone would leave the larger production gaps intact.

This assessment separates three questions: whether a system serves the current game, whether it can support a larger production team and content set, and whether it provides the capabilities required by a particular AAA genre. A focused puzzle engine can meet demanding production standards without acquiring every feature of a general engine.

## Scope and standard

“AAA-level” has no universal feature checklist. Here it means an engine that can reliably support a large content production, multiple disciplines working simultaneously, strict runtime budgets, release updates that preserve player data, and an explicitly supported hardware and platform matrix.

The baseline production requirements used here are:

- Content and saves have durable identities, versioned schemas, and defined compatibility policies.
- Artists and designers can create, diagnose, and validate supported content without routine engine programming.
- CPU time, GPU time, memory, I/O, audio processing, and loading latency have budgets and meaningful failure policies.
- Builds, cooked content, dependencies, and releases are reproducible and attributable to exact source and tool versions.
- Supported platforms and devices are tested under realistic workloads and lifecycle changes.
- Failures produce usable evidence, and releases can be maintained after launch.
- Localization and accessibility are built into the supported player experience.

Open worlds, networked multiplayer, advanced physics, facial animation, ray tracing, and virtualized geometry are conditional requirements. Their absence is a capability gap for projects that need them; it does not automatically make a single-player puzzle engine unfit to ship.

The comparisons to Unreal and Wwise below illustrate established production mechanisms. They are not a requirement to copy those products or adopt their architecture wholesale.

This is a source and repository-evidence assessment, supported by runs of existing test binaries. It is not a fresh rebuild certification, a new hardware benchmark, or a console certification review. Recommendations and workload examples are engineering judgments; numerical acceptance workloads are proposed starting points, not industry-wide standards.

## Architecture that exists today

| Area | Main implementation | Present architecture |
| --- | --- | --- |
| Application and lifecycle | Application, Window, Time, ShellFlow, SettingsCoordinator | One application owns the game flow, platform window, concrete renderer, audio, menus, saves, and developer tools. |
| Gameplay | Rules, GameplaySession, GameplayLoop, ActionPlan, ActionScheduler, Reservation, StateDelta | Pure discrete rules, stable gameplay entity IDs, planned actions, presentation timing, undo, checkpoints, and replay. |
| Rendering | VulkanRenderer, VulkanSceneRecorder, IsoScenePreparer and individual passes | Vulkan 1.3 backend, explicit resource management, CPU scene preparation, sorted and instanced draws, manually coordinated passes. |
| Materials and animation | GltfMesh, PbrMaterial, GpuMaterial, AnimationController, GpuSkinning | glTF core material support, CPU pose sampling, vertex-shader skinning, clip selection and two-clip crossfades. |
| Content | AssetManifest, ContentPipeline, RuntimeTextureCatalog | Manifest inventory, dependency validation, staged loose-file content, BC7 artifacts, local caching, package index. |
| Asset loading | AssetLoadScheduler, AssetLoadState, VulkanModelResources, ResidencyBudget | Prioritized background CPU preparation, render-thread publication, upload ring, eviction, mip-tail fallback. |
| Audio | AudioSystem, AtmosphericAudio | miniaudio playback, randomized sound sets, shared loops, proximity gain, streamed music and crossfades. |
| Input | InputState, InputBindings, InputRouter, InputActions.def | SDL devices, semantic Boolean actions, key chords, remapping, game/editor/menu routing, controller prompts. |
| UI | UiContext, FontAtlas, MenuKit, OptionsMenu, LecternDialog | Custom immediate drawing and layout with a fixed ASCII font atlas and game-specific screens. |
| Authoring | LevelEditor, LevelProjectStore, manifest and animation editors, SplatPainter, ShaderHotReload | Headless document models, ImGui interfaces, grouped undo/redo, transactional publication, selected live reload. |
| Execution and memory | TaskSystem, FrameArena, reusable scratch, VMA, geometry and upload allocators | Independent worker tasks, coarse parallel preparation, retained storage, tracked GPU allocation and residency. |
| Support and validation | Profiler, VulkanGpuProfiler, Log, CrashDiagnostics, CTest, packaging scripts | CPU traces, GPU timestamps, local logs and Windows minidumps, extensive unit suites, smoke and package gates. |

The ordinary frame runs events, game update, UI, render-frame construction, scene preparation, and Vulkan draw/present in sequence. Preparation can split independent work across workers, but the application does not currently run a general dependency graph or an independently advancing render pipeline. See [the application loop](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/Application.cpp:1047>).

Content takes a separate route: source assets and levels → validated inventory → staged content and compressed textures → startup package validation → background CPU loading → budgeted GPU publication → frame-owned use and retirement. This separation is one of the engine's valuable foundations.

## Relative maturity and distance

These labels describe scope and evidence, not a percentage of completion. “Established foundation” means useful production mechanisms exist, while broader scale or release validation remains incomplete.

| Existing system | Maturity within this game | Distance to broad AAA production use | Principal gap |
| --- | --- | --- | --- |
| Audio | Basic functional subsystem | Very large | Independent voices, event authoring, buses, spatial audio, budgets and profiling. |
| UI text, localization and accessibility | Functional menus; narrow text infrastructure | Very large | Unicode shaping, localization resources, adaptable accessible presentation. |
| Animation | Functional game-specific playback | Very large for character-heavy work | Authored graphs, skeleton binding, compression, layers, IK, LOD and tools. |
| Scene architecture and authoring | Strong puzzle authoring; narrow engine model | Very large for engine reuse | General scene composition, stable authored identity, prefab workflows and extension points. |
| VFX | Useful CPU particles and ribbons | Large | Designer authoring, simulation budgets, culling, scalability and richer rendering modes. |
| Task scheduling and frame execution | Sound independent-task foundation | Large | Dependencies, priorities, continuations, queue limits and contention diagnostics. |
| Asset cooking and team workflows | Established foundation | Large | Full cooking, durable registry, shared derived data, chunks and patching. |
| Runtime streaming | Established foundation | Large | Chunked I/O, time/byte publication limits, quality feedback and world streaming. |
| Platforms and release operations | Desktop foundation | Large for PC/console breadth | Platform services and recorded hardware, lifecycle and release acceptance. |
| Rendering and materials | One of the more developed systems | Large for visual breadth and scene scale | Render graph, scalable visibility/lighting, broader material and output workflows. |
| Input | Relatively well developed for puzzle controls | Moderate for current game; large for broader genres | Typed analog actions, player/device ownership, richer triggers and accessibility settings. |
| Persistence | Strong storage reliability | Critical compatibility gap | Supported migrations and stable content identity across releases. |
| Profiling, memory and automated quality | Strong local foundations | Moderate to large | Shipping diagnostics, complete budgets, hardware regression gates and broader fault coverage. |
| Deterministic puzzle gameplay | Strongest relative to intended scope | Smallest within current scope | Content-scale invariants, replay coverage and update compatibility. |

Physics, navigation, general AI frameworks, networking, scripting, cinematic sequencing, and platform online services do not appear as general engine subsystems in the inspected first-party code. Those belong in a separate conditional capability backlog. The solver and puzzle enemies should not be counted as a general AI/navigation system.

The capability ranking and implementation priority are different. Save migration and a reliable release gate deserve early attention even though they require less new infrastructure than a large animation system. Asset identity and cooking deserve early attention because they reduce the cost of work elsewhere.

## Audio

### Existing strengths

Audio handles device initialization failure, randomized variations with immediate-repeat avoidance, footsteps, drag/platform loops, short fades, streamed music, character/level soundtrack selection, music crossfades, previewing, and proximity-driven ambience.

Effects are decoded during AudioSystem construction; music uses streamed sounds. Construction runs concurrently with renderer initialization, reducing startup latency through background overlap. See [AudioSystem initialization](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/AudioSystem.cpp:94>) and [asynchronous application startup](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/Application.cpp:187>).

### Production gaps and their consequences

The most fundamental gap is the absence of independently managed playback voices. Each variation owns one ma_sound. playOneShot selects that object, seeks it to the beginning, changes its start time, and starts it. Repeated use of the same variation reuses that playback object. A busy scene cannot treat repeated instances of one sample as independent sounds with different positions, gain, lifetimes, and ownership. See [one-shot playback](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/AudioSystem.cpp:283>).

The API is also tied to gameplay orchestration. update receives walking, stone pushing, cart movement and elevator movement. A general engine would expose emitters and audio events, with game-specific orchestration layered above it.

Atmosphere measures the nearest portal or conveyor and changes a shared loop's gain. This is useful ambience, but it does not provide independently located emitters or acoustic propagation. One-shots accept a name and delay, without a source position or emitter handle. The integration does not establish a general listener/emitter spatial model.

Master/music/SFX controls are scalar volume management. The engine does not expose an authored bus hierarchy, send effects, mix snapshots, sidechain ducking, or gameplay-driven audio parameters. Those features matter when dialogue, music, UI, ambience and effects compete for intelligibility.

There is no engine-level system for voice priorities, concurrency groups, stealing policy, virtualization, audio residency, or diagnostic accounting. Backend threading and streaming do not supply an engine-facing production policy by themselves. Wwise's documented virtual voices demonstrate how inaudible or excess sounds can retain logical state while avoiding full processing cost. [Audiokinetic documentation](https://www.audiokinetic.com/en/library/edge/?id=concept_virtualvoices.html&source=SDK).

Every manifest effect is decoded and all music entries are initialized at startup. Background overlap helps startup latency, but loading scope still grows with the total catalog. Audio is not part of the existing Model/Animation/Texture load scheduler.

The tests establish cadence and atmosphere mathematics. They do not establish actual mixed output correctness, independent polyphony, real-time callback behavior, device switching or streaming starvation.

### Work needed

1. Introduce AudioEventId, EmitterHandle and VoiceHandle. Keep sound data separate from playback instances. Support stopping, querying and updating one voice without affecting another.
2. Move footsteps, cart loops and other gameplay rules into an orchestration layer that submits audio events.
3. Define global, bus and event concurrency limits, priorities, fade-based stealing and virtual voice behavior.
4. Add buses, DSP sends, mix snapshots, parameter-driven event selection and transitions. Begin with UI, effects, ambience, music and dialogue as the project's needs dictate.
5. Add listener/emitter transforms, attenuation and panning. Add occlusion, rooms, reverb and richer spatialization when required by the game.
6. Cook audio with explicit codec, channel, loudness, loop-point and streaming policy. Load banks or bundles on demand and account for decoded and streaming memory.
7. Provide an audio inspector showing live/virtual voices, routing, CPU cost, memory, stream starvation and event history.
8. Add offline mixing tests and real-device lifecycle acceptance.

A production acceptance fixture should repeatedly trigger the same sound at several positions, verify independent playback and defined concurrency behavior, and exercise streaming music during heavy content loads. For an initial stress target, try 128 physical voices and hundreds of logical emitters, then adjust to the supported hardware budget. The target must cover mixing cost and memory, not just successful playback.

miniaudio is a viable backend. The missing layer is engine policy and authoring. Choosing a middleware product instead would still require integration, data ownership, budgets and tests.

## UI text, localization and accessibility

### Existing strengths

The game has real font rendering, menus, pointer interaction, controller navigation, dynamic binding prompts, tutorial text, wrapping and pagination, tested layout behavior, and bounded frame drawing storage. UI commands use an arena with an 8,192-command budget and overflow telemetry.

### Production gaps and their consequences

FontAtlas contains characters 32 through 126: 95 printable ASCII characters. Its glyph API accepts char; measuring and drawing traverse the string byte by byte. Unsupported bytes resolve to a question mark. A UTF-8 character can therefore become several fallback glyphs. Adding translation files would not make accented Latin, CJK, Arabic or other scripts render correctly. See [the font character range](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/ui/FontAtlas.hpp:39>) and [glyph fallback and measurement](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/ui/FontAtlas.cpp:104>).

The player UI does not have a localization resource/catalog layer or a general shaping and bidirectional layout pipeline. Dynamic tutorial prompts are a good foundation, but translated text also needs stable string keys, plurals, formatting, fallback and context for translators.

Accessibility is broader than remapping. The settings model does not establish a general policy for scalable UI, high contrast, non-color cues, reduce motion, hold/toggle preferences, screen narration or subtitles. Some of these depend on the title; internationally usable text, adaptable layouts and legible controls are broad production concerns.

The draw API is a useful immediate renderer but does not expose a general semantic accessibility tree. Extending the UI solely through visual draw commands would make narration and focus inspection difficult. A production UI can remain immediate mode if it emits stable semantic control records alongside rendering.

### Work needed

- Decode UTF-8 into text runs and operate on Unicode code points and grapheme boundaries where appropriate.
- Use a shaping pipeline with glyph positioning, script support, bidirectional text and font fallback. Cache shaped runs separately from atlas placement.
- Support dynamic glyph atlas pages and control their memory and upload budget.
- Introduce stable localization keys, extraction, translator context, plural rules, argument formatting and missing-key diagnostics.
- Make text layout responsive to longer translations, UI scale and safe areas. Preserve binding prompt embedding through localized rich text.
- Define semantic focus, navigation and accessibility metadata for controls.
- Add the title's accessibility settings, including visual alternatives for color-dependent information and adjustable motion/readability.
- Build automated pseudo-localization, long-string and screenshot/layout tests.

Acceptance should include accented Latin, CJK, right-to-left text and mixed-script input prompts; large text; long translations; all supported resolutions; and complete keyboard/controller navigation. Check text and controls for overlap and clipping rather than verifying only that glyphs exist.

This is one of the highest-return improvements for the current game because the present ASCII limitation is a hard boundary.

## Animation and character presentation

### Existing strengths

Animation supports glTF LINEAR, STEP and CUBICSPLINE interpolation, looping and clamped clips, per-instance playback, two-clip crossfades, attachments, GPU skinning, semantic catalog bindings, timeline events and event gates, editor previews and unit tests.

### Production gaps and their consequences

AnimationController emits a request containing a source clip, a target clip, two sample times and a blend factor. Its playback state is built around choosing one active animation and crossfading from another. This is a useful playback controller, but it does not provide layered graphs, additive poses, blend spaces, synchronization groups, root motion, procedural constraints or an animation authoring graph. See [the controller request and state](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/AnimationController.hpp:19>).

Clips target skeleton nodes by name. During sampling, the engine builds a node-name map and ignores channels whose names do not match. This makes the current supported rigs easy to use, but a larger pipeline needs explicit skeleton compatibility and binding diagnostics. Name matching is not general retargeting. Duplicate names, missing tracks and differing bind poses must have defined behavior. See [pose sampling](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/GltfMesh.cpp:1571>).

The loader takes the first skin from a document, uses four influences per vertex, and skips morph-weight channels. These are supported-content limits, particularly relevant to multi-rig assets, faces and deformation-heavy characters. Content validation should make unsupported features explicit before runtime.

Skinning has fixed limits of 128 joints, 128 nodes and 256 skinned instances per frame. Exceeding the instance budget causes dropped skinning instances and unavailable poses. Increasing constants moves the boundary without creating an animation scalability policy. See [skinning capacities](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/GpuSkinning.hpp:11>).

Each GPU instance reserves a maximum-size palette. Pose sampling still runs on the CPU and creates pose vectors and a name map. There is no visible distance/visibility-driven update-rate, bone LOD or pose-sharing system. Existing allocation measurements are scoped and should not be interpreted as proving every animation workload is allocation-free.

### Work needed

1. Cook skeletons and clips into versioned runtime data. Bind tracks to stable bone indices once, report unsupported/missing bindings, and measure compression error against an explicit tolerance.
2. Build reusable pose buffers and job-based sampling. Allocate palette storage according to the rig and workload.
3. Add the graph features the game needs: state transitions, blend spaces, additive layers, masks and synchronized locomotion first.
4. Define root-motion ownership and event semantics under loops, transitions, reduced update rates and replay.
5. Add IK, retargeting, morphs and procedural control when the character brief requires them.
6. Introduce visibility and distance budgets, animation update rates, skeleton LOD and pose sharing.
7. Expand tools with graph debugging, active weights, skeleton/track inspection, event tracing and clip comparison.

A meaningful acceptance scene should animate hundreds of varied rigs, including transitions and attachments, under a CPU/palette budget. Verify event delivery through crossfades and time jumps, explicit rejection of incompatible skeletons, and graceful behavior above capacity.

A graph need not be visually authored at first; a validated data description and clear debugger can establish the same ownership and execution model. Unreal's Animation Blueprint system illustrates designer-controlled blending and bone manipulation as an established workflow. [Epic documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprints-in-unreal-engine).

## Asset identity, cooking and team production

### Existing strengths

The asset pipeline is much more developed than a simple file-copy stage. It validates the manifest and glTF dependency graph, resolves embedded/external texture sources and material semantics, rejects invalid paths, stages through a temporary tree, includes notices, generates native BC7 KTX2 artifacts, creates mip chains as required by sampling, uses a content-addressed local texture cache, and validates a versioned package index.

Developer staging can skip unchanged content. Cache keys include input bytes, texture interpretation and encoder revision. Shipping staging is clean. These are good foundations to retain. See [ContentPipeline's contracts](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/ContentPipeline.hpp:84>).

### Production gaps and their consequences

Asset runtime IDs are ordered manifest indices. Appending preserves current IDs, and names resolve references, but the system does not establish a durable asset registry independent of path and ordering across branches, renames and release patches. Editor code contains substantial remapping logic for numbered screens and associations; this solves real current cases, but broader production needs a systematic identity boundary. See [manifest ID contracts](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/AssetManifest.hpp:204>).

Models and animations are loaded from glTF at runtime. Texture cooking has advanced further than mesh, skeleton, animation and audio cooking. Parsing interchange files repeatedly introduces avoidable runtime CPU work, temporary memory and file fan-out. See [runtime model loading](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/VulkanModelResources.cpp:669>) and [animation loading](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/VulkanModelResources.cpp:771>).

There is a local texture derived-data cache, but no general shared cache/cooker for all asset classes. Multiple machines repeat import work, and content builders do not expose a general dependency/rebuild service. Unreal's DDC illustrates a hierarchy of disposable local and shared derived data; its documentation also distinguishes development caches from cooked shipping content. [Epic documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-derived-data-cache-in-unreal-engine).

Incremental staging primarily checks source size and modification time before skipping the whole stage. This is practical locally, but changes with preserved size/timestamp can evade that fast path. A reproducible distributed build needs content digests and explicit dependency/tool identities as its authoritative keys.

content.index records paths and sizes, counts and total bytes. Validation checks containment, existence, size and complete inventory, but it does not verify file contents through per-file digests. Same-size corruption can pass this boundary. This is a limit of package integrity verification, separate from the stronger texture-cache key. See [index generation](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/ContentPipeline.cpp:151>) and [package verification](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/ContentPipeline.cpp:1014>).

The package is a loose-file tree. It does not have engine-level runtime chunks, delta patch manifests, install groups or asset-bundle ownership. Startup verifies and enumerates the tree; that policy becomes expensive as file counts grow.

BC7 is the prepared texture format. A capability-based RGBA fallback exists, so unsupported BC7 does not simply mean total failure. However, fallback is not an efficient platform-specific cooking strategy.

### Work needed

- Introduce durable authored AssetIds and a registry mapping IDs to source paths, dependencies, import settings, revisions and cooked artifacts. Resolve IDs to compact runtime handles after loading.
- Treat rename, move, delete, duplicate and redirect as explicit registry transactions.
- Generalize cooking to meshes, skeletons, clips, materials, audio and levels. Bake layout conversion, binding and validation before runtime.
- Build a dependency graph with authoritative input digests. Keys should include tool/schema versions, target platform and quality settings.
- Extend the current local cache into a disposable shared derived-data cache with verified publication and garbage collection.
- Add deterministic chunk/archive creation, per-chunk integrity, asset bundles and patch/install manifests.
- Validate shipping packages and their metadata without an unbounded startup scan. Verify chunk data when consumed or during installation as the product requires.
- Provide dependency and reverse-reference views, import diagnostics, missing-resource reports and bulk validation in the editor.
- Add target-specific texture/audio formats and quality variants.

Start with identity and one complete mesh/animation cooking path. Those changes are prerequisites for robust structural reload, streaming, collaboration and patching. A remote cache service should follow a stable artifact/key contract.

Acceptance should demonstrate that moving or renaming an asset preserves every authored reference; identical inputs yield identical cooked outputs on clean builders; changed dependencies invalidate the correct outputs; same-size corrupted chunks are detected; and a small asset edit produces a proportionate patch.

## Runtime loading and streaming

### Existing strengths

The runtime separates CPU preparation from Vulkan publication. Visible assets take precedence over speculative prefetches, queued prefetch can be canceled, prepared memory has reservations, models/textures have residency limits, resources remain charged until frame-safe retirement, and compressed textures can load a smaller complete mip tail.

The default scheduler has two CPU jobs, one publication per frame, 32 MiB of prepared payload, 128 MiB of model residency and 256 MiB of texture residency. A separate 64 MiB upload ring manages staging. These are explicit policies, not an absence of budgeting. See [loading budgets](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/AssetLoadScheduler.hpp:35>).

### Production gaps and their consequences

Loading units are whole model, animation or texture objects. Active filesystem work cannot safely be interrupted. There is no general prioritized asynchronous I/O layer for chunks with resumable decode and fine-grained cancellation.

A publication-count limit bounds the number of operations, but one large operation may still exceed a frame-time budget. The renderer explicitly publishes at most one ready asset in its maintenance path. Admission should consider time, bytes, temporary space and deadlines as well as operation count. See [frame maintenance](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/VulkanRenderer.cpp:529>).

Texture mip fallback chooses a complete tail that fits the available residency at publication. This is useful degradation, but the inspected path does not establish continuous screen-footprint-driven mip requests and promotion as camera distance and pressure change. That is a distinct stage of streaming maturity.

Budget figures describe logical payload and selected pools. Audio, swapchain targets, driver allocations, temporary conversion, and other process memory have different ownership and accounting. Hardware heap telemetry exists through VMA, but it is not a unified adaptive budget policy.

A resource set that cannot fit may remain unavailable. Diagnostic counters are valuable, but production needs a defined end-user policy: acceptable placeholders, lower-detail substitutions, terminal errors and retry deadlines. Otherwise a mathematically correct hard budget can still leave the player waiting indefinitely.

### Work needed

1. Load cooked chunks through a prioritized I/O service with deadlines, cancellation tokens and bounded decode work.
2. Add upload byte/time budgets and incremental publication for large assets.
3. Model resource dependency readiness explicitly and specify fallback for each asset class.
4. Use screen footprint and predicted visibility to request quality; add hysteresis to avoid promotion/eviction oscillation.
5. Coordinate budgets across actual platform heaps and transient peaks, including audio and render targets.
6. Add world/scene residency ownership when streamed worlds are required.
7. Expose request-to-visible latency, I/O queues, decode cost, failed requests, quality debt and residency churn.

Acceptance should stress rapid scene changes, slow storage, constrained GPU memory, missing assets and an oversized required set. Rendering and input must remain responsive, total memory must stay bounded, and the engine must reach either usable content or a clear terminal result.

## Scene architecture, gameplay extension and editor workflows

### Existing strengths

Gameplay has explicit state, stable entity IDs, pure rules, state deltas, reservations, action planning, concurrent presentation handling and replay. The editor has a headless model, grouped strokes, undo/redo, dirty-draft protection, source/runtime publication, decoration gizmos, splat painting, project operations and cached browser listings.

These are substantial production features for puzzle authoring, including drag painting, redo, shortcuts and browser caching.

### Production gaps and their consequences

The engine's data model is strongly organized around Players, Movables, Enemies, Elevators, Minecarts and other puzzle devices. Decorations are rendered separately from gameplay. There is no general scene composition and component/service model spanning arbitrary authored entities.

Application includes and owns the concrete VulkanRenderer and coordinates many system consequences directly. InputRouter produces a frame containing specific gameplay, title, options and editor inputs. Those choices are reasonable for one game but make engine reuse expensive. Every new domain tends to expand known structs and central routing.

The CMake boundaries separate core, UI and Vulkan, and the core is checked for Vulkan independence. That is valuable layering. However, sokoban_core still contains gameplay, authoring, content, audio and several render-domain preparation systems. It is a broad project library, rather than a general engine kernel with separate game modules.

Editor undo records store before/after document snapshots. This gives simple correctness and fits current boards. At much larger scene sizes, history needs memory limits, compact commands/deltas and transaction-aware object identity. See [snapshot history](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/LevelEditor.hpp:422>).

The tools have no general property/schema reflection, prefab inheritance/overrides, reference finder, scene hierarchy, batch processing or designer scripting layer. Model reload and structural manifest reload also remain limited: structural changes request a restart. The watcher uses file sizes and timestamps and does not report deletions. See [structural reload handling](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/Application.cpp:2455>) and [watcher contracts](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/SourceWatcher.hpp:11>).

Collaboration is primarily through ordinary project files and source control. There is no editor-level workflow for checkout/locks, revision comparison, conflict diagnosis or partial ownership of large scenes.

### Work needed

- Define an authored scene schema with durable entity IDs, composable data, serialization and ownership rules. An ECS is one possible implementation, not a requirement.
- Separate general services from Sokoban-specific orchestration along demonstrated reuse boundaries.
- Add a consistent property/schema metadata layer for editing, validation and versioning.
- Introduce prefab/template instances, reusable authored compositions and override diagnostics.
- Add reference finding, safe rename/delete, source-control status and conflict handling.
- Make undo memory bounded and retain transaction correctness as snapshots evolve into commands or deltas.
- Generalize asset reload around revisions/generations, then support models, skeletons and structural registry changes safely.
- Provide stable extension APIs for editor panels, importers and gameplay systems.
- Add scripting or data-driven behavior only when designer iteration requires it.

Prove reuse with a second small project or a deliberately different scene workflow. A renderer demo using the same board records would not demonstrate general scene architecture. Preserve the pure puzzle rules as a game module while the scene/service boundary evolves.

## Task scheduling and frame execution

### Existing strengths

TaskSystem provides futures, stack-owned scoped tasks, parallel loops with caller participation, exception propagation, construction failure cleanup and retained queue storage. Scoped lifetimes keep captured references valid during unwinding. Independent scene/shadow/particle preparation and startup work already overlap.

### Production gaps and their consequences

The TaskSystem contract explicitly prohibits blocking on other tasks because there is no dependency tracking or work stealing. This is a fundamental limit for graphs involving animation, visibility, physics, streaming and rendering. See [the task contract](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/TaskSystem.hpp:33>).

Workers share a mutex-protected FIFO queue. Its storage can grow; there is no general job priority, deadline, admission or producer backpressure. Asset priority is applied before tasks enter this worker system. A long or flooded background workload can therefore compete with latency-sensitive work without scheduler-level policy.

The default global pool creates hardware-concurrency minus one workers, and other asynchronous services have their own lifetimes. Thread count alone does not establish an optimal policy across low-core devices, integrated GPUs, audio, foreground work and blocking I/O.

The ordinary application frame remains sequential at major phases. Coarse internal parallelism is useful, but there is no graph critical-path model, independently scheduled render frame or explicit multi-system dependency contract.

### Work needed

1. Add task handles, dependency completion and continuations so workers can yield rather than occupy threads while waiting.
2. Distinguish latency-sensitive CPU jobs, background CPU work and blocking I/O. Apply priorities and bounded admission.
3. Add cancellation, owner lifetime and shutdown semantics to the task API.
4. Introduce work stealing or another measured load-balancing strategy.
5. Name and trace tasks, dependencies, queue time, worker utilization and waits.
6. Express a few existing workloads as graphs before generalizing the full frame.
7. Add cooperative waiting/continuations and tests on one-worker configurations.
8. Introduce render pipelining only with explicit data ownership, latency targets and evidence of a CPU benefit.

Acceptance should saturate background decode while running foreground preparation, use one or two workers, inject task failures, cancel owners and shut down. There must be no dependency deadlock, runaway queue growth or unbounded foreground delay.

For the present discrete game, retain its rules semantics. A fixed-step continuous simulation layer belongs with future physics or network prediction; rendering every frame does not require rewriting the deterministic puzzle rules.

## Rendering, lighting and materials

### Existing strengths

The renderer has Vulkan 1.3 dynamic rendering and synchronization2, descriptor-capacity checks, runtime texture descriptor indexing, VMA allocation, geometry suballocation, a mapped upload ring, frame-safe retirement, persistent driver pipeline cache, pipeline-only reload recovery, GPU timestamp profiling and evidence capture.

Rendering features include core glTF PBR maps, tangent-space normals, metallic/roughness, emissive and occlusion maps, opaque/masked/blended policy, GPU skinning, sun and point shadows, frustum/range culling, cached point-shadow faces, instancing, sorted model draws, batched particles, SSAO, atmosphere, water, internal HDR scene targets, bloom and tonemapping.

The renderer also has careful present-semaphore ownership and swapchain/resource reconfiguration. These are meaningful correctness mechanisms and should survive any architectural modernization.

### Architecture gaps

Pass scheduling and resource transitions are coordinated manually through the scene recorder and swapchain/pass helpers. New postprocessing or multi-queue work increases the number of ordering, feedback-loop and lifetime cases engineers must reason about manually.

A render graph would let passes declare reads/writes and resource lifetimes, validate dependencies, and derive transitions. Add transient aliasing and queue overlap after those contracts work. Epic's RDG documents dependency validation, resource lifetime optimization, pass culling, parallel recording and asynchronous-compute scheduling as concrete examples of this approach. [Epic documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine).

The draw path is CPU-prepared. Static opaque draws can instance; skinned draws are not included in those batches. There is no visible engine mesh-LOD hierarchy, hierarchical-depth occlusion system or GPU-generated indirect visibility path. Larger scene scaling will need a spatial hierarchy and geometry detail policy before simply increasing draw capacities. See [batch formation](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/VulkanSceneRecorder.cpp:2797>).

General renderer reuse is also constrained by board-specific scene preparation and specialized draw data. A render-world extraction layer should make persistent objects and frame views independent of puzzle tiles.

### Visual capability gaps

Lighting has one scene sun, flat ambient light and a fixed eight-point-light array. The fragment shader loops over up to eight lights. Adding hundreds of lights by increasing the array would increase shading cost and descriptor/shadow pressure. Clustered or tiled light assignment and independently budgeted shadows are the appropriate next scale mechanisms. See [the light capacity](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/render/RenderTypes.hpp:213>) and [the shader light loop](<C:/Users/arthu/Documents/Projects/Sokoban Game/shaders/triangle.frag.glsl:403>).

There is no general environment-probe/IBL pipeline or indirect-lighting system in the inspected renderer. Metallic/roughness material support alone cannot reproduce a broad range of reflective environments. Add environment maps and probes before pursuing costly global illumination for its own sake.

Water uses screen-space/refraction/reflection effects; a general reflection system and material authoring framework remain separate needs. Existing PBR functionality is a good foundation, but shader families are manually authored and tied to specialized draw modes. Production artists need a supported material domain, parameter instances, validation and previewing. A full visual material graph is optional if a smaller domain covers the title.

Internal half-float HDR exists. Display HDR output, calibration and gamut/transfer-function selection are different capabilities; the current output path targets the surface/display format with SDR tonemapping. Do not count HDR scene values as proof of HDR display support.

No general temporal reconstruction/motion-vector pipeline appears in the source. MSAA and manual render scale can be appropriate for this art style. TAA/upscaling, automatic resolution control, motion blur and related effects should follow a measured visual/performance requirement.

Broader sun-shadow ranges, richer light types, decals, terrain/foliage systems, virtualized geometry and ray tracing are genre-dependent. Prioritize the intended content rather than treating every omission as a release blocker.

### Work needed and order

1. Define render-world objects, view data and material contracts independently of board records.
2. Introduce resource declarations and dependency validation around existing passes.
3. Add spatial hierarchy and mesh LOD; measure CPU submission and overdraw at increased content scale.
4. Add scalable light assignment and separate shadow budgets.
5. Add environment lighting/probes and material instances/tooling.
6. Improve quality tiers and automatic budget control.
7. Add temporal reconstruction or advanced reflection/GI paths only when visual goals justify them.
8. Add additional graphics backends when supported platform requirements justify them.

Acceptance needs reference scenes covering many materials, lights, animated characters, transparency, reflections and repeated resource reloads. Compare CPU/GPU phase time, transient memory, dropped draws and image quality on discrete and integrated GPUs.

The October 5 water cache review reports 24–25% translucency improvement and 9–11% whole-GPU-frame improvement on one RTX 4060 Laptop GPU at 1280×720. It also records a 12,000-frame validation run. This establishes effective targeted optimization, not performance at arbitrary AAA scale. See [the measured water work](<C:/Users/arthu/Documents/Projects/Sokoban Game/docs/performance/2026-10-05-water/README.md:33>).

## VFX

The particle system supports randomized CPU bursts, trails and ribbons, alignment, HDR emission, nine-slice textures, delays and drawing order. Batched submission and parallel preparation improve its scaling.

The remaining subsystem is a CPU vector of particles plus ribbons, updated and removed each frame. It does not expose a designer effect graph, authored curves/modules, effect bounds and significance, global admission policy, simulation LOD, collision events, GPU simulation, or a broad set of particle rendering types. See [simulation](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/ParticleSystem.cpp:165>).

The first improvement should be authoring and budgets: reusable effect assets, parameter curves, previewing, bounds, spawn limits, distance/visibility policies and per-effect diagnostics. Add CPU/GPU execution choices according to workload, preserving deterministic seeds for tests where useful.

Later needs may include mesh particles, flipbooks, lighting, depth fade, collision and simulation events. GPU simulation is valuable at high counts but adds synchronization, event-readback and reproducibility complexity.

Acceptance should cover many simultaneous effects, rapid emit/cancel cycles, extreme input values, offscreen effects and capacity exhaustion. Distinguish particle update cost, preparation cost, draw count and pixel overdraw; a low draw count does not imply an inexpensive effect.

## Input and device handling

Input is more complete than the weakest systems. It already centralizes semantic actions, names, aliases, contexts and defaults; supports modifier chords and remapping; tracks gamepad connection/remapping/focus; routes captured events; and changes prompts with the active device.

Its generalization limit is the action value model. Gameplay consumes pressed/down Booleans, including thresholded gamepad axes. There is no general scalar/vector action stream, radial dead-zone/response pipeline, rich trigger state machine, timestamped action history or per-local-player device ownership. Mouse buttons exist as raw editor input but are not a binding variant alongside keyboard and gamepad bindings. See [bindings](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/InputBindings.hpp:75>) and [device state](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/Input.hpp:118>).

One active gamepad supplies the current arrays, although several can be opened. That is different from simultaneous local-player ownership. The first-party integration also does not provide a general haptics, gyro or touch system.

Epic's Enhanced Input documents typed action values, context priority, modifiers, dead zones and trigger states as established mechanisms for broader controls. [Epic documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine).

Work should add typed action values while preserving simple digital puzzle actions; dynamic prioritized contexts; mouse bindings; configurable dead zones, sensitivity and hold/toggle behavior; user/device association; and timestamped recording for diagnosis and replay. Haptics and advanced device APIs follow supported hardware needs.

Acceptance should combine virtual-device automation with physical controllers: reconnect, focus changes, remapping while held, simultaneous devices, noisy sticks and modal capture. Save-format compatibility must preserve bindings across action-registry changes.

For the current game, input refinement and accessibility settings have a better near-term return than replacing the input stack wholesale.

## Persistence and compatibility

Storage reliability is a relative strength. Saves use durable atomic replacement, backups, recovery candidates, deletion markers, typed asynchronous results, checkpoint restoration and future-format preservation. These are carefully designed failure boundaries.

The major release gap is explicit: there are no profile migrations. The current profile format is 35; older formats are set aside and the player starts fresh. This is an agreed early-development policy, but it cannot serve a long-lived release that promises progress preservation through updates. See [the version policy](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/PlayerProfile.hpp:17>) and [fresh-start behavior](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/SaveStore.cpp:367>).

Required work is a supported migration chain from the first public format onward, golden historical fixtures, backup-before-migrate, interruption-safe migration commits, explicit incompatibility handling and stable content identifiers. Progress and settings should evolve independently so adding a binding or video option does not invalidate campaign progress.

Level updates also need policy: how a checkpoint behaves after its puzzle content changes, how renamed/deleted screens map, and whether old undo histories remain valid. Cloud saves add conflict and deletion semantics if the product needs them.

Acceptance should load each supported release fixture, migrate it, round-trip it, interrupt every migration boundary, and exercise downgrade/future-version behavior. “A backup exists” is not enough if the new build cannot interpret its contents.

## Memory, profiling and operational diagnosis

These systems have stronger foundations than their general capability breadth might suggest. Frame arenas, retained scratch, the upload ring, geometry/material suballocators, VMA statistics, residency counters, per-thread CPU capture, Perfetto export and nonblocking GPU timestamps already exist.

The October 5 allocation audit measures zero C++ heap allocations after warm-up in specific steady-frame paths. It explicitly excludes C allocators, audio backends, drivers, OS allocations and several event/tool paths. Preserve those measurements and their limits. See [the allocation audit](<C:/Users/arthu/Documents/Projects/Sokoban Game/docs/performance/2026-10-05-frame-allocations/README.md:41>).

The next gap is completeness: per-subsystem CPU ownership, temporary decode peaks, fragmentation, high-water trends, cross-pool GPU pressure, audio memory and driver/OS headroom. Logical asset payload is not total process memory. Add memory tags, lifetime categories and central budget reporting before replacing allocators.

CPU profiling scope macros are compiled out when developer UI is disabled. GPU/frame telemetry continues through its own paths, but a shipping build lacks the same scope-level CPU tracing. A controlled profiling build or low-overhead shipping capture mode would make customer-only hitches easier to investigate. See [profiling compilation policy](<C:/Users/arthu/Documents/Projects/Sokoban Game/src/engine/Profiler.hpp:128>).

Crash support provides local logs and Windows minidumps, and packaging can retain matching symbols. A broader operational workflow needs build IDs, symbol indexing, GPU breadcrumbs/device-fault evidence where supported, repro bundles and crash grouping. Add collection/transfer only according to the product's support and consent policy.

Acceptance should run repeated transitions, reloads and suspend/resume over hours, with bounded memory and stable retained capacities. Correlate CPU queues, GPU phases, I/O, asset readiness and audio in one timeline. Recovery or termination must produce actionable evidence without relying on a functioning renderer.

## Platforms, packaging and release quality

The project has Linux and Windows build/test workflows, sanitizer and static-analysis jobs, profile fuzzing, Vulkan smoke tests, a clean-extraction package gate, runtime/symbol separation and optional installer/signing workflows. The quality infrastructure is substantial.

The supported runtime path remains SDL plus concrete Vulkan. There is no implemented general platform-services boundary for user accounts, achievements, cloud storage, online sessions or platform-specific graphics/storage. macOS and consoles are not established supported targets in this tree.

Platform breadth should begin with an explicit promise. If Windows/Linux are the intended targets, harden those rather than adding abstraction with no consumer. If consoles or macOS are required, investigate graphics, lifecycle, storage, device/user ownership and platform services early; those requirements can change asset and runtime architecture.

The release validation document has a hardware and lifecycle matrix, but its acceptance rows are blank. Historical renderer evidence is primarily from one NVIDIA laptop, while hosted Linux smoke uses a software Vulkan implementation and hosted Windows omits hardware smoke. Those are useful correctness checks, but they do not establish AMD, Intel, mixed-DPI, controller, upgrade or long-duration real-device readiness. See [release acceptance](<C:/Users/arthu/Documents/Projects/Sokoban Game/packaging/ReleaseValidation.md:127>) and [CI](<C:/Users/arthu/Documents/Projects/Sokoban Game/.github/workflows/required-tests.yml:85>).

Next steps are recorded supported-device acceptance, automated scenario replays, long-duration tests, regression budgets on controlled hardware, stable release/symbol manifests and update-compatibility tests. Extend fuzzing beyond profile JSON into native content boundaries and corrupted cooked data. Add concurrency validation where worker ownership changes.

A 240-frame smoke is a useful gate; it is not sufficient evidence for hours of streaming, device loss, suspend/resume or repeated editor transactions.

## Verification results and confidence

The configured Release and optimized-editor CTest registries list 95 suites.

For this assessment, the initial run used the existing Release binaries and excluded Vulkan smoke, validation teardown and shipping-package validation. It passed 84 of 92 suites and failed eight. Those binaries were dated October 5 and predated later source/content work, so those eight failures are not treated as eight current engine defects.

The newer optimized-editor binaries passed 90 of the same 92 selected suites. The remaining failures are:

- asset_manifest_editor: six checks in external sound import.
- content_pipeline: the draft missing sound path audio/missing.ogg is rejected as escaping its content root.

Both reproduce in isolated single-suite execution and with temporary directories redirected under the workspace. The October 5 allocation audit independently records these same two failures in its 93/95 result, including its hardware smoke tests. This confirms they predate this assessment. Their root cause was not established here; they still prevent claiming an entirely passing production gate.

Current-run outputs accompany this review in [optimized-editor results](<C:/Users/arthu/Documents/Projects/Sokoban Game/docs/reviews/2026-10-06-engine-production-readiness/evidence/engine-readiness-dev-fast-tests.txt>), [isolated content reruns](<C:/Users/arthu/Documents/Projects/Sokoban Game/docs/reviews/2026-10-06-engine-production-readiness/evidence/engine-readiness-content-tests.txt>) and [workspace-temporary reruns](<C:/Users/arthu/Documents/Projects/Sokoban Game/docs/reviews/2026-10-06-engine-production-readiness/evidence/engine-readiness-content-workspace-temp-tests.txt>). The [evidence notes](<C:/Users/arthu/Documents/Projects/Sokoban Game/docs/reviews/2026-10-06-engine-production-readiness/evidence/README.md>) record the commands and limitations.

No fresh compilation, new GPU timing run or new hardware acceptance was performed. Source-confirmed interface limits and architectural gaps have high confidence. Cross-platform performance, audible mix quality, visual fidelity and throughput at proposed large workloads require the acceptance work described above.

## Implementation priorities

### First establish a dependable release baseline

Resolve the two reproducible content tests, rebuild current source through the documented workflow, and retain exact build/content identifiers. Complete the current supported-device and installer/lifecycle matrix. Establish public-save migration policy before releasing a format that must remain supported.

Add budget reporting for dropped draws, unavailable assets, UI overflow and streaming delays to release acceptance. A system that quietly omits required presentation needs a tested fallback or explicit failure.

These steps are prerequisites for knowing whether later changes improve readiness.

### Then remove hard player-facing boundaries

Build Unicode/shaping/localization foundations and the first accessibility requirements. Introduce independent audio voices, event handles, buses and concurrency policy. Extend existing input settings and remapping where accessibility requires it.

These improve the current game directly and do not require a fully generalized world architecture.

### Build the shared content foundation

Introduce durable asset IDs, an authoritative dependency registry, cooked meshes/skeletons/clips/audio, verified artifacts and platform variants. Extend the local derived cache after its contracts stabilize. Add chunk/archive and patch manifests.

This is the highest-impact infrastructure phase because it supports reload, streaming, animation tooling, collaboration and release updates.

### Expand runtime scalability

Add bounded I/O and publication budgets, quality-driven residency, task dependencies/priorities, animation binding and LOD, and whole-process memory reporting. Validate on low-core and integrated-GPU hardware.

Use workload evidence to decide whether frame pipelining, GPU particles or indirect draw submission are needed.

### Expand authoring and visual breadth

Introduce reusable scene composition, property metadata, prefab/override workflows, source-control visibility, animation authoring and richer effect assets. Evolve rendering around resource declarations, spatial/LOD policy, scalable lights and material/environment tooling.

Add genre-specific features only against a concrete content brief.

| Work package | Main dependency | Completion evidence |
| --- | --- | --- |
| Save migration and release gate | Defined supported releases/devices | Historical saves survive upgrade; clean rebuild and acceptance matrix pass. |
| Unicode and accessible UI | Text-run and semantic-control contracts | Pseudo-localized and multi-script layouts work at supported scales. |
| General audio | Event/data/voice separation | Independent voices, mix control, bounded stress and device lifecycle pass. |
| Asset registry and full cooking | Durable authored IDs and schemas | Rename-safe references and deterministic verified cooked artifacts. |
| Streaming and memory policy | Cooked chunks and ownership | Slow-I/O/low-memory transitions remain bounded and reach usable results. |
| Task dependencies and priorities | Explicit ownership and completion | Low-worker stress, cancellation and shutdown pass without deadlock/starvation. |
| Animation scalability | Cooked rigs/clips and task execution | Rig binding, layering, events and LOD meet character workload budgets. |
| Scene/editor generalization | Scene schema and asset registry | Second workflow/project can be authored through reusable tools. |
| Renderer scale | Render-world, LOD and resource declarations | Larger reference scenes meet CPU/GPU/memory/image targets across GPUs. |
| Optional genre/platform systems | Explicit product requirements | Representative vertical slice proves required behavior on target platforms. |

Several packages can proceed alongside one another once contracts are established. Animation graph work should not wait for every renderer feature, and localization should not wait for a scene architecture rewrite. Changes that share asset identity, versioning or runtime ownership should agree on those boundaries first.

## Concrete production targets

A first production specification should state the supported frame rates/resolutions/devices, expected scene scale, minimum storage throughput, memory ceilings, loading latency, voice counts, language set and release compatibility window.

For a 60 FPS target, the entire frame has 16.67 ms; 120 FPS has 8.33 ms. Allocate budgets using actual overlap and critical-path measurements, rather than adding inclusive profiler scopes. Track p95/p99 and hitch frequency as well as averages.

Illustrative initial gates are:

| Dimension | Proposed gate |
| --- | --- |
| Correctness | Zero unresolved required-suite failures; complete deterministic replay coverage for shipped campaign content. |
| Content | Complete dependency resolution and verified artifacts; clean/incremental cooks agree. |
| Runtime capacity | Supported stress scenes have zero unexplained dropped models, poses or UI commands. |
| Memory | Defined CPU/GPU/audio/transient ceilings; no persistent growth during repeated transitions and reloads. |
| Loading | A measured request-to-usable-content target on minimum supported storage; terminal failure rather than indefinite wait. |
| Audio | Defined physical/logical voice limits with no unintended sample restart, starvation or callback-budget overruns. |
| UI | Supported languages and accessibility scales show no unreadable or unreachable controls. |
| Updates | Every supported public save migrates; content changes preserve or explicitly recover checkpoints. |
| Platforms | Recorded hardware, controller, DPI, lifecycle, upgrade and uninstall acceptance. |
| Support | Exact build/content identity and matching symbols recover useful crash/hitch evidence. |

For broader engine reuse, create representative stress content substantially larger than the current game: for example, an asset registry with 10,000 entries, hundreds of animated actors, many independent sounds and a light-rich scene. These are useful test shapes, not proof of AAA readiness by themselves. Select counts from the intended games and measure the whole workflow from import through authoring, cooking, loading, gameplay and support.

The strongest investment is preserving the engine's deterministic rules and transactional/resource-lifetime discipline while extending its content, authoring and runtime contracts. Audio and international UI need the earliest capability work. Asset identity/cooking and task ownership need the earliest shared infrastructure work. Rendering expansion should follow explicit scene and visual targets.

