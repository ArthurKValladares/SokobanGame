#!/usr/bin/env python3
"""Build the floating lily pad; standard-library only, deterministic GLB output.

Engine coordinates are x east, y south and z up. The leaf occupies a tile's
unit XY box, with its notched mouth facing south. Preserve source scale:
bottom z=0, standing surface z=0.070, gently curled rim z<0.090. Animation
belongs to the renderer, so this mesh contains no stem, flower or armature.

Run normally to regenerate the runtime asset. To also recreate the editable
Blender source and its studio preview, run Blender in background mode with:
  blender --background --factory-startup --python tools/make_lily_pad_model.py -- --blender
The optional Blender path creates a new isolated scene in its own process.
"""
from __future__ import annotations

import json
import math
import struct
import sys
from pathlib import Path

# Blender's --python runner does not add the script directory to sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_rotator_models import Primitive, _to_gltf

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "assets/custom/models/lily_pad.glb"
SOURCE = ROOT / "assets/custom/source/lily_pad"
CENTER = (0.5, 0.5)
STANDING_HEIGHT = 0.070
SEGMENTS = 40
START = math.radians(112)
END = math.radians(428)
RINGS = (0.0, 0.30, 0.66, 0.90, 0.965, 1.0)
HEIGHTS = (0.070, 0.068, 0.064, 0.069, 0.083, 0.064)
COLORS = {
    "Leaf jade": (0.195, 0.365, 0.070, 1.0),
    "Leaf sunlit": (0.225, 0.405, 0.083, 1.0),
    "Leaf shade": (0.175, 0.330, 0.060, 1.0),
    "Soft lime rim": (0.285, 0.455, 0.105, 1.0),
    "Green edge": (0.155, 0.295, 0.060, 1.0),
    "Dark underside": (0.090, 0.190, 0.047, 1.0),
    "Pale leaf veins": (0.390, 0.535, 0.165, 1.0),
}


def radius_variation(angle):
    # Gentle organic lobes, with a restrained asymmetry rather than a disc.
    return 1 + 0.019 * math.sin(3 * angle + 0.6) + 0.010 * math.sin(5 * angle)


def point(radius, angle, z):
    variation = radius_variation(angle)
    return (CENTER[0] + 0.452 * radius * variation * math.cos(angle),
            CENTER[1] + 0.438 * radius * variation * math.sin(angle), z)


def top_height(radius, angle):
    for i in range(1, len(RINGS)):
        if radius <= RINGS[i] + 1e-8:
            mix = (radius - RINGS[i - 1]) / (RINGS[i] - RINGS[i - 1])
            height = HEIGHTS[i - 1] * (1 - mix) + HEIGHTS[i] * mix
            return height + 0.0012 * radius * math.sin(3 * angle + 0.4)
    raise ValueError("Point outside the leaf")


