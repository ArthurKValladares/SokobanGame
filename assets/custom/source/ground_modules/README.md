# Native modular ground

`../ground_modules.blend` is the editable Blender source for 240 body
GLBs in `../../models/ground_modules/`. Ten existing sandstone styles each
have six shapes in both chamfered and square-top profiles, with regular and
floating-island bottoms. The ten imported reference GLBs and their six
shared PBR maps remain separate from the editable native kit.

| Shape | Exposed sides | Canonical mask |
| --- | --- | --- |
| Island | North, East, South, West | 15 |
| End | North, East, West | 11 |
| Strip | North, South | 5 |
| Corner | North, East | 3 |
| Edge | North | 1 |
| Interior | None | 0 |

Quarter turns cover all sixteen cardinal configurations. Different styles
join at the same height. Ground connects to ground; cliff walls connect to
cliff walls. Unequal-height joins retain exposed sides and use square lower
tops to close the contact with raised ground. Diagonal neighbors
also select the appropriate concave top at three-cell junctions.

## Source scenes

**GroundSourceReferences** retains the ten imported meshes. **GroundModules**
contains sixty editable Body/Top pairs, arranged by shape and style.
**GroundConcaveTopReferences** retains 190 additional native top meshes in a
hidden reference collection; there are 250 authored chamfered tops.
**GroundModules_AssemblyPreview** shows mixed-style strips, a plateau,
corners, a ring and all ten standalone styles. The original **Scene** is
retained separately.

**GroundSquareModules** in the **GroundModules** scene contains sixty
additional editable Body/Top pairs. **GroundModules_LayerPreview** shows
mixed-style raised terrain and a three-layer stack. Square bodies restore
the original shell height to Z=1 and extend the rock seal and backing to
that height. Their tops are full unit-cell quads at Z=1, with no chamfer or
wall-blend band. Both top profiles now meet chunky rock shoulders directly,
without the former vertical stone collar beneath the cap.

The hidden **GroundModuleConstructionSources** collection stores independent
copies of all 120 original regular and square native bodies. Treat these
construction meshes as immutable references for the earlier regular and
bottom rebuild tools. They are excluded from previews and runtime exports.

**GroundShoulderConstructionSources** stores independent frozen copies of
all 240 accepted chunky regular and floating-bottom bodies from before the
collar removal. The current explicit shoulder rebuild reads these meshes;
it never derives a new transition from already remodeled or artist-edited
bodies. Keep this hidden collection intact and out of runtime exports.

Regular revision 2 bodies retain the exact `GroundModuleRecess` backing and
original two-triangle flat base at Z=0. Chamfered bodies keep their level lip
at Z=0.91; square bodies keep their full-height seal at Z=1. Closed unequal
faceted boulders form the visible sides and broad lower ledges, with a square
overall shape. Their accepted geometry and UV charts remain intact.
Interior bodies remain the two original base faces with no rock masses.

`ShoulderTransitionRevision=1` removes only the old outer vertical collar
at Z>=0.78 from both regular bodies and revision 5 floating bottoms. The
horizontal cap-lip seals, recessed backing, bases, and all accepted
`BoulderMassNN` and `FloatingRockNN` surfaces remain intact. New closed
`ShoulderRockNN` masses rise from Z>=0.4 to within approximately 0.001..0.004
tile units of the lip, continuing the chunky rock faces into the cap.
Regular shoulders stay within X/Y=0..1. Bottom shoulders allow a 0.04
exposed-side margin low down, tapering to the unit footprint at their roof;
hidden connection planes remain within the cell. All 310 authored caps and
the shared PBR maps remain unchanged.

`RegularCore` covers retained regular geometry, with `RecessBacking` and
`FlatBase` subsets for editing. The remaining `UpperRockBand` faces are the
horizontal lip seals. `BoulderMassNN` groups isolate accepted boulders and
`ShoulderRockNN` groups isolate the new closed shoulders. Retained surfaces
keep their UV charts; shoulders have flat facet normals and their own UVs
for the existing shared stone materials.

## Coordinates and seams

Blender X/Y are horizontal and Z is height. The nominal cell spans 0..1;
the flat top is at Z=1. North is Y=0, East X=1, South Y=1 and West X=0.
Catalog objects have display offsets; export each Body at an identity node
transform with vertices in cell coordinates. The static loader ignores node
transforms. Manifest models use `preserveSourceScale: true` and no half turn.

`contract.json` records the native lip and top surface requirements.
`cap_surfaces.json` contains the actual vertices, faces, normals and material
coverage exported from the 250 editable Blender tops. The renderer's literal
lookup data is serialized from these meshes, rather than constructing a rim
or deforming a loaded mesh.

Each exposed side has three broad chamfer panels. Their outer border is
level at Z=0.91; their irregular inner widths vary by style and meet the flat
center at Z=1. All styles share corner width 0.12 and depth 0.09. Common corner
ramps make hidden joins exact across styles and rotations. The height drop is
independent of the varying widths, so tile endpoints do not rise into teeth.

