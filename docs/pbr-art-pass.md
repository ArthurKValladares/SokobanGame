# Model and terrain PBR art pass

The October 6, 2026 pass reviewed all 64 registered models and authored PBR
variants for 61, plus the barbarian's held axe. The three animation reference
rigs retain their neutral preview materials. Original vendor and procedural
sources remain intact. Active manifest entries point to `assets/custom/pbr/models`.

The terrain follow-up adds albedo-aligned normal and ORM pairs for grass, rock
tops and sandstone ground walls. The splat shader blends all three channels
and tangent-space normals using the same weights and world UVs as color, and
shares Cook-Torrance GGX with the model shader. Natural terrain is nonmetallic
and matte, with weak cavity occlusion applied only to ambient lighting.

The existing silhouettes, color atlases, gameplay tints and animation are
preserved. Stone is matte, wood has restrained grain, fabric has a woven response,
leather has soft pores, and exposed metal reflects light. Colored plastic and
paint stay dielectric. The monitor and handheld screen have masked emission;
ordinary props and clothing do not emit light. Glass gains a smoother roughness
response, with its authored transparency and the engine's special effects intact.

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
triangle. Explicit tangents match the normal map's UV basis. Vertices split only
at projection and material seams; vertex count increases approximately 8.6%.
Skinning attributes, skeletons, transforms, animation curves and triangle geometry
are preserved. The conveyor's scrolling material uses separate UV0 maps so its
animation never crosses atlas cells. All ten GroundRock bodies now use the
dedicated wall maps on UV0, with tangents derived from that UV basis. Their UV1
atlas remains available for diagnostics. BricksA and Stone omit the vendor color image
and keep a white base factor so their gameplay tint remains authoritative.

Cavity AO is deliberately weak and applies only to wood, leather and stone. It
is derived surface detail, not a geometry AO bake. Large-scale contact uses the
renderer SSAO. No displacement, height rendering, clearcoat or transmission maps
are added because the current model shader does not consume them.

## Authoring

Python 3, Pillow and NumPy are required by the authoring tools. The game has no new
runtime dependency. From the repository root:

```powershell
python tools/make_terrain_pbr.py
python tools/make_pbr_art_pass.py --update-manifest
python tools/check_pbr_art_pass.py
cmake --build --preset dev-fast
.\out\dev-fast\RelWithDebInfo\sokoban.exe --bake-tile-thumbnails --save-directory out/art-pass/thumbnail-profile
```

Edit `tools/pbr_art_pass.json` to tune surface responses, palette regions and
material assignments. The config retains canonical source paths independently
of the manifest, so regeneration remains repeatable. Regenerate a custom source
model first, then run the PBR tool. New manifest models require an explicit
review entry. Sources that gain artist-authored PBR maps cause the tool to stop
for review rather than replace their UV basis. Restart the game after model edits.

The height source was generated with built-in ImageGen, using the complete prompt
in `tools/pbr_texture_prompt.json`, and saved as
`assets/custom/pbr/surface_height_source.png`. The generator mathematically encodes
the normal vectors and ORM channels from that source. Generated files are committed
art assets; an unchanged regeneration preserves their bytes and timestamps.

Terrain height sources were edited from the three existing albedos using built-in
ImageGen. Their complete prompts are in `tools/terrain_pbr_prompts.json`, and
the sources are `assets/custom/pbr/ground_*_height_source.png`. The terrain tool
uses a periodic-plus-smooth decomposition and wrapped derivatives to encode
seamless maps, without changing albedo or editor-painted splat weights. Height
images are authoring inputs, not displacement textures consumed by the engine.

Optional terrain maps use manifest companion names: `FooNormal` and `FooOrm`
belong to the albedo named `Foo`. Default screens, overworld regions, custom
`@groundsplat` assignments, residency planning and thumbnails use the same resolver.
Missing companion maps fall back to a neutral normal and matte dielectric.
Ground retains its cheaper one-tap point-shadow policy; sharing GGX does not
change model or terrain shadow sampling counts.

## Coverage

All upgraded models receive metallic/roughness maps. Normal detail is omitted for
the standalone glass model; it benefits from a smooth surface. Occlusion is attached
where cavity detail is useful, and emission is attached only to display-bearing
materials. `assets/custom/pbr/inventory.json` includes per-model triangle counts
and every original source path.

