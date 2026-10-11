"""Sculpt/export square, stackable ground with native faceted boulder sides.

Normal exports preserve artist edits. -- --remodel-regular explicitly builds
the regular bodies from the immutable native construction meshes. Caps and
the accepted floating bottoms are never rebuilt by this tool.
"""
from __future__ import annotations

import json
from pathlib import Path
import random
import sys

import bmesh
import bpy
from mathutils import Vector
from mathutils.geometry import barycentric_transform, closest_point_on_tri

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_bottom_ground_modules import export_body, synchronize_preview_groups
from sculpt_ground_bottoms import rock_hull

SOURCE = ROOT / 'assets/custom/source/ground_modules.blend'
MODELS = ROOT / 'assets/custom/models/ground_modules'
FAMILIES = ('island', 'end', 'strip', 'corner', 'edge', 'interior')
MASKS = (15, 11, 5, 3, 1, 0)
HEIGHT = .78


def construction_sources():
    collection = bpy.data.collections.get('GroundModuleConstructionSources')
    if collection is None:
        collection = bpy.data.collections.new('GroundModuleConstructionSources')
        bpy.data.scenes['GroundModules'].collection.children.link(collection)
        collection.hide_render = True
        collection.hide_viewport = True
    for square in (False, True):
        for family in FAMILIES:
            for variant in range(10):
                stem = f"{'Square' if square else ''}{family.title()}{variant+1:02d}_Body"
                name = 'GroundConstruction'+stem
                if name not in bpy.data.objects:
                    original = bpy.data.objects['Ground'+stem]
                    assert not original.get('RegularDesignRevision'), original.name
                    seed = original.copy()
                    seed.data = original.data.copy()
                    seed.name = name
                    seed.data.name = name+'Mesh'
                    collection.objects.link(seed)
                    seed.hide_render = True
                    seed['ConstructionOnly'] = True
    return collection


def upper_band(original):
    """Clip only original outer plates; retain their separate UV charts."""
    bm = bmesh.new()
    bm.from_mesh(original.data)
    source_face = bm.faces.layers.int.new('OriginalRockFace')
    uv_names = [layer.name for layer in bm.loops.layers.uv.values()]
    charts = []
    for index, face in enumerate(bm.faces):
        face[source_face] = index
        charts.append(([loop.vert.co.copy() for loop in face.loops],
            {name: [Vector((*loop[bm.loops.layers.uv[name]].uv, 0)) for loop in face.loops] for name in uv_names}))
    removed = [face for face in bm.faces
               if original.data.materials[face.material_index].name == 'GroundModuleRecess'
               or all(abs(v.co.z) < 1e-7 for v in face.verts)]
    bmesh.ops.delete(bm, geom=removed, context='FACES_ONLY')
    unused = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=unused, context='VERTS')
    if not bm.faces:
        bm.free()
        return []
    bmesh.ops.bisect_plane(bm, geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
        dist=1e-7, plane_co=(0,0,HEIGHT), plane_no=(0,0,1), clear_inner=False, clear_outer=False)
    protected = set()
    for face in bm.faces:
        cut = [v for v in face.verts if abs(v.co.z-HEIGHT)<1e-6]
        if (len(cut)>=2 and all(v.co.z>=HEIGHT-1e-6 for v in face.verts)
                and any(v.co.z>HEIGHT+.0005 for v in face.verts)
                and max(v.co.z for v in face.verts)<HEIGHT+.002
                and any(.0001<(a.co-b.co).length<.0005 for i,a in enumerate(cut) for b in cut[i+1:])):
            protected.update(cut)
    bmesh.ops.remove_doubles(bm, verts=[v for v in bm.verts if abs(v.co.z-HEIGHT)<1e-6 and v not in protected], dist=.0005)
    bmesh.ops.delete(bm, geom=[face for face in bm.faces if any(v.co.z<HEIGHT-1e-6 for v in face.verts)], context='FACES_ONLY')
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    records = []
    for face in bm.faces:
        points, coordinates = charts[face[source_face]]
        values = {name: [] for name in uv_names}
        for loop in face.loops:
            match = next((i for i,p in enumerate(points) if (p-loop.vert.co).length<1e-7), None)
            triangle = min(((0,i,i+1) for i in range(1,len(points)-1)),
                key=lambda indices:(closest_point_on_tri(loop.vert.co,*[points[i] for i in indices])-loop.vert.co).length_squared)
            for name in uv_names:
                uv = coordinates[name][match] if match is not None else barycentric_transform(loop.vert.co,
                    *[points[i] for i in triangle], *[coordinates[name][i] for i in triangle])
                values[name].append(tuple(uv.xy))
        records.append(([tuple(v.co) for v in face.verts], face.material_index, values, 'UpperRockBand'))
    bm.free()
    return records


