# Sokoban PBR variants

These are authored derivatives of the project's existing models. Original meshes,
color atlases and procedural generators remain in their source directories.
`inventory.json` records the source of every derivative and the three retained
animation reference rigs. The generator and editable material assignments are
`tools/make_pbr_art_pass.py` and `tools/pbr_art_pass.json`.

Mesh and base-color sources are Kay Lousberg's KayKit Adventurers 2.0, Block Bits
1.0, Board Game Bits 1.0, Furniture Bits 1.0, Platformer Pack 1.0, Witch and
Magical Girl packs; Kenney's Platformer Kit; and this project's custom models.
The original packs and their included notices remain the source of licensing
information. Kenney's Platformer Kit notice is preserved here as `License.txt`.

The grayscale surface-detail source atlas was created with built-in ImageGen.
Its complete prompt is recorded in `tools/pbr_texture_prompt.json`. Normal and
packed material maps are deterministically encoded from that source.

Normal/ORM images are linear data. ORM channels are R=small-scale cavity
occlusion, G=perceptual roughness, B=metallic. Emissive is sRGB, black outside
the display cell. UV0 retains the original color mapping; UV1 and explicit
tangents carry the surface atlas. The scrolling belt uses separate UV0 maps.

Terrain height sources were edited from the existing grass, rock and wall
albedos using built-in ImageGen; prompts are in `tools/terrain_pbr_prompts.json`.
`tools/make_terrain_pbr.py` encodes six repeatable normal/ORM maps using periodic
height derivatives. Ground splats blend them in world UVs; the ten cliff models
use the matching sandstone maps on their original UV0 with matching tangents.

See `docs/pbr-art-pass.md` for coverage, regeneration and validation details.