def geometry():
    meshes = {name: Primitive() for name in COLORS}
    angles = [START + (END - START) * i / SEGMENTS for i in range(SEGMENTS + 1)]
    top = [[point(r, a, top_height(r, a)) for a in angles] for r in RINGS]
    lower = [[point(r, a, 0.035 * r ** 1.7) for a in angles] for r in RINGS]
    surface = []

    def face(mesh, a, b, c, up):
        mesh.triangle(a, b, c, (0, 0, up))
        if up > 0:
            surface.append((a, b, c))

    for ring in range(1, len(RINGS)):
        for i in range(SEGMENTS):
            angle = (angles[i] + angles[i + 1]) / 2
            shade = math.sin(angle * 2 + 0.4) + 0.3 * math.sin(angle * 5)
            name = "Leaf sunlit" if shade > 0.65 else "Leaf shade" if shade < -0.75 else "Leaf jade"
            if ring >= len(RINGS) - 2:
                name = "Soft lime rim"
            a, b = top[ring - 1][i], top[ring - 1][i + 1]
            c, d = top[ring][i + 1], top[ring][i]
            face(meshes[name], a, d, c, 1)
            if ring > 1:
                face(meshes[name], a, c, b, 1)
            a, b = lower[ring - 1][i], lower[ring - 1][i + 1]
            c, d = lower[ring][i + 1], lower[ring][i]
            face(meshes["Dark underside"], a, d, c, -1)
            if ring > 1:
                face(meshes["Dark underside"], a, c, b, -1)

    # Rounded edge and both sides of the deep V notch are physically closed.
    for i in range(SEGMENTS):
        a, b = lower[-1][i], lower[-1][i + 1]
        c, d = top[-1][i + 1], top[-1][i]
        angle = (angles[i] + angles[i + 1]) / 2
        meshes["Green edge"].quad(a, b, c, d, (math.cos(angle), math.sin(angle), 0))
    for i, sign in ((0, -1), (-1, 1)):
        angle = angles[i]
        outward = (sign * -math.sin(angle), sign * math.cos(angle), 0)
        for ring in range(1, len(RINGS)):
            meshes["Green edge"].quad(lower[ring - 1][i], lower[ring][i],
                                      top[ring][i], top[ring - 1][i], outward)

    def surface_height(x, y):
        # Follow the actual faceted triangles, avoiding buried or floating veins.
        for a, b, c in surface:
            determinant = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(determinant) < 1e-12:
                continue
            u = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / determinant
            v = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / determinant
            w = 1 - u - v
            if min(u, v, w) >= -1e-7:
                return u * a[2] + v * b[2] + w * c[2]
        raise ValueError(f"Vein outside leaf: {(x, y)}")

    def vein(path, width):
        # A small triangular ridge catches light; each tapered tip ends cleanly.
        rows = []
        for i, p in enumerate(path):
            previous, following = path[max(0, i - 1)], path[min(len(path) - 1, i + 1)]
            dx, dy = following[0] - previous[0], following[1] - previous[1]
            length = math.hypot(dx, dy)
            taper = 0.12 + 0.88 * (1 - i / (len(path) - 1)) ** 0.7
            half = width * taper * 0.5
            edge_points = ((p[0] - dy / length * half, p[1] + dx / length * half),
                           p, (p[0] + dy / length * half, p[1] - dx / length * half))
            rows.append([(x, y, surface_height(x, y) + (0.0028 if j == 1 else 0.0007))
                         for j, (x, y) in enumerate(edge_points)])
        for a, b in zip(rows, rows[1:]):
            meshes["Pale leaf veins"].quad(a[0], b[0], b[1], a[1], (0, 0, 1))
            meshes["Pale leaf veins"].quad(a[1], b[1], b[2], a[2], (0, 0, 1))

    # Seven veins radiate away from the cleft, with delicate branching ribs.
    for index, degrees in enumerate((135, 175, 215, 265, 310, 350, 395)):
        angle = math.radians(degrees)
        curve = 0.035 * math.sin(index * 1.8)
        path = [point(0.055 + 0.79 * i / 12,
                      angle + curve * math.sin(i / 12 * math.pi), 0)[:2] for i in range(13)]
        vein(path, 0.008 if index == 3 else 0.006)
        for step, side in ((5, -1), (8, 1)):
            radius = 0.055 + 0.79 * step / 12
            start = point(radius, angle + curve * math.sin(step / 12 * math.pi), 0)
            end = point(min(0.89, radius + 0.18), angle + side * 0.22, 0)
            branch = [((1 - t) * start[0] + t * end[0],
                       (1 - t) * start[1] + t * end[1]) for t in (0, .25, .5, .75, 1)]
            vein(branch, 0.0035)
    return meshes


