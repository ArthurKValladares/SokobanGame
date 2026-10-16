# Tile decorations

`../tile_decorations.blend` contains 48 editable decoration assets, authored in
Blender 5.0.1. The set follows the supplied references: varied faceted stones,
meadow grass with blades and oval leaves, and overlapping rounded moss lobes
with small pale-yellow flecks.

All three styles use natural random scatter with even coverage along each
decorated edge. Twelve nominal placement sites per edge vary in both position
along the border and distance inward. Positions cover the full band without
dense clusters, large gaps, straight rows or long diagonal progressions.
Adjacent decorated edges share one corner site. Each edge has one randomly
positioned outermost piece touching its border; the others sit farther inward,
giving the border a naturally broken outline.

The scatter bands extend 0.110 units inward for pebbles, 0.145 for grass and
0.125 for moss. Individual footprint widths and depths vary independently
within those bands. All four variants retain the same site counts and band
widths while varying positions, shapes, sizes and colors.

Each pebble site contains one stone drawn from rounded, slab, wedge, angular
and elongated river profiles. Flat and taller stones have asymmetric
shoulders and varied proportions, with gray, olive, tan and cream colors.
Grass mixes five tuft families: upright blades, low spreading fans, tall
reeds, windswept tufts and broad leaves. Blade and leaf counts, height,
width, lean and bend vary between tufts. Each moss site forms a group of
three or four overlapping rounded lobes, mixing compact triads of similarly
sized bulbs with broader cushions and lopsided satellites. Lobe sizes,
heights, proportions and green tones vary; small pale-yellow flecks nest
into their tops. Rounded crowns retain their cushion shape as patch size
varies; each moss patch's height is limited by its horizontal footprint.

Each style has four variations for each layout:

| Filename configuration | Decorated edges | Mask | Sites per asset | Variations per style |
| --- | --- | --- | --- | --- |
| `edge` | North | 1 | 12 | 4 |
| `corner` | North, East | 3 | 23 | 4 |
| `strip` | North, South | 5 | 24 | 4 |
| `end` | North, East, West | 11 | 34 | 4 |

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

Each asset is one native mesh containing disconnected stone, blade, leaf or moss
islands. Use Select Linked in Edit Mode to edit individual components. Objects
are marked as Blender assets and tagged by style and configuration. Geometry
has no modifiers, procedural shader dependencies, transparency cards or rigs.
Grass blades and leaves are closed meshes with real thickness.

## Placement and export

- One tile is one Blender unit. X/Y are horizontal; Z is up. North is -Y.
- Mesh coordinates are centered on the tile: X/Y stay within -0.5..0.5.
- Each layout reaches its requested tile borders exactly. All styles scatter
  smaller pieces across a fixed edge band with one border contact per edge.
- Place the origin at the supporting tile's center and top surface. The bases
  slightly intersect that surface to hide contact seams. Conservative height
  limits are 0.062 units for pebbles, 0.190 for grass and 0.064 for moss,
  including its small flecks. `catalog.json` records each mesh's exact bounds.
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
