"""Author and export the tile decoration kit in Blender 5.

Run in Blender's scripting workspace, or blender --background --python this.py.
Preserves existing scenes; creates TileDecorations with editable mesh islands.
Only the selected asset mesh is exported, with catalog spacing removed.
"""
from pathlib import Path
import json
import math
import random
import struct
import bpy
import bmesh
from mathutils import Vector, Quaternion

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'assets/custom/source'
KIT = SOURCE / 'tile_decorations'
MODELS = ROOT / 'assets/custom/models/tile_decorations'
PREVIEWS = KIT / 'previews'
for directory in (KIT, MODELS, PREVIEWS):
    directory.mkdir(parents=True, exist_ok=True)

CONFIGS = [('edge', 1, (0,), '1 EDGE'),
           ('corner', 3, (0, 1), '2 ADJACENT'),
           ('strip', 5, (0, 2), '2 OPPOSITE'),
           ('end', 11, (0, 1, 3), '3 EDGES')]
SCENE_NAME = 'TileDecorations'
if bpy.data.scenes.get(SCENE_NAME):
    raise RuntimeError('TileDecorations already exists. Rename it before regenerating the kit.')
scene = bpy.data.scenes.new(SCENE_NAME)
bpy.context.window.scene = scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
assets_collection = bpy.data.collections.new('Decoration assets | 48 meshes')
display_collection = bpy.data.collections.new('Presentation | do not export')
scene.collection.children.link(assets_collection)
scene.collection.children.link(display_collection)


def solid_material(name, color, roughness=.88):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    node = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    node.inputs['Base Color'].default_value = (*color, 1)
    node.inputs['Roughness'].default_value = roughness
    mat.diffuse_color = (*color, 1)
    return mat


PALETTES = {
    'pebbles': [(.31,.37,.40), (.39,.44,.46), (.47,.44,.36), (.55,.51,.42),
                (.60,.52,.38), (.68,.60,.46), (.46,.50,.37), (.54,.58,.44)],
    'grass': [(.33,.46,.12), (.39,.51,.15), (.45,.56,.19), (.51,.61,.23),
              (.37,.49,.14), (.43,.54,.17), (.49,.59,.21), (.55,.64,.26)],
    'moss': [(.28,.42,.10), (.34,.48,.13), (.45,.53,.12), (.52,.60,.17),
             (.44,.58,.25), (.51,.64,.31), (.62,.66,.22), (.68,.72,.29)]}


