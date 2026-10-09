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
assets_collection = bpy.data.collections.new('Decoration assets | 32 meshes')
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
    'pebbles': [(0.59,.60,.46), (.67,.67,.52), (.74,.73,.58), (.79,.77,.62),
                (.65,.64,.51), (.72,.70,.55), (.83,.80,.66), (.60,.63,.52)],
    'grass': [(.31,.46,.075), (.42,.58,.10), (.53,.65,.15), (.65,.73,.24),
              (.24,.43,.065), (.37,.55,.08), (.59,.69,.20), (.73,.78,.31)]}


def palette_material(style):
    colors = PALETTES[style]
    image = bpy.data.images.new('TileDetails_' + style + '_palette', width=128, height=16, alpha=False)
    pixels = []
    for y in range(16):
        for x in range(128):
            color = colors[x // 16]
            # Quiet painted variation, with each swatch safely isolated in UV0.
            shade = .97 + .035 * y / 15 + .008 * math.sin(x * .8 + y)
            pixels.extend([min(1, c * shade) for c in color] + [1])
    image.pixels.foreach_set(pixels)
    image.filepath_raw = str(KIT / (style + '_palette.png'))
    image.file_format = 'PNG'
    image.save()
    image.pack()
    mat = solid_material('TileDetails_' + style, (.6,.6,.6), .91 if style == 'pebbles' else .85)
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
    n = rng.choice([7, 8, 9])
    elongation = rng.uniform(.73, 1.14)
    rotation = rng.uniform(0, math.tau)
    radial = [rng.uniform(.90, 1.10) for _ in range(n)]
    verts = []
    for ring, (scale, z) in enumerate(((.63, -.008), (1, .20 * height),
                                      (.91, .66 * height), (.53, .94 * height))):
        for j in range(n):
            angle = j * math.tau / n + rotation + .035 * ring
            z_jitter = rng.uniform(-.035, .035) * height if ring else 0
            verts.append((x + math.cos(angle) * radius * radial[j] * scale,
                          y + math.sin(angle) * radius * elongation * radial[j] * scale,
                          z + z_jitter))
    verts.append((x + .08 * radius, y - .07 * radius, height))
    faces, swatches = [tuple(reversed(range(n)))], [max(0, swatch - 1)]
    for ring in range(3):
        for j in range(n):
            faces.append((ring*n+j, ring*n+(j+1)%n, (ring+1)*n+(j+1)%n, (ring+1)*n+j))
            swatches.append(swatch if ring < 2 else min(7, swatch+1))
    for j in range(n):
        faces.append((3*n+j, 3*n+(j+1)%n, 4*n))
        swatches.append(min(7, swatch + (j%3 == 0)))
    builder.island(verts, faces, swatches)


def blade(builder, x, y, height, width, angle, lean, swatch):
    """Closed, curved, folded blade; real volume rather than alpha cards."""
    forward = Vector((math.cos(angle), math.sin(angle), 0))
    side = Vector((-math.sin(angle), math.cos(angle), 0))
    verts = []
    # Three diamond sections, then a tip. Broadest near the lower middle.
    for t, profile in ((0, .30), (.38, 1), (.74, .58)):
        center = Vector((x,y,-.006 + height*t)) + forward*(lean*t*t)
        halfwidth = width*profile*.5
        ridge = max(.0012, width*.11)*profile
        verts.extend([tuple(center-side*halfwidth), tuple(center+forward*ridge),
                      tuple(center+side*halfwidth), tuple(center-forward*ridge)])
    verts.append((x+forward.x*lean, y+forward.y*lean, height))
    faces, swatches = [(3,2,1,0)], [0]
    for ring in range(2):
        for j in range(4):
            faces.append((ring*4+j, ring*4+(j+1)%4, (ring+1)*4+(j+1)%4, (ring+1)*4+j))
            swatches.append(min(7, swatch + ring + (j%2)))
    for j in range(4):
        faces.append((8+j, 8+(j+1)%4, 12))
        swatches.append(min(7, swatch+2))
    builder.island(verts, faces, swatches)


def leaf(builder, x, y, length, width, angle, swatch):
    # Small oval ground leaves echo the broad leaves in the supplied reference.
    local = [(0,0,.003), (.22*length,-.44*width,.014),
             (.60*length,-.5*width,.023), (length,0,.035),
             (.60*length,.5*width,.023), (.22*length,.44*width,.014),
             (.53*length,0,.032), (.48*length,0,.005)]
    verts = [(x+u*math.cos(angle)-v*math.sin(angle),
              y+u*math.sin(angle)+v*math.cos(angle),z) for u,v,z in local]
    faces = [(j,(j+1)%6,6) for j in range(6)] + [((j+1)%6,j,7) for j in range(6)]
    builder.island(verts,faces,[swatch+(j%3==0) for j in range(6)]+[swatch]*6)


def decoration_focus(config, variation):
    shift=(-.018,.018,-.009,.009)[variation]
    if config=='corner':
        return (.337+shift*.45,-.337+shift*.25)
    if config=='strip':
        return (shift,shift*.35)
    return (shift,-.34+(-.006,.008,.004,-.003)[variation])


def trail_lengths(config, focus):
    x,y=focus
    if config=='edge':
        return (x+.415,.415-x)
    if config=='corner':
        return (x+.415,.415-y)
    if config=='strip':
        return (y+.415,.415-y)
    return (x+.34+.415-y,.34-x+.415-y)


def trail_point(config, focus, branch, distance):
    x,y=focus
    if config=='edge':
        return (x+branch*distance,y,branch,0)
    if config=='corner':
        return (x-distance,y,-1,0) if branch<0 else (x,y+distance,0,1)
    if config=='strip':
        return (x,y+branch*distance,0,branch)
    turn=x+.34 if branch<0 else .34-x
    if distance<=turn:
        return (x+branch*distance,y,branch,0)
    return (branch*.34,y+distance-turn,0,1)


def density_weight(config, variation, distance):
    width={'edge':.234,'corner':.306,'strip':.234,'end':.510}[config]
    width*=(1.0,1.08,.94,1.035)[variation]
    return math.exp(-((distance/width)**1.30))


def trail_samples(config, variation, rng):
    # One global peak. Spacing increases continuously along both tails,
    # including across corners; turning onto another edge never restarts it.
    focus=decoration_focus(config,variation)
    for branch,length in zip((-1,1),trail_lengths(config,focus)):
        distance=.023
        while distance<length:
            weight=density_weight(config,variation,distance)
            x,y,tx,ty=trail_point(config,focus,branch,distance)
            yield (x,y,tx,ty,weight,distance)
            distance+=(.030+.060*(1-weight))*rng.uniform(.92,1.08)


def inward_angle(x,y):
    return math.atan2(-y,-x) if abs(x)+abs(y)>.07 else math.pi/2


def make_pebbles(builder, config, variation, rng):
    focus=decoration_focus(config,variation)
    placed=[]

    def stone(x,y,radius,shade=None):
        if any(math.hypot(x-a,y-b)<.79*(radius+r) for a,b,r in placed):
            return False
        placed.append((x,y,radius))
        pebble(builder,x,y,radius,radius*rng.uniform(.80,1.10),rng,
               rng.choice([1,2,3,4,5]) if shade is None else shade)
        return True

    stone(*focus,.049,3)
    # A loose nucleus feeds into the same gradually thinning trail.
    for attempt in range(110):
        angle=rng.uniform(0,math.tau)
        radius=.116*math.sqrt(rng.random())
        x=focus[0]+math.cos(angle)*radius
        y=focus[1]+math.sin(angle)*radius*.80
        stone(x,y,rng.uniform(.018,.037)*(1-.25*radius/.116))
        if len(placed)>=14:
            break
    for x,y,tx,ty,weight,distance in trail_samples(config,variation,rng):
        width=.009+.038*weight
        # Blend one- and two-stone widths continuously instead of a row cutoff.
        for side in ((-1,1) if rng.random()<weight else (0,)):
            offset=side*width+rng.uniform(-.010,.010)
            size=(.009+.030*weight)*rng.uniform(.84,1.12)
            stone(x-ty*offset,y+tx*offset,size)


def make_grass(builder, config, variation, rng):
    focus=decoration_focus(config,variation)
    # Fewer, more spread-out core leaves leave breathing room at the focus.
    for j in range(15):
        angle=j*2.39996+rng.uniform(-.20,.20)
        radius=.080*math.sqrt((j+.5)/15)
        x=focus[0]+math.cos(angle)*radius
        y=focus[1]+math.sin(angle)*radius*.84
        leaf(builder,x,y,rng.uniform(.078,.113),rng.uniform(.052,.078),
             inward_angle(x,y)+rng.uniform(-1.05,1.05),rng.choice([0,1,4,5]))
    # Taller blades emerge through the same core, not from separate clumps.
    for j in range(13):
        x=focus[0]+rng.uniform(-.070,.070)
        y=focus[1]+rng.uniform(-.050,.050)
        blade(builder,x,y,rng.uniform(.096,.161),rng.uniform(.024,.036),
              inward_angle(x,y)+rng.uniform(-1.7,1.7),rng.uniform(.035,.064),rng.choice([0,1,2,4]))
    for x,y,tx,ty,weight,distance in trail_samples(config,variation,rng):
        width=.005+.040*weight
        rows=[0]
        for side in (-1,1):
            if rng.random()<.84*weight:
                rows.append(side)
        for side in rows:
            offset=side*width+rng.uniform(-.009,.009)
            px,py=x-ty*offset,y+tx*offset
            leaf(builder,px,py,(.023+.069*weight)*rng.uniform(.88,1.10),
                 (.014+.043*weight)*rng.uniform(.88,1.10),
                 inward_angle(px,py)+rng.uniform(-1.12,1.12),rng.choice([0,1,4,5]))
        if rng.random()<.28+.64*weight:
            # Blade count, height, width and lean all fade with the same field.
            expected=1+1.7*weight
            count=int(expected)+int(rng.random()<expected-int(expected))
            fan=inward_angle(x,y)+rng.uniform(-1.05,1.05)
            for j in range(count):
                blade(builder,x+rng.uniform(-.011,.011),y+rng.uniform(-.011,.011),
                      (.035+.112*weight)*rng.uniform(.75,1.09),
                      (.009+.020*weight)*rng.uniform(.85,1.10),
                      fan+(j-(count-1)/2)*.53,
                      (.014+.041*weight)*rng.uniform(.85,1.10),rng.choice([0,1,2,4]))


def make_decoration(builder, style, config, variation, rng):
    (make_pebbles if style=='pebbles' else make_grass)(builder,config,variation,rng)
    return [*decoration_focus(config,variation),0]


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
           'grass':solid_material('Presentation | meadow',(.39,.46,.18))}
