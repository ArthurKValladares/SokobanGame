"""Blend native chunky ground into its authored beveled or square cap.

Normal export preserves edits. -- --remodel-shoulders replaces the old outer
collar with closed rock shoulders, using frozen accepted native body meshes.
"""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
import random
import sys
import time

import bmesh
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_bottom_ground_modules import export_body,synchronize_preview_groups
from sculpt_ground_bottoms import rock_hull

SOURCE=ROOT/'assets/custom/source/ground_modules.blend'
MODELS=ROOT/'assets/custom/models/ground_modules'
FAMILIES=('island','end','strip','corner','edge','interior')
MASKS=(15,11,5,3,1,0)
MINIMUM_Z=.40
COLLAR_Z=.78


def clean_hull(points):
    """Discard microscopic nearly collinear clip corners before UV tangents."""
    points=[p.copy() for p in points]
    for attempt in range(24):
        hull=bmesh.new()
        for p in points:hull.verts.new(p)
        result=bmesh.ops.convex_hull(hull,input=list(hull.verts),use_existing_faces=False)
        unused={v for key in ('geom_interior','geom_unused') for v in result.get(key,[])
                if isinstance(v,bmesh.types.BMVert) and v.is_valid}
        if unused:bmesh.ops.delete(hull,geom=list(unused),context='VERTS')
        bmesh.ops.recalc_face_normals(hull,faces=list(hull.faces))
        bmesh.ops.triangulate(hull,faces=list(hull.faces));hull.verts.index_update()
        points=[v.co.copy() for v in hull.verts];faces=[[v.index for v in f.verts] for f in hull.faces]
        hull.free()
        violation,offender=max(((p-points[f[0]]).dot((points[f[1]]-points[f[0]]).cross(points[f[2]]-points[f[0]]).normalized()),i)
                               for f in faces for i,p in enumerate(points))
        # Near-coplanar clipped crowns can produce slightly nonplanar hull
        # quads. Enumerating approximate support planes can overlap triangles
        # on them. Drop only their microscopic outlier and rebuild a single
        # watertight BMesh hull instead.
        remove=None
        if violation>1e-5:
            assert violation<.005,('Unexpected large convex-hull error',violation)
            points.pop(offender)
            continue
        for face in faces:
            a,b,c=[points[i] for i in face]
            longest=max((a-b).length_squared,(b-c).length_squared,(c-a).length_squared)
            if (b-a).cross(c-a).length/max(longest,1e-12)>=.001:continue
            candidates=[]
            for i in range(3):
                p,q,r=[points[face[j%3]] for j in (i,i+1,i+2)]
                edge=r-q;t=(p-q).dot(edge)/max(edge.length_squared,1e-12)
                if 0<t<1:candidates.append(((p-q-t*edge).length,face[i]))
            if candidates and min(candidates)[0]<.001:
                remove=min(candidates)[1];break
        if remove is None:
            edges=Counter(tuple(sorted((a,b))) for face in faces for a,b in zip(face,face[1:]+face[:1]))
            directed=Counter((a,b) for face in faces for a,b in zip(face,face[1:]+face[:1]))
            assert all(count==2 for count in edges.values()),'Open shoulder hull'
            assert all(directed[b,a]==count for (a,b),count in directed.items()),'Inconsistent shoulder winding'
            return points,faces
        points.pop(remove)
    raise AssertionError('Shoulder hull cleanup did not converge')


def body_name(bottom,square,family,variant):
    return f"Ground{'Bottom' if bottom else ''}{'Square' if square else ''}{family.title()}{variant+1:02d}_Body"


def source_name(name):
    return name.replace('Ground','GroundShoulderSource',1)


def freeze_sources():
    name='GroundShoulderConstructionSources'
    collection=bpy.data.collections.get(name)
    if collection is None:
        collection=bpy.data.collections.new(name)
        bpy.data.scenes['GroundModules'].collection.children.link(collection)
        collection.hide_render=True;collection.hide_viewport=True
    for bottom in (False,True):
        for square in (False,True):
            for family in FAMILIES:
                for variant in range(10):
                    original=bpy.data.objects[body_name(bottom,square,family,variant)]
                    name=source_name(original.name)
                    if name not in bpy.data.objects:
                        assert not original.get('ShoulderTransitionRevision'),original.name
                        seed=original.copy();seed.data=original.data.copy()
                        seed.name=name;seed.data.name=name+'Mesh'
                        collection.objects.link(seed);seed.hide_render=True
                        seed['ConstructionOnly']=True