def paint_palette(image, style):
    colors = PALETTES[style]
    pixels = []
    for y in range(16):
        for x in range(128):
            color = colors[x // 16]
            # Quiet painted variation, with each swatch safely isolated in UV0.
            shade = .97 + .035 * y / 15 + .008 * math.sin(x * .8 + y)
            pixels.extend([min(1, c * shade) for c in color] + [1])
    image.pixels.foreach_set(pixels)
    image.update()
    image.filepath_raw = str(KIT / (style + '_palette.png'))
    image.save()
    image.pack()


def palette_material(style):
    image = bpy.data.images.new('TileDetails_' + style + '_palette', width=128, height=16, alpha=False)
    paint_palette(image,style)
    mat = solid_material('TileDetails_' + style, (.6,.6,.6),
                         {'pebbles':.91,'grass':.85,'moss':.94}[style])
    tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
    tex.image = image
    tex.interpolation = 'Linear'
    bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    return mat


materials = {style: palette_material(style) for style in PALETTES}


class MeshBuilder:
    def __init__(self):
        self.vertices, self.faces, self.swatches = [], [], []
        self.focal_points = []

    def island(self, verts, faces, swatches):
        offset = len(self.vertices)
        self.vertices.extend(verts)
        self.faces.extend(tuple(i + offset for i in face) for face in faces)
        self.swatches.extend(swatches)

    def object(self, name, material):
        mesh = bpy.data.meshes.new(name + '_mesh')
        mesh.from_pydata(self.vertices, [], self.faces)
        mesh.materials.append(material)
        mesh.update()
        if material.name.startswith('TileDetails_moss'):
            for polygon in mesh.polygons:
                polygon.use_smooth = True
        # Recalculate outward normals on every disconnected closed stone/blade.
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(mesh)
        bm.free()
        uv = mesh.uv_layers.new(name='PaletteUV')
        for face, swatch in zip(mesh.polygons, self.swatches):
            count = len(face.loop_indices)
            for j, loop_index in enumerate(face.loop_indices):
                angle = j * math.tau / count
                uv.data[loop_index].uv = ((swatch + .5 + .23 * math.cos(angle)) / 8,
                                           .5 + .28 * math.sin(angle))
        obj = bpy.data.objects.new(name, mesh)
        assets_collection.objects.link(obj)
        return obj


def edge_point(edge, t, inset):
    """Clockwise edge parameter; north is -Y. All geometry stays in the cell."""
    return [(t, -.5 + inset), (.5 - inset, t),
            (-t, .5 - inset), (-.5 + inset, -t)][edge]


def pebble(builder, x, y, radius, height, rng, swatch):
    shape=rng.choice(('rounded','slab','wedge','angular','river'))
    n={'rounded':7,'slab':5,'wedge':4,'angular':5,'river':6}[shape]
    elongation=rng.uniform(*{'rounded':(.85,1.1),'slab':(.65,1.15),
                            'wedge':(.55,.90),'angular':(.70,1.20),
                            'river':(.35,.55)}[shape])
    height*= {'rounded':.94,'slab':.48,'wedge':.87,'angular':1.0,'river':.65}[shape]
    rotation = rng.uniform(0, math.tau)
    radial = [rng.uniform(.76,1.20) for _ in range(n)]
    if shape=='angular':
        radial[rng.randrange(n)]*=.65
    shoulder={'rounded':(.91,.48),'slab':(.98,.86),'wedge':(.72,.28),
              'angular':(.85,.58),'river':(.94,.66)}[shape]
    top_offset=(rng.uniform(-.28,.28)*radius,rng.uniform(-.24,.24)*radius)
    slope=rng.uniform(.12,.22) if shape=='wedge' else rng.uniform(-.06,.06)
    verts = []
    for ring, (scale, z) in enumerate(((.64,-.008),(1,.22*height),
                                      (shoulder[0],.68*height),(shoulder[1],.93*height))):
        for j in range(n):
            angle = j * math.tau / n + rotation + .035 * ring
            z_jitter = (rng.uniform(-.045,.045)+slope*math.cos(angle))*height if ring else 0
            drift=(0,.18,.58,.82)[ring]
            verts.append((x+math.cos(angle)*radius*radial[j]*scale+top_offset[0]*drift,
                          y+math.sin(angle)*radius*elongation*radial[j]*scale+top_offset[1]*drift,
                          z + z_jitter))
    verts.append((x+top_offset[0],y+top_offset[1],height))
    faces, swatches = [tuple(reversed(range(n)))], [max(0, swatch - 1)]
    for ring in range(3):
        for j in range(n):
            faces.append((ring*n+j, ring*n+(j+1)%n, (ring+1)*n+(j+1)%n, (ring+1)*n+j))
            swatches.append(swatch+1 if ring==2 else swatch)
    for j in range(n):
        faces.append((3*n+j, 3*n+(j+1)%n, 4*n))
        swatches.append(swatch+1)
    builder.island(verts, faces, swatches)


def blade(builder, x, y, height, width, angle, lean, swatch):
    """One simple closed blade: two folded sections and a tapered tip."""
    forward = Vector((math.cos(angle), math.sin(angle), 0))
    side=Vector((-math.sin(angle),math.cos(angle),0))
    verts = []
    for t, profile in ((0,.58),(.46,1)):
        center=Vector((x,y,-.006+(height+.006)*t))+forward*(lean*t*t)
        halfwidth = width*profile*.5
        ridge=max(.0015,width*.08)*profile
        verts.extend([tuple(center-side*halfwidth),tuple(center+forward*ridge),
                      tuple(center+side*halfwidth),tuple(center-forward*ridge)])
    verts.append((x+forward.x*lean, y+forward.y*lean, height))
    faces,swatches=[(3,2,1,0)],[swatch]
    for j in range(4):
        faces.append((j,(j+1)%4,4+(j+1)%4,4+j))
        swatches.append(min(7,swatch+(j<2)))
    for j in range(4):
        faces.append((4+j,4+(j+1)%4,8))
        swatches.append(min(7,swatch+(j<2)))
    builder.island(verts, faces, swatches)


def make_pebbles(builder, rng):
    """A single simple stone, from small flat chips to broad chunky pieces."""
    pebble(builder,0,0,rng.uniform(.045,.115),rng.uniform(.026,.058),
           rng,rng.choice((0,2,4,6)))


def ground_cover(builder, x, y, radius, depth, height, rng, swatch):
    """One low closed mat with a round, kidney, tongue or scalloped outline."""
    n=16
    phase=rng.uniform(0,math.tau)
    rotation=rng.uniform(0,math.tau)
    form=rng.choice(('round','kidney','tongue','scallop'))
    lobes=rng.choice((2,3,4))
    amplitude=rng.uniform(.17,.31)
    radial=[]
    for j in range(n):
        angle=j*math.tau/n
        if form=='round':
            r=1+.08*math.cos(2*angle+phase)+.04*math.sin(3*angle)
        elif form=='kidney':
            r=1-.42*max(0,math.cos(angle-phase))**4+.15*math.sin(2*angle+phase)
        elif form=='tongue':
            r=1+.24*math.cos(angle-phase)+.08*math.cos(3*angle+phase)
        else:
            r=1+amplitude*math.cos(lobes*angle+phase)+.05*math.sin(5*angle+phase*.6)
        radial.append(r)
    verts=[]
    for scale,z in ((.90,-.006),(1,height*.30),(.78,height*.82)):
        for j in range(n):
            angle=j*math.tau/n+rotation
            verts.append((x+math.cos(angle)*radius*radial[j]*scale,
                          y+math.sin(angle)*depth*radial[j]*scale,z))
    verts.append((x-radius*.10,y+depth*.08,height))
    faces=[tuple(reversed(range(n)))]
    swatches=[max(0,swatch-1)]
    for ring in range(2):
        for j in range(n):
            faces.append((ring*n+j,ring*n+(j+1)%n,(ring+1)*n+(j+1)%n,(ring+1)*n+j))
            swatches.append(swatch)
    for j in range(n):
        faces.append((2*n+j,2*n+(j+1)%n,3*n))
        # Two wide painted regions, without speckles or per-face randomness.
        swatches.append(swatch+1 if j<n//2 else swatch)
    builder.island(verts,faces,swatches)


def make_grass(builder, rng):
    """One independent blade per site, without a base, leaves or tuft."""
    blade(builder,0,0,rng.uniform(.075,.175),rng.uniform(.032,.060),
          math.pi/2+rng.uniform(-1.0,1.0),rng.uniform(.025,.080),
          rng.choice((1,2,4,5)))


def decoration_distribution(style):
    """Broad footprints, with enough clear tile between larger silhouettes."""
    return {
        'pebbles':(.185,(.075,.205),(.055,.165)),
        'grass':(.160,(.032,.065),(.035,.090)),
        'moss':(.235,(.115,.285),(.075,.220)),
    }[style]


def scatter_edge_sites(edge, indices, band, widths, depths, rng):
    """Five loose strata replace the previous twelve miniature clusters."""
    ranks=list(range(len(indices)))
    rng.shuffle(ranks)
    sites=[]
    for index,rank in zip(indices,ranks):
        width,depth=rng.uniform(*widths),rng.uniform(*depths)
        margin=width/2+.002
        t=max(-.5+margin,min(.5-margin,(index+.5)/5-.5+rng.uniform(-.023,.023)))
        fraction=(rank+rng.uniform(.12,.88))/len(indices)
        inset=depth/2+.003+fraction*(band-depth-.006)
        sites.append({'edge':edge,'corner':False,'t':t,'inset':inset,
                      'width':width,'depth':depth})
    outer=rng.choice(sites)
    outer['inset']=outer['depth']/2
    return sites


def perimeter_sites(style, config, rng):
    """Five broad patches per edge, sharing a single patch at each corner."""
    edges={'edge':(0,),'corner':(0,1),'strip':(0,2),'end':(0,1,3)}[config]
    band,widths,depths=decoration_distribution(style)
    sites=[]
    for edge in edges:
        indices=[i for i in range(5)
                 if not (i==0 and (edge-1)%4 in edges)
                 and not (i==4 and (edge+1)%4 in edges)]
        sites.extend(scatter_edge_sites(edge,indices,band,widths,depths,rng))
        if (edge+1)%4 in edges:
            width,depth=rng.uniform(*widths),rng.uniform(*depths)
            sites.append({'edge':edge,'corner':True,'width':width,'depth':depth,
                          'center':(.5-width/2,-.5+depth/2)})
    return sites


def place_perimeter_patch(builder, patch, site):
    """Fit uniformly so distinct native proportions survive placement."""
    edge,corner=site['edge'],site['corner']
    width,depth=site['width'],site['depth']
    points=patch.vertices
    if corner:
        # North/east corner: point the plant inward along the diagonal.
        points=[((x-y)/math.sqrt(2),(x+y)/math.sqrt(2),z) for x,y,z in points]
    low=[min(p[i] for p in points) for i in (0,1)]
    high=[max(p[i] for p in points) for i in (0,1)]
    scale=min(1,width/(high[0]-low[0]),depth/(high[1]-low[1]))
    actual_width=(high[0]-low[0])*scale
    actual_depth=(high[1]-low[1])*scale
    center=[(a+b)/2 for a,b in zip(low,high)]
    # Border anchors follow the actual geometry after uniform fitting.
    inset=(actual_depth/2 if abs(site.get('inset',-1)-depth/2)<1e-9
           else site.get('inset',0))
    placed=[]
    for x,y,z in points:
        u=(x-center[0])*scale
        v=(y-center[1])*scale
        if corner:
            px=.5-actual_width/2+u
            py=-.5+actual_depth/2+v
            # Rotate the shared NE site to the other three corners.
            px,py=((px,py),(-py,px),(-px,-py),(py,-px))[edge]
        else:
            px,py=edge_point(edge,site['t']+u,inset+v)
        placed.append((px,py,z))
    builder.island(placed,patch.faces,patch.swatches)


def make_uniform_decoration(builder, style, config, variation, rng):
    band,_,_=decoration_distribution(style)
    maker={'pebbles':make_pebbles,'grass':make_grass,'moss':make_moss}[style]
    for site in perimeter_sites(style,config,rng):
        patch=MeshBuilder()
        maker(patch,rng)
        if style=='moss':
            # Preserve the low cushion profile even for narrow footprints.
            height=max(p[2] for p in patch.vertices)
            scale=min(1,.80*min(site['width'],site['depth'])/height)
            patch.vertices=[(x,y,z*scale if z>0 else z) for x,y,z in patch.vertices]
        place_perimeter_patch(builder,patch,site)
    # Retain compatible representative points for existing catalog readers.
    focus=[0,-.5+band/2,0]
    if config=='corner':
        focus=[.5-band/2,-.5+band/2,0]
    builder.focal_points=[focus]
    if config=='strip':
        builder.focal_points.append([0,.5-band/2,0])
    return focus


def make_moss(builder, rng):
    """One simple mat with varied size, low profile, outline and green family."""
    ground_cover(builder,0,0,rng.uniform(.065,.155),rng.uniform(.040,.110),
                 rng.uniform(.010,.033),rng,rng.choice((0,2,4,6)))


def make_decoration(builder, style, config, variation, rng):
    return make_uniform_decoration(builder,style,config,variation,rng)


def link_display(obj):
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    display_collection.objects.link(obj)


def display_box(name, center, size, material, bevel=0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj=bpy.context.object
    obj.name=name
    obj.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    obj.data.materials.append(material)
    if bevel:
        mod=obj.modifiers.new('Soft display corners','BEVEL')
        mod.width=bevel
        mod.segments=2
    link_display(obj)
    return obj


font_path=Path('C:/Windows/Fonts/segoeui.ttf')
bold_path=Path('C:/Windows/Fonts/segoeuib.ttf')
font = bpy.data.fonts.load(str(font_path)) if font_path.exists() else None
bold_font = bpy.data.fonts.load(str(bold_path)) if bold_path.exists() else font
ink = solid_material('Presentation | forest ink',(.055,.095,.065))
secondary = solid_material('Presentation | muted ink',(.23,.29,.22))


def label(text, location, size, bold=False, mat=ink):
    data=bpy.data.curves.new('Label | '+text,'FONT')
    data.body=text
    data.size=size
    data.align_x='CENTER'
    chosen_font=bold_font if bold else font
    if chosen_font:
        data.font=chosen_font
    data.materials.append(mat)
    obj=bpy.data.objects.new('Label | '+text,data)
    display_collection.objects.link(obj)
    obj.location=location
    return obj


tile_mats={'pebbles':solid_material('Presentation | sandstone',(.57,.44,.26)),
           'grass':solid_material('Presentation | meadow',(.39,.46,.18)),
           'moss':solid_material('Presentation | moss stone',(.50,.49,.32))}
board_mat=solid_material('Presentation | warm paper',(.80,.79,.70))
display_box('Catalog backdrop',(8,3.7,-.18),(19.1,9.8,.10),board_mat,.08)
label('T I L E   D E T A I L S',(8,7.75,-.119),.36,True)
label('Pebbles, meadow grass & moss  /  modular edge dressing',(8,7.30,-.119),.16,False,secondary)
for style,offset in [('pebbles',0),('grass',6.0),('moss',12.0)]:
    label(style.upper(),(offset+1.95,6.65,-.119),.24,True)
    label('four variations per layout',(offset+1.95,6.33,-.119),.115,False,secondary)

catalog=[]
objects=[]
for style_index,style in enumerate(('pebbles','grass','moss')):
    for row,(config,mask,edges,title) in enumerate(CONFIGS):
        for variation in range(4):
            rng=random.Random(44071+style_index*17003+row*713+variation*101)
            builder=MeshBuilder()
            focus=make_decoration(builder,style,config,variation,rng)
            name=f'tile_{style}_{config}_{variation+1:02d}'
            obj=builder.object(name,materials[style])
            obj.location=(style_index*6+variation*1.30,(3-row)*1.57+.55,0)
            obj['style']=style
            obj['configuration']=config
            obj['edge_mask']=mask
            obj['variation']=variation+1
            obj['placement']='Origin at tile center on supporting top; preserve source scale.'
            obj.asset_mark()
            obj.asset_data.description=f'{style.title()} / {title.lower()} / variation {variation+1}. 1x1 tile, Z=0 base.'
            for tag in ('Tile decorations',style,config,'Sokoban'):
                obj.asset_data.tags.new(tag)
            objects.append(obj)
            display_box('Display tile | '+name,(obj.location.x,obj.location.y,-.065),
                        (1.05,1.05,.13),tile_mats[style],.045)
            label(f'{title}  /  {variation+1:02d}',(obj.location.x,obj.location.y-.75,-.119),.105,False,secondary)
            obj.data.calc_loop_triangles()
            coords=[v.co for v in obj.data.vertices]
            catalog.append({'id':name,'style':style,'configuration':config,'variant':variation+1,
                            'edge_mask':mask,'focal_point':focus,'focal_points':builder.focal_points,
                            'model':f'assets/custom/models/tile_decorations/{name}.glb',
                            'triangles':len(obj.data.loop_triangles),'vertex_count':len(coords),
                            'bounds':{'min':[min(p[i] for p in coords) for i in range(3)],
                                      'max':[max(p[i] for p in coords) for i in range(3)]}})

label('48 MESHES     /     1 × 1 TILE     /     ROTATE IN QUARTER TURNS',(8,-.86,-.119),.14,True,secondary)

# Soft orthographic studio lighting, set up in the native file for re-rendering.
world=bpy.data.worlds.new('TileDetails | studio')
world.use_nodes=True
background=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
background.inputs['Color'].default_value=(.76,.82,.88,1)
background.inputs['Strength'].default_value=.5
scene.world=world


def area_light(name,location,power,size):
    data=bpy.data.lights.new(name,'AREA')
    data.energy=power
    data.shape='DISK'
    data.size=size
    obj=bpy.data.objects.new(name,data)
    display_collection.objects.link(obj)
    obj.location=location
    obj.rotation_euler=(Vector((8,3,0))-obj.location).to_track_quat('-Z','Y').to_euler()
    return obj


area_light('Studio | broad key',(-2,-1,10),1600,8)
area_light('Studio | sky fill',(11,5,8),950,7)
camera_data=bpy.data.cameras.new('TileDetails camera')
camera=bpy.data.objects.new('TileDetails camera',camera_data)
display_collection.objects.link(camera)
target=Vector((7.95,3.52,0))
camera.location=target+Vector((0,-7.7,19))
camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO'
camera.data.ortho_scale=19.2
scene.camera=camera
scene.render.engine='BLENDER_EEVEE'
scene.render.resolution_x=3200
scene.render.resolution_y=1800
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.filepath=str(PREVIEWS/'tile_decorations_catalog.png')
scene.render.film_transparent=False
scene.view_settings.view_transform='AgX'

# Export actual Blender meshes, never display tiles, type, lights or offsets.
for obj in objects:
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active=obj
    display_offset=obj.location.copy()
    obj.location=(0,0,0)
    export_path=MODELS/(obj.name+'.glb')
    bpy.ops.export_scene.gltf(filepath=str(export_path),
                             export_format='GLB',use_selection=True,use_active_scene=True,
                             export_animations=False,export_normals=True,
                             export_texcoords=True,export_yup=True,
                             export_materials='EXPORT')
    obj.location=display_offset
    payload=export_path.read_bytes()
    json_length=struct.unpack_from('<I',payload,12)[0]
    exported=json.loads(payload[20:20+json_length])
    record=next(a for a in catalog if a['id']==obj.name)
    record['source_vertex_count']=record['vertex_count']
    record['vertex_count']=sum(exported['accessors'][p['attributes']['POSITION']]['count']
                               for m in exported['meshes'] for p in m['primitives'])

manifest={'version':1,'revision':'Varied simple stones and moss mats with separate single grass blades',
          'tile_size':1,'coordinates':'Blender X/Y horizontal, Z up; origin at tile center and surface',
          'north':'-Y','placement_z':0,'preserveSourceScale':True,
          'configurations':{name:{'edge_mask':mask,'edges':[('N','E','S','W')[i] for i in edges]}
                            for name,mask,edges,title in CONFIGS},'assets':catalog}
(KIT/'catalog.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
bpy.ops.object.select_all(action='DESELECT')
bpy.context.view_layer.objects.active=objects[0]
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.shading.type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE/'tile_decorations.blend'))
print(json.dumps({'assets':len(catalog),'triangles_min':min(a['triangles'] for a in catalog),
                  'triangles_max':max(a['triangles'] for a in catalog),'blend':bpy.data.filepath}))