At a grid corner occupied by three cells, the cell with both incident sides
hidden uses a matching concave wedge. The six body shapes remain unchanged;
each style has 25 canonical cap configurations, covering all 256 cardinal
and diagonal neighbor patterns through rotation. A completely surrounded
tile with all diagonals present retains the single flat top quad.

When any ground cell has ground one layer above within its 3x3 horizontal
neighborhood, its same-layer cardinal-connected ground group uses square
bodies and tops. This closes direct stacks, steps and diagonal corner
contacts. Selecting the whole connected group keeps shared edge endpoints
aligned between styles. Disconnected terrain can retain its chamfered
profile. Layer visibility and editor changes re-evaluate the emitted
terrain; hover previews query the same context without changing neighbors.
Square models are registered as `GroundRockSquare01` through `10`, with
`End`, `Strip`, `Corner`, `Edge` and `Interior` inserted before the number
for the other shapes. Saved tile types and splat assignments are unchanged.

## Tops, painting and placement

Body GLBs omit their caps. Every native Top remains editable in the source.
The renderer draws the selected authored top, including its chamfer, with up
to 38 triangle/quad patches. Supported interiors skip their flat-bottom-only
Body; unsupported interiors draw their jagged underside. An interior
without diagonal gaps uses just one square top quad. Drawing, shadows and
splat picking share the same vertices and facet normals. Logical editor
selection and collision keep the full square cell.

The cap retains the existing ground splat shader and world XY paint
coordinates, independent of body rotation. The chamfer uses the existing
rock-to-painted-top material transition. Saved splat assignments survive
placement, movement, randomization, undo/redo and save/load.

**Tiles > Terrain** groups Ground and Cliff Wall. Each group opens its
existing style picker. **Randomize Ground** and **Randomize Walls** are below
the groups. Existing GroundRock01–10 names and saved tile characters remain
stable; their island model paths now point to this kit. Fifty additional model
entries provide the other shapes. The same neighbor resolver used by cliffs
selects these models in gameplay, editor drafts and previews.

## Export and validation

Export the selected Body from the active **GroundModules** scene as glTF
Binary, with normals, tangents, both UV layers, original external PNG images,
no animations and an identity object/node transform. Do not export the source
reference scene, construction meshes or cap objects. Keep relative PNG
dependencies inside assets.
Base color is sRGB; normal and packed AO/roughness/metallic maps are linear.

After exporting the actual native top meshes into `cap_surfaces.json`, update
the literal C++ data, or check that it matches the source:

```text
python tools/serialize_ground_caps.py
python tools/serialize_ground_caps.py --check
python tools/serialize_ground_caps.py --square
python tools/serialize_ground_caps.py --square --check
```

The serializer transfers the native vertices, normals and material coverage;
it neither creates geometry nor edits the Blender meshes.

Export all 240 existing regular and floating-bottom bodies, in both top
profiles, with:

```text
blender --background assets/custom/source/ground_modules.blend --python tools/sculpt_ground_shoulders.py
```

Normal export preserves native artist edits and writes the ignored
`out/ground-modules/shoulder-source-audit.json` snapshot. `-- --audit-only`
refreshes that snapshot without exporting. The explicit rebuild is:

```text
blender --background assets/custom/source/ground_modules.blend --python tools/sculpt_ground_shoulders.py -- --remodel-shoulders
```

This rebuild replaces current body mesh edits using the 240 frozen accepted
meshes in **GroundShoulderConstructionSources** and updates linked previews.
It removes the vertical collar and adds the editable shoulders, preserving
accepted boulder/floating-rock forms, UVs, horizontal lip seals, recessed
backing and bases. It does not rebuild the authored caps.

Blender 5 may write both image URI and bufferView. The existing
`tools/finalize_cliff_gltf.py` finalizer can normalize these ground files too;
its `finalize(path)` API removes only the redundant image field and preserves
binary mesh payloads. Finalize all 240 exported ground GLBs, not only the
regular or floating subset. For example, from PowerShell:

```powershell
$groundModels = Get-ChildItem assets/custom/models/ground_modules/ground_*.glb | ForEach-Object { $_.FullName }
python tools/finalize_cliff_gltf.py @groundModels
```

Tangents are exported from the native meshes with both UV streams; check
them in the mesh audit rather than applying a tangent-repair pass.

Run the independent mesh and seam audit from the project root:

```text
python tools/validate_ground_shoulders.py
python tests/ValidateGroundShouldersTests.py
python tests/ValidateShoulderCoverageTests.py
```

The default audit reads `out/ground-modules/shoulder-source-audit.json` and
writes `out/ground-modules/shoulder-transition-validation.json`. Its frozen
accepted library is
`out/ground-modules/pre-shoulder-transition-20261011-175324/`, containing all
240 pre-transition GLBs, the original source and frozen role snapshots.
Keep that local baseline intact for retention proofs.

