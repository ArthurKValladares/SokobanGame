"""Run in Blender's Python console/MCP to author the crystal silhouettes.
Preserves other scenes; only replaces the two workshop crystal objects.
"""
import bpy, math, random, json
from itertools import combinations
from mathutils import Vector, Matrix
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'assets/custom/source/crystals'


def sculpt(skipping):
    rng=random.Random(221 if skipping else 120)
    p=2.05 if skipping else 3.25
    q=p/(p-1)
    normals=[]
    count=42 if skipping else 54
    for i in range(count):
        z=1-2*(i+.5)/count
        a=i*math.pi*(3-math.sqrt(5))+rng.uniform(-.09,.09)
        normals.append(Vector((math.sqrt(1-z*z)*math.cos(a),math.sqrt(1-z*z)*math.sin(a),z)))
    normals += [Vector(v) for v in [(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)]]
    planes=[]
    for n in normals:
        h=sum(abs(c)**q for c in n)**(1/q)
        planes.append((n,h*rng.uniform(.977,1.025)))
    if not skipping:
        # Shallow, individually tilted cuts break each broad block face into
        # crystal facets while retaining the underlying square silhouette.
        for axis in range(3):
            for sign in [-1,1]:
                for a,b in [(-1,-1),(-1,1),(1,-1),(1,1)]:
                    n=Vector((0,0,0));n[axis]=sign
                    n[(axis+1)%3]=a*.14;n[(axis+2)%3]=b*.14
                    n.normalize();planes.append((n,rng.uniform(.995,1.027)))
    points={}
    for ia,ib,ic in combinations(range(len(planes)),3):
        selected=[planes[i] for i in [ia,ib,ic]]
        m=Matrix([n for n,d in selected])
        if abs(m.determinant())<1e-7:continue
        v=m.inverted()@Vector([d for n,d in selected])
        if all(n.dot(v)<=d+1e-6 for n,d in planes):
            points[tuple(round(c,6) for c in v)]=v
    vertices=list(points.values());faces=[]
    for n,d in planes:
        ids=[i for i,v in enumerate(vertices) if abs(n.dot(v)-d)<1e-5]
        if len(ids)<3:continue
        center=sum((vertices[i] for i in ids),Vector())/len(ids)
        u=n.cross(Vector((0,0,1)) if abs(n.z)<.9 else Vector((0,1,0))).normalized()
        v=n.cross(u)
        ids.sort(key=lambda i: math.atan2((vertices[i]-center).dot(v),(vertices[i]-center).dot(u)))
        faces.append(ids)
    lo=Vector([min(v[k] for v in vertices) for k in range(3)])
    hi=Vector([max(v[k] for v in vertices) for k in range(3)])
    dims=(.96,.92,.91) if skipping else (1.0,.95,.96)
    for v in vertices:
        for k in range(3):v[k]=(v[k]-lo[k])/(hi[k]-lo[k])*dims[k]-(dims[k]/2 if k<2 else 0)
        # Slightly offset top and middle contours, retaining the block/sphere.
        v.x += .012*math.sin(v.z*6+v.y*3)
        v.y += .009*math.sin(v.z*5+v.x*3)
    if not skipping:
        # Follow the actual ordinary boulder's envelope, adding only small
        # low-frequency variation to its rounded block silhouette.
        from mathutils.bvhtree import BVHTree
        ref=bpy.data.objects.get('Reference_Rock')
        if ref is None:
            before=set(bpy.data.objects)
            bpy.ops.import_scene.gltf(filepath=str(ROOT/'assets/custom/models/chunky_boulder.glb'))
            ref=next(ob for ob in set(bpy.data.objects)-before if ob.type=='MESH')
            ref.name='Reference_Rock'
        coords=[ref.matrix_world@v.co-ref.location for v in ref.data.vertices]
        tree=BVHTree.FromPolygons(coords,[list(p.vertices) for p in ref.data.polygons])
        center=Vector([(min(v[k] for v in coords)+max(v[k] for v in coords))/2 for k in range(3)])
        for i,point in enumerate(vertices):
            direction=(point-Vector((0,0,.48))).normalized()
            hit,normal,index,distance=tree.ray_cast(center,direction,3)
            if hit is not None:
                variation=1+.025*math.sin(direction.x*4.2+direction.z*3.7)+.014*math.cos(direction.y*5.4-direction.z*4.1)
                sampled=center+(hit-center)*variation
                vertices[i]=sampled*.85+point*.15
        bottom=min(v.z for v in vertices)
        for v in vertices:v.z-=bottom
    return vertices,faces


def main():
    scene=bpy.data.scenes.get('Crystal_Silhouette_Workshop')
    if scene is None:scene=bpy.data.scenes.new('Crystal_Silhouette_Workshop')
    bpy.context.window.scene=scene
    records={}
    for skip in [False,True]:
        name='Crystal_Skipping_Rounded' if skip else 'Crystal_Block_Squared'
        old=bpy.data.objects.get(name)
        if old:bpy.data.objects.remove(old,do_unlink=True)
        vertices,faces=sculpt(skip)
        mesh=bpy.data.meshes.new(name+'_Sculpt');mesh.from_pydata(vertices,[],faces);mesh.update()
        ob=bpy.data.objects.new(name,mesh);scene.collection.objects.link(ob);ob.location.x=1.5 if skip else -1.5
        for poly in mesh.polygons:poly.use_smooth=False
        records['skipping_crystal' if skip else 'crystal']={
            'vertices':[list(v) for v in vertices],'faces':faces}
        print(name,tuple(round(x,3) for x in ob.dimensions),len(faces),'facets')
    SOURCE.mkdir(parents=True,exist_ok=True)
    (SOURCE/'silhouettes.json').write_text(json.dumps(records,indent=2))

if __name__=='__main__':main()
