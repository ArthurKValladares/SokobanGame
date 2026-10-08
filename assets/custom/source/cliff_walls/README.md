# Modular cliff walls

`../cliff_walls.blend` is the editable native Blender source for the cliff
library in `../../models/cliff_walls/`. The game registers two solid **Cliff
Wall** brushes in one palette group. Their bodies automatically choose the
matching modular configuration; the flat tops support ground splat painting
and saved texture assignments. Quarry-marble stone walls are a separate family.

The current native Blender revision removes all 84 decorative rim stones.
Three broad, staggered horizontal rock masses retain unequal prominence,
angular recessed breaks, rounded native bevel geometry and chipped facets.
The exposed walls and convex outer corners taper gently inward with height.
Jagged upper shoulders follow the smaller, flat top, while endpoint collars
at occupied-neighbor joins remain exact.
There are no stone components protruding over the grass.

Rock materials use a cliff-only cool slate factor `[0.25, 0.32, 0.52, 1]`
over the existing ground-side albedo. Soil and bottom factors are
`[0.035, 0.045, 0.065, 1]`; the rock normal strength is 0.55. These native
shader settings export as glTF PBR factors. Ground texture images stay
unchanged. Canonical source caps retain the existing grass material; the
assembly preview shows the new wall-top moss and slate splat materials.

## Native library and placement

