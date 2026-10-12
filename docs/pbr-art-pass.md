# Model and terrain PBR art pass

The current manifest registers 375 models. The format-2 ownership inventory in
`tools/pbr_art_pass.json` covers every one: 50 generated PBR variants and 325
retained models owned by other authoring workflows. The barbarian's held axe is
one additional generated attachment. A retained entry records its exact active
manifest path and the reason this tool must preserve it.

The October 6, 2026 pass covered an earlier 64-model inventory, with 61 generated
variants and three neutral animation reference rigs. Those historical counts and
render captures do not describe today's model ownership. Native terrain,
artist-authored models and newer mechanics have since expanded the inventory.
Original vendor meshes, procedural inputs and editable Blender sources remain
separate from generated PBR outputs.

## Ownership and current coverage

| Model family | Registered models | Authoring owner |
| --- | ---: | --- |
| Generated PBR variants | 50 | Canonical sources and surface rules in `tools/pbr_art_pass.json`; `tools/make_pbr_art_pass.py` |
| Native modular ground | 240 | `assets/custom/source/ground_modules.blend`; normal export through `tools/sculpt_ground_shoulders.py` |
| Native cliff walls | 12 | `assets/custom/source/cliff_walls.blend`; Blender export and `tools/finalize_cliff_gltf.py` |
| Tile decorations | 48 | `assets/custom/source/tile_decorations.blend`; `tools/make_tile_decorations.py` and `tools/update_tile_decorations.py` |
| Quarry-marble stone walls | 8 | `assets/custom/source/stone_walls.blend`; `tools/make_wall_marble_pbr.py` and `tools/finalize_wall_marble_gltf.py` |
| Energy plates and activation components | 11 | Native PBR and emissive materials from `tools/make_energy_plate_models.py` |
| Wardrobe | 1 | Native materials from `tools/make_wardrobe_model.mjs` |
| Stone | 1 | Artist-painted `assets/custom/source/chunky_boulder.blend` |
| Lorekeeper | 1 | Vendor-authored skinned character and embedded materials |
| Animation reference rigs | 3 | Vendor animation libraries; neutral preview materials |

The generated group includes BricksA, Glass, Conveyor, six screen-selector flags,
Turret, six characters, furniture and props, rotators, LockPlate, Lectern, Ladder,
minecart pieces and ElevatorPlatform. Their active paths are
`custom/pbr/models/<name>.glb`. The generated axe belongs explicitly to
`Barbarian` attachment slot 0; publication preserves its node and transform.

GroundRock01 through GroundRock10 retain the native paths
`custom/models/ground_modules/ground_island_01.glb` through
`ground_island_10.glb`. They share ownership with the other 230 native ground
bodies. These names and paths participate in the renderer's native-ground
contract and must not be redirected to legacy PBR copies. The native models keep
their own geometry, UV basis, materials and texture bindings.

See the authoring instructions for [native ground](../assets/custom/source/ground_modules/README.md),
[cliff walls](../assets/custom/source/cliff_walls/README.md),
[tile decorations](../assets/custom/source/tile_decorations/README.md), and
[stone walls](../assets/custom/source/README.md). Follow those workflows when
editing retained families; the PBR atlas generator does not own their exports.

The ten earlier GroundRock generation specifications are preserved in
`legacy_models` as LegacyGroundRock01 through LegacyGroundRock10. Normal authoring
excludes them. Explicit legacy generation creates distinctly named, unregistered
`custom/pbr/models/LegacyGroundRockNN.glb` outputs; it cannot replace the active
native entries. Existing `custom/pbr/models/GroundRockNN.glb` reference artifacts
remain separate because native-ground migration audits use them as inputs.

## Map conventions

The model atlas and belt use five shared runtime images, and terrain adds six:

| Image | Encoding and use |
| --- | --- |
| `surface_normal.png` | Linear tangent-space normals, including flat glass/display cells |
| `surface_orm.png` | Linear R = conservative micro-cavity AO, G = roughness, B = metallic |
| `surface_emissive.png` | sRGB display color; black in all other cells |
| `belt_normal.png` | Repeating linear normals for the scrolling conveyor belt |
| `belt_orm.png` | Linear rubber response for the scrolling conveyor belt |
| `ground_grass_normal.png`, `ground_grass_orm.png` | Repeating linear data, aligned with grass albedo; roughness around .94 |
| `ground_rock_normal.png`, `ground_rock_orm.png` | Repeating linear data, aligned with stone slabs; roughness around .86 |
| `ground_rock_side_normal.png`, `ground_rock_side_orm.png` | Repeating linear data, aligned with sandstone walls; roughness around .88 |

The atlas contains 16 surface profiles with 24-texel dilated padding per cell.
UV0 keeps original colors; projected UV1 stays inside one material cell per
triangle. Explicit tangents match the normal map's UV basis. Vertices split at
projection and material seams. Skinning attributes, skeletons, transforms,
animation curves and triangle geometry are preserved. The conveyor's scrolling
material uses separate UV0 maps so its animation never crosses atlas cells.
LegacyGroundRock diagnostics use dedicated wall maps on UV0, with matching
tangents, and keep their UV1 atlas for diagnostics. This legacy layout is not a
requirement for retained native ground.

Stone is matte, wood has restrained grain, fabric has a woven response, leather
has soft pores, and exposed metal reflects light. Colored plastic and paint stay
dielectric. The monitor and handheld screen have masked emission; ordinary props
and clothing do not emit light. Glass has a smoother roughness response, with its
authored transparency and the engine's special effects intact.

BricksA omits the vendor color image and keeps a white base factor so its gameplay
tint remains authoritative. Stone uses the artist-authored
`custom/models/chunky_boulder.glb`, with packed terracotta-and-slate albedo and a
matte dielectric material. Its white draw tint, preserved aspect ratio and 0.93
tile scale keep the painted boulder inside one cell. The PBR generator retains
this asset and its editable Blender source.

Cavity AO is deliberately weak and applies only to wood, leather and stone. It
is derived surface detail, not a geometry AO bake, and affects ambient lighting.
Large-scale contact uses renderer SSAO. No displacement, height rendering,
clearcoat or transmission maps are added because the model shader does not
consume them.

The terrain splat shader blends all three normal and ORM channels using the same
weights and world UVs as color, and shares Cook-Torrance GGX with the model shader.
Natural terrain is nonmetallic and matte. Ground retains its cheaper one-tap
point-shadow policy; sharing GGX does not change shadow sampling counts.

## Authoring

Python 3, Pillow and NumPy are required by the PBR tools. Native-family authoring
also needs the Blender workflows linked above. The game has no new runtime
dependency. Start with the read-only checks from the repository root:

```powershell
python tools/make_pbr_art_pass.py --check-only
python tools/check_pbr_art_pass.py
python -m unittest discover -s tests -p test_pbr_art_pass.py
```

`--check-only` validates complete ownership and the publication plan without
writing maps, models, inventory or manifest. The checker audits 50 generated
models and one generated attachment against their sources, plus the ownership,
exact manifest paths and readability of 325 retained models. Retained families
keep their native layouts; their own validators provide detailed geometry and
material checks. The regression suite covers ownership conflicts, dry-run safety,
generated publication and retained-asset preservation in temporary directories.

When intentionally regenerating the authored PBR outputs:

```powershell
python tools/make_terrain_pbr.py
python tools/make_pbr_art_pass.py --update-manifest
python tools/check_pbr_art_pass.py
cmake --build --preset dev-fast
.\out\dev-fast\RelWithDebInfo\sokoban.exe --bake-tile-thumbnails --save-directory out/art-pass/thumbnail-profile
```

The generator validates complete ownership before writing outputs.
`--update-manifest` publishes generated models and the explicitly owned attachment,
while retaining other manifest paths. Edit `tools/pbr_art_pass.json` to tune surface
responses, palette regions and material assignments. Canonical source paths are
independent of the generated manifest paths. Regenerate a custom source model
first, then run the PBR tool. New manifest models require an explicit ownership
entry. Generated sources that gain artist-authored PBR maps cause the tool to
stop for review rather than replace their UV basis. Restart the game after
model edits.