The current audit compares retained surfaces against the frozen accepted
240-body library, protects both cap files and shared PBR maps, and checks
closed rooted shoulders, native/exported face ownership, unit bounds and
exposed-side overhang taper. It retains native cap, neighbor-pattern,
mixed-style seam, stepped-contact and stacked-contact checks. Shoulder
coverage is measured just below each lip at two heights; broad rock support
must cover at least 80% of every exposed side within the existing 0.055
backing depth. The report includes continuous covered intervals and gap
sizes. Destructive fixtures exercise missing shoulders, narrow spikes,
inset faces, missing height slices and side rotations.
The `ground_tile_geometry` C++ suite additionally loads every actual GLB
through the engine and checks module selection, materials, painting,
shadow/pick footprints, editor updates and surrounded-top rendering.

Native modules bypass the old ground mesh trimming, rim deformation,
generated rim caches and processed-ground artifacts. The legacy paths remain
available for original source assets and experimental fixtures. The native
source, contract and documentation are not staged into runtime packages.

## Floating-island bottoms

The **GroundBottomModules** collection contains 120 editable combined bodies:
six connection shapes, ten stone styles, and both chamfered and square top
profiles. **GroundModules_BottomPreview** shows isolated tiles, a strip, a
corner, a larger island and a stack. Revision 5 shares the collar-free
shoulder transition with regular revision 2; authored caps and shared PBR
maps are reused.

The accepted revision 4 bodies supplied a backing core derived from the
original fractured walls, recessed below local Z=0.78. Unequal closed faceted boulders embed
into that core or overlap other rooted masses. Broad slanted shoulders,
deep creases and staggered lower masses define the visible sides and rocky
underside. Varied detached rocks float below exposed edges. An irregular
triangulated floor closes its base. Revision 5 keeps those accepted boulder,
floating-rock and lower-core surfaces intact, including both UV charts and
PBR bindings. Only the outer vertical collar is removed; closed shoulders
connect the visible rock clusters to the unchanged cap-lip seal. The lateral
compression introduced in revision 4 remains part of the preserved shapes.

Hidden connection planes keep the unit XY footprint, with their lower
border fixed at Z=-0.34 across shapes, rotations and mixed styles. Below
Z=0.78, boulders and detached rocks may extend at most 0.04 tile units beyond
exposed sides only. New shoulders retain that margin below Z=0.78 and taper
it toward zero between Z=0.78 and their roof. Connected sides stay within
0..1. All geometry extends no deeper than Z=-1.

`IslandCore` covers the backing core. Its `IslandRockWalls` and
`IslandUnderside` subsets isolate the recessed walls and closing floor.
`BoulderMassNN` groups isolate the embedded closed rock masses, and
`FloatingRock01`–`03` isolate detached rocks. Interior bodies contain the
core and underside boulders but no floating debris. `ShoulderRockNN` groups
isolate the new transition masses on exposed sides. These native vertex
groups remain editable in Blender; no sculpting modifier runs in the game.

The renderer selects `GroundRockBottom[Shape]NN` or
`GroundRockBottomSquare[Shape]NN` automatically when there is no full solid
cell directly below. Support comes from the full level/editor document and
live movable state, including hidden layers: ground, wall, cliff wall,
decorative block and ice provide support; air, water, thin plates and moving
platforms do not. Ground
supported by another cell uses the regular body, so stacked terrain has one
underside at its exposed base. Square-top selection remains independent.
Drawing, shadows, camera framing and thumbnails account for the exposed-side
overhang and depth. Logical placement, collision and picking stay within the
original unit cell.

Use the combined shoulder export and audit workflow above for current
floating bodies as well as regular ones.

## Historical revision tools

The earlier exporters remain available for older source revisions and
retention comparisons. They are not the current 240-body rebuild workflow:

```text
blender --background assets/custom/source/ground_modules.blend --python tools/export_chunky_ground_modules.py -- --remodel-regular
blender --background assets/custom/source/ground_modules.blend --python tools/export_bottom_ground_modules.py -- --remodel-bottom-variants
```

These explicit options replace their respective body designs from the
original meshes in **GroundModuleConstructionSources**, including their
former upper bands. The bottom tool uses `tools/sculpt_ground_bottoms.py`;
`-- --tighten-bottom-borders` applies its historical 0.12-to-0.04 compression
to boulder and floating-rock groups. `tools/export_square_ground_modules.py`
exports the earlier square body/cap format or adds missing square variants.
Use historical audits only with their matching source, exports and local
reference baselines:

```text
python tools/validate_ground_modules.py --regular-revision chunky-v1 --output out/ground-modules/regular-chunky-validation.json --approve-bottom-baseline out/ground-modules/regular-chunky-approved-bottom-hashes.json
python tools/validate_ground_bottoms.py --baseline out/ground-modules/regular-chunky-approved-bottom-hashes.json --output out/ground-modules/bottom-v4-after-regular-validation.json
python tests/ValidateRegularGroundTests.py
python tests/ValidateGroundBottomsTests.py
```

Those older audits deliberately require the former preserved upper band and
their respective regular/bottom revision metadata. Keep their recorded
baselines intact rather than replacing them to make current geometry pass.
