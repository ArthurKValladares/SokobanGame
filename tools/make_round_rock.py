"""Build the round skipping rock with the regular boulder's painted palette.

Run in a separate Blender process:
  blender --background --factory-startup --python tools/make_round_rock.py

The sculpt is a single closed, subtly uneven sphere with a small resting
foot. Its source material is loaded from the existing editable boulder and
rebaked onto this rock's own UV atlas. The exported GLB embeds the texture.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "assets/custom/source/round_rock"
OUTPUT = ROOT / "assets/custom/models/round_rock.glb"
BOULDER_SOURCE = ROOT / "assets/custom/source/chunky_boulder.blend"


def enum(owner, prop, preferred):
    identifiers = [item.identifier for item in owner.bl_rna.properties[prop].enum_items]
    return next(value for value in preferred if value in identifiers)


def srgb(value):
    channels = [int(value[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4
                 for v in channels) + (1,)


def main():
    ART.mkdir(parents=True, exist_ok=True)
    # This script is run with --factory-startup; the live Blender is untouched.
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.name = "RoundRock_Studio"
    asset = bpy.data.collections.new("ASSET_RoundRock")
    studio = bpy.data.collections.new("STUDIO_PreviewOnly")
    scene.collection.children.link(asset)
    scene.collection.children.link(studio)

    def move_to(ob, collection):
        for previous in list(ob.users_collection):
            previous.objects.unlink(ob)
        collection.objects.link(ob)

    # Broad facets survive the game camera. Low-frequency asymmetry changes
    # the whole silhouette instead of adding noisy bumps to a perfect ball.
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=1)
    rock = bpy.context.object
    rock.name = "RoundRock"
    rock.data.name = "RoundRock_ClosedRiverStone"
    move_to(rock, asset)
    for vertex in rock.data.vertices:
        x, y, z = vertex.co
        variation = (1 + .032 * math.sin(x * 3.1 + y * 2.7 + .8)
                     + .021 * math.cos(y * 4.4 - z * 3.3)
                     + .014 * math.sin(z * 5.1 + x * 1.7))
        vertex.co = (x * .474 * variation + .016 * z * z,
                     y * .451 * variation - .012 * z,
                     max(-.423, z * .469 * variation) + .423)
    # Weld the resting foot into a single closed face without interior edges.
    bm = bmesh.new()
    bm.from_mesh(rock.data)
    floor_faces = [f for f in bm.faces if all(abs(v.co.z) < 1e-6 for v in f.verts)]
    if floor_faces:
        bmesh.ops.dissolve_faces(bm, faces=floor_faces, use_verts=True)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(rock.data)
    bm.free()
    bevel = rock.modifiers.new("Softened water-worn facet edges", "BEVEL")
    bevel.width = .012
    bevel.segments = 2
    bevel.limit_method = enum(bevel, "limit_method", ["ANGLE"])
    bevel.angle_limit = .12
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    for face in rock.data.polygons:
        face.use_smooth = True
    normal = rock.modifiers.new("Broad stone face normals", "WEIGHTED_NORMAL")
    normal.keep_sharp = True
    normal.weight = 45
    bpy.ops.object.modifier_apply(modifier=normal.name)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(55), island_margin=.025)
    bpy.ops.object.mode_set(mode="OBJECT")
    rock.data.uv_layers.active.name = "RoundRockPaintUV"

    with bpy.data.libraries.load(str(BOULDER_SOURCE), link=False) as (available, loaded):
        loaded.materials = [name for name in available.materials
                            if name.startswith("Boulder_PaintedTerracottaAndSlate")]
    if len(loaded.materials) != 1:
        raise RuntimeError("Expected the regular boulder's painted material")
    material = loaded.materials[0]
    material.name = "RoundRock_PaintedTerracottaAndSlate"
    rock.data.materials.append(material)
    nodes, links = material.node_tree.nodes, material.node_tree.links
    shader = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    output = next(n for n in nodes if n.type == "OUTPUT_MATERIAL")
    authoring_emission = next(n for n in nodes if n.type == "EMISSION")
    # The original authoring emission retains the exact procedural palette.
    links.new(authoring_emission.outputs[0], output.inputs["Surface"])
    image = bpy.data.images.new("RoundRock_Albedo_1024", width=1024, height=1024, alpha=False)
    image.filepath_raw = str(ART / "round_rock_albedo.png")
    image.file_format = enum(image, "file_format", ["PNG"])
    image.colorspace_settings.name = "sRGB"
    target = nodes.new("ShaderNodeTexImage")
    target.name = "RoundRock baked albedo"
    target.label = "BAKED GAME ALBEDO - UV0"
    target.image = image
    nodes.active = target
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 1
    scene.render.bake.margin = 12
    bake_type = next(item.identifier for item in
                     bpy.ops.object.bake.get_rna_type().properties["type"].enum_items
                     if item.name == "Emission")
    bpy.ops.object.bake(type=bake_type)
    image.save()
    image.pack()
    links.new(target.outputs["Color"], shader.inputs["Base Color"])
    links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    for node in list(nodes):
        if node.type == "TEX_IMAGE" and node != target:
            nodes.remove(node)
    rock["asset_notes"] = "Water-worn faceted sphere, subtle asymmetry, terracotta crown and slate base."
    rock["runtime_asset"] = "custom/models/round_rock.glb"
    rock["generator"] = "tools/make_round_rock.py"
    rock["export_notes"] = "Origin at resting base; Z up; studio excluded; embedded albedo."

    # The studio uses the same framing and lighting as the regular boulder.
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -.008))
    ground = bpy.context.object
    ground.name = "Studio_SandBackdrop"
    move_to(ground, studio)
    ground_material = bpy.data.materials.new("Studio_WarmSand")
    ground_material.use_nodes = True
    bsdf = next(n for n in ground_material.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = srgb("d8c4a3")
    bsdf.inputs["Roughness"].default_value = 1
    ground.data.materials.append(ground_material)
    scene.world = bpy.data.worlds.new("RoundRockStudio_World")
    scene.world.use_nodes = True
    background = next(n for n in scene.world.node_tree.nodes if n.type == "BACKGROUND")
    background.inputs["Color"].default_value = srgb("929fbc")
    background.inputs["Strength"].default_value = .45
    for name, position, power, size, color in (
            ("Studio_PeachKey", (-3, -4, 6), 420, 4, "ffe1c1"),
            ("Studio_CoolFill", (4, -1, 3), 160, 3, "bccfff"),
            ("Studio_SoftRim", (1, 4, 5), 280, 3, "ffddb4")):
        bpy.ops.object.light_add(type="AREA", location=position)
        light = bpy.context.object
        light.name = name
        light.data.energy, light.data.size = power, size
        light.data.shape = enum(light.data, "shape", ["DISK"])
        light.data.color = srgb(color)[:3]
        light.rotation_euler = (Vector((0, 0, .43)) - light.location).to_track_quat("-Z", "Y").to_euler()
        move_to(light, studio)
    bpy.ops.object.camera_add(location=(2.3, -3.5, 2.65))
    camera = bpy.context.object
    camera.name = "Studio_Camera"
    camera.rotation_euler = (Vector((0, 0, .43)) - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = enum(camera.data, "type", ["ORTHO"])
    camera.data.ortho_scale = 1.55
    move_to(camera, studio)
    scene.camera = camera
    scene.render.resolution_x = 1000
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    scene.cycles.samples = 32
    scene.cycles.use_denoising = True
    scene.render.image_settings.file_format = enum(scene.render.image_settings, "file_format", ["PNG"])
    scene.render.filepath = str(ART / "preview.png")
    scene.view_settings.view_transform = "AgX"
    bpy.ops.object.select_all(action="DESELECT")
    rock.select_set(True)
    bpy.context.view_layer.objects.active = rock
    # Verify actual topology before publishing; the underside remains closed.
    bm = bmesh.new()
    bm.from_mesh(rock.data)
    if any(not edge.is_manifold for edge in bm.edges):
        raise RuntimeError("Round rock must be a single closed manifold shell")
    bm.free()
    bpy.ops.export_scene.gltf(filepath=str(OUTPUT), use_selection=True, use_active_scene=True,
                             export_apply=True, export_yup=True, export_normals=True,
                             export_texcoords=True, export_tangents=False, export_animations=False)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                area.spaces.active.region_3d.view_perspective = "CAMERA"
                area.spaces.active.shading.type = "MATERIAL"
                area.spaces.active.overlay.show_overlays = False
    previous_save_version = bpy.context.preferences.filepaths.save_version
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(ART / "round_rock.blend"))
    bpy.context.preferences.filepaths.save_version = previous_save_version
    bpy.ops.render.render(write_still=True)
    print(json.dumps({"source": str(ART / "round_rock.blend"), "export": str(OUTPUT),
                      "dimensions": list(rock.dimensions), "vertices": len(rock.data.vertices),
                      "faces": len(rock.data.polygons), "preview": scene.render.filepath}, indent=2))


if __name__ == "__main__":
    main()