def shoulders(variant,mask,lip,bottom):
    rng=random.Random(140711+variant*997+mask*31)
    masses=[];wrapping=set()
    center_z=(MINIMUM_Z+lip)/2
    radius_z=(lip-MINIMUM_Z)*.80
    def mass(center,radii):
        index=len(masses)
        # The accepted regular masses already use .0008+.0007*n. Use a
        # separate, shallower range so the new front planes never coincide.
        inset=.00020+.000045*index
        roof=lip-(.001+.0006*index)
        points,faces=rock_hull(center,radii,rng,0,z_bounds=(MINIMUM_Z+.0003*index,roof))
        # Broad, uneven upper crowns carry each rock to the cap. A rotated
        # boulder alone can retreat by half a tile immediately beneath its
        # roof, exposing the backing again. These eight blunt crown corners
        # become part of the same convex rock, rather than another collar.
        crown_rng=random.Random(981311+variant*997+mask*31+index*53)
        low=[max(0,center[i]-.95*radii[i]) for i in (0,1)]
        high=[min(1,center[i]+.95*radii[i]) for i in (0,1)]
        cut=[(high[i]-low[i])*crown_rng.uniform(.05,.07) for i in (0,1)]
        crown=((low[0]+cut[0],low[1]),(high[0]-cut[0],low[1]),
               (high[0],low[1]+cut[1]),(high[0],high[1]-cut[1]),
               (high[0]-cut[0],high[1]),(low[0]+cut[0],high[1]),
               (low[0],high[1]-cut[1]),(low[0],low[1]+cut[1]))
        points.extend(Vector((x,y,roof-crown_rng.uniform(.002,.006))) for x,y in crown)
        for p in points:
            p.x=inset+(1-2*inset)*p.x
            p.y=inset+(1-2*inset)*p.y
            if bottom:
                margin=.04*min(1,max(0,(roof-p.z)/(roof-COLLAR_Z)))
                for axis,lower_side,upper_side in ((0,3,1),(1,0,2)):
                    lo=-margin if mask&(1<<lower_side) else 0
                    hi=1+margin if mask&(1<<upper_side) else 1
                    p[axis]=lo+(hi-lo)*p[axis]
        points,faces=clean_hull(points)
        masses.append((points,faces))
    for x,y,a,b in ((0,0,0,3),(1,0,0,1),(1,1,2,1),(0,1,2,3)):
        if mask&(1<<a) and mask&(1<<b):
            wrapping.add((x,y))
            mass(Vector((.20 if not x else .80,.20 if not y else .80,center_z)),
                 Vector((rng.uniform(.33,.39),rng.uniform(.31,.37),radius_z)))
    for side in range(4):
        if not mask&(1<<side):continue
        ends=(((0,0),(1,0)),((1,0),(1,1)),((1,1),(0,1)),((0,1),(0,0)))[side]
        occupied=[p in wrapping for p in ends]
        if all(occupied):continue
        anchors=(.24,.74) if not any(occupied) else (.73 if occupied[0] else .27,)
        for along in anchors:
            along+=rng.uniform(-.015,.015)
            center=Vector(((along,.16,center_z),(.84,along,center_z),(1-along,.84,center_z),(.16,1-along,center_z))[side])
            radii=Vector((rng.uniform(.34,.40),rng.uniform(.24,.29),radius_z))
            if side%2:radii.x,radii.y=radii.y,radii.x
            mass(center,radii)
    return masses


