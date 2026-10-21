# Round rock

`round_rock.blend` is the editable native source for
`../../models/round_rock.glb`. `preview.png` shows the exported geometry with
the same studio lighting used by the regular boulder.

The single closed mass has a nearly spherical silhouette with subtle broad
asymmetry, softly beveled facets and a small flat resting foot. It reuses
the regular boulder's painted terracotta crown and cool slate lower palette,
baked onto its own UV atlas. The embedded 1024-pixel albedo and matte material
work directly in the game. The preview studio is excluded from the export.

The origin is centered at the resting base, Z=0 in Blender. Dimensions are
approximately 0.941 x 0.874 x 0.885. The GLB uses Y up, with no node transform.
The manifest uses the same aspect-preserving normalization and 0.93 tile
scale as the regular rock.

Recreate the sculpt, UV bake, GLB, native file and preview in a separate
Blender process:

```powershell
blender --background --factory-startup --python tools/make_round_rock.py
python tests/test_round_rock_model.py
```

The generator loads the existing material authoring nodes from
`../chunky_boulder.blend`. It replaces this asset's generated outputs;
edit the generator to preserve changes across rebuilds. The PBR art pass
retains this model and its authored material.
