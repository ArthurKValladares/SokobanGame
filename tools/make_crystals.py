"""Texture and export the Blender-authored crystal silhouettes. Python: numpy, Pillow.

Run model_crystal_silhouettes.py in Blender first to author the sculpt.
Each facet has its own atlas island with a subtle colored-glass gradient. GLBs contain indexed triangles, flat normals, UVs and
an embedded texture. Authored Z-up geometry is written as glTF Y-up.
"""
from pathlib import Path
import io, json, struct
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/custom/models'
SOURCE = ROOT / 'assets/custom/source/crystals'


def paint_edge_scratches(draw, coords, vertex_ids, offset, plans, base, skipping):
    """Broken, grainy catches on a sparse selection of physical mesh edges.

    Shared-edge seeds keep neighboring UV islands in agreement. Most edges
    are completely unmarked; selected ones carry only short partial strokes.
    """
    center=coords.mean(axis=0)+offset
    tint=np.array((247,164,207) if skipping else (246,194,149),dtype=float)
    for i,va in enumerate(vertex_ids):
        vb=vertex_ids[(i+1)%len(vertex_ids)]
        plan=plans.get(tuple(sorted((va,vb))))
        if plan is None:continue
        a=coords[i]+offset;b=coords[(i+1)%len(coords)]+offset
        if va>vb:a,b=b,a
        edge=b-a;length=np.linalg.norm(edge)
        if length<12:continue
        inward=center-(a+b)/2;inward/=np.linalg.norm(inward)
        rng=np.random.default_rng(plan)
        start=rng.uniform(.04,.28);end=min(.97,start+rng.uniform(.48,.78))
        steps=max(6,int(length*(end-start)/2.2))
        for j in range(steps):
            if rng.random()<.10:continue
            t=start+(end-start)*j/steps
            t2=min(end,t+(end-start)/steps*rng.uniform(.90,1.10))
            p=a+edge*t+inward*rng.uniform(.35,1.0)
            q=a+edge*t2+inward*rng.uniform(.35,1.1)
            taper=np.sin(np.pi*(j+.5)/steps)**.5
            width=max(1,round(taper*rng.uniform(2,4)))
            strength=rng.uniform(.40,.90)
            color=tuple(np.clip(base*(1-strength)+tint*strength,0,255).astype(int))
            draw.line([tuple(p),tuple(q)],fill=color,width=width)
            # Fine offset scraping and chipped flecks break up the main catch.
            if rng.random()<.28:
                shift=inward*rng.uniform(2,4)
                draw.line([tuple(p+shift),tuple(q+shift)],fill=color,width=1)
            if rng.random()<.10:
                tip=p+inward*rng.uniform(3,7)+edge/length*rng.uniform(-2,2)
                draw.line([tuple(p),tuple(tip)],fill=color,width=1)


