# Tile decorations

`../tile_decorations.blend` contains 48 editable decoration assets, authored in
Blender 5.0.1. The set uses varied faceted stones, independent grass blades
and shallow moss cushions. Simple geometry and broad color regions keep the
decorations readable at the game's camera distance.

All three styles use natural random scatter with even coverage along each
decorated edge. Five nominal placement sites per edge vary in both position
along the border and distance inward. Adjacent decorated edges share one corner
site. Each edge has a randomly chosen patch touching its border, while shared
corner patches reach both borders. The remaining patches sit farther inward,
giving the border a naturally broken outline with clear tile between shapes.

The scatter bands extend 0.185 units inward for pebbles, 0.160 for grass and
0.235 for moss. Placement fit boxes vary independently in width and depth:
pebbles use 0.075–0.205 by 0.055–0.165 units, grass 0.032–0.065 by
0.035–0.090, and moss 0.115–0.285 by 0.075–0.220. These are maximum fit
boxes; actual pieces may be smaller. Each piece uses one uniform XY scale,
capped at 1, preserving its native proportions and smaller source sizes.
Border anchors use the resulting dimensions so the fitted geometry still
reaches its requested edges. All four variants retain the same site counts
and band widths while varying positions, shapes, sizes and colors.

Each pebble site contains one stone, with rounded, slab, wedge, angular and
elongated river profiles varying its outline, shoulders, crown and height.
Sizes range from small flat chips to broad chunky pieces; colors include cool
gray, warm tan and muted olive. Each grass site contains one independent blade
with two folded diamond sections and a tapered tip: 9 source vertices and
14 triangles. Blade width, height and lean vary between sites, without leaves,
base pads or multi-blade tufts. Each moss site is one low mat with a round,
kidney, tongue or scalloped outline, varied proportions and size, and broad
olive, green or yellow-green regions. Moss has no separate flecks, and its
height is limited by its horizontal footprint.

Each style has four variations for each layout:

| Filename configuration | Decorated edges | Mask | Sites per asset | Variations per style |
| --- | --- | --- | --- | --- |
| `edge` | North | 1 | 5 | 4 |
| `corner` | North, East | 3 | 9 | 4 |
| `strip` | North, South | 5 | 10 | 4 |
| `end` | North, East, West | 11 | 13 | 4 |

Every layout follows the perimeter. The opposite-edge pair has no geometry
through the tile center. `catalog.json` records `focal_points` in Blender
coordinates: two for `strip`, one for other layouts. The compatible
`focal_point` field contains the first point. For every style these are
representative points retained for compatibility and do not control density.

Quarter turns cover every one-, two- and three-edge orientation. Assets are
named `tile_pebbles_edge_01` through `tile_moss_end_04`; `catalog.json` records
every filename, mask, variation, mesh bounds and exported counts.

## Blender source

**TileDecorations** is the complete labeled catalog. **Decoration assets |
48 meshes** contains the actual assets; **Presentation | do not export** holds
the display tiles, text, lights, background and camera. **TileDecorations_Closeup**
shows representative corner assets for all three styles at a larger scale.
**TileDecorations_LayoutPreview** compares all four layouts for all three
styles. The prior default scene is retained separately.

Each asset is one native mesh containing disconnected stone, blade or moss
islands. Use Select Linked in Edit Mode to edit individual components. Objects
are marked as Blender assets and tagged by style and configuration. Geometry
has no modifiers, procedural shader dependencies, transparency cards or rigs.
Grass blades are closed meshes with real thickness.

## Placement and export

- One tile is one Blender unit. X/Y are horizontal; Z is up. North is -Y.
- Mesh coordinates are centered on the tile: X/Y stay within -0.5..0.5.
- Each layout reaches its requested tile borders exactly. All styles scatter
  individual pieces across a fixed edge band, with shared sites at adjacent corners.
- Place the origin at the supporting tile's center and top surface. The bases
  slightly intersect that surface to hide contact seams. Conservative height
  limits are 0.057 units for pebbles, 0.175 for grass and 0.033 for moss.
  `catalog.json` records each mesh's exact bounds.
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

All 48 are registered in `assets/manifest.json` as `TileDecorationPebblesEdge01`
through `TileDecorationMossEnd04`, with authored scale and embedded materials retained.
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
twelve-example `previews/tile_decorations_layouts.png` compares the scattered
edge bands across every style and layout. Colored square platforms are
presentation objects.

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
GLBs with refreshed catalog counts by default. Set the optional `UPDATE_STYLES`
global to select styles; selecting grass and pebbles refreshes only their 32
assets and retains moss.