def boulders(variant, mask):
    rng = random.Random(78019+variant*997+mask*31)
    masses = []
    corners = ((0,0,0,3),(1,0,0,1),(1,1,2,1),(0,1,2,3))
    wrapping = set()
    for x,y,a,b in corners:
        if mask & (1<<a) and mask & (1<<b):
            wrapping.add((x,y))
            broad = (x+y+variant)%2 == 0
            center = Vector((.20 if not x else .80, .20 if not y else .80, rng.uniform(.39,.46)))
            radii = Vector((rng.uniform(.34,.40) if broad else rng.uniform(.29,.34),
                            rng.uniform(.30,.37), rng.uniform(.34,.39) if broad else rng.uniform(.39,.44)))
            masses.append(rock_hull(center,radii,rng,0,z_bounds=(.02,.777)))
    for side in range(4):
        if not mask & (1<<side):
            continue
        ends = (((0,0),(1,0)),((1,0),(1,1)),((1,1),(0,1)),((0,1),(0,0)))[side]
        occupied = [point in wrapping for point in ends]
        if all(occupied):
            continue
        anchors = (.24,.74) if not any(occupied) else (.73 if occupied[0] else .27,)
        for along in anchors:
            along += rng.uniform(-.025,.025)
            center = Vector(((along,.13,.42),(.87,along,.42),(1-along,.87,.42),(.13,1-along,.42))[side])
            center.z += rng.uniform(-.04,.04)
            radii = Vector((rng.uniform(.35,.43),rng.uniform(.18,.23),rng.uniform(.36,.43)))
            if side%2:
                radii.x,radii.y = radii.y,radii.x
            masses.append(rock_hull(center,radii,rng,0,z_bounds=(.02,.777)))
    # Low, broad ledges carry the stone facing to the flat base without a
    # rectangular strip of exposed backing beneath the main shoulders.
    for side in range(4):
        if not mask & (1<<side):
            continue
        along=rng.uniform(.44,.57)
        center=Vector(((along,.09,.12),(.91,along,.12),(1-along,.91,.12),(.09,1-along,.12))[side])
        radii=Vector((rng.uniform(.49,.56),rng.uniform(.12,.17),rng.uniform(.17,.22)))
        if side%2:
            radii.x,radii.y=radii.y,radii.x
        masses.append(rock_hull(center,radii,rng,0,z_bounds=(.02,.777)))
    # Independently clipped hulls otherwise share exact outside planes. Their
    # coincident faces self-shadow in Cycles and fight in the game renderer.
    # Give each embedded mass a tiny, distinct inset inside the unit footprint.
    for index,(points,_) in enumerate(masses):
        inset=.0008+.0007*index
        for p in points:
            p.x=inset+(1-2*inset)*p.x
            p.y=inset+(1-2*inset)*p.y
            p.z=.02+inset+(p.z-.02)*(1-2*inset/.757)
    return masses


def remodel(body, original, variant, mask):
    source = original.data
    records = upper_band(original)
    for polygon in source.polygons:
        role = 'FlatBase' if all(abs(source.vertices[i].co.z)<1e-7 for i in polygon.vertices) else 'RecessBacking' if source.materials[polygon.material_index].name=='GroundModuleRecess' else None
        if role:
            records.append(([tuple(source.vertices[i].co) for i in polygon.vertices],polygon.material_index,
                            {layer.name:[tuple(layer.data[i].uv) for i in polygon.loop_indices] for layer in source.uv_layers},role))
    masses = boulders(variant,mask)
    material = next(p.material_index for p in source.polygons if all(abs(source.vertices[i].co.z)<1e-7 for i in p.vertices))
    for index,(points,faces) in enumerate(masses):
        for face in faces:
            coordinates = [points[i] for i in face]
            normal = (coordinates[1]-coordinates[0]).cross(coordinates[2]-coordinates[0]).normalized()
            def uv(p):
                return (p.x/4,p.y/4) if abs(normal.z)>=max(abs(normal.x),abs(normal.y)) else (p.x/4,p.z/4) if abs(normal.y)>=abs(normal.x) else (p.y/4,p.z/4)
            records.append(([tuple(p) for p in coordinates],material,
                            {layer.name:[uv(p) for p in coordinates] for layer in source.uv_layers},f'BoulderMass{index+1:02d}'))
    vertices, faces, lookup, roles = [],[],{},{}
    for coordinates,_,_,role in records:
        face = []
        for point in coordinates:
            key = (role,*point)
            if key not in lookup:
                lookup[key]=len(vertices);vertices.append(point)
            face.append(lookup[key])
        faces.append(face)
        roles.setdefault(role,set()).update(face)
    mesh = bpy.data.meshes.new(body.name+'Mesh')
    mesh.from_pydata(vertices,[],faces)
    for material in source.materials:
        mesh.materials.append(material)
    for name in [layer.name for layer in source.uv_layers]:
        mesh.uv_layers.new(name=name)
    for polygon,(_,material,values,_) in zip(mesh.polygons,records):
        polygon.material_index=material
        for name,uvs in values.items():
            for index,uv in zip(polygon.loop_indices,uvs):
                mesh.uv_layers[name].data[index].uv=uv
    previous=body.data
    for linked in bpy.data.objects:
        if linked.type=='MESH' and linked.data==previous and not linked.get('ConstructionOnly'):
            linked.data=mesh
    body.vertex_groups.clear()
    core=set().union(*(indices for name,indices in roles.items() if not name.startswith('BoulderMass')))
    for name,indices in {'RegularCore':core,**roles}.items():
        body.vertex_groups.new(name=name).add(sorted(indices),1,'REPLACE')
    mesh.update()
    normals=[(0,0,0)]*len(mesh.loops)
    for polygon in mesh.polygons:
        polygon.use_smooth=False
        for index in polygon.loop_indices:
            normals[index]=tuple(polygon.normal)
    mesh.normals_split_custom_set(normals)
    for name in [layer.name for layer in mesh.uv_layers]:
        mesh.calc_tangents(uvmap=name)
        assert all(loop.tangent.length>.5 for loop in mesh.loops),(body.name,name)
    body['RegularDesignRevision']=1
    body['RegularRemodelHeight']=HEIGHT
    body['RegularRockMinimumZ']=.02
    body['RegularRockMaximumZ']=.777
    body['RegularBoulderMassCount']=len(masses)
    body['RegularConstructionSource']=original.name
    synchronize_preview_groups(body)
    return body


