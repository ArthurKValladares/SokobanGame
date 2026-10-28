# Floating ground debris

The 100 exposed floating-ground bodies in `assets/custom/source/ground_modules.blend`
contain 190 detached fragments. Their existing `FloatingRockNN` vertex groups
identify each editable rock. Revision 1 varies their sizes, proportions,
orientations and positions in all three axes. Fixed terrain and cap geometry,
triangle ownership, UV charts and materials retain their existing authoring.

Each fragment has a constant positive seed in the native POINT/FLOAT attribute
`_GROUND_DEBRIS`. Fixed vertices have zero. Export **Attributes** as well as
normals, tangents and both UV charts. The static loader requires an unnormalized
FLOAT SCALAR per vertex with finite values in [0, 1]. Old assets without the
attribute remain stationary.

`shaders/include/GroundDebris.glsl` translates each marked rock as one rigid
piece, using independent speeds, phases and amplitudes on three axes. Two slow
sine components keep the motion from following a single repeated beat. A stable
tile seed separates repeated model instances. The clock follows Simulation Speed,
including freeze at zero. Model rendering, sun shadows, point shadows and
outlines share the deformation. The mesh still renders in one draw per tile;
there are no per-frame mesh uploads or individual rock objects at runtime.

Full excursions are at most 0.008 tile in X/Y and 0.020 in Z. Authoring reserves
that envelope within the existing side/depth bounds and retains at least 0.04
tile of visible surface clearance throughout motion. Conservative swept bounds
protect visibility and shadow culling. Point shadows refresh when the animation
clock advances and can reuse depth when it is frozen.

For artist edits, open the native source and edit the existing fragment groups.
Run `tools/vary_floating_ground_debris.py` in Blender to refresh markers, save
the source and export the 100 current exposed bodies. Normal exports preserve
their edited geometry. `-- --vary-debris` performs the initial deterministic
layout pass only on bodies that do not yet have `FloatingDebrisRevision=1`;
it does not repeatedly resize an already revised body. Existing bottom/shoulder
export tools also retain the custom attribute.

After exporting, normalize Blender image references and recapture source
evidence from the saved source in a fresh background process:

```powershell
$models = Get-ChildItem assets/custom/models/ground_modules -Filter 'ground_bottom_*.glb'
python tools/finalize_cliff_gltf.py $models.FullName
blender --background assets/custom/source/ground_modules.blend --python tools/snapshot_current_ground.py
python tools/validate_current_ground.py --output out/floating-debris/current-ground-validation.json
```

The current audit independently checks native/export marker ownership, rigid
seeds, source/export corners, fixed terrain, actual surface gaps and the full
motion envelope. Historical migration validators describe their frozen revisions.
