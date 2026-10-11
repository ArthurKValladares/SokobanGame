"""Author/export editable floating-island bodies in ground_modules.blend.

Normal exports preserve native mesh edits. Use -- --add-bottom-variants once
to add missing bodies and a preview scene; -- --audit-only only records meshes.
-- --remodel-bottom-variants explicitly rebuilds the bottom bodies from their
regular sources using a recessed core and interlocking faceted boulders.
-- --tighten-bottom-borders narrows existing boulder forms without remeshing.
The original regular/square bodies and cap surfaces are never rewritten.
"""
from __future__ import annotations

import contextlib
import io
import json
import math
from pathlib import Path
import random
import sys
import tempfile

import bmesh
import bpy
from mathutils import Vector
from mathutils.geometry import barycentric_transform, closest_point_on_tri, delaunay_2d_cdt

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sculpt_ground_bottoms import FINAL_OVERHANG, add_rock_masses, tighten_rock_point

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/custom/source/ground_modules.blend"
MODELS = ROOT / "assets/custom/models/ground_modules"
FAMILIES = ("island", "end", "strip", "corner", "edge", "interior")
MASKS = (15, 11, 5, 3, 1, 0)
REMODEL_HEIGHT = .78
CONNECTION_HEIGHT = -.34


def side_point(side, t, z):
    return ((t, 0, z), (1, t, z), (1-t, 1, z), (0, 1-t, z))[side]


def synchronize_preview_groups(body):
    names = [group.name for group in body.vertex_groups]
    for linked in bpy.data.objects:
        if linked is body or linked.type != 'MESH' or linked.data != body.data:
            continue
        # Group definitions belong to the object, but deform weights belong to
        # the shared mesh. Rename indices rather than clearing shared weights.
        before = [[(weight.group, weight.weight) for weight in vertex.groups] for vertex in body.data.vertices]
        for group in linked.vertex_groups:
            group.name = f"PreviewGroup{group.index:02d}"
        while len(linked.vertex_groups) < len(names):
            linked.vertex_groups.new(name="PreviewGroup")
        while len(linked.vertex_groups) > len(names):
            linked.vertex_groups.remove(linked.vertex_groups[-1])
        for group, name in zip(linked.vertex_groups, names):
            group.name = name
        after = [[(weight.group, weight.weight) for weight in vertex.groups] for vertex in body.data.vertices]
        assert before == after, "Preview group synchronization changed authored mesh weights"


