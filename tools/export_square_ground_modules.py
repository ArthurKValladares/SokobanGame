"""Export the editable square ground kit from ground_modules.blend.

Run with Blender, for example:
  blender --background assets/custom/source/ground_modules.blend --python tools/export_square_ground_modules.py
Use -- --add-square-variants once to add the native meshes to an older source.
Normal exports read existing meshes and do not rebuild artist edits.
"""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile

import bpy

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/custom/source/ground_modules.blend"
DATA = SOURCE.parent / "ground_modules"
MODELS = ROOT / "assets/custom/models/ground_modules"
FAMILIES = ("island", "end", "strip", "corner", "edge", "interior")
MASKS = (15, 11, 5, 3, 1, 0)


def finish_body(mesh):
    mesh.update()
    normals = [(0, 0, 0)] * len(mesh.loops)
    for polygon in mesh.polygons:
        polygon.use_smooth = False
        for index in polygon.loop_indices:
            normals[index] = tuple(polygon.normal)
    mesh.normals_split_custom_set(normals)
    for layer in mesh.uv_layers:
        mesh.calc_tangents(uvmap=layer.name)
        assert all(loop.tangent.length > .5 for loop in mesh.loops), mesh.name


def add_variants(scene):
    collection = bpy.data.collections.get("GroundSquareModules")
    if collection is None:
        collection = bpy.data.collections.new("GroundSquareModules")
        scene.collection.children.link(collection)
    for variant in range(10):
        for shape, family in enumerate(FAMILIES):
            stem = f"GroundSquare{family.title()}{variant+1:02d}"
            body = bpy.data.objects.get(stem + "_Body")
            if body is None:
                original = bpy.data.objects[f"Ground{family.title()}{variant+1:02d}_Body"]
                body = original.copy()
                body.data = original.data.copy()
                body.name = stem + "_Body"
                body.data.name = stem + "_BodyMesh"
                collection.objects.link(body)
                # Undo the existing upper-band lip lowering in native Blender
                # vertices. Rock fractures, horizontal coordinates and UV charts
                # stay intact, including the backing that closes the grooves.
                for vertex in body.data.vertices:
                    if vertex.co.z > .7:
                        vertex.co.z = .7 + (vertex.co.z - .7) / .7
                finish_body(body.data)
            body.location = (shape * 1.8 + 12, variant * 1.8, 0)
            body["GroundModuleVariant"] = variant
            body["GroundModuleShape"] = family
            body["ExposedSideMask"] = MASKS[shape]
            body["NativeLipHeight"] = 1.0
            body["SquareTop"] = True
            if bpy.data.objects.get(stem + "_Top") is None:
                mesh = bpy.data.meshes.new(stem + "_TopMesh")
                mesh.from_pydata([(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)], [], [(0, 1, 2, 3)])
                mesh.materials.append(bpy.data.materials["GroundModuleCap"])
                mesh.update()
                uv = mesh.uv_layers.new(name="GroundUV")
                for loop in mesh.loops:
                    point = mesh.vertices[loop.vertex_index].co
                    uv.data[loop.index].uv = (point.x / 4, point.y / 4)
                top = bpy.data.objects.new(stem + "_Top", mesh)
                collection.objects.link(top)
                top.location = body.location
                top["GroundModuleVariant"] = variant
                top["ExposedSideMask"] = MASKS[shape]
                top["ConcaveCorners"] = 0
                top["SquareTop"] = True


def add_preview():
    name = "GroundModules_LayerPreview"
    if name in bpy.data.scenes:
        return
    preview = bpy.data.scenes.new(name)
    preview["Description"] = "Square lower lips close stepped and stacked terrain; chamfered exposed upper surfaces remain."
    # Two mixed-style tiers with a low walkway, followed by a three-high stack.
    instances = []
    for y in range(3):
        for x in range(3):
            instances.append((True, (x + y * 3) % 10, (x, y, 0)))
            if x == 2:
                instances.append((False, (y + 4) % 10, (x, y, 1)))
    for z in range(3):
        instances.append((z < 2, (z + 6) % 10, (5, 1, z)))
    for index, (square, variant, location) in enumerate(instances):
        stem = f"Ground{'Square' if square else ''}Island{variant+1:02d}"
        for part in ("Body", "Top"):
            obj = bpy.data.objects[stem + "_" + part].copy()
            obj.name = f"LayerPreview_{index:02d}_{part}"
            preview.collection.objects.link(obj)
            obj.location = location


