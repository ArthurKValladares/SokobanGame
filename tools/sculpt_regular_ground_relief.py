"""Very shallow native rock-face relief, preserving accepted ground outlines.

Normal export preserves artist edits and exports only regular bodies. Explicit
-- --remodel-regular-relief rebuilds this detail from frozen accepted revision-2
meshes; bottom bodies, caps, backing and original rock edges never move.
"""
from __future__ import annotations

import json
from pathlib import Path
import random
import sys
import time

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sculpt_ground_shoulders import FAMILIES, MASKS, SOURCE, MODELS, body_name, snapshot
from export_bottom_ground_modules import export_body, synchronize_preview_groups

MAXIMUM_DEPTH = .0072


def source_name(name):
    return name.replace('Ground', 'GroundReliefSource', 1)


def freeze_sources():
    name = 'GroundRegularReliefConstructionSources'
    collection = bpy.data.collections.get(name)
    if collection is None:
        collection = bpy.data.collections.new(name)
        bpy.data.scenes['GroundModules'].collection.children.link(collection)
        collection.hide_render = True
        collection.hide_viewport = True
    for square in (False, True):
        for family in FAMILIES:
            for variant in range(10):
                body = bpy.data.objects[body_name(False, square, family, variant)]
                name = source_name(body.name)
                if name in bpy.data.objects:
                    continue
                assert body.get('RegularDesignRevision') == 2 and not body.get('RegularReliefRevision'), body.name
                seed = body.copy()
                seed.data = body.data.copy()
                seed.name = name
                seed.data.name = name + 'Mesh'
                collection.objects.link(seed)
                seed.hide_render = True
                seed['ConstructionOnly'] = True


def remodel(body, seed, variant, mask):
    old = seed.data
    old.calc_loop_triangles()
    triangles = list(old.loop_triangles)
    group_names = {g.index: g.name for g in seed.vertex_groups}
    weights = [{group_names[w.group]: w.weight for w in v.groups} for v in old.vertices]
    # One broad face per rock and exposed direction gets a small indentation.
    # Existing edges and every silhouette vertex stay exactly where they were.
    directions = (Vector((0, -1, 0)), Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((-1, 0, 0)))
    candidates = {}
    roles_by_face = {}
    potential = []
    for p in triangles:
        shared = set.intersection(*(set(name for name, weight in weights[i].items() if weight > .5) for i in p.vertices))
        roles = sorted(name for name in shared if name.startswith(('BoulderMass', 'ShoulderRock')))
        roles_by_face[p.index] = roles[0] if len(roles) == 1 else None
        if len(roles) != 1 or p.area < .030:
            continue
        side = max(range(4), key=lambda i: p.normal.dot(directions[i]))
        if not mask & (1 << side) or p.normal.dot(directions[side]) < .985:
            continue
        potential.append((p, roles[0], side))
    # The accepted closed masses overlap. Do not indent a visible face through
    # a neighbouring mass: that would reveal a triangular patch of its UVs.
    trees = {}
    for p, role, side in sorted(potential, key=lambda value: value[0].area, reverse=True):
        key = (role, side)
        if key in candidates:
            continue
        if role not in trees:
            trees[role] = BVHTree.FromPolygons([v.co for v in old.vertices],
                [tuple(q.vertices) for q in triangles if roles_by_face[q.index] != role], all_triangles=True)
        tree = trees[role]
        center = sum((old.vertices[i].co for i in p.vertices), Vector()) / 3
        if tree.ray_cast(center + p.normal * .00001, p.normal, 2)[0] is not None:
            continue
        collision = False
        for weight in (1, .75, .5, .25):
            samples = [center] if weight == 1 else [center * weight + old.vertices[i].co * (1 - weight) for i in p.vertices]
            for sample in samples:
                if tree.ray_cast(sample + p.normal * .00001, p.normal, 2)[0] is not None:
                    continue  # This portion is already hidden inside a mass.
                hit = tree.ray_cast(sample - p.normal * .000001, -p.normal, MAXIMUM_DEPTH * weight + .00002)
                if hit[0] is not None:
                    collision = True
                    break
            if collision:
                break
        if not collision:
            candidates[key] = p
    selected = {p.index: role for (role, side), p in candidates.items()}
    uv_names = [layer.name for layer in old.uv_layers]
    vertices = [tuple(v.co) for v in old.vertices]
    groups = {name: {i: values[name] for i, values in enumerate(weights) if name in values} for name in group_names.values()}
    faces, materials, charts, normals, encoded = [], [], [], [], []
    records = []
    for p in triangles:
        corners = list(p.vertices)
        uvs = {name: [tuple(old.uv_layers[name].data[i].uv) for i in p.loops] for name in uv_names}
        if p.index not in selected:
            faces.append(corners)
            materials.append(p.material_index)
            charts.append(uvs)
            normals.append([tuple(old.corner_normals[i].vector) for i in p.loops])
            encoded.append([tuple(old.attributes['custom_normal'].data[i].value) for i in p.loops] if old.has_custom_normals else None)
            continue
        rng = random.Random(711901 + variant * 991 + mask * 37 + p.index * 53)
        barycentric = [rng.uniform(.28, .39) for _ in range(3)]
        total = sum(barycentric)
        barycentric = [w / total for w in barycentric]
        center = sum((old.vertices[i].co * w for i, w in zip(corners, barycentric)), Vector())
        # Bound slopes as well as depth, so a narrow face never becomes a dent.
        altitude = min((center - old.vertices[a].co).cross(old.vertices[b].co - old.vertices[a].co).length
                       / (old.vertices[b].co - old.vertices[a].co).length
                       for a, b in zip(corners, corners[1:] + corners[:1]))
        depth = min(rng.uniform(.0036, .006), altitude * .06, MAXIMUM_DEPTH)
        center -= p.normal * depth
        index = len(vertices)
        vertices.append(tuple(center))
        shared = set.intersection(*(set(weights[i]) for i in corners))
        for name in shared:
            groups[name][index] = min(weights[i][name] for i in corners)
        center_uvs = {name: tuple(sum(w * values[j][axis] for j, w in enumerate(barycentric)) for axis in (0, 1))
                      for name, values in uvs.items()}
        for j in range(3):
            faces.append([corners[j], corners[(j + 1) % 3], index])
            materials.append(p.material_index)
            charts.append({name: [values[j], values[(j + 1) % 3], center_uvs[name]] for name, values in uvs.items()})
            normals.append(None)
            encoded.append(None)
        records.append({'parent': p.index, 'vertices': corners, 'center': index,
                        'barycentric': barycentric, 'depth': depth, 'role': selected[p.index]})
    mesh = bpy.data.meshes.new(body.name + 'ReliefMesh')
    mesh.from_pydata(vertices, [], faces)
    for material in old.materials:
        mesh.materials.append(material)
    for name in uv_names:
        mesh.uv_layers.new(name=name)
    for p, material, values in zip(mesh.polygons, materials, charts):
        p.material_index = material
        p.use_smooth = False
        for name, uvs in values.items():
            for i, uv in zip(p.loop_indices, uvs):
                mesh.uv_layers[name].data[i].uv = uv
    previous = body.data
    for linked in bpy.data.objects:
        if linked.type == 'MESH' and linked.data == previous and not linked.get('ConstructionOnly'):
            linked.data = mesh
    body.vertex_groups.clear()
    for name, values in groups.items():
        group = body.vertex_groups.new(name=name)
        for index, weight in values.items():
            group.add([index], weight, 'REPLACE')
    mesh.update()
    loop_normals = [(0, 0, 0)] * len(mesh.loops)
    for p, original in zip(mesh.polygons, normals):
        for corner, index in enumerate(p.loop_indices):
            loop_normals[index] = original[corner] if original else tuple(p.normal)
    mesh.normals_split_custom_set(loop_normals)
    for p, original in zip(mesh.polygons, encoded):
        if original is not None:
            for index, value in zip(p.loop_indices, original):
                mesh.attributes['custom_normal'].data[index].value = value
    mesh.update()
    for name in uv_names:
        mesh.calc_tangents(uvmap=name)
        assert all(loop.tangent.length > .5 for loop in mesh.loops), (body.name, name)
    body['RegularDesignRevision'] = 3
    body['RegularReliefRevision'] = 1
    body['RegularReliefSource'] = seed.name
    body['RegularReliefFaces'] = json.dumps(records, separators=(',', ':'))
    body['RegularReliefMaximumDepth'] = MAXIMUM_DEPTH
    synchronize_preview_groups(body)
    return records