def add_body(original, collection, stem, variant, mask):
    body = bpy.data.objects.get(stem + "_Body")
    mesh = original.data.copy()
    if body is None:
        body = original.copy()
        body.name = stem + "_Body"
        body.data = mesh
        collection.objects.link(body)
        # Construction seeds are hidden references, not runtime/preview bodies.
        body.hide_render = False
        if "ConstructionOnly" in body:
            del body["ConstructionOnly"]
    else:
        previous = body.data
        # Preview instances share the authored mesh and must follow revisions.
        for linked in bpy.data.objects:
            if linked.type == 'MESH' and linked.data == previous:
                linked.data = mesh
        if previous.users == 0:
            bpy.data.meshes.remove(previous)
    body.data.name = stem + "_BodyMesh"
    body.vertex_groups.clear()
    core_group = body.vertex_groups.new(name="IslandCore").index
    underside_group = body.vertex_groups.new(name="IslandUnderside").index
    wall_group = body.vertex_groups.new(name="IslandRockWalls").index
    bm = bmesh.new()
    bm.from_mesh(body.data)
    source_face = bm.faces.layers.int.new("OriginalRockFace")
    uv_names = [layer.name for layer in bm.loops.layers.uv.values()]
    source_charts = []
    for index, face in enumerate(bm.faces):
        face[source_face] = index
        source_charts.append(([loop.vert.co.copy() for loop in face.loops],
            {name: [Vector((*loop[bm.loops.layers.uv[name]].uv, 0)) for loop in face.loops] for name in uv_names}))
    deform = bm.verts.layers.deform.verify()
    for v in bm.verts:
        v[deform][core_group] = 1
    flat_bottom = [f for f in bm.faces if all(abs(v.co.z) < 1e-6 for v in f.verts)]
    assert len(flat_bottom) == 2, (original.name, len(flat_bottom))
    material = flat_bottom[0].material_index
    bmesh.ops.delete(bm, geom=flat_bottom, context="FACES_ONLY")
    vertices = {}

    rng = random.Random(19331 + variant*977)
    profiles = []
    for side in range(4):
        # The old fractured walls become an inset backing behind the boulders.
        # Hidden-side protection keeps their shared connection frames exact.
        stations = (0, rng.uniform(.22, .43), rng.uniform(.60, .84), 1)
        shelf = rng.uniform(.04,.10)
        values = (0, shelf, shelf+rng.uniform(-.02,.02), 0)
        insets = (rng.uniform(.20,.24), rng.uniform(.23,.28), rng.uniform(.23,.28), rng.uniform(.20,.24))
        profiles.append((stations, values, insets))

    def interpolate(t, stations, values):
        t = max(0, min(1, t))
        for i in range(len(stations)-1):
            if t <= stations[i+1]:
                blend = (t-stations[i])/(stations[i+1]-stations[i])
                return values[i]*(1-blend) + values[i+1]*blend
        return values[-1]

    def warp(co):
        x, y, z = co
        if z >= REMODEL_HEIGHT:
            return Vector(co)
        x0, y0 = max(0,min(1,x)), max(0,min(1,y))
        distances = (y0, 1-x0, 1-y0, x0)
        tangents = (x0, y0, 1-x0, 1-y0)
        protection = 1.0
        for side, distance in enumerate(distances):
            if not mask & (1 << side):
                t = max(0,min(1,distance/.28))
                protection *= t*t*(3-2*t)
        weights = [1/(d+.14)**3 for d in distances]
        changes = [interpolate(tangents[s], profiles[s][0], profiles[s][1]) if mask & (1 << s) else 0 for s in range(4)]
        floor = CONNECTION_HEIGHT + protection*(sum(w*v for w,v in zip(weights,changes))/sum(weights)
                    + .06*(math.sin(math.pi*x0)*math.sin(math.pi*y0))**.8)
        floor = max(-.34,min(-.17,floor))
        progress = max(0,min(1,1-z/REMODEL_HEIGHT))
        new_z = z + floor*progress
        insets = [interpolate(tangents[s], profiles[s][0], profiles[s][2]) if mask & (1 << s) else 0 for s in range(4)]
        taper = protection*progress**.72
        new_x = x + taper*(insets[3]*(1-x0)**3 - insets[1]*x0**3)
        new_y = y + taper*(insets[0]*(1-y0)**3 - insets[2]*y0**3)
        if new_z < 0:
            new_x, new_y = max(0,min(1,new_x)), max(0,min(1,new_y))
        return Vector((new_x, new_y, new_z))

    # Split the old base edges at every existing boundary vertex and the large
    # silhouette anchors. Both the stretched walls and closing surface then
    # follow exactly the same piecewise outline without T-junction gaps.
    base = [v.co.copy() for v in bm.verts if abs(v.co.z) < 1e-7]
    stations = []
    for side in range(4):
        axis, plane = (1 if side % 2 == 0 else 0), (1 if side in (1,2) else 0)
        values = {0.0,1.0,*profiles[side][0]}
        for p in base:
            if abs(p[axis]-plane) < 1e-7:
                values.add(max(0,min(1,(p.x,p.y,1-p.x,1-p.y)[side])))
        stations.append(sorted(values))
    for edge in list(bm.edges):
        a, b = (v.co.copy() for v in edge.verts)
        if abs(a.z) > 1e-7 or abs(b.z) > 1e-7:
            continue
        for side in range(4):
            axis, plane = (1 if side % 2 == 0 else 0), (1 if side in (1,2) else 0)
            if abs(a[axis]-plane) > 1e-7 or abs(b[axis]-plane) > 1e-7:
                continue
            ta, tb = (a.x,a.y,1-a.x,1-a.y)[side], (b.x,b.y,1-b.x,1-b.y)[side]
            cuts = sorted((t-ta)/(tb-ta) for t in stations[side] if min(ta,tb)+1e-6 < t < max(ta,tb)-1e-6) if abs(tb-ta) > 1e-8 else []
            start, remaining, previous_cut = edge.verts[0], edge, 0
            for cut in cuts:
                new_edge, new_vertex = bmesh.utils.edge_split(remaining, start, (cut-previous_cut)/(1-previous_cut))
                remaining = next(e for e in new_vertex.link_edges if any((v.co-b).length < 1e-6 for v in e.verts))
                start, previous_cut = new_vertex, cut
            break

    # Preserve the upper contact band by cutting faces first. The original rock
    # plates and UVs continue below the former bottom; no collar is appended.
    bmesh.ops.bisect_plane(bm, geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
        dist=1e-7, plane_co=(0,0,REMODEL_HEIGHT), plane_no=(0,0,1), clear_inner=False, clear_outer=False)
    # A cut can fall micrometers from an existing fracture corner. Merge only
    # new cut-plane vertices so such slivers cannot create zero tangent frames.
    protected_cut = set()
    for face in bm.faces:
        cut = [v for v in face.verts if abs(v.co.z-REMODEL_HEIGHT)<1e-6]
        if (len(cut) >= 2 and all(v.co.z >= REMODEL_HEIGHT-1e-6 for v in face.verts)
                and any(v.co.z > REMODEL_HEIGHT+.0005 for v in face.verts)
                and max(v.co.z for v in face.verts) < REMODEL_HEIGHT+.002
                and any(.0001 < (a.co-b.co).length < .0005 for i,a in enumerate(cut) for b in cut[i+1:])):
            # A small retained upper facet can own a distinct atlas corner.
            # Keep its boundary even when lower slivers require cleanup.
            protected_cut.update(cut)
    bmesh.ops.remove_doubles(bm, verts=[v for v in bm.verts if abs(v.co.z-REMODEL_HEIGHT)<1e-6 and v not in protected_cut], dist=.0005)
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    # Keep the original face charts explicit across clipping/triangulation.
    # UV1 can have distinct per-face corners even where UV0 is continuous.
    for face in bm.faces:
        source_points, source_uvs = source_charts[face[source_face]]
        for loop in face.loops:
            if loop.vert.co.z < REMODEL_HEIGHT-1e-6:
                continue
            original = next((i for i,p in enumerate(source_points) if (p-loop.vert.co).length < 1e-7), None)
            triangle = min(((0,i,i+1) for i in range(1,len(source_points)-1)),
                key=lambda indices:(closest_point_on_tri(loop.vert.co,*[source_points[i] for i in indices])-loop.vert.co).length_squared)
            for name, coordinates in source_uvs.items():
                uv = coordinates[original] if original is not None else barycentric_transform(loop.vert.co,
                    *[source_points[i] for i in triangle], *[coordinates[i] for i in triangle])
                loop[bm.loops.layers.uv[name]].uv = uv.xy
    bm.faces.layers.int.remove(source_face)
    base = [v.co.copy() for v in bm.verts if abs(v.co.z) < 1e-7]
    for v in bm.verts:
        if v.co.z < REMODEL_HEIGHT-1e-7:
            v[deform][wall_group] = 1
            v.co = warp(v.co)
    bmesh.ops.dissolve_degenerate(bm, dist=.0001,
        edges=[e for e in bm.edges if all(wall_group in v[deform] for v in e.verts)])

    def vertex(co, group):
        key = (group, *[round(float(x), 8) for x in co])
        if key not in vertices:
            value = bm.verts.new(co)
            value[deform][group] = 1
            if group == underside_group:
                value[deform][core_group] = 1
            vertices[key] = value
        return vertices[key]

    def triangle(coords, group=underside_group):
        face = bm.faces.new([vertex(p, group) for p in coords])
        face.material_index = material
        face.smooth = False
        face.normal_update()
        normal = face.normal
        assert normal.length > .9
        for loop in face.loops:
            co = loop.vert.co
            uv = (co.x/4, co.y/4) if abs(normal.z) >= max(abs(normal.x), abs(normal.y)) else (co.x/4, co.z/4) if abs(normal.y) >= abs(normal.x) else (co.y/4, co.z/4)
            for layer in bm.loops.layers.uv.values():
                loop[layer].uv = uv

    # Close the warped old base with an irregular triangulated rock surface,
    # including all original base vertices. It has no central conical tip.
    coordinates = {}
    for p in base:
        coordinates.setdefault((round(p.x,5), round(p.y,5)), p)
    for side in range(4):
        for t in stations[side]:
            p = Vector(side_point(side,t,0))
            coordinates.setdefault((round(p.x,5),round(p.y,5)),p)
    for x,y in ((.23,.24),(.70,.27),(.40,.63),(.78,.74),(.53,.46)):
        p = Vector((x+rng.uniform(-.035,.035),y+rng.uniform(-.035,.035),0))
        coordinates[(round(p.x,5),round(p.y,5))] = p
    original_points = list(coordinates.values())
    points = [warp(p) for p in original_points]
    ring = []
    for side in range(4):
        axis, plane = (1 if side % 2 == 0 else 0), (1 if side in (1,2) else 0)
        indices = [i for i,p in enumerate(original_points) if abs(p[axis]-plane)<1e-7]
        indices.sort(key=lambda i:(original_points[i].x,original_points[i].y,1-original_points[i].x,1-original_points[i].y)[side])
        ring.extend(indices[:-1])
    coords, _, faces, origins, _, _ = delaunay_2d_cdt([Vector((p.x,p.y)) for p in points],[],[ring],1,1e-6)
    surface = []
    for co, ids in zip(coords,origins):
        assert ids, (body.name,"intersecting lower outline")
        assert max(points[i].z for i in ids)-min(points[i].z for i in ids)<1e-5
        surface.append(Vector((co.x,co.y,points[ids[0]].z)))
    for face in faces:
        triangle([surface[i] for i in reversed(face)])

    boulder_count, count = add_rock_masses(body, triangle, variant, mask)
    bm.normal_update()
    bm.to_mesh(body.data)
    bm.free()
    body.data.update()
    normals = [(0, 0, 0)] * len(body.data.loops)
    for polygon in body.data.polygons:
        polygon.use_smooth = False
        for index in polygon.loop_indices:
            normals[index] = tuple(polygon.normal)
    body.data.normals_split_custom_set(normals)
    for name in [layer.name for layer in body.data.uv_layers]:
        body.data.calc_tangents(uvmap=name)
        layer = body.data.uv_layers[name]
        bad = [loop.index for loop in body.data.loops if loop.tangent.length <= .5]
        if bad:
            print(json.dumps({"body":body.name,"uv":layer.name,"bad_tangent_faces":[
                {"points":[list(body.data.vertices[i].co) for i in p.vertices],
                 "uvs":[list(layer.data[i].uv) for i in p.loop_indices]}
                for p in body.data.polygons if any(i in bad for i in p.loop_indices)][:12]}))
        assert not bad, body.name
    for v in body.data.vertices:
        if v.co.z < REMODEL_HEIGHT:
            assert (-FINAL_OVERHANG-1e-6 if mask & 8 else -1e-6) <= v.co.x <= (1+FINAL_OVERHANG+1e-6 if mask & 2 else 1.000001), (body.name, v.co[:])
            assert (-FINAL_OVERHANG-1e-6 if mask & 1 else -1e-6) <= v.co.y <= (1+FINAL_OVERHANG+1e-6 if mask & 4 else 1.000001) and v.co.z >= -1, (body.name, v.co[:])
    body["GroundModuleBottom"] = True
    body["BottomAttachmentHeight"] = 0.0
    body["BottomMaxDepth"] = 1.0
    body["FloatingRockCount"] = count
    body["BoulderMassCount"] = boulder_count
    body["BottomMaxOverhang"] = FINAL_OVERHANG
    body["BottomRevision"] = 4
    body["BottomRemodelHeight"] = REMODEL_HEIGHT
    body["BottomConnectionHeight"] = CONNECTION_HEIGHT
    synchronize_preview_groups(body)
    return body


