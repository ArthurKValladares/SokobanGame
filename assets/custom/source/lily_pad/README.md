# Lily pad

`lily_pad.blend` is the editable native source for the single-tile floating
leaf in `../../models/lily_pad.glb`. `preview.png` shows the actual runtime
geometry on a studio water backdrop. The studio collection is presentation
only and is excluded from the runtime GLB.

The leaf has a broad, slightly irregular rounded silhouette, deep V notch,
gently curled beveled rim, seven pale raised veins with smaller branches,
and a closed dark green underside. Its quiet matte palette and flat-shaded
facets fit the game's KayKit foliage. There is no flower or projecting stem
to interfere with standing units.

The `LilyPad` manifest model preserves source scale. Engine coordinates are
X east, Y south, Z up; the notch faces south (+Y). Geometry stays inside the
tile's XY unit square and is about 0.91 tile wide. Local bottom is Z=0;
the standing plane at the center is Z=0.070. Broad leaf surfaces stay near
that plane, while the curled rim reaches about Z=0.084. Translation, bobbing,
and subtle tilt are supplied by the renderer. The glTF rotates engine
coordinates to `(X,Y,Z) = (x,z,-y)`; no node transform is required.

Regenerate the runtime mesh deterministically with Python's standard library:

```powershell
python tools/make_lily_pad_model.py
python tests/test_lily_pad_model.py
```

Recreate the native Blender source and preview in a separate Blender process:

```powershell
blender --background --factory-startup --python tools/make_lily_pad_model.py -- --blender
```

Regeneration replaces the outputs, including any manual native edits. Modify
the generator to preserve changes across rebuilds. Blender is optional for
the runtime asset and tests; it is needed only to recreate the native file
and rendered preview. The standard PBR art pass retains this model and its
authored green materials rather than replacing them.
