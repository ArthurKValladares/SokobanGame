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
wall-blend band. The upper band retains its original horizontal coordinates,
fracture details, UV charts and PBR bindings.

The hidden **GroundModuleConstructionSources** collection stores independent
copies of all 120 original regular and square native bodies. Treat these
construction meshes as immutable references: explicit regular remodeling
and bottom rebuilding read them, rather than deriving a new design from
already remodeled or artist-edited bodies. They are excluded from previews
and runtime exports.

Regular bodies retain the original fractured upper band at Z>=0.78, the
exact `GroundModuleRecess` backing, and the original two-triangle flat base
at Z=0. Chamfered bodies keep their level lip at Z=0.91; square bodies keep
their full-height seal at Z=1. Below the upper band, closed unequal faceted
boulders form the visible sides and broad lower ledges. These new masses
stay within X/Y=0..1 and Z=0.02..0.777, leaving neighboring and stacked
contacts square. Interior bodies remain the two original base faces with
no boulder masses. The authored caps and shared PBR maps are independent of
this regular body revision.

`RegularCore` covers the retained geometry, with `UpperRockBand`,
`RecessBacking` and `FlatBase` subsets for editing. `BoulderMassNN` groups
isolate the closed rock masses. Original upper-band and backing UV charts
are retained; new boulders have flat facet normals and their own UVs for the
shared stone materials.

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

Export the 120 existing regular chamfered and square bodies with:

```text
blender --background assets/custom/source/ground_modules.blend --python tools/export_chunky_ground_modules.py
```

Normal export preserves artist mesh edits and writes an ignored native-source
snapshot. `-- --audit-only` refreshes that snapshot without exporting.
The explicit `-- --remodel-regular` option rebuilds all regular bodies from
**GroundModuleConstructionSources**, replaces their mesh edits, and updates
linked preview meshes. It preserves the original upper band, backing and
flat base; it does not rebuild caps or the accepted revision 4 floating
bottoms. Finalize the resulting `ground_[square_]<shape>_<style>.glb` files
as described below.

For square bodies and tops, export the existing native source with:

```text
blender --background assets/custom/source/ground_modules.blend --python tools/export_square_ground_modules.py
```

The exporter writes the sixty `ground_square_<shape>_<style>.glb` files,
the actual editable top meshes in `square_cap_surfaces.json`, and an ignored
native-source audit snapshot. Normal exports preserve artist edits. The
optional `-- --add-square-variants` upgrades an older source once by copying
the native bodies and restoring their lip height; existing square meshes
are retained. Run the finalizer below on the new GLBs after export.

Blender 5 may write both image URI and bufferView. The existing
`tools/finalize_cliff_gltf.py` finalizer can normalize these ground files too;
its `finalize(path)` API removes only the redundant image field and preserves
binary mesh payloads. No tangent repair is needed for the current export.

Run the independent mesh and seam audit from the project root:

```text
python tools/validate_ground_modules.py --regular-revision chunky-v1 --output out/ground-modules/regular-chunky-validation.json --approve-bottom-baseline out/ground-modules/regular-chunky-approved-bottom-hashes.json
python tests/ValidateRegularGroundTests.py
```

Before regular remodeling, preserve the pre-remodel body exports in
`out/ground-modules/pre-regular-chunky-models/`, their hashes in
`pre-regular-chunky-regular-hashes.json`, and protected bottom/cap/reference
hashes in `pre-regular-chunky-protected-hashes.json` in the same output
directory. The revision audit requires these local baselines. The separate
`--record-baseline` option records the imported meshes/PBR maps and earlier
chamfered-kit hashes; it does not replace the pre-remodel snapshots.

The explicit regular-revision audit compares the upper band, backing and
flat base against frozen pre-remodel exports. It checks closed boulder
volumes, editable group ownership and unit bounds while protecting accepted
revision 4 bottoms, authored caps, imported references and PBR maps through
separate local baselines. It retains the native cap, neighbor-pattern,
mixed-style seam, stepped-contact and stacked-contact checks. Destructive
fixtures exercise retention, bounds and topology failures. The unqualified
audit remains available for the earlier regular-body revision. The approved
regular/cap reference file is written only after this full audit passes; the
earlier baselines stay intact for historical comparisons and retention proofs.
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
corner, a larger island and a stack. Their accepted revision 4 geometry is
independent of the regular chunky body revision; authored caps and shared
PBR maps are reused.

Revision 4 bodies retain a backing core derived from the original fractured
walls, recessed below local Z=0.78. Unequal closed faceted boulders embed
into that core or overlap other rooted masses. Broad slanted shoulders,
deep creases and staggered lower masses define the visible sides and rocky
underside. Varied detached rocks float below exposed edges. An irregular
triangulated floor closes its base. The original top band at Z>=0.78,
including both UV charts and PBR bindings, and the separate authored caps
remain intact. A small affine XY compression of the accepted revision 3
boulder and floating-rock shapes brings them closer to the tile borders.
It preserves their facets, topology, UVs and Z coordinates; the core and
hidden connection planes are unchanged.

Hidden connection planes keep the unit XY footprint, with their lower
border fixed at Z=-0.34 across shapes, rotations and mixed styles. Below
Z=0.78, boulders and detached rocks may extend at most 0.04 tile units beyond
exposed sides only. Connected sides stay within 0..1. All geometry extends
no deeper than Z=-1; the preserved upper band retains its original shape.

`IslandCore` covers the backing core. Its `IslandRockWalls` and
`IslandUnderside` subsets isolate the recessed walls and closing floor.
`BoulderMassNN` groups isolate the embedded closed rock masses, and
`FloatingRock01`–`03` isolate detached rocks. Interior bodies contain the
core and underside boulders but no floating debris. These native vertex
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

Export the existing native bodies with:

```text
blender --background assets/custom/source/ground_modules.blend --python tools/export_bottom_ground_modules.py
```

Normal export preserves artist edits. `-- --add-bottom-variants` adds only
missing native objects when upgrading an older source. The explicit
`-- --remodel-bottom-variants` option rebuilds all 120 bottom variants from
the immutable original bodies in **GroundModuleConstructionSources** using
`tools/sculpt_ground_bottoms.py`; it replaces their mesh edits and updates
linked preview meshes. Normal export leaves the accepted bottom geometry
intact. The helper constructs
the accepted rock forms with a raw 0.12 exposed-side margin before the final
compression to 0.04. `-- --tighten-bottom-borders` applies that compression
to the existing `BoulderMassNN` and `FloatingRockNN` groups without rebuilding
their shapes or changing the core. `-- --audit-only` refreshes the ignored
source snapshot without exporting. Finalize the new
`ground_bottom_[square_]<shape>_<style>.glb` files with the glTF finalizer
described above, then run:

```text
python tools/validate_ground_bottoms.py --baseline out/ground-modules/regular-chunky-approved-bottom-hashes.json --output out/ground-modules/bottom-v4-after-regular-validation.json
python tests/ValidateGroundBottomsTests.py
```

The independent audit compares the preserved top band, materials and both UV
streams. It checks the core, triangulated floor, convex boulder
volumes, their attachment graph, detached-rock gaps and size variation,
source/export triangle ownership, and exposed-side overhang limits. Rotated
mixed-style hidden joins meet at Z=-0.34. Local baselines protect approved
regular body exports, both cap files, imported references and PBR maps.
Destructive audit fixtures reject pasted-on geometry, unrooted or open
boulders, hidden-side overhang, missing surface gaps and changes to the
preserved upper band. Runtime tests cover unsupported placement,
full-document support, previews, shadows, visual bounds, unchanged picking,
thumbnails and asset loading.
