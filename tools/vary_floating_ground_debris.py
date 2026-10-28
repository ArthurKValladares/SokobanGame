"""Edit/export only floating ground fragments in the native Blender source.

Run in ground_modules.blend with -- --vary-debris for the one-time layout pass.
The default exports the current artist-edited bodies without changing geometry.
Every FloatingRockNN keeps its faces, UVs and material; all other vertices stay
fixed. A POINT/FLOAT _GROUND_DEBRIS attribute gives the renderer one seed per
rigid fragment, with zero on the terrain. Bobbing itself runs in the renderer.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
from pathlib import Path
import random
import sys
import tempfile

import bpy
from mathutils import Euler, Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from export_bottom_ground_modules import export_body
from update_ground_recess_material import native_color, update_glb

SOURCE = ROOT / "assets/custom/source/ground_modules.blend"
MODELS = ROOT / "assets/custom/models/ground_modules"
ATTRIBUTE = "_GROUND_DEBRIS"
OFFSET = Vector((.008, .008, .020))
MOTION_RADIUS = OFFSET.length
GAP = .04


def fragments(body):
    return {group.name: [v.index for v in body.data.vertices
                        if any(w.group == group.index and w.weight > .5 for w in v.groups)]
            for group in body.vertex_groups if group.name.startswith("FloatingRock")}


def seed_for(body, index):
    # Independent of object/catalog translation and Python's randomized hash.
    variant = int(body["GroundModuleVariant"])
    mask = int(body["ExposedSideMask"])
    return (.5 + random.Random(99173 + variant * 7919 + mask * 173 + index * 31).random() * .5)


def mark_fragments(body):
    attribute = body.data.attributes.get(ATTRIBUTE)
    if attribute is None:
        attribute = body.data.attributes.new(ATTRIBUTE, "FLOAT", "POINT")
    assert attribute.data_type == "FLOAT" and attribute.domain == "POINT"
    attribute.data.foreach_set("value", [0.] * len(body.data.vertices))
    for index, indices in enumerate(fragments(body).values()):
        seed = seed_for(body, index)
        for vertex in indices:
            attribute.data[vertex].value = seed
    body["FloatingDebrisRevision"] = 1


def vary_body(body):
    if body.get("FloatingDebrisRevision") == 1:
        return False
    mesh = body.data
    groups = fragments(body)
    floating = {index for indices in groups.values() for index in indices}
    original = [v.co.copy() for v in mesh.vertices]
    normals = [n.vector.copy() for n in mesh.corner_normals]
    encoded = [tuple(item.value) for item in mesh.attributes['custom_normal'].data] if mesh.has_custom_normals else None
    fixed_faces = [list(p.vertices) for p in mesh.polygons if not any(i in floating for i in p.vertices)]
    fixed = BVHTree.FromPolygons(original, fixed_faces, all_triangles=True)
    # A sphere/surface test is conservative, but also exclude centers inside a
    # closed attached mass. The recessed core is well above this search volume.
    mass_planes = []
    for group in body.vertex_groups:
        if not group.name.startswith("BoulderMass"):
            continue
        members = {v.index for v in mesh.vertices if any(w.group == group.index and w.weight > .5 for w in v.groups)}
        planes = []
        for face in mesh.polygons:
            if all(i in members for i in face.vertices):
                a, b, c = [original[i] for i in face.vertices]
                planes.append((a, (b-a).cross(c-a).normalized()))
        mass_planes.append(planes)
    mask, variant = int(body["ExposedSideMask"]), int(body["GroundModuleVariant"])
    rng = random.Random(280102 + variant * 977 + mask * 7919)
    lower = Vector((-.04 if mask & 8 else 0, -.04 if mask & 1 else 0, -1)) + OFFSET
    upper = Vector((1.04 if mask & 2 else 1, 1.04 if mask & 4 else 1, -.59)) - OFFSET
    placed = []
    for index, (name, indices) in enumerate(groups.items()):
        lo = Vector([min(original[i][a] for i in indices) for a in range(3)])
        hi = Vector([max(original[i][a] for i in indices) for a in range(3)])
        pivot = (lo+hi)/2
        extent = hi-lo
        widest = 0 if extent.x > extent.y else 1
        for attempt in range(4000):
            # Broad vs narrow chips vary in three dimensions, including thickness.
            # Reselect size periodically when the available space is tight.
            if attempt % 40 == 0:
                size = Vector((rng.uniform(.052, .125), rng.uniform(.052, .125), rng.uniform(.060, .170)))
                size[widest] = rng.uniform(.145, .265) if index == 0 else rng.uniform(.070, .160)
                rotation = Euler((rng.uniform(-.36, .36), rng.uniform(-.36, .36), rng.uniform(-1.2, 1.2))).to_matrix()
                offsets = [rotation @ Vector([(original[i][a]-pivot[a])*size[a]/extent[a] for a in range(3)]) for i in indices]
                local_lo = Vector([min(p[a] for p in offsets) for a in range(3)])
                local_hi = Vector([max(p[a] for p in offsets) for a in range(3)])
                radius = max(p.length for p in offsets)
            if max(local_hi.x-local_lo.x, local_hi.y-local_lo.y) < (.125 if index == 0 else .045):
                continue
            if any(lower[a]-local_lo[a] > upper[a]-local_hi[a] for a in range(3)):
                continue
            center = Vector([rng.uniform(lower[a]-local_lo[a], upper[a]-local_hi[a]) for a in range(3)])
            nearest = fixed.find_nearest(center)
            if nearest[0] is None or nearest[3] < radius + GAP + MOTION_RADIUS:
                continue
            if any(all((center-point).dot(normal) <= 1e-6 for point, normal in planes) for planes in mass_planes):
                continue
            if any((center-other).length < radius+other_radius+GAP+2*MOTION_RADIUS for other, other_radius, _ in placed):
                continue
            actual_size = max(local_hi.x-local_lo.x, local_hi.y-local_lo.y)
            if index and placed and placed[0][2]/actual_size < 1.2:
                continue
            for vertex, offset in zip(indices, offsets):
                mesh.vertices[vertex].co = center+offset
            members = set(indices)
            for loop in mesh.loops:
                if loop.vertex_index in members:
                    normals[loop.index] = (rotation @ Vector([normals[loop.index][a]*extent[a]/size[a] for a in range(3)])).normalized()
            placed.append((center, radius, actual_size))
            break
        else:
            raise AssertionError((body.name, name, "No debris position with safe bobbing clearance"))
    assert all(mesh.vertices[i].co == original[i] for i in range(len(original)) if i not in floating)
    mesh.update()
    mesh.normals_split_custom_set(normals)
    # Preserve unchanged packed normals exactly; passing decoded vectors back
    # through Blender's encoder can subtly alter otherwise untouched corners.
    if encoded is not None:
        for loop in mesh.loops:
            if loop.vertex_index not in floating:
                mesh.attributes['custom_normal'].data[loop.index].value = encoded[loop.index]
        mesh.update()
    mark_fragments(body)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vary-debris", action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    assert Path(bpy.data.filepath).resolve() == SOURCE.resolve(), "Open ground_modules.blend first"
    bpy.context.window.scene = bpy.data.scenes["GroundModules"]
    temporary = ROOT / "out/floating-debris/export-temp"
    temporary.mkdir(parents=True, exist_ok=True)
    tempfile.tempdir = str(temporary)
    contract = json.loads((SOURCE.parent / 'ground_modules/contract.json').read_text(encoding='utf-8'))
    recess_color = contract['recessMaterialBaseColorFactor']
    native_color(bpy, recess_color, True)
    changed = exported = 0
    bodies = [o for o in bpy.data.collections["GroundBottomModules"].objects
              if o.name.endswith("_Body") and fragments(o)]
    for body in bodies:
        if args.vary_debris:
            changed += vary_body(body)
        else:
            mark_fragments(body)
    # Direct bpy data writes do not consistently set is_dirty. The marker and
    # revision are part of the saved source even on a normal artist export.
    bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE))
    for body in bodies:
        stem = body.name.removesuffix("_Body").removeprefix("GroundBottom")
        square = stem.startswith("Square")
        stem = stem.removeprefix("Square")
        filename = f"ground_bottom_{'square_' if square else ''}{stem[:-2].lower()}_{stem[-2:]}.glb"
        path = MODELS / filename
        pending = path.with_suffix(".debris-tmp.glb")
        try:
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                export_body(body, pending)
            # Blender stores shader colors as float32. Retain the exact
            # contracted runtime factor when serializing the GLB JSON.
            exported_bytes = pending.read_bytes()
            normalized, _ = update_glb(exported_bytes, recess_color)
            if normalized != exported_bytes:
                pending.write_bytes(normalized)
            pending.replace(path)
            exported += 1
        finally:
            pending.unlink(missing_ok=True)
    print(json.dumps({"changed_layouts": changed, "exported_bodies": exported, "source": str(SOURCE)}))


if __name__ == "__main__":
    main()
