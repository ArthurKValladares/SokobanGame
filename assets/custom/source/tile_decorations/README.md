# Tile decorations

`../tile_decorations.blend` contains 48 editable decoration assets, authored in
Blender 5.0.1. The set is deliberately plain: pebbles are a dotted line of
small smooth grey stones, grass is a loose trail of short slim leaves with the
odd upright sprig, and moss is a few green lumps grown into one another. Each
style is one shape in one colour, and a border holds a handful of pieces with
clear tile between them, so a decorated tile stays quiet next to the things
the player has to read.

Every style lies along each decorated border in the same way. An even run of
pieces follows the border, most of them close to it, and a few strays sit
among them. Where two decorated borders meet, one piece sits in the corner
and reaches both; elsewhere the first piece of a run reaches its border. Each
border owns the nearer half of a shared corner. The bands are 0.16 units deep
for pebbles, 0.21 for grass and 0.26 for moss. All four variants of a layout
keep its bands and vary every piece.

| Style | The run along a border | Strays | Sways |
| --- | --- | --- | --- |
| Pebbles | 5-7 single stones of radius 0.03-0.05 | 1-3 stones of half that size | No |
| Grass | 14-17 single leaves, each lying well over | 6-8 more leaves and 1-2 sprigs of three or four upright blades | Yes |
| Moss | 2-3 clumps of three to seven lumps of radius 0.034-0.06, some with a pale fleck | 1-3 lone small lumps | Crowns, a little |

A stone and a lump of moss are the same shape, `mound`: a skirt in the ground,
a shoulder, a brow and a crown, with an uneven outline. A leaf is a closed
diamond section over two or three rings and a tip, arching away from its root.
Everything is smooth shaded and every normal leans toward the sky, grass and
moss by more than stones, so a piece takes the light of the ground it lies on
and reads as one flat colour rather than as a lit half and a shaded half.

## Palettes

Each style has one embedded 128 by 16 palette of eight ramps, written beside
this file as `<style>_palette.png`. A column is a ramp and a row is a place
along it: the bottom row is the colour at a root, a skirt or an underside, the
top row the colour at a tip or a crown. A face picks its column and sits on
that column's centre; each of its corners picks its row. The ramps are
`PALETTES` in `tools/make_tile_decorations.py`. A style keeps to a few close
ramps, repeated to fill the eight columns: four warm greys for pebbles, three
light greens for grass, and three deeper greens and a pale fleck for moss.

## Wind

Grass and moss carry a per-vertex `_WIND` weight, exported as a custom float
attribute: 0 at anything in the ground and rising to a tip or a crown. A leaf
reaches 0.9 at its tip and a sprig's blade 1. A lump of moss reaches 0.5 at
its crown, less if it is small, and a fleck takes the weight of the surface
it sits on so that it rides with it. Pebble meshes have no `_WIND` attribute
at all. The game's wind moves a vertex by its weight squared, so moss gives
about a quarter as far as grass; see `docs/stylized-rendering.md`. The weight
is `winds` in `MeshBuilder`, set by `leaf` and `mound` from their `wind`
argument.

Each style has four variations for each layout:

| Filename configuration | Decorated edges | Mask | Corner pieces | Variations per style |
| --- | --- | --- | --- | --- |
| `edge` | North | 1 | 0 | 4 |
| `corner` | North, East | 3 | 1 | 4 |
| `strip` | North, South | 5 | 0 | 4 |
| `end` | North, East, West | 11 | 2 | 4 |

Every layout follows the perimeter. The opposite-edge pair has no geometry
through the tile center. `catalog.json` records `focal_points` in Blender
coordinates: two for `strip`, one for other layouts. The compatible
`focal_point` field contains the first point. For every style these are
representative points retained for compatibility and do not control density.

Quarter turns cover every one-, two- and three-edge orientation. Assets are
named `tile_pebbles_edge_01` through `tile_moss_end_04`; `catalog.json` records
every filename, mask, variation, mesh bounds and exported counts.

## Blender source

As of 2026-11-26 the `.blend` and the previews are one step behind the
models: they still hold the previous kit. Open `../tile_decorations.blend`
and run `tools/update_tile_decorations.py` once, then render the three
scenes, and remove this paragraph.

**TileDecorations** is the complete labeled catalog. **Decoration assets |
48 meshes** contains the actual assets; **Presentation | do not export** holds
the display tiles, text, lights, background and camera. **TileDecorations_Closeup**
shows representative corner assets for all three styles at a larger scale.
**TileDecorations_LayoutPreview** compares all four layouts for all three
styles. The prior default scene is retained separately.

Each asset is one native mesh containing disconnected stone, leaf and lump
islands; the lumps of a clump of moss overlap. Use Select Linked in Edit Mode to edit
individual components. Objects are marked as Blender assets and tagged by
style and configuration. Geometry has no modifiers, procedural shader
dependencies, transparency cards or rigs. Every island is closed, blades
included, because the game culls back faces. Normals are custom split normals
and `_WIND` is a point attribute, both written by the authoring scripts; an
edit in Blender that recalculates normals or adds geometry should be followed
by a refresh from the scripts, or it will export without them.

## Placement and export

- One tile is one Blender unit. X/Y are horizontal; Z is up. North is -Y.
- Mesh coordinates are centered on the tile: X/Y stay within -0.5..0.5.
- Each layout reaches its requested tile borders exactly and crosses none. All
  styles scatter their pieces across a fixed edge band, with one shared piece
  at adjacent corners.
- Place the origin at the supporting tile's center and top surface. The bases
  slightly intersect that surface to hide contact seams. Stones stay under
  0.035 units tall, moss under 0.05 and grass, sprigs included, under 0.14.
  `catalog.json` records each mesh's exact bounds. In the game the wind
  carries a tip up to its sway amplitude past those bounds.
- Preserve source scale. Do not fit these partial-edge meshes to a whole cell.
- Catalog object locations are display offsets. Before export, temporarily set
  the chosen mesh's location to zero; restore its catalog location afterward.
- Export only that mesh as glTF Binary, with Selected Objects, Active Scene,
  normals, UVs and custom attributes enabled, and animation disabled. Custom
  attributes are what carries `_WIND`. Keep identity node transforms: the
  game's static loader reads the authored vertices without node transforms.

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
embedded PNG data. It also checks the wind weights: grass and moss must carry
`_WIND` as an unnormalized float in 0..1 with something that sways and
nothing in the ground that does, and pebbles must not carry it. The refresh
script checks the native Blender meshes for open boundaries, nonmanifold
edges and loose vertices before it exports; all are closed.

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