def tighten_body(body):
    """Edit authored rock vertices only; topology, UVs and depth stay exact."""
    previous = float(body.get("BottomMaxOverhang", .12))
    assert previous >= FINAL_OVERHANG, (body.name, previous)
    rock_groups = {group.index for group in body.vertex_groups
                   if group.name.startswith(("BoulderMass", "FloatingRock"))}
    if abs(previous-FINAL_OVERHANG) > 1e-8:
        for vertex in body.data.vertices:
            if any(weight.group in rock_groups and weight.weight > .5 for weight in vertex.groups):
                vertex.co = tighten_rock_point(vertex.co, int(body["ExposedSideMask"]), previous)
        body.data.update()
        normals = [(0, 0, 0)]*len(body.data.loops)
        for polygon in body.data.polygons:
            for index in polygon.loop_indices:
                normals[index] = tuple(polygon.normal)
        body.data.normals_split_custom_set(normals)
        for layer in body.data.uv_layers:
            body.data.calc_tangents(uvmap=layer.name)
            assert all(loop.tangent.length > .5 for loop in body.data.loops), body.name
    body["BottomMaxOverhang"] = FINAL_OVERHANG
    body["BottomRevision"] = 4
    synchronize_preview_groups(body)


def add_variants(scene, remodel=False):
    collection = bpy.data.collections.get("GroundBottomModules")
    if collection is None:
        collection = bpy.data.collections.new("GroundBottomModules")
        scene.collection.children.link(collection)
    for square in (False, True):
        for variant in range(10):
            for shape, family in enumerate(FAMILIES):
                stem = f"GroundBottom{'Square' if square else ''}{family.title()}{variant+1:02d}"
                original_stem = f"Ground{'Square' if square else ''}{family.title()}{variant+1:02d}"
                body = bpy.data.objects.get(stem + "_Body")
                if body is None or remodel:
                    seed = bpy.data.objects.get(original_stem.replace('Ground','GroundConstruction',1) + "_Body")
                    body = add_body(seed or bpy.data.objects[original_stem + "_Body"], collection, stem, variant, MASKS[shape])
                body.location = (shape*1.8 + (36 if square else 24), variant*2.2, 0)
                body["GroundModuleVariant"] = variant
                body["GroundModuleShape"] = family
                body["ExposedSideMask"] = MASKS[shape]
                body["SquareTop"] = square
                if bpy.data.objects.get(stem + "_Top") is None:
                    top = bpy.data.objects[original_stem + "_Top"].copy()
                    top.name = stem + "_Top"
                    collection.objects.link(top)
                    top.location = body.location


