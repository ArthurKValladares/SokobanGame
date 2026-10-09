# Tile decorations

`../tile_decorations.blend` contains 32 editable decoration assets, authored in
Blender 5.0.1. The set follows the supplied references: small pale, rounded
faceted pebbles, and dense meadow grass with overlapping oval leaves.

Each asset is one broad cluster with a single focal point. Pieces overlap or
nearly touch at that nucleus, then become smaller and more widely spaced with
distance along the tails. Density continues fading around corners; each edge
does not start another chunk. Grass combines a broad leafy core with blades
growing through it. The four variations change the focal position slightly,
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
| `strip` | Tile center, with one narrow north–south strip connecting opposite edges |
| `end` | North-edge midpoint, fading around both corners and down east/west |

The opposite-edge strip crosses the tile center to form one cohesive cluster.
The other layouts follow the perimeter. `catalog.json` records each asset's
`focal_point` in Blender coordinates.

Quarter turns cover every one-, two- and three-edge orientation. Assets are
named `tile_pebbles_edge_01` through `tile_grass_end_04`; `catalog.json` records
every filename, mask, variation, mesh bounds and exported counts.

## Blender source

**TileDecorations** is the complete labeled catalog. **Decoration assets |
32 meshes** contains the actual assets; **Presentation | do not export** holds
the display tiles, text, lights, background and camera. **TileDecorations_Closeup**
shows two representative corner assets at a larger scale.
**TileDecorations_LayoutPreview** compares all four focal layouts for both
styles. The prior default scene is retained separately.

Each asset is one native mesh containing disconnected stone, blade and leaf
islands. Use Select Linked in Edit Mode to edit individual components. Objects
are marked as Blender assets and tagged by style and configuration. Geometry
has no modifiers, procedural shader dependencies, transparency cards or rigs.
Grass blades and leaves are closed meshes with real thickness.

## Placement and export

- One tile is one Blender unit. X/Y are horizontal; Z is up. North is -Y.
- Mesh coordinates are centered on the tile: X/Y stay within -0.5..0.5.
- Place the origin at the supporting tile's center and top surface. The bases
  slightly intersect that surface to hide contact seams. Pebbles rise at most
  0.053 units; grass rises at most 0.161 units.
- Preserve source scale. Do not fit these partial-edge meshes to a whole cell.
- Catalog object locations are display offsets. Before export, temporarily set
  the chosen mesh's location to zero; restore its catalog location afterward.
- Export only that mesh as glTF Binary, with Selected Objects, Active Scene,
  normals and UVs enabled, and animation disabled. Keep identity node transforms:
  the game's static loader reads the authored vertices without node transforms.

The 32 ready-to-use GLBs are in `../../models/tile_decorations/`. Each contains
one mesh, one opaque material and its embedded palette PNG. No external files
are needed at runtime. UV0 (`PaletteUV`) provides the color variation; the two
packed source palettes are also saved beside this README for editing. Pebbles
use roughness 0.91; grass uses 0.85; both are nonmetallic.

The files contain 1,154–3,146 triangles apiece. All 32 are registered in
`assets/manifest.json` as `TileDecorationPebblesEdge01` through
`TileDecorationGrassEnd04`, with authored scale and embedded materials retained.
The content pipeline stages the GLBs and their palettes with the game.

Use the editor's **Tile Decorations** tab to choose Pebbles or Grass, a layout,
one of four variants, and a rotation in 90-degree steps. Place a decoration on
the supporting tile's top surface. Placement uses the existing saved mesh
decoration records, so these decorations also render during gameplay.

## Preview and verification

`docs/examples/tile-decorations.scr` is a game-ready gallery with every variant
on its own tile. Pebbles occupy the left four columns and grass the right four;
rows show `edge`, `corner`, `strip`, then `end`, with variants 01–04 left to right
within each style. A separate small platform supplies the hero start and exit.

`previews/tile_decorations_catalog.png` shows all 32 assets, and
`previews/tile_decorations_closeup.png` shows the two styles in detail. The
eight-example `previews/tile_decorations_layouts.png` shows every layout's
focal point and taper. Colored square platforms are presentation objects.

From the project root, run:

```text
python -B tools/validate_tile_decorations.py
```

The independent audit checks the complete catalog, distinct variants, triangle
geometry, finite normals and UVs, bounds, identity transforms, materials and
embedded PNG data. Native Blender meshes were additionally checked for open
boundaries, nonmanifold edges and loose vertices; all are closed.

`tools/make_tile_decorations.py` regenerates the models and catalog from a fresh
Blender session. It preserves the prior scene and refuses to replace an existing
TileDecorations scene. The close-up presentation is retained in the editable
source rather than generated by that script.

To refresh the existing source in place after editing those authoring
functions, run `tools/update_tile_decorations.py` in its Blender session. It
preserves object names, placements, asset metadata and the presentation scenes,
updates shared close-up meshes, checks native topology, and re-exports all 32
GLBs with refreshed catalog counts.