def main():
    assert Path(bpy.data.filepath).resolve()==SOURCE.resolve()
    scene=bpy.data.scenes['GroundModules']
    bpy.context.window.scene=scene
    arguments=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    if '--remodel-regular' in arguments:
        construction_sources()
    snapshot=[]
    cap_snapshots={False:[],True:[]}
    for square in (False,True):
        for variant in range(10):
            for family,mask in zip(FAMILIES,MASKS):
                stem=f"{'Square' if square else ''}{family.title()}{variant+1:02d}_Body"
                body=bpy.data.objects['Ground'+stem]
                if '--remodel-regular' in arguments:
                    remodel(body,bpy.data.objects['GroundConstruction'+stem],variant,mask)
                mesh=body.data
                groups={g.name:[v.index for v in mesh.vertices if any(w.group==g.index and w.weight>.5 for w in v.groups)] for g in body.vertex_groups}
                selected={name:set(indices) for name,indices in groups.items()}
                points=[list(v.co) for v in mesh.vertices]
                mesh.calc_loop_triangles()
                triangles=list(mesh.loop_triangles)
                snapshot.append({'variant':variant,'shape':family,'exposedSideMask':mask,'squareTop':square,'body':{
                    'object':body.name,'vertices':len(points),'faces':len(triangles),'points':points,
                    'bounds':{'min':[min(p[i] for p in points) for i in range(3)],'max':[max(p[i] for p in points) for i in range(3)]},
                    'polygons':[list(p.vertices) for p in triangles],'properties':dict(body.items()),'vertexGroups':groups,
                    'faceGroups':{name:[i for i,p in enumerate(triangles) if all(v in indices for v in p.vertices)] for name,indices in selected.items()}}})
                top=bpy.data.objects[body.name.removesuffix('_Body')+'_Top']
                cap_snapshots[square].append({'variant':variant,'shape':family,'exposedSideMask':mask,
                    'body':{'object':body.name,'vertices':len(points),'faces':len(mesh.polygons),
                            'bounds':snapshot[-1]['body']['bounds'],'properties':dict(body.items())},
                    'top':{'object':top.name,'vertices':[list(v.co) for v in top.data.vertices],
                           'faces':[list(p.vertices) for p in top.data.polygons],'properties':dict(top.items())}})
                if '--audit-only' not in arguments:
                    export_body(body,MODELS/f"ground_{'square_' if square else ''}{family}_{variant+1:02d}.glb")
    if '--remodel-regular' in arguments:
        previous=bpy.context.preferences.filepaths.save_version
        bpy.context.preferences.filepaths.save_version=0
        bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE))
        bpy.context.preferences.filepaths.save_version=previous
    output=ROOT/'out/ground-modules/regular-chunky-source-audit.json'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(snapshot,indent=2)+'\n',encoding='utf-8')
    for square,rows in cap_snapshots.items():
        path=output.parent/('square-source-audit.json' if square else 'native-source-audit.json')
        path.write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'regular_bodies':len(snapshot),'exported':'--audit-only' not in arguments}))


if __name__=='__main__':
    main()