def validate_geometry(meshes):
    positions = [p for mesh in meshes.values() for p in mesh.positions]
    assert positions and all(math.isfinite(v) for p in positions for v in p)
    assert all(0 <= p[0] <= 1 and 0 <= p[1] <= 1 and 0 <= p[2] < .09 for p in positions)
    assert min(p[2] for p in positions) == 0
    assert .88 < max(p[0] for p in positions) - min(p[0] for p in positions) < .94
    for name, mesh in meshes.items():
        assert mesh.positions and len(mesh.positions) == len(mesh.normals), name
        assert len(mesh.positions) % 3 == 0
        assert all(math.isclose(sum(v * v for v in n), 1, abs_tol=1e-7) for n in mesh.normals)
    # The solid leaf, excluding its overlaid veins, is a closed two-manifold.
    edges = {}
    for name, mesh in meshes.items():
        if name == "Pale leaf veins":
            continue
        for i in range(0, len(mesh.positions), 3):
            triangle = mesh.positions[i:i + 3]
            for a, b in zip(triangle, triangle[1:] + triangle[:1]):
                edge = tuple(sorted((a, b)))
                edges[edge] = edges.get(edge, 0) + 1
    assert all(count == 2 for count in edges.values()), "Leaf shell has an open or nonmanifold edge"


def write_glb(meshes, path=OUTPUT):
    binary, views, accessors, primitives = bytearray(), [], [], []

    def accessor(values, kind, component=5126, target=34962):
        binary.extend(bytes(-len(binary) % 4))
        offset = len(binary)
        if kind == "SCALAR":
            data = struct.pack(f"<{len(values)}I", *values)
        else:
            size = {"VEC2": 2, "VEC3": 3}[kind]
            data = b"".join(struct.pack(f"<{size}f", *v) for v in values)
        binary.extend(data)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        item = {"bufferView": len(views) - 1, "componentType": component,
                "count": len(values), "type": kind}
        if kind == "VEC3":
            item.update(min=[min(v[i] for v in values) for i in range(3)],
                        max=[max(v[i] for v in values) for i in range(3)])
        accessors.append(item)
        return len(accessors) - 1

    for material, mesh in enumerate(meshes.values()):
        position = accessor([_to_gltf(p) for p in mesh.positions], "VEC3")
        normal = accessor([_to_gltf(n) for n in mesh.normals], "VEC3")
        uv = accessor([p[:2] for p in mesh.positions], "VEC2")
        indices = accessor(list(range(len(mesh.positions))), "SCALAR", 5125, 34963)
        primitives.append({"attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv},
                           "indices": indices, "material": material, "mode": 4})
    document = {
        "asset": {"version": "2.0", "generator": "tools/make_lily_pad_model.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "LilyPad"}],
        "meshes": [{"name": "LilyPad", "primitives": primitives}],
        "materials": [{"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": color, "metallicFactor": 0.0,
            "roughnessFactor": 0.58 if name == "Pale leaf veins" else 0.72}}
            for name, color in COLORS.items()],
        "buffers": [{"byteLength": len(binary)}], "bufferViews": views, "accessors": accessors,
    }
    data = json.dumps(document, separators=(",", ":"), sort_keys=True).encode()
    data += b" " * (-len(data) % 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 28 + len(data) + len(binary)) +
                     struct.pack("<II", len(data), 0x4E4F534A) + data +
                     struct.pack("<II", len(binary), 0x004E4942) + binary)