def rotated_mask(mask, turns):
    return ((mask << turns) | (mask >> (4-turns))) & 15 if turns else mask


def add_preview():
    name = "GroundModules_BottomPreview"
    if name in bpy.data.scenes:
        return
    scene = bpy.data.scenes.new(name)
    scene["Description"] = "Native floating-island bottoms: all connection families, mixed styles, and a supported square-top stack."
    cells = {(x, y, 0) for x in range(3) for y in range(3)}
    cells |= {(5, 0, 0), (0, 5, 0), (1, 5, 0), (2, 5, 0), (3, 5, 0), (6, 3, 0), (6, 4, 0), (7, 4, 0), (6, 7, 0), (6, 7, 1)}
    for index, (x, y, z) in enumerate(sorted(cells)):
        mask = sum(1 << side for side, (dx, dy) in enumerate(((0,-1),(1,0),(0,1),(-1,0))) if (x+dx,y+dy,z) not in cells)
        shape, turns = next((s, q) for s, canonical in enumerate(MASKS) for q in range(4) if rotated_mask(canonical,q) == mask)
        bottom, square = (x,y,z-1) not in cells, (x,y,z+1) in cells
        stem = f"Ground{'Bottom' if bottom else ''}{'Square' if square else ''}{FAMILIES[shape].title()}{index%10+1:02d}"
        for part in ("Body", "Top"):
            obj = bpy.data.objects[stem + "_" + part].copy()
            scene.collection.objects.link(obj)
            obj.name = f"BottomPreview_{index:02d}_{part}"
            obj.rotation_euler = (0, 0, turns*math.pi/2)
            offset = ((0,0),(1,0),(1,1),(0,1))[turns]
            obj.location = (x+offset[0], y+offset[1], z)
    camera_data = bpy.data.cameras.new("GroundBottomPreviewCamera")
    camera = bpy.data.objects.new("GroundBottomPreviewCamera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = (16, -21, 17)
    camera.rotation_euler = (Vector((3.7,3.0,0))-camera.location).to_track_quat('-Z','Y').to_euler()
    camera_data.type = 'ORTHO'
    camera_data.ortho_scale = 13
    scene.camera = camera
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1100
    scene.render.resolution_percentage = 100
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 32
    scene.world = bpy.data.worlds.new("GroundBottomPreviewWorld")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.20,.24,.30,1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = .7
    light_data = bpy.data.lights.new("GroundBottomPreviewSun", 'SUN')
    light_data.energy = 3
    light = bpy.data.objects.new("GroundBottomPreviewSun", light_data)
    scene.collection.objects.link(light)
    light.rotation_euler = (.45,-.55,-.4)


def export_body(body, path):
    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    location = body.location.copy()
    body.location = (0, 0, 0)
    try:
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True,
                use_active_scene=True, export_normals=True, export_tangents=True, export_texcoords=True,
                export_animations=False, export_yup=True, export_keep_originals=True, export_extras=True)
        assert "FINISHED" in result
    finally:
        body.location = location


