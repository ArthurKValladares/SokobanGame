"""Capture current native ground evidence in Blender without saving or exporting art.

blender --background assets/custom/source/ground_modules.blend \
    --python tools/snapshot_current_ground.py

The compressed bundle is a tracked validation input. Its source SHA binds the
native geometry, corner streams and cap meshes to the exact .blend file.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import zlib

import bpy

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plain(value):
    if hasattr(value, "to_dict"):
        return {key: plain(item) for key, item in value.to_dict().items()}
    if hasattr(value, "to_list"):
        return [plain(item) for item in value.to_list()]
    if isinstance(value, dict):
        return {key: plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    return value


def snapshot(obj, triangulated=True):
    # Exporters discard gallery translation but retain other object transforms.
    # Capture the mesh only when its local coordinates are the runtime geometry.
    if (obj.type != "MESH" or obj.parent is not None
            or any(abs(obj.matrix_basis[row][column] - (1 if row == column else 0)) > 1e-7
                   for row in range(3) for column in range(3))
            or any(modifier.show_render or modifier.show_viewport for modifier in obj.modifiers)
            or any(not constraint.mute for constraint in obj.constraints)
            or obj.data.shape_keys is not None):
        raise ValueError(f"{obj.name}: apply rotation/scale and evaluated mesh changes before capturing evidence")
    mesh = obj.data
    mesh.calc_loop_triangles()
    faces = list(mesh.loop_triangles if triangulated else mesh.polygons)
    loops = [index for face in faces for index in
             (face.loops if triangulated else face.loop_indices)]
    points = [list(vertex.co) for vertex in mesh.vertices]
    if not points:
        raise ValueError(f"{obj.name}: empty source mesh")
    groups = {group.name: [vertex.index for vertex in mesh.vertices
                          if any(item.group == group.index and item.weight > .5
                                 for item in vertex.groups)]
              for group in obj.vertex_groups}
    sets = {name: set(indices) for name, indices in groups.items()}
    polygons = [list(face.vertices) for face in faces]
    materials = []
    for face in faces:
        polygon = mesh.polygons[face.polygon_index] if triangulated else face
        material = obj.material_slots[polygon.material_index].material
        if material is None:
            raise ValueError(f"{obj.name}: unassigned native face material")
        materials.append(material.name)
    properties = {name: plain(value) for name, value in obj.items()}
    # This marker is deliberately separate from both authored UV charts. Its
    # value identifies a whole rigid fragment for shared runtime deformation.
    debris = mesh.attributes.get("_GROUND_DEBRIS")
    point_attributes = {}
    if debris is not None:
        if debris.domain != "POINT" or debris.data_type != "FLOAT":
            raise ValueError(f"{obj.name}: _GROUND_DEBRIS must be a POINT FLOAT attribute")
        point_attributes["_GROUND_DEBRIS"] = {
            "domain": debris.domain, "dataType": debris.data_type,
            "values": [item.value for item in debris.data]}
    return {
        "object": obj.name, "vertices": len(points), "faces": len(faces), "points": points,
        "bounds": {"min": [min(point[i] for point in points) for i in range(3)],
                   "max": [max(point[i] for point in points) for i in range(3)]},
        "polygons": polygons, "properties": properties,
        "vertexGroups": groups,
        "faceGroups": {name: [i for i, polygon in enumerate(polygons)
                              if all(vertex in indices for vertex in polygon)]
                       for name, indices in sets.items()},
        "reliefFaces": json.loads(obj.get("RegularReliefFaces", "[]")),
        "cornerNormals": [list(mesh.corner_normals[index].vector) for index in loops],
        "uvLayerNames": [layer.name for layer in mesh.uv_layers],
        "uvLayers": {layer.name: [list(layer.data[index].uv) for index in loops]
                     for layer in mesh.uv_layers},
        "faceMaterials": materials,
        "pointAttributes": point_attributes,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path)
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = parser.parse_args(arguments)
    if not bpy.app.background:
        raise ValueError("Capture evidence from the saved file in a fresh background Blender process")
    root = args.root.resolve()
    contract_path = root / "assets/custom/source/ground_modules/contract.json"
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    source = (contract_path.parent / contract["source"]).resolve()
    if Path(bpy.data.filepath).resolve() != source:
        raise ValueError(f"Open the contract's native source first: {source}")
    if bpy.data.is_dirty:
        raise ValueError("Save the native source before capturing evidence, then reopen it in background Blender")
    source_sha = sha256(source)
    output = args.output or contract_path.parent / "current_source_audit.json.gz"
    models = []
    for bottom in (False, True):
        for square in (False, True):
            for family in contract["families"]:
                for variant in range(contract["variantCount"]):
                    name = (f"Ground{'Bottom' if bottom else ''}{'Square' if square else ''}"
                            f"{family['name'].title()}{variant + 1:02d}_Body")
                    filename = (f"ground_{'bottom_' if bottom else ''}{'square_' if square else ''}"
                                f"{family['name']}_{variant + 1:02d}.glb")
                    row = {"bottom": bottom, "squareTop": square, "shape": family["name"],
                           "variant": variant, "exposedSideMask": family["mask"],
                           "body": snapshot(bpy.data.objects[name]),
                           "exportSha256": sha256(root / "assets/custom/models/ground_modules" / filename)}
                    if not bottom:
                        row["reference"] = snapshot(bpy.data.objects[name.replace("Ground", "GroundReliefSource", 1)])
                    models.append(row)
    caps, cap_hashes = [], {}
    for key in ("capSurfaces", "squareCapSurfaces"):
        path = contract_path.parent / contract[key]
        cap_hashes[path.name] = sha256(path)
        table = json.loads(path.read_text(encoding="utf-8"))
        caps.extend(snapshot(bpy.data.objects[row["object"]], triangulated=False)
                    for row in table["models"])
    if bpy.data.is_dirty or sha256(source) != source_sha:
        raise ValueError("Native source changed while capturing validation evidence")
    bundle = {"format": 1, "source": {"path": contract["source"], "sha256": source_sha},
              "models": models, "caps": caps, "capTableSha256": cap_hashes}
    payload = (json.dumps(bundle, separators=(",", ":"), sort_keys=True, allow_nan=False) + "\n").encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_file():
        try:
            unchanged = gzip.decompress(output.read_bytes()) == payload
        except (OSError, EOFError, zlib.error):
            unchanged = False
        if unchanged:
            print(f"Current ground source evidence is unchanged: {output}")
            return
    compressed = io.BytesIO()
    with gzip.GzipFile(fileobj=compressed, mode="wb", filename="", mtime=0) as stream:
        stream.write(payload)
    temporary = output.with_name(output.name + ".tmp")
    try:
        temporary.write_bytes(compressed.getvalue())
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    print(json.dumps({"models": len(models), "caps": len(caps), "source_sha256": source_sha,
                      "compressed_bytes": len(compressed.getvalue()), "output": str(output)}))


if __name__ == "__main__":
    main()