def create_blender_source(meshes):
    """Optional artist-editable native source and a faithful rendered preview."""
    import bpy
    from mathutils import Vector

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    leaf_collection = bpy.data.collections.new("LilyPad - exported mesh")
    bpy.context.scene.collection.children.link(leaf_collection)
    vertices, faces, material_ids = [], [], []
    for material_id, mesh in enumerate(meshes.values()):
        offset = len(vertices)
        vertices.extend(mesh.positions)
        for i in range(0, len(mesh.positions), 3):
            faces.append((offset + i, offset + i + 1, offset + i + 2))
            material_ids.append(material_id)
    data = bpy.data.meshes.new("LilyPad faceted leaf and raised veins")
    data.from_pydata(vertices, [], faces)
    data.update()
    leaf = bpy.data.objects.new("LilyPad", data)
    leaf_collection.objects.link(leaf)
    for name, color in COLORS.items():
        material = bpy.data.materials.new(name)
        material.diffuse_color = color
        material.use_nodes = True
        bsdf = material.node_tree.nodes.get("Principled BSDF")
        bsdf.inputs["Base Color"].default_value = color
        bsdf.inputs["Roughness"].default_value = 0.72
        data.materials.append(material)
    for polygon, index in zip(data.polygons, material_ids):
        polygon.material_index = index
    # Weld coincident shell vertices, keeping the faceted face normals.
    bpy.context.view_layer.objects.active = leaf
    leaf.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=1e-7)
    bpy.ops.object.mode_set(mode="OBJECT")
    leaf["standing_surface_z"] = STANDING_HEIGHT
    leaf["runtime_asset"] = "custom/models/lily_pad.glb"
    leaf["coordinates"] = "Engine XYZ: east, south, up. Notch faces south."
    leaf["generator"] = "tools/make_lily_pad_model.py"

    studio = bpy.data.collections.new("Preview studio - never export")
    bpy.context.scene.collection.children.link(studio)

    def move_to_studio(obj):
        for collection in list(obj.users_collection):
            collection.objects.unlink(obj)
        studio.objects.link(obj)

    bpy.ops.mesh.primitive_plane_add(size=200, location=(.5, .5, -.008))
    water = bpy.context.object
    water.name = "Studio water backdrop"
    move_to_studio(water)
    material = bpy.data.materials.new("Studio muted turquoise water")
    material.diffuse_color = (.085, .24, .29, 1)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    bsdf = nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (.085, .24, .29, 1)
    bsdf.inputs["Roughness"].default_value = .35
    noise = nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 5
    noise.inputs["Roughness"].default_value = .3
    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = .12
    bump.inputs["Distance"].default_value = .035
    material.node_tree.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    material.node_tree.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    water.data.materials.append(material)
    for name, location, energy, size in (
            ("Large warm key", (-1.4, 0, 3), 170, 2.4),
            ("Soft sky fill", (1.5, 1.4, 2), 65, 2.0)):
        bpy.ops.object.light_add(type="AREA", location=location)
        light = bpy.context.object
        light.name, light.data.energy, light.data.shape, light.data.size = name, energy, "DISK", size
        light.rotation_euler = (Vector((.5, .5, .04)) - light.location).to_track_quat("-Z", "Y").to_euler()
        move_to_studio(light)
    bpy.ops.object.camera_add(location=(1.45, 1.9, 2.1))
    camera = bpy.context.object
    camera.name = "LilyPad preview"
    camera.rotation_euler = (Vector((.5, .5, .025)) - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type, camera.data.ortho_scale = "ORTHO", 1.35
    move_to_studio(camera)
    scene = bpy.context.scene
    scene.camera = camera
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 40
    scene.render.resolution_x, scene.render.resolution_y = 1000, 850
    scene.render.resolution_percentage = 100
    scene.world.color = (.2, .2, .2)
    scene.view_settings.view_transform = "AgX"
    scene.render.image_settings.file_format = "PNG"
    SOURCE.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(SOURCE / "preview.png")
    bpy.ops.object.select_all(action="DESELECT")
    leaf.select_set(True)
    bpy.context.view_layer.objects.active = leaf
    bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE / "lily_pad.blend"))
    bpy.ops.render.render(write_still=True)


def main():
    meshes = geometry()
    validate_geometry(meshes)
    write_glb(meshes)
    print(f"{OUTPUT}: {sum(len(m.positions) // 3 for m in meshes.values())} triangles")
    if "--blender" in sys.argv:
        create_blender_source(meshes)


if __name__ == "__main__":
    main()
