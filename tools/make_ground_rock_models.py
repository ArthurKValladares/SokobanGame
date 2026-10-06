#!/usr/bin/env python3
"""Export ten editable, static glTF rock bodies with authored fracture layouts.

The broad stone plates have planar fronts and narrow, steep chipped bevels.
Unlike the previous runtime displacement grid, reducing the depth does not
flatten those bevels. Layouts are deliberately specified below, not randomized.
Square perimeters stay intact; shallow ledges extend less than 3% of a tile.
The models have sides and a bottom; the engine supplies the painted splat top.
Coordinates here are engine x/y/z; glTF stores x/z/-y. Only stdlib is required.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Each pair is the centre of a broad fracture plate in face-local u/height.
# Different counts and offset heights give long splits, shelves, and small chips.
LAYOUTS = [
    [(0.13,.23),(.44,.17),(.78,.30),(.16,.75),(.50,.64),(.82,.83),(.63,.93)],
    [(.12,.48),(.39,.25),(.73,.15),(.91,.54),(.44,.80),(.73,.76)],
    [(.20,.14),(.60,.23),(.90,.17),(.11,.69),(.38,.59),(.72,.65),(.40,.93)],
    [(.13,.18),(.40,.43),(.77,.22),(.14,.86),(.60,.83),(.91,.67)],
    [(.11,.27),(.42,.13),(.67,.40),(.92,.26),(.25,.72),(.58,.91),(.89,.80)],
    [(.22,.35),(.59,.15),(.86,.48),(.12,.83),(.50,.74),(.79,.94)],
    [(.12,.14),(.37,.34),(.75,.18),(.90,.75),(.57,.66),(.21,.78),(.52,.94)],
    [(.17,.48),(.48,.18),(.81,.36),(.30,.91),(.59,.72),(.92,.89)],
    [(.10,.22),(.39,.11),(.73,.32),(.93,.15),(.12,.80),(.45,.63),(.76,.85)],
    [(.24,.19),(.66,.17),(.93,.50),(.12,.68),(.42,.82),(.73,.70),(.50,.48)],
]
# Short bevels give visible chipped edges within 7.5% of the original wall.
BEVEL_WIDTHS = [.020, .015, .024, .018, .013, .022, .016]
FRONT_DEPTHS = [.010, .030, -.010, .023, -.004, .016, -.012]
SHADES = [.93, .85, .97, .89, .95, .87, .91]


def sub(a, b):
    return tuple(x-y for x,y in zip(a,b))


def cross(a,b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def dot(a,b):
    return sum(x*y for x,y in zip(a,b))


def unit(a):
    length=math.sqrt(dot(a,a))
    return tuple(x/length for x in a)


def clip(polygon, nx, nz, bound):
    result=[]
    for a,b in zip(polygon,polygon[1:]+polygon[:1]):
        da=nx*a[0]+nz*a[1]-bound
        db=nx*b[0]+nz*b[1]-bound
        if da <= 1e-10:
            result.append(a)
        if (da <= 0) != (db <= 0):
            t=da/(da-db)
            result.append((a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])))
    return result


def cells(layout):
    result=[]
    for i,(u,z) in enumerate(layout):
        poly=[(0.,0.),(1.,0.),(1.,1.),(0.,1.)]
        for j,(v,w) in enumerate(layout):
            if i != j:
                # A vertical bias favours long rock plates over pebble shapes.
                poly=clip(poly,2*(v-u), .8*(w-z), v*v-u*u+.4*(w*w-z*z))
        assert len(poly)>=3
        result.append(poly)
    return result


def inset(poly,width):
    # Intersect offset lines: the front remains one true planar polygon.
    lines=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        dx,dz=b[0]-a[0],b[1]-a[1]
        length=math.hypot(dx,dz)
        nx,nz=dz/length,-dx/length
        lines.append((nx,nz,nx*a[0]+nz*a[1]-width))
    result=[]
    for a,b in zip(lines[-1:]+lines[:-1],lines):
        det=a[0]*b[1]-b[0]*a[1]
        result.append(((a[2]*b[1]-b[2]*a[1])/det,
                       (a[0]*b[2]-b[0]*a[2])/det))
    # A tiny corner chip may lose an edge at the larger bevel width. Narrow
    # its bevel until the offset polygon has the same valid edge topology.
    if any(nx*u+nz*z>bound+1e-9 for u,z in result for nx,nz,bound in lines):
        return inset(poly,width*.65)
    return result


def world(side,u,depth,z):
    # Positive depth is inward; negative depth makes a small chipped ledge.
    return [(u,depth,z),(1-depth,u,z),(1-u,1-depth,z),(depth,1-u,z)][side]


class Primitive:
    def __init__(self,shade):
        self.shade=shade
        self.positions=[]
        self.normals=[]
        self.uvs=[]

    def triangle(self,a,b,c,side):
        normal=cross(sub(b,a),sub(c,a))
        if dot(normal,normal)<1e-16:
            return
        normal=unit(normal)
        for p in (a,b,c):
            self.positions.append(p)
            self.normals.append(normal)
            self.uvs.append((p[0 if side%2==0 else 1]/2.5,p[2]/2.5))


def rock(variant):
    primitives=[]
    for side in range(4):
        layout=LAYOUTS[(variant+side*3)%10]
        if (variant+side)%2:
            layout=[(1-u,z) for u,z in layout]
        for plate,poly in enumerate(cells(layout)):
            k=(plate+variant+side)%len(SHADES)
            width=BEVEL_WIDTHS[k]
            inner=inset(poly,width)
            centre=(sum(p[0] for p in inner)/len(inner),sum(p[1] for p in inner)/len(inner))
            # Small planar inclinations, plus steep bevels, avoid triangulated
            # noise while giving broad faces subtly different lighting.
            tilt_u=[.022,-.018,.013,-.026,.009,-.012,.017][k]
            tilt_z=[-.018,.023,.008,-.014,.019,-.008,.012][k]
            depth=lambda u,z: FRONT_DEPTHS[k]+tilt_u*(u-centre[0])+tilt_z*(z-centre[1])
            front=[world(side,u,depth(u,z),z) for u,z in inner]
            outer=[]
            for u,z in poly:
                perimeter=u<1e-7 or u>1-1e-7 or z<1e-7 or z>1-1e-7
                # Shared groove vertices close every plate without gaps. The
                # irregular path creates chips; it isn't a texture crack.
                groove=0.0 if perimeter else .062+.010*z
                outer.append(world(side,u,groove,z))
            prim=Primitive(SHADES[k])
            for j in range(1,len(front)-1):
                prim.triangle(front[0],front[j],front[j+1],side)
            for j in range(len(front)):
                n=(j+1)%len(front)
                prim.triangle(outer[j],outer[n],front[n],side)
                prim.triangle(outer[j],front[n],front[j],side)
            primitives.append(prim)
    # The square bottom allows arbitrary stacking; the separate splat top
    # closes the body at height one without hiding authored mesh materials.
    bottom=Primitive(.90)
    bottom.triangle((0,0,0),(0,1,0),(1,1,0),0)
    bottom.triangle((0,0,0),(1,1,0),(1,0,0),0)
    primitives.append(bottom)
    return primitives


def gltf_vector(p):
    return p[0],p[2],-p[1]


def write(path,primitives):
    binary=bytearray()
    views=[]
    accessors=[]
    parts=[]
    materials=[]
    def accessor(values,kind,components):
        start=len(binary)
        for value in values:
            binary.extend(struct.pack('<'+'f'*components,*value))
        views.append({'buffer':0,'byteOffset':start,'byteLength':len(binary)-start,'target':34962})
        entry={'bufferView':len(views)-1,'componentType':5126,'count':len(values),'type':kind}
        if kind=='VEC3':
            entry.update(min=[min(v[i] for v in values) for i in range(components)],
                         max=[max(v[i] for v in values) for i in range(components)])
        accessors.append(entry)
        return len(accessors)-1
    for i,p in enumerate(primitives):
        attrs={'POSITION':accessor([gltf_vector(v) for v in p.positions],'VEC3',3),
               'NORMAL':accessor([gltf_vector(v) for v in p.normals],'VEC3',3),
               'TEXCOORD_0':accessor(p.uvs,'VEC2',2)}
        index_start=len(binary)
        binary.extend(b''.join(struct.pack('<I',v) for v in range(len(p.positions))))
        views.append({'buffer':0,'byteOffset':index_start,'byteLength':len(binary)-index_start,'target':34963})
        accessors.append({'bufferView':len(views)-1,'componentType':5125,
                          'count':len(p.positions),'type':'SCALAR'})
        parts.append({'attributes':attrs,'indices':len(accessors)-1,'material':i,'mode':4})
        materials.append({'name':f'Sandstone plate {i+1}',
            'pbrMetallicRoughness':{'baseColorFactor':[p.shade,p.shade*.99,p.shade*.97,1],
                'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1}})
    doc={'asset':{'version':'2.0','generator':'Authored fracture layouts: tools/make_ground_rock_models.py'},
         'scene':0,'scenes':[{'nodes':[0]}],'nodes':[{'mesh':0,'name':path.stem}],
         'meshes':[{'name':path.stem,'primitives':parts}], 'materials':materials,
         'images':[{'uri':'../textures/ground_rock_side.png'}],
         'samplers':[{'magFilter':9729,'minFilter':9987,'wrapS':10497,'wrapT':10497}],
         'textures':[{'sampler':0,'source':0}],
         'buffers':[{'uri':path.with_suffix('.bin').name,'byteLength':len(binary)}],
         'bufferViews':views,'accessors':accessors}
    path.write_text(json.dumps(doc,indent=2)+'\n')
    path.with_suffix('.bin').write_bytes(binary)


def main():
    target=ROOT/'assets/custom/models'
    target.mkdir(parents=True,exist_ok=True)
    for variant in range(10):
        prims=rock(variant)
        points=[p for prim in prims for p in prim.positions]
        # These bounds are also checked after loading through the real engine.
        assert all(-.0301<=coordinate<=1.0301 for p in points for coordinate in p)
        write(target/f'ground_rock_{variant+1:02}.gltf',prims)
        print(f'Rock {variant+1:02}: {len(points)//3} triangles, {len(prims)} plates')


if __name__=='__main__':
    main()