| Registered model | Surface responses / decision |
| --- | --- |
| BricksA | stone |
| Stone | stone |
| Glass | glass |
| Conveyor | paint, rubber, steel |
| ScreenSelectorAPlayable | cloth, paint |
| ScreenSelectorASolved | cloth, paint |
| ScreenSelectorAUnavailable | cloth, paint |
| ScreenSelectorBPlayable | cloth, paint |
| ScreenSelectorBSolved | cloth, paint |
| ScreenSelectorBUnavailable | cloth, paint |
| Turret | brass, steel |
| Rogue | cloth, hair, leather, skin, steel |
| Knight | cloth, hair, leather, skin, steel |
| Druid | cloth, hair, leather, skin, steel |
| Witch | cloth, glass, hair, leather, skin, steel |
| Bard | brass, cloth, hair, skin, steel |
| Barbarian | cloth, hair, leather, skin, steel |
| PictureFrameLargeA | paper, wood |
| Decoration_cactus_small_A | ceramic, foliage |
| Decoration_swiper_yellow | paint |
| Decoration_desk | wood |
| Decoration_gameconsole_handheld | display, paint, rubber |
| Decoration_chair_C | cloth, wood |
| Decoration_chair_B | cloth, wood |
| Decoration_mousepad_A | rubber |
| Decoration_monitor | display, paint |
| Decoration_barrier_3x1x1_blue | paint |
| Decoration_rug_rectangle_stripes_B | cloth |
| Decoration_arrow_crossbow | foliage, steel, wood |
| Decoration_metal | steel |
| Decoration_striped_block_blue | paint |
| Decoration_chair_A | cloth, wood |
| Decoration_chair_desk_A | cloth, paint, rubber |
| Decoration_keyboard | paint |
| Decoration_Rig_Large_MovementBasic | Reference animation rig: retain neutral preview material. |
| Decoration_Rig_Large_General | Reference animation rig: retain neutral preview material. |
| Decoration_Rig_Medium_General | Reference animation rig: retain neutral preview material. |
| Decoration_star_yellow | paint |
| Decoration_spring_pad_yellow | paint, steel |
| Decoration_coin_2_gold | brass |
| Decoration_coin_2_copper | brass |
| RotatorClockwise | paint, steel |
| RotatorCounterClockwise | paint, steel |
| LockPlate | paint, steel |
| Lectern | brass, cloth, endgrain, leather, paper, wood |
| Ladder | steel, wood |
| RotatorGear | steel |
| RotatorIconClockwise | paint |
| RotatorIconCounterClockwise | paint |
| MinecartRailStraight | steel, wood |
| MinecartRailStop | paint, steel, wood |
| MinecartRailCorner | steel, wood |
| MinecartHandcar | steel, wood |
| ElevatorPlatform | wood |
| GroundRock01 | stone |
| GroundRock02 | stone |
| GroundRock03 | stone |
| GroundRock04 | stone |
| GroundRock05 | stone |
| GroundRock06 | stone |
| GroundRock07 | stone |
| GroundRock08 | stone |
| GroundRock09 | stone |
| GroundRock10 | stone |

The held axe also uses the authored metal/wood/paint-detail atlas through the
barbarian's material binding. Its source-scale geometry and attachment transform
are preserved.

## Validation and visual review

`check_pbr_art_pass.py` checks all 64 inventory entries, 61 upgraded models and the
held axe. It verifies 87,355 triangles against their sources, original UV0, bone
weights and animation data, nondegenerate detail UVs, unit orthogonal tangents,
material factors and display masks. Sub-float-precision coincident seam slivers
in the imported barbarian head are excluded only from the UV-area check.
The terrain checks verify all six linear repeat maps, matte/nonmetallic data,
UV0 wall tangents and handedness, and every level decoration's registered model.

The optimized editor build completes with warnings-as-errors. Content validation,
GPU loading and six gallery render captures exercise the new assets. The gallery
covers every static variant plus all five heroes and the barbarian with its axe.
A gameplay before/after capture uses the same overworld view. The 57 editor tile
thumbnails were rebaked; unchanged images retain the same bytes.

`ctest --preset dev-fast -j 1 --output-on-failure` passed all 96 suites, including
Vulkan and package validation. The restricted Windows sandbox reports temporary
path/import failures in `asset_manifest_editor` and `content_pipeline`; both pass
outside the sandbox, including the complete 96-suite run. The audio/filesystem
implementation was not changed by this art pass.

Local review outputs are under `out/art-pass/`: `before/`, `after/`, `gallery-0/`
through `gallery-5/`, and `review-model-order.json` (gallery items in row order).
These validation outputs are ignored by Git. The temporary gallery levels live
in an isolated review content tree and do not alter campaign levels.

Terrain evidence is under `out/terrain-pass/`: the matched `overworld/` view,
three `gallery-<0..2>/` views for pure grass, pure rock and blending across the ten
cliff profiles, the eight-light `point-lights/` stress view, `preview.png`, and
`ctest-passed.log`. The captures report no dropped
draws, poses, residency evictions or capacity blocks; six terrain data maps add
about 8 MiB of observed GPU texture residency.

## Remaining gaps and deliberate exclusions

The follow-up checks active manifest models, attachment materials, source level
decorations, default and custom terrain assignments, shader consumers, residency
requirements and thumbnail rendering. No additional registered model is missing
the maps selected for its surface. Glass deliberately stays smooth; emission is
limited to displays. The three animation reference rigs stay neutral.

These procedural surfaces still use the triangle shader's uniform fallback
material (metallic 0, roughness 1), without per-surface texture maps:

| Surface | Remaining art opportunity |
| --- | --- |
| Pressure plates, buttons and End markers | Authored surface relief and distinct roughness |
| Decorative fallback cubes | A defined material appearance and mapped surface detail |
| Portal aperture frames and minecart barriers | Separate painted/metal surface response; portal sparks already emit |

These are remaining art opportunities outside the registered-model pass, rather
than missing maps on imported models. Water, mirror energy, gate energy, beams,
particles and UI have dedicated procedural/unlit rendering; material maps would
not be consumed by those paths. Unused vendor-library meshes and future editor
imports also need an explicit review before joining the current inventory.

The renderer still lacks environment probes/IBL, clearcoat and transmission.
Current metals use direct lighting and approximate ambient fill; glass retains
its existing blur/refraction effect. Extra map files cannot supply those features.
