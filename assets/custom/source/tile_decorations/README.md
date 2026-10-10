# Tile decorations

`../tile_decorations.blend` contains 48 editable decoration assets, authored in
Blender 5.0.1. The set follows the supplied references: small pale, rounded
faceted pebbles, dense meadow grass with overlapping oval leaves, and low moss
cushions with rounded green lobes and small pale-yellow flecks.

Connected-edge layouts form one broad cluster with a single focal point.
Opposite-edge layouts use one cluster on each of the two tile borders, leaving
the center clear. Pieces overlap or
nearly touch at that nucleus, then become smaller and more widely spaced with
distance along the tails. Density continues fading around corners; each edge
does not start another chunk. Grass combines a broad leafy core with blades
growing through it. Moss uses smooth shading and closed cushion meshes, with
small solid flecks nestled into their tops. The four variations change the focal position slightly,
the falloff width, and the individual stone/plant compositions.

The softer-taper pass opens up the central nucleus and keeps intermediate-sized
pieces farther along the trails. Row widths and blade counts blend gradually
with density, avoiding a sudden transition from a wide cluster to a thin line.

Each style has four genuinely different compositions for each layout:

| Filename configuration | Decorated edges | Mask | Variations per style |
| --- | --- | --- | --- |
| `edge` | North | 1 | 4 |
| `corner` | North, East | 3 | 4 |
| `strip` | North, South | 5 | 4 |
| `end` | North, East, West | 11 | 4 |

| Configuration | Focal point and taper |
| --- | --- |
| `edge` | North-edge midpoint, fading toward both endpoints |
| `corner` | Shared northeast corner, fading along north and east |
| `strip` | Separate north- and south-edge centers, each fading along its border |
| `end` | North-edge midpoint, fading around both corners and down east/west |

Every layout follows the perimeter. The opposite-edge pair has no geometry
through the tile center. `catalog.json` records `focal_points` in Blender
coordinates: two for `strip`, one for other layouts. The compatible
`focal_point` field contains the first focus.

Quarter turns cover every one-, two- and three-edge orientation. Assets are
named `tile_pebbles_edge_01` through `tile_moss_end_04`; `catalog.json` records
every filename, mask, variation, mesh bounds and exported counts.

## Blender source

**TileDecorations** is the complete labeled catalog. **Decoration assets |
48 meshes** contains the actual assets; **Presentation | do not export** holds
the display tiles, text, lights, background and camera. **TileDecorations_Closeup**
shows representative corner assets for all three styles at a larger scale.
**TileDecorations_LayoutPreview** compares all four focal layouts for all three
styles. The prior default scene is retained separately.

Each asset is one native mesh containing disconnected stone, blade, leaf or moss
islands. Use Select Linked in Edit Mode to edit individual components. Objects
are marked as Blender assets and tagged by style and configuration. Geometry
has no modifiers, procedural shader dependencies, transparency cards or rigs.
Grass blades and leaves are closed meshes with real thickness.

## Placement and export

- One tile is one Blender unit. X/Y are horizontal; Z is up. North is -Y.
- Mesh coordinates are centered on the tile: X/Y stay within -0.5..0.5.
- Each layout reaches its requested tile borders exactly, without the former
  inset gap. Edge/corner clusters translate as a whole; three-edge layouts
  also stretch horizontally. Opposite-edge layouts combine two independent
  edge clusters, with the second rotated by 180 degrees. These transforms
  retain gradual taper, height, topology and palette assignments.
- Place the origin at the supporting tile's center and top surface. The bases
  slightly intersect that surface to hide contact seams. Pebbles rise at most
  0.053 units; grass rises at most 0.161 units; moss rises at most 0.085 units.
- Preserve source scale. Do not fit these partial-edge meshes to a whole cell.
- Catalog object locations are display offsets. Before export, temporarily set
  the chosen mesh's location to zero; restore its catalog location afterward.
- Export only that mesh as glTF Binary, with Selected Objects, Active Scene,
  normals and UVs enabled, and animation disabled. Keep identity node transforms:
  the game's static loader reads the authored vertices without node transforms.

The 48 ready-to-use GLBs are in `../../models/tile_decorations/`. Each contains
one mesh, one opaque material and its embedded palette PNG. No external files
are needed at runtime. UV0 (`PaletteUV`) provides the color variation; the three
packed source palettes are also saved beside this README for editing. Pebbles
use roughness 0.91; grass uses 0.85; moss uses 0.94. All are nonmetallic.

The files contain 1,154–3,450 triangles apiece. All 48 are registered in
`assets/manifest.json` as `TileDecorationPebblesEdge01` through
`TileDecorationMossEnd04`, with authored scale and embedded materials retained.
The content pipeline stages the GLBs and their palettes with the game.

Use the editor's **Tile Decorations** tab to choose Pebbles, Grass or Moss, a layout,
one of four variants or **Random**, and a rotation in 90-degree steps. Random
keeps the preview stable and chooses a different variant after each placement.
Press `R` while placing to rotate (the remappable editor rotate binding).
Place a decoration on the supporting tile's top surface. New placements do
not select the placed item; click a placed decoration or its list entry to
select it for editing. Placement uses the existing saved mesh
decoration records, so these decorations also render during gameplay.

## Preview and verification

`docs/examples/tile-decorations.scr` is a game-ready gallery with every variant
on its own tile. Pebbles, grass and moss occupy successive groups of four columns;
rows show `edge`, `corner`, `strip`, then `end`, with variants 01–04 left to right
within each style. A separate small platform supplies the hero start and exit.

`previews/tile_decorations_catalog.png` shows all 48 assets, and
`previews/tile_decorations_closeup.png` shows the three styles in detail. The
twelve-example `previews/tile_decorations_layouts.png` shows every layout's
focal point and taper. Colored square platforms are presentation objects.

From the project root, run:

```text
python -B tools/validate_tile_decorations.py
```

The independent audit checks the complete catalog, distinct variants, triangle
geometry, finite normals and UVs, bounds, exact border contact, the clear
center of opposite-edge layouts, identity transforms, materials and
embedded PNG data. Native Blender meshes were additionally checked for open
boundaries, nonmanifold edges and loose vertices; all are closed.

`tools/make_tile_decorations.py` regenerates the models and catalog from a fresh
Blender session. It preserves the prior scene and refuses to replace an existing
TileDecorations scene. The close-up presentation is retained in the editable
source rather than generated by that script.

To refresh the existing source in place after editing those authoring
functions, run `tools/update_tile_decorations.py` in its Blender session. It
preserves object names, placements, asset metadata and the presentation scenes,
updates shared close-up meshes, checks native topology, and re-exports all 48
GLBs with refreshed catalog counts.
