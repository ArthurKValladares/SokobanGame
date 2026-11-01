# Crystal models

The silhouettes are authored in the live Blender workshop via
`tools/model_crystal_silhouettes.py`: a squat crystal fitted to the actual ordinary boulder envelope, with
softened shoulders and subtle additional irregularity, and a gently irregular sphere matching
the ordinary skipping stone. Both are single connected closed solids.

`silhouettes.json` stores the Blender-authored polygon meshes in Z-up space.
Run `python tools/make_crystals.py` with numpy and Pillow to regenerate the
painted facet atlases and indexed, textured GLBs from those silhouettes.
The game applies crystal translucency and background blur.

`crystal_studio.blend` contains the editable sculpts, UVs, packed atlas
materials, comparison rocks and studio camera. The original Blender scene
is retained in this file. The orange and pink crystals share a surface style
but deliberately retain their block and sphere silhouettes.

The facet palette and painted gradients use a narrow tonal range. Softer
highlights and subtle colored shadow fill keep the surfaces readable without
large light-to-dark jumps. Facets are defined by their shape and gentle
color variation. About a third of physical edges carry partial scratch
highlights with broken coverage, varied thickness, fine scraping and flecks.
Most edges remain unmarked; shared-edge seeds align neighboring UV islands.