board_mat=solid_material('Presentation | warm paper',(.80,.79,.70))
display_box('Catalog backdrop',(5,3.7,-.18),(13.1,9.8,.10),board_mat,.08)
label('T I L E   D E T A I L S',(5,7.75,-.119),.36,True)
label('Pebbles & meadow grass  /  modular edge dressing',(5,7.30,-.119),.16,False,secondary)
for style,offset in [('pebbles',0),('grass',6.0)]:
    label(style.upper(),(offset+1.95,6.65,-.119),.24,True)
    label('four variations per layout',(offset+1.95,6.33,-.119),.115,False,secondary)

catalog=[]
objects=[]
for style_index,style in enumerate(('pebbles','grass')):
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
                            'edge_mask':mask,'focal_point':focus,
                            'model':f'assets/custom/models/tile_decorations/{name}.glb',
                            'triangles':len(obj.data.loop_triangles),'vertex_count':len(coords),
                            'bounds':{'min':[min(p[i] for p in coords) for i in range(3)],
                                      'max':[max(p[i] for p in coords) for i in range(3)]}})

label('32 MESHES     /     1 × 1 TILE     /     ROTATE IN QUARTER TURNS',(5,-.86,-.119),.14,True,secondary)

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
    obj.rotation_euler=(Vector((5,3,0))-obj.location).to_track_quat('-Z','Y').to_euler()
    return obj


area_light('Studio | broad key',(-2,-1,10),1600,8)
area_light('Studio | sky fill',(11,5,8),950,7)
camera_data=bpy.data.cameras.new('TileDetails camera')
camera=bpy.data.objects.new('TileDetails camera',camera_data)
display_collection.objects.link(camera)
target=Vector((4.95,3.52,0))
camera.location=target+Vector((0,-7.7,19))
camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO'
camera.data.ortho_scale=13.2
scene.camera=camera
scene.render.engine='BLENDER_EEVEE'
scene.render.resolution_x=2560
scene.render.resolution_y=1920
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

manifest={'version':1,'revision':'Single focal cluster with softer taper and open center',
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