def audit():
    rows = []
    for bottom in (False, True):
        for square in (False, True):
            for family, mask in zip(FAMILIES, MASKS):
                for variant in range(10):
                    body = bpy.data.objects[body_name(bottom, square, family, variant)]
                    data = snapshot(body)
                    if not bottom:
                        data['reliefFaces'] = json.loads(body.get('RegularReliefFaces', '[]'))
                    rows.append({'bottom': bottom, 'squareTop': square, 'shape': family, 'variant': variant,
                                 'exposedSideMask': mask, 'body': data})
    output = ROOT / 'out/ground-modules/regular-relief-source-audit.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
    return rows


def main():
    assert Path(bpy.data.filepath).resolve() == SOURCE.resolve()
    bpy.context.window.scene = bpy.data.scenes['GroundModules']
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if '--remodel-regular-relief' in arguments:
        freeze_sources()
        for square in (False, True):
            for family, mask in zip(FAMILIES, MASKS):
                for variant in range(10):
                    body = bpy.data.objects[body_name(False, square, family, variant)]
                    remodel(body, bpy.data.objects[source_name(body.name)], variant, mask)
        previous = bpy.context.preferences.filepaths.save_version
        try:
            bpy.context.preferences.filepaths.save_version = 0
            bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE))
        finally:
            bpy.context.preferences.filepaths.save_version = previous
    rows = audit()
    if '--audit-only' not in arguments:
        for row in rows:
            if row['bottom']:
                continue
            body = bpy.data.objects[row['body']['object']]
            path = MODELS / f"ground_{'square_' if row['squareTop'] else ''}{row['shape']}_{row['variant'] + 1:02d}.glb"
            temporary = path.with_suffix('.export-tmp.glb')
            try:
                export_body(body, temporary)
                for attempt in range(8):
                    try:
                        temporary.replace(path)
                        break
                    except PermissionError:
                        if attempt == 7:
                            raise
                        time.sleep(min(.1 * 2 ** attempt, 1))
            finally:
                if temporary.exists():
                    temporary.unlink()
    print(json.dumps({'bodies': len(rows), 'detailed_regular_triangles': sum(len(r['body'].get('reliefFaces', [])) for r in rows),
                      'exported_regular': 0 if '--audit-only' in arguments else 120, 'exported_bottom': 0}))


if __name__ == '__main__':
    main()