def record_top(obj, variant, shape):
    mesh = obj.data
    return {"variant": variant, "shapeIndex": shape, "exposedSideMask": MASKS[shape],
            "concaveCorners": 0, "object": obj.name,
            "vertices": [list(vertex.co) for vertex in mesh.vertices],
            "faces": [list(polygon.vertices) for polygon in mesh.polygons],
            "normals": [list(polygon.normal) for polygon in mesh.polygons],
            "wallCoverage": [[0.0] * len(polygon.vertices) for polygon in mesh.polygons]}


def export_body(body, path):
    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    location = body.location.copy()
    body.location = (0, 0, 0)
    try:
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = bpy.ops.export_scene.gltf(
                filepath=str(path), export_format="GLB", use_selection=True, use_active_scene=True,
                export_normals=True, export_tangents=True, export_texcoords=True,
                export_animations=False, export_yup=True, export_keep_originals=True, export_extras=True)
        assert "FINISHED" in result, result
    finally:
        body.location = location


def main():
    assert Path(bpy.data.filepath).resolve() == SOURCE.resolve(), "Open the native ground_modules.blend source first"
    scene = bpy.data.scenes["GroundModules"]
    bpy.context.window.scene = scene
    # Blender's image encoder uses Python temporary files. Keep those inside
    # the workspace so exports also work under the desktop filesystem sandbox.
    temporary = ROOT / "out/ground-modules/export-temp"
    temporary.mkdir(parents=True, exist_ok=True)
    tempfile.tempdir = str(temporary)
    arguments = sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else []
    if "--add-square-variants" in arguments:
        add_variants(scene)
        add_preview()
        # Keep the existing source in place; Blender's optional .blend1 backup
        # is unnecessary because Git already retains the original file.
        previous = bpy.context.preferences.filepaths.save_version
        try:
            bpy.context.preferences.filepaths.save_version = 0
            bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE))
        finally:
            bpy.context.preferences.filepaths.save_version = previous
    records, snapshot = [], []
    MODELS.mkdir(parents=True, exist_ok=True)
    for variant in range(10):
        for shape, family in enumerate(FAMILIES):
            stem = f"GroundSquare{family.title()}{variant+1:02d}"
            body = bpy.data.objects[stem + "_Body"]
            top = bpy.data.objects[stem + "_Top"]
            record = record_top(top, variant, shape)
            records.append(record)
            points = [vertex.co for vertex in body.data.vertices]
            snapshot.append({"variant": variant, "shape": family, "exposedSideMask": MASKS[shape],
                             "body": {"object": body.name, "vertices": len(points), "faces": len(body.data.polygons),
                                      "bounds": {"min": [min(p[i] for p in points) for i in range(3)],
                                                 "max": [max(p[i] for p in points) for i in range(3)]},
                                      "properties": dict(body.items())},
                             "top": {**record, "properties": dict(top.items())}})
            if "--audit-only" not in arguments:
                export_body(body, MODELS / f"ground_square_{family}_{variant+1:02d}.glb")
    (DATA / "square_cap_surfaces.json").write_text(json.dumps(
        {"format": 2, "source": "../ground_modules.blend",
         "coordinateSystem": "Blender XY horizontal, Z up; local unit cell", "models": records}, indent=2) + "\n", encoding="utf-8")
    output = ROOT / "out/ground-modules/square-source-audit.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(snapshot, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"exported_square_bodies": 0 if "--audit-only" in arguments else 60,
                      "exported_native_caps": len(records), "source": str(SOURCE)}))


if __name__ == "__main__":
    main()