def generate(skipping):
    rng=np.random.default_rng(120 if skipping else 51)
    # Shared painted surface style; distinct Blender-authored silhouettes.
    if skipping:
        palette=[(224,39,120),(244,68,145),(200,39,116),(246,95,160),(219,52,144)]
    else:
        palette=[(222,118,50),(241,146,62),(205,98,56),(241,159,79),(226,127,72)]
    # Compress the palette around its average hue; keep gentle glass facets.
    center=np.array(palette,dtype=float).mean(axis=0)
    palette=[center+(np.array(color)-center)*.35 for color in palette]
    emission=[.055,.008,.025] if skipping else [.055,.022,.008]
    name='skipping_crystal' if skipping else 'crystal'
    authored=json.loads((SOURCE/'silhouettes.json').read_text())[name]
    vertices=np.array(authored['vertices'])
    faces=[vertices[ids] for ids in authored['faces']]
    scratch_rng=np.random.default_rng(1701 if skipping else 1907)
    physical_edges=sorted({tuple(sorted((ids[i],ids[(i+1)%len(ids)])))
                           for ids in authored['faces'] for i in range(len(ids))})
    plans={edge:int(scratch_rng.integers(1,2**31)) for edge in physical_edges
           if scratch_rng.random()<.22}
    # Add a modest second layer without moving the established scratch marks.
    for i,edge in enumerate(physical_edges):
        if edge in plans:continue
        extra_rng=np.random.default_rng(22003+i+(1000 if skipping else 0))
        if extra_rng.random()<.15:
            plans[edge]=int(extra_rng.integers(1,2**31))
    columns=10;cell=128;size=columns*cell
    atlas=Image.new('RGB',(size,size),(170,90,135));draw=ImageDraw.Draw(atlas)
    positions=[];normals=[];uvs=[];indices=[]
    for fi,face in enumerate(faces):
        uv_normal=np.cross(face[1]-face[0],face[2]-face[0]);uv_normal/=np.linalg.norm(uv_normal)
        # Surface fitting can gently warp polygon faces. Use all edges for
        # a stable outward shading normal rather than three adjacent corners.
        n=np.cross(face,np.roll(face,-1,axis=0)).sum(axis=0);n/=np.linalg.norm(n)
        u=face[1]-face[0];u/=np.linalg.norm(u);v=np.cross(uv_normal,u)
        coords=np.c_[face@u,face@v];mn=coords.min(axis=0);mx=coords.max(axis=0)
        coords=(coords-mn)/max(mx-mn)*100+14
        ox=(fi%columns)*cell;oy=(fi//columns)*cell
        base=np.array(palette[int(rng.integers(len(palette)))],float)
        # Color lives in the atlas, so tile tint stays white and does not wash
        # the orange or magenta into the earlier uniform salmon color.
        base*=rng.uniform(.975,1.025)
        for y in range(cell):
            t=y/(cell-1);rgb=np.clip(base*(1.035-.07*t)+np.array([2,3,5])*(1-t),0,255).astype(int)
            draw.line((ox,oy+y,ox+cell-1,oy+y),fill=tuple(rgb))
        paint_edge_scratches(draw,coords,authored['faces'][fi],
                             np.array((ox,oy)),plans,base,skipping)
        offset=len(positions)
        for pt,xy in zip(face,coords):
            positions.append((pt[0],pt[2],-pt[1]))
            normals.append((n[0],n[2],-n[1]))
            uvs.append(((ox+xy[0])/size,(oy+xy[1])/size))
        for j in range(1,len(face)-1):indices.extend([offset,offset+j,offset+j+1])
    name='skipping_crystal' if skipping else 'crystal'
    SOURCE.mkdir(parents=True,exist_ok=True)
    atlas.save(SOURCE/(name+'_facets.png'))
    png=io.BytesIO();atlas.save(png,format='PNG');data=bytearray();views=[];accessors=[]
    def append(blob):
        start=len(data);data.extend(blob);views.append({'buffer':0,'byteOffset':start,'byteLength':len(blob)})
        data.extend(b'\0'*((-len(data))%4));return len(views)-1
    for values,kind,ctype in [(positions,'VEC3',5126),(normals,'VEC3',5126),(uvs,'VEC2',5126),(indices,'SCALAR',5123)]:
        arr=np.array(values,dtype='<f4' if ctype==5126 else '<u2');view=append(arr.tobytes())
        accessor={'bufferView':view,'componentType':ctype,'count':len(values),'type':kind}
        if len(accessors)==0:accessor.update(min=arr.min(axis=0).tolist(),max=arr.max(axis=0).tolist())
        accessors.append(accessor)
    pv=append(png.getvalue())
    doc={'asset':{'version':'2.0','generator':'make_crystals.py'},'buffers':[{'byteLength':len(data)}],
         'bufferViews':views,'accessors':accessors,'images':[{'bufferView':pv,'mimeType':'image/png'}],
         'samplers':[{'magFilter':9729,'minFilter':9987,'wrapS':33071,'wrapT':33071}],
         'textures':[{'source':0,'sampler':0}],
         'materials':[{'name':'PaintedCrystalGlass','emissiveFactor':emission,'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':.04,'roughnessFactor':.38}}],
         'meshes':[{'name':name,'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2},'indices':3,'material':0,'mode':4}]}],
         'nodes':[{'mesh':0}],'scenes':[{'nodes':[0]}],'scene':0}
    js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*((-len(js))%4)
    glb=struct.pack('<III',0x46546c67,2,28+len(js)+len(data))+struct.pack('<II',len(js),0x4e4f534a)+js+struct.pack('<II',len(data),0x004e4942)+data
    (OUT/(name+'.glb')).write_bytes(glb)
    print(name,len(faces),'facets,',len(indices)//3,'triangles,',len(glb),'bytes')

if __name__=='__main__':
    generate(False)
    generate(True)
