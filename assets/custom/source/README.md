# Stone walls

`stone_walls.blend` is the editable source for the eight `wall_stone_01.glb`
through `wall_stone_08.glb` assets in `../models/`. The initial prototype was
modeled through Blender 5.0.1's interface. The current meshes were reworked
in the live Blender scene through MCP for Blender, with individually specified
stone sizes and placements, chamfered edges, and overlapping capstones.

The set follows the reference's chunky rectangular stones. Each
wall has an asymmetric arrangement of broad and narrow body stones, recessed
faces, and top blocks with different widths, depths, and thicknesses. Height
differences stay modest so the overall silhouette remains rectangular. The
previous four equal sections have been replaced with distinct compositions.
All variants now share a textured quarry-marble PBR material: pale ivory and
cool gray, broken mineral veins, and sparse ochre weathering, painted in the
same style as the ground textures. The marble remains nonmetallic and mostly
matte, with variation across fresh and weathered surface patches.

Each wall now has four or five main upper sections with unequal widths and
modest height tiers. Small raised badges and short stacked pieces were removed;
their larger neighbors extend across those areas to simplify the in-game
silhouette and reduce seams. Existing chamfers, broad facets, and chips remain
on the surviving stones. Fixed lower cap edges and the recessed backing keep
the assembly closed within its original horizontal tile footprint.

The joints have recessed stone backing, so gaps between the visible stones
do not show through the wall. Large faces have pronounced broad facets, and
edges have varied bevels, irregularities, and readable localized chips baked
into the mesh. Quieter faces balance the stronger details. This rock pass
preserves each composition and its overall dimensions. It needs no procedural
shader or runtime geometry generation.

The scene names the objects `WallStone01` through `WallStone08` and spaces
them along X for editing. Each wall is a single mesh containing separate
stone islands and a joined backing core; use Select Linked in Edit Mode to
edit a stone independently. The `RockSurface` vertex group identifies the
visible stone shells for future surface edits.

`QuarryUV` is UV0: the original cube projection uses a 1.35-unit texture scale,
with different image crops for the individual stones and wall variants. The
simplified stones retain those crops as their faces extend into adjacent areas.
Mapping offsets are baked into the UV coordinates. Geometry and closed joints
are unchanged by the material pass. Three images are packed in the Blender
source and shared by the exported GLBs in `../pbr/wall_quarry_marble/`:

- `albedo.png`: 1024-square sRGB painted base color, neutral 0.85 linear RGB
  material factor for a slightly darker marble. Blender's Color Multiply node
  exports this factor directly; the original texture artwork remains intact.
- `normal.png`: aligned linear OpenGL/Blender +Y tangent-space detail; normal
  strength 1, with encoded relief around 8 degrees at the 90th percentile.
- `orm.png`: linear R = weak local cavity AO, G = roughness, B = metallic 0.
  Roughness factor is 1; metallic factor is 0. This image is bound to both
  metallic/roughness and occlusion slots. AO affects ambient lighting only.

Standalone `roughness.png`, `occlusion.png`, and `metallic.png` accompany the
packed map for editing. Native `albedo_source.png` and `height_source.png` are
the authored ImageGen inputs; height is an authoring source. The exact prompts
are saved in `tools/wall_marble_texture_prompts.json`. Regenerate the aligned
data maps with `python tools/make_wall_marble_pbr.py`; this mathematical encoding
preserves the source artwork and uses wrapped derivatives for repeating detail.

To update one game asset, select its object and export as glTF Binary (`.glb`)
with **Selected Objects**, normals, UVs, and tangents enabled and animation
disabled. Enable **Keep Original** textures for the shared PNG references,
then run `python tools/finalize_wall_marble_gltf.py` from the project root.
This removes Blender 5.0's redundant image buffer-view references and repairs
three empty tangent records on very thin backing triangles from their matching
adjacent face frames. Positions, normals, UVs, indices, and the wall geometry
remain unchanged. Use repeat wrapping and linear filtering.
Keep the stone assembly joined in one mesh, with the stones'
relative transforms baked into its vertices: the game's static model loader
reads mesh coordinates and fits the whole wall into its cell. It does not
apply glTF node transforms. The scene's catalog spacing is only for editing.

`Wall` / `#` remains variant 01 for existing levels. The Wall palette group
uses the same variant picker as Ground and exposes all eight stone brushes.