def remodel(body,seed,variant,mask,square,bottom):
    old=seed.data;lip=1.0 if square else .91
    old_groups={g.index:g.name for g in seed.vertex_groups}
    weights={v.index:{old_groups[w.group]:w.weight for w in v.groups} for v in old.vertices}
    vertices=[];faces=[];materials=[];charts=[];normal_charts=[];encoded_normals=[];groups={g.name:{} for g in seed.vertex_groups};lookup={}
    groups['CapLipSeal']={}
    uv_names=[layer.name for layer in old.uv_layers]
    removed=0
    for polygon in old.polygons:
        rock=all(any(name.startswith(('BoulderMass','FloatingRock')) and value>.5 for name,value in weights[i].items()) for i in polygon.vertices)
        recess=old.materials[polygon.material_index].name=='GroundModuleRecess'
        horizontal=all(abs(old.vertices[i].co.z-lip)<1e-6 for i in polygon.vertices)
        collar=not rock and not recess and not horizontal and all(old.vertices[i].co.z>=COLLAR_Z-1e-6 for i in polygon.vertices)
        if collar:
            removed+=1;continue
        face=[]
        for i in polygon.vertices:
            if i not in lookup:
                index=len(vertices);lookup[i]=index;vertices.append(tuple(old.vertices[i].co))
                for name,value in weights[i].items():groups[name][index]=value
            face.append(lookup[i])
        if horizontal:
            groups['CapLipSeal'].update({i:1.0 for i in face})
        faces.append(face);materials.append(polygon.material_index)
        charts.append({name:[tuple(old.uv_layers[name].data[i].uv) for i in polygon.loop_indices] for name in uv_names})
        normal_charts.append([tuple(old.corner_normals[i].vector) for i in polygon.loop_indices])
        encoded_normals.append([tuple(old.attributes['custom_normal'].data[i].value) for i in polygon.loop_indices]
                               if old.has_custom_normals else None)
    masses=shoulders(variant,mask,lip,bottom)
    material=next((p.material_index for p in old.polygons
                   if all(any(name.startswith('BoulderMass') and value>.5 for name,value in weights[i].items()) for i in p.vertices)),None)
    assert material is not None or not masses,(body.name,'No accepted boulder material')
    for index,(points,polygons) in enumerate(masses):
        group=f'ShoulderRock{index+1:02d}';offset=len(vertices)
        vertices.extend(tuple(p) for p in points);groups[group]={offset+i:1.0 for i in range(len(points))}
        for face in polygons:
            ps=[points[i] for i in face];normal=(ps[1]-ps[0]).cross(ps[2]-ps[0]).normalized()
            def uv(p):
                return (p.x/4,p.y/4) if abs(normal.z)>=max(abs(normal.x),abs(normal.y)) else (p.x/4,p.z/4) if abs(normal.y)>=abs(normal.x) else (p.y/4,p.z/4)
            faces.append([offset+i for i in face]);materials.append(material)
            charts.append({name:[uv(p) for p in ps] for name in uv_names})
            normal_charts.append(None)
            encoded_normals.append(None)
    mesh=bpy.data.meshes.new(body.name+'ShoulderMesh');mesh.from_pydata(vertices,[],faces)
    for material in old.materials:mesh.materials.append(material)
    for name in uv_names:mesh.uv_layers.new(name=name)
    for polygon,material,values in zip(mesh.polygons,materials,charts):
        polygon.material_index=material
        for name,uvs in values.items():
            for index,uv in zip(polygon.loop_indices,uvs):mesh.uv_layers[name].data[index].uv=uv
    previous=body.data
    for linked in bpy.data.objects:
        if linked.type=='MESH' and linked.data==previous and not linked.get('ConstructionOnly'):linked.data=mesh
    body.vertex_groups.clear()
    for name,values in groups.items():
        group=body.vertex_groups.new(name=name)
        for index,weight in values.items():group.add([index],weight,'REPLACE')
    mesh.update();normals=[(0,0,0)]*len(mesh.loops)
    for polygon,original_normals in zip(mesh.polygons,normal_charts):
        polygon.use_smooth=False
        for corner,index in enumerate(polygon.loop_indices):
            normals[index]=original_normals[corner] if original_normals else tuple(polygon.normal)
    mesh.normals_split_custom_set(normals)
    # Blender stores custom normals as two compressed integers per corner.
    # Reencoding decoded vectors can shift nearly coplanar core normals.
    # The retained flat faces have the same corner basis, so copy their native
    # records directly and keep the accepted normal streams exactly intact.
    for polygon,original in zip(mesh.polygons,encoded_normals):
        if original is not None:
            for index,value in zip(polygon.loop_indices,original):
                mesh.attributes['custom_normal'].data[index].value=value
    mesh.update()
    for name in uv_names:
        mesh.calc_tangents(uvmap=name)
        bad={loop.index for loop in mesh.loops if loop.tangent.length<=.5}
        if bad:
            print(json.dumps({'body':body.name,'uv':name,'bad_faces':[
                {'points':[list(mesh.vertices[i].co) for i in p.vertices],
                 'uv':[list(mesh.uv_layers[name].data[i].uv) for i in p.loop_indices],
                 'normal':list(p.normal),'area':p.area}
                for p in mesh.polygons if any(i in bad for i in p.loop_indices)]}))
        assert not bad,(body.name,name)
    body['ShoulderTransitionRevision']=1;body['ShoulderTransitionMinimumZ']=MINIMUM_Z
    body['ShoulderRockCount']=len(masses);body['ShoulderSource']=seed.name
    body['ShoulderRemovedCollarFaces']=removed
    body['BottomRevision' if bottom else 'RegularDesignRevision']=5 if bottom else 2
    synchronize_preview_groups(body)
    return body