The source retains the original `Scene` as a reference. **CliffWalls** contains
the twelve editable **Body**/**Top** pairs. **CliffWall_AssemblyPreview** shows
mixed variants in a straight strip, an L wall, a 3×3 plateau with an interior
tile, and isolated four-sided tiles. Source meshes retain editable quads,
bevel strips and an enabled **Triangulate** modifier.

| Configuration | Exposed sides | Canonical mask |
| --- | --- | --- |
| Island | North, East, South, West | 15 |
| End | North, East, West | 11 |
| Strip | North, South | 5 |
| Corner | North, East | 3 |
| Edge | North | 1 |
| Interior | None | 0 |

Quarter turns of these six configurations cover all sixteen cardinal exposure
masks, with two variants per configuration. Interior bodies contain only a
bottom surface. The kit supports arbitrary same-height, cardinally connected
shapes; unequal-height connections would require additional geometry.

Author in Blender **X/Y = horizontal, Z = height**. The logical cell is 0..1
on all axes; grass is at Z = 1. North is Y = 0, East X = 1, South Y = 1, and
West X = 0. Bake relative transforms into mesh vertices and export nodes at
identity: the static loader ignores node transforms. Manifest models must set
`preserveSourceScale: true`, `rotateHalfTurn: false`, and scale 1.

## Taper, top and seam contract

The nominal cell remains the placement, collision and editor-selection box.
Only exposed faces taper. Body relief is bounded by 0.035 outward and 0.175
inward, including taper; its height remains 0..1. No rim decorations remain.
The catalog records the actual bounds of every exported model.

All variants preserve the thirteen endpoint height stations in `catalog.json`.
At occupied-neighbor joins, their collars remain at zero inward displacement;
along-side coordinates 0, 1/12, 11/12 and 1 stay fixed at every height. This
keeps mixed variants and L/T junctions closed.

Convex corners activate only when both incident sides are exposed. Their
native vertices move diagonally inward by `c*z*(1-dx/r)*(1-dy/r)`, with factors
clamped to 0..1 and distances measured from the original corner. Radius
`r = 1/6`; `c = 0.0605` for A and `0.066` for B. The base stays fixed, and the
upper corner retreats in both axes. The same edit applies to native caps and
assembly previews. Hidden joins and concave junctions retain their original
geometry.

Each native flat cap has 48 boundary points: twelve samples per side, running
clockwise North/East/South/West. At parameter `u = sample / 12`, exposed edges
use their variant's `contract.topFootprint.insetProfiles`; hidden edges use
zero inset, then the convex-corner map above transforms the 48 points at Z = 1.
The face and corner taper is 10% stronger than the preceding revision.
The maximum side setback is 0.07986 tiles. Adjacent hidden edges keep
their full unit length, and exposed middles retain an uneven outline. Source
caps are subdivided native planes whose boundaries meet the body exactly.

The game draws the same planar footprint with 24 fan quads. Drawing, blend
mask picking and shadows share it. Logical selection keeps the full cell box;
its invisible top is not a splat surface, so painting cannot hit empty setback
space. Splats sample world XY and the screen paint origin, independent of body
rotation. Assignment-color mode uses the same footprint.

Keep the native top profiles synchronized with `src/engine/CliffWallGeometry.hpp`.
Changing native outlines requires a matching renderer-profile update and a
body/cap/seam audit.

Body GLBs omit the top. `cliff_wall_top.glb` is now a tapered **island-A**
authoring preview cap, rather than a universal square cap. It is not registered
in the runtime manifest. The editable source contains the proper cap for each
mask and variant. The legacy square source plane remains an authoring reference.

## Painting and PBR export

The brushes are **Cliff Wall** (`u`, style A) and **Cliff Wall 02** (`x`, style B).
**Ground Paint** supports blend-mask painting and per-tile splat assignments.
Assignments survive style changes, dragging, save/load and undo. **Randomize
Walls** changes variants within each wall family and preserves assignments.

### Wall-top splat materials

**WallSlateRock** is medium cool gray with broad, staggered horizontal plates.
Its albedo value matches the side rock. **WallMoss** is a darker, muted green
moss carpet in the ground artwork's painted faceted style. Both are available
in **Ground Paint** Base/Detail selectors and can be assigned to cliff tiles.

The textures are `custom/textures/wall_slate_rock.png` and `wall_moss.png`.
Each has aligned 1024×1024 normal and ORM companions in `custom/pbr/`, with
the same stem plus `_normal.png` and `_orm.png`. Albedo is sRGB; data maps are
linear, repeating and filtered linearly. World XY divided by four aligns all
maps across tiles and rotations. ORM packs AO, roughness and zero metallic.

Artwork and registered grayscale height sources were created with built-in
ImageGen; the prompt set is saved in `wall_top_texture_prompts.json`. Sources
use the same normalized dimensions as their albedos. Rock normal gain is 12,
roughness 0.88; moss gain is 10, roughness 0.94. The mathematical encoding
reuses the terrain pipeline's periodic height correction and wrapped
derivatives. It preserves the authored albedo and height pixels. Rebuild only
these four data maps from the project root with:

```text
python tools/make_wall_top_pbr.py
```

Blender includes native **WallSlateRock** and **WallMoss** PBR materials. In
the assembly preview, the strip and L use slate tops, and the plateau and
isolated examples show both choices. Body materials retain the existing maps.

Body materials are `CliffRock_North/East/South/West`,
`CliffSoil_North/East/South/West`, and `CliffBottom`. **TerrainUV** is the sole
body UV layer and exported UV0. Base color is sRGB; normal and ORM maps are
linear. Normals use OpenGL/Blender +Y tangent space with authored float4
tangents. ORM stores R = occlusion, G = roughness, B = metallic. Keep repeat
wrapping, linear filtering and identity texture transforms. Relative PNG
dependencies stay inside `assets/`.

For the slate factor, use native **Mix Color**, RGBA, Multiply at factor 1,
between albedo and Principled Base Color. Blender 5 recognizes this as glTF
`baseColorFactor`; the legacy MixRGB node does not retain that factor. Set
colors on shader inputs. Broad facets stay flat; bevel strips may shade
smoothly only where normals stay in the face hemisphere. Two small East bevel
polygons in each of island-A, end-A and corner-A use local flat shading after
the taper, preserving their positions and the other smooth bevel strips.

Export each selected Body as glTF Binary (`.glb`) with normals, UVs and
tangents, **Keep Original** textures, **Apply Modifiers**, no animations,
opaque single-sided materials and an identity object/node transform. Use only
the active **CliffWalls** scene or a temporary identity-transform export scene.
Export the island-A preview Top separately. From the project root run:

```text
python tools/finalize_cliff_gltf.py
python tools/finalize_cliff_gltf.py --verify
```

The finalizer removes redundant image `bufferView` fields when Blender also
exports relative PNG URIs; mesh binary chunks stay unchanged. Its optional
`--repair-zero-tangents` conservatively repairs only isolated empty tangent
records from agreeing neighboring authored frames. This taper revision needed
no tangent repairs.

The content pipeline stages all twelve registered body GLBs and their PBR
dependencies. Editable Blender source, catalog, documentation and the optional
island-A preview cap stay outside the runtime package.