To opt into the unregistered legacy diagnostics:

```powershell
python tools/make_pbr_art_pass.py --check-only --include-legacy
python tools/make_pbr_art_pass.py --include-legacy
```

The second command performs normal generation and additionally writes the ten
LegacyGroundRock outputs. It does not publish those diagnostics into the manifest
or overwrite the earlier GroundRock reference files.

The atlas height source was generated with built-in ImageGen, using the complete
prompt in `tools/pbr_texture_prompt.json`, and saved as
`assets/custom/pbr/surface_height_source.png`. The generator mathematically encodes
normal vectors and ORM channels from that source. Generated files are committed
art assets; unchanged generation preserves their bytes and timestamps.

Terrain height sources were edited from the three existing albedos using built-in
ImageGen. Their complete prompts are in `tools/terrain_pbr_prompts.json`, and the
sources are `assets/custom/pbr/ground_*_height_source.png`. The terrain tool uses a
periodic-plus-smooth decomposition and wrapped derivatives to encode seamless
maps, without changing albedo or editor-painted splat weights. Height images are
authoring inputs, not displacement textures consumed by the engine.

Optional terrain maps use manifest companion names: `FooNormal` and `FooOrm`
belong to the albedo named `Foo`. Default screens, overworld regions, custom
`@groundsplat` assignments, residency planning and thumbnails use the same
resolver. Missing companion maps fall back to a neutral normal and matte
dielectric.

## Validation and visual review

`check_pbr_art_pass.py` checks generated geometry against canonical sources:
triangle counts, original UV0, bone weights, animation data, nondegenerate detail
UVs, unit orthogonal tangents, material factors and display masks.
Sub-float-precision coincident seam slivers in the imported barbarian head are
excluded only from the UV-area check. Terrain checks cover the six linear repeat
maps and matte/nonmetallic data, and level decoration references must resolve to
registered models. The checker prints current ownership totals rather than
assuming a fixed historical model count.

`assets/custom/pbr/inventory.json` records the most recent generation report,
including source paths, retained decisions and generated triangle counts. The
config and current manifest determine current coverage; an older committed
report is not a substitute for ownership validation.

The original art pass built the optimized editor with warnings-as-errors and ran
content validation, GPU loading, six gallery captures, a matched gameplay view,
and 57 tile thumbnail bakes. Its 96-suite CTest result is historical evidence,
not a claim about the current test suite. Rerun the current checks after asset or
pipeline changes.

Local historical review outputs are under `out/art-pass/`: `before/`, `after/`,
`gallery-0/` through `gallery-5/`, and `review-model-order.json`. These outputs are
ignored by Git. Temporary gallery levels live in an isolated review content tree.
Terrain evidence is under `out/terrain-pass/`: a matched `overworld/` view, three
`gallery-<0..2>/` views, the eight-light `point-lights/` stress view, `preview.png`,
and `ctest-passed.log`. Those captures reported no dropped draws, poses, residency
evictions or capacity blocks; the six terrain maps added about 8 MiB of observed
GPU texture residency in that run.

## Remaining gaps and deliberate exclusions

Complete ownership does not mean every model uses the shared atlas. Retained
models keep their native maps, embedded palettes or uniform materials; the three
animation reference rigs remain neutral. Glass deliberately stays smooth, and
emission in the generated atlas is limited to displays. Newly imported vendor
meshes need an explicit ownership review before joining the registered inventory.

Decorative fallback cubes, portal aperture frames and minecart barriers still
offer opportunities for more specific material appearances. Pressure plates,
buttons, End markers and their activation components now have their own native
model workflow. Water, mirror energy, gate energy, beams, particles and UI have
dedicated procedural or unlit rendering, so model material maps would not be
consumed by those paths.

The renderer still lacks environment probes/IBL, clearcoat and transmission.
Current metals use direct lighting and approximate ambient fill; glass retains
its existing blur/refraction effect. Extra map files cannot supply those features.