def snapshot(body):
    mesh=body.data;mesh.calc_loop_triangles();triangles=list(mesh.loop_triangles)
    groups={g.name:[v.index for v in mesh.vertices if any(w.group==g.index and w.weight>.5 for w in v.groups)] for g in body.vertex_groups}
    sets={name:set(indices) for name,indices in groups.items()};points=[list(v.co) for v in mesh.vertices]
    return {'object':body.name,'vertices':len(points),'faces':len(triangles),'points':points,
            'bounds':{'min':[min(p[i] for p in points) for i in range(3)],'max':[max(p[i] for p in points) for i in range(3)]},
            'polygons':[list(p.vertices) for p in triangles],'properties':dict(body.items()),'vertexGroups':groups,
            'faceGroups':{name:[i for i,p in enumerate(triangles) if all(v in indices for v in p.vertices)] for name,indices in sets.items()}}


def main():
    assert Path(bpy.data.filepath).resolve()==SOURCE.resolve()
    bpy.context.window.scene=bpy.data.scenes['GroundModules']
    arguments=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    if '--remodel-shoulders' in arguments:freeze_sources()
    rows=[]
    for bottom in (False,True):
        for square in (False,True):
            for family,mask in zip(FAMILIES,MASKS):
                for variant in range(10):
                    body=bpy.data.objects[body_name(bottom,square,family,variant)]
                    if '--remodel-shoulders' in arguments:
                        remodel(body,bpy.data.objects[source_name(body.name)],variant,mask,square,bottom)
                    rows.append({'bottom':bottom,'squareTop':square,'shape':family,'variant':variant,'exposedSideMask':mask,'body':snapshot(body)})
                    if '--audit-only' not in arguments:
                        path=MODELS/f"ground_{'bottom_' if bottom else ''}{'square_' if square else ''}{family}_{variant+1:02d}.glb"
                        temporary=path.with_suffix('.export-tmp.glb')
                        try:
                            export_body(body,temporary)
                            # A running asset watcher or Windows scanner can
                            # briefly hold the destination without delete sharing.
                            for attempt in range(8):
                                try:
                                    temporary.replace(path)
                                    break
                                except PermissionError:
                                    if attempt==7:raise
                                    time.sleep(min(.1*2**attempt,1))
                        finally:
                            if temporary.exists():temporary.unlink()
    if '--remodel-shoulders' in arguments:
        previous=bpy.context.preferences.filepaths.save_version
        try:
            bpy.context.preferences.filepaths.save_version=0
            bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE))
        finally:
            bpy.context.preferences.filepaths.save_version=previous
    output=ROOT/'out/ground-modules/shoulder-source-audit.json';output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'bodies':len(rows),'exported':'--audit-only' not in arguments}))


if __name__=='__main__':main()
