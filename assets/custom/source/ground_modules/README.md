# Native modular ground

`../ground_modules.blend` is the editable Blender source for the sixty body
GLBs in `../../models/ground_modules/`. Ten existing sandstone styles each
have six shapes. The original GLBs and their six shared PBR maps remain
unchanged as references.

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
cliff walls. Unequal-height joins retain exposed sides. Diagonal neighbors
also select the appropriate concave top at three-cell junctions.

## Source scenes

**GroundSourceReferences** retains the ten imported meshes. **GroundModules**
contains sixty editable Body/Top pairs, arranged by shape and style.
**GroundConcaveTopReferences** retains 190 additional native top meshes in a
hidden reference collection; there are 250 authored tops in total.
**GroundModules_AssemblyPreview** shows mixed-style strips, a plateau,
corners, a ring and all ten standalone styles. The original **Scene** is
retained separately.

Bodies keep the original fractured stone shells and PBR material regions.
Hidden sides are removed in the native meshes. The bodies end at a shallow,
level lip at Z=0.91. A small rock ledge and recessed backing close cracks
between shells. Original horizontal positions remain intact; the earlier
diagonal XY corner taper is removed. Microscopic duplicate vertices were
merged within 0.0005 tile units to remove acute degenerate tangent frames.
Large stone UV charts remain intact. Normals are flat facet normals, with
valid tangent frames exported for the normal maps.

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

## Tops, painting and placement

Body GLBs omit their caps. Every native Top remains editable in the source.
The renderer draws the selected authored top, including its chamfer, with up
to 38 triangle/quad patches. Interiors skip their bottom-only Body; an interior
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
reference scene or cap objects. Keep relative PNG dependencies inside assets.
Base color is sRGB; normal and packed AO/roughness/metallic maps are linear.

After exporting the actual native top meshes into `cap_surfaces.json`, update
the literal C++ data, or check that it matches the source:

```text
python tools/serialize_ground_caps.py
python tools/serialize_ground_caps.py --check
```

The serializer transfers the native vertices, normals and material coverage;
it neither creates geometry nor edits the Blender meshes.

Blender 5 may write both image URI and bufferView. The existing
`tools/finalize_cliff_gltf.py` finalizer can normalize these ground files too;
its `finalize(path)` API removes only the redundant image field and preserves
binary mesh payloads. No tangent repair is needed for the current export.

Run the independent mesh and seam audit from the project root:

```text
python tools/validate_ground_modules.py
```

It checks all sixty files, shared PBR dependencies, finite normals/tangents,
native body/cap closure, all 250 native tops, all 256 neighbor patterns and
mixed-style three-dimensional seam traces.
The `ground_tile_geometry` C++ suite additionally loads every actual GLB
through the engine and checks module selection, materials, painting,
shadow/pick footprints, editor updates and surrounded-top rendering.

Native modules bypass the old ground mesh trimming, rim deformation,
generated rim caches and processed-ground artifacts. The legacy paths remain
available for original source assets and experimental fixtures. The native
source, contract and documentation are not staged into runtime packages.