def main():
    assert Path(bpy.data.filepath).resolve() == SOURCE.resolve(), "Open ground_modules.blend first"
    scene = bpy.data.scenes["GroundModules"]
    bpy.context.window.scene = scene
    temporary = ROOT / "out/ground-modules/export-temp"
    temporary.mkdir(parents=True, exist_ok=True)
    tempfile.tempdir = str(temporary)
    arguments = sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else []
    if "--add-bottom-variants" in arguments or "--remodel-bottom-variants" in arguments:
        add_variants(scene, "--remodel-bottom-variants" in arguments)
        add_preview()
    if "--tighten-bottom-borders" in arguments:
        for body in bpy.data.collections["GroundBottomModules"].objects:
            if body.name.endswith("_Body"):
                tighten_body(body)
    if any(flag in arguments for flag in ("--add-bottom-variants", "--remodel-bottom-variants", "--tighten-bottom-borders")):
        previous = bpy.context.preferences.filepaths.save_version
        try:
            bpy.context.preferences.filepaths.save_version = 0
            bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE))
        finally:
            bpy.context.preferences.filepaths.save_version = previous
    records = []
    for square in (False, True):
        for variant in range(10):
            for shape, family in enumerate(FAMILIES):
                stem = f"GroundBottom{'Square' if square else ''}{family.title()}{variant+1:02d}"
                body = bpy.data.objects[stem + "_Body"]
                mesh = body.data
                points = [list(v.co) for v in mesh.vertices]
                groups = {g.name: [v.index for v in mesh.vertices if any(w.group == g.index and w.weight > .5 for w in v.groups)] for g in body.vertex_groups}
                group_vertices = {name: set(indices) for name, indices in groups.items()}
                records.append({"variant": variant, "shape": family, "exposedSideMask": MASKS[shape], "squareTop": square,
                    "body": {"object": body.name, "vertices": len(points), "faces": len(mesh.polygons),
                        "bounds": {"min": [min(p[i] for p in points) for i in range(3)], "max": [max(p[i] for p in points) for i in range(3)]},
                        "properties": dict(body.items()), "points": points,
                        "polygons": [list(p.vertices) for p in mesh.polygons], "vertexGroups": groups,
                        "faceGroups": {name: [p.index for p in mesh.polygons if all(i in indices for i in p.vertices)] for name, indices in group_vertices.items()}}})
                if "--audit-only" not in arguments:
                    export_body(body, MODELS / f"ground_bottom_{'square_' if square else ''}{family}_{variant+1:02d}.glb")
    output = ROOT / "out/ground-modules/bottom-source-audit.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"exported_bottom_bodies": 0 if "--audit-only" in arguments else len(records), "source": str(SOURCE)}))


if __name__ == "__main__":
    main()
