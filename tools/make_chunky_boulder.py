"""Author the reference-inspired boulder in Blender, with a baked game material.

Run inside Blender's Python context using this file's path. Output paths are
relative to this checkout. Only this asset's scene is replaced.
The existing open project is not saved or altered on disk.
"""
import bpy
import bmesh
import math
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/custom/source/chunky_boulder.blend'
OUT = ROOT / 'assets/custom/models/chunky_boulder.glb'
ART = ROOT / 'assets/custom/source/chunky_boulder'
ART.mkdir(parents=True, exist_ok=True)


def enum(owner, prop, preferred):
    items = owner.bl_rna.properties[prop].enum_items
    ids = [i.identifier for i in items]
    for value in preferred:
        if value in ids:
            return value
    raise ValueError((prop, ids, preferred))


def srgb(hex_value):
    values = [int(hex_value[i:i+2], 16) / 255 for i in (0, 2, 4)]
    return tuple(v / 12.92 if v <= .04045 else ((v + .055) / 1.055)**2.4 for v in values) + (1,)


for prior in list(bpy.data.scenes):
    if prior.name.startswith('ChunkyBoulder_Studio'):
        if bpy.context.window.scene == prior:
            fallback = next((s for s in bpy.data.scenes if s != prior), None)
            bpy.context.window.scene = fallback or bpy.data.scenes.new('BoulderBuild_Empty')
        for ob in list(prior.objects):
            bpy.data.objects.remove(ob, do_unlink=True)
        bpy.data.scenes.remove(prior)
scene = bpy.data.scenes.new('ChunkyBoulder_Studio')
bpy.context.window.scene = scene
asset_collection = bpy.data.collections.new('ASSET_ChunkyBoulder')
scene.collection.children.link(asset_collection)
studio = bpy.data.collections.new('STUDIO_PreviewOnly')
scene.collection.children.link(studio)

# A single, closed, asymmetric mass. Unequal inset columns form the lower
# knuckles and deep clefts; the oversized crown remains quiet and broad.
angles = [0, 22, 47, 69, 92, 115, 138, 160, 182, 204, 227, 247, 265, 285, 310, 336]
variation = [1.00, .92, 1.01, 1.04, .92, 1.04, .95, 1.03, .96, 1.04, .98, .96, 1.04, .98, 1.03, .98]
levels = [(0.0, .77), (.10, .95), (.39, 1.04), (.72, .97), (.92, .76)]
mid_height = [.41,.33,.48,.36,.46,.50,.38,.42,.48,.31,.42,.52,.38,.49,.31,.45]
shoulder_height = [.77,.70,.80,.69,.74,.79,.67,.77,.81,.72,.65,.76,.80,.68,.75,.81]
top_width = [.98,.95,1.04,1.06,.88,1.0,1.13,1.04,.97,.91,1.02,.91,.85,1.07,1.12,1.01]
verts = []
for row, (z, radius) in enumerate(levels):
    for i, degrees in enumerate(angles):
        a = math.radians(degrees)
        x = math.copysign(abs(math.cos(a))**.48, math.cos(a))
        y = math.copysign(abs(math.sin(a))**.48, math.sin(a))
        r = radius * variation[i]
        if i in (10, 13, 1, 6):
            r *= [1.0, .91, .80, .91, 1.0][row]
        if row == 4:
            r *= top_width[i]
        if i in (11, 14) and row in (1, 2):
            r *= 1.025
        xx = x * .49 * r + [-.025, -.018, .015, -.025, -.055][row]
        yy = y * .445 * r + [.008, .005, -.018, .015, .045][row]
        zz = z
        if row == 1:
            zz += .018 * math.sin(a * 2.0 + row)
        if row == 2:
            zz = mid_height[i]
        if row == 3:
            zz = shoulder_height[i]
        if row == 4:
            zz += .045 * xx - .07 * yy
        verts.append((xx, yy, zz))
faces = [tuple(reversed(range(16)))]
for row in range(4):
    for i in range(16):
        j = (i + 1) % 16
        faces.append((row*16+i, row*16+j, (row+1)*16+j, (row+1)*16+i))
faces.append(tuple(64+i for i in range(16)))
mesh = bpy.data.meshes.new('ChunkyBoulder_ClosedSculpt')
mesh.from_pydata(verts, [], faces)
mesh.update()
rock = bpy.data.objects.new('ChunkyBoulder', mesh)
asset_collection.objects.link(rock)
bpy.context.view_layer.objects.active = rock
rock.select_set(True)
bm = bmesh.new()
bm.from_mesh(mesh)
bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
bm.to_mesh(mesh)
bm.free()

# Three broad, embedded knuckles break the silhouette and give the cool lower
# half the rounded, interlocking shapes seen in the reference. Boolean union
# makes these part of the same watertight mass rather than loose stones.
for index, (position, size) in enumerate([
    ((-.25,-.345,.275),(.45,.25,.50)),
    ((.245,-.345,.255),(.39,.26,.46)),
    ((.405,.17,.275),(.24,.40,.46)),
]):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=1)
    lobe = bpy.context.object
    lobe.name = 'Temporary_Knuckle_%d' % index
    for vertex in lobe.data.vertices:
        q = vertex.co.copy()
        q.x = math.copysign(abs(q.x)**.76,q.x)
        q.y = math.copysign(abs(q.y)**.76,q.y)
        q.z = math.copysign(abs(q.z)**.85,q.z)
        wobble = 1 + .035*math.sin(q.x*13+q.y*7+index)
        vertex.co = Vector((q.x*size[0]*.5*wobble+position[0],
                            q.y*size[1]*.5*wobble+position[1],
                            q.z*size[2]*.5*wobble+position[2]))
    for ob in bpy.context.selected_objects: ob.select_set(False)
    rock.select_set(True); bpy.context.view_layer.objects.active=rock
    union=rock.modifiers.new('Rooted lower knuckle %d' % index,'BOOLEAN')
    union.operation=enum(union,'operation',['UNION']); union.solver=enum(union,'solver',['EXACT'])
    union.object=lobe
    bpy.ops.object.modifier_apply(modifier=union.name)
    bpy.data.objects.remove(lobe,do_unlink=True)

# One short, tapered chisel scar across the crown, not a network of cracks.
# V-section cutters create a real concavity whose shadow survives rotation.
scar_path = [(-.31, -.28), (-.19, -.12), (-.12, -.025), (.015, .07), (.12, .27)]
cut_v, cut_f = [], []
for i, (x, y) in enumerate(scar_path):
    previous = Vector(scar_path[max(0, i-1)])
    following = Vector(scar_path[min(len(scar_path)-1, i+1)])
    tangent = (following-previous).normalized()
    normal = Vector((-tangent.y, tangent.x))
    width = [.005, .03, .037, .029, .004][i]
    top = .92 + .045*x - .07*y
    cut_v.extend([(x+normal.x*width, y+normal.y*width, top+.10),
                  (x-normal.x*width, y-normal.y*width, top+.10),
                  (x, y, top-.058)])
for i in range(len(scar_path)-1):
    a, b = i*3, (i+1)*3
    cut_f.extend([(a,b,b+1,a+1),(a+1,b+1,b+2,a+2),(a+2,b+2,b,a)])
cut_f.extend([(2,1,0),(12,13,14)])
cm = bpy.data.meshes.new('CrownScar_Cutter')
cm.from_pydata(cut_v, [], cut_f)
cm.update()
co = bpy.data.objects.new('CrownScar_Cutter', cm)
asset_collection.objects.link(co)
cbm = bmesh.new(); cbm.from_mesh(cm)
bmesh.ops.recalc_face_normals(cbm, faces=list(cbm.faces)); cbm.to_mesh(cm); cbm.free()
boolean = rock.modifiers.new('One carved crown scar', 'BOOLEAN')
boolean.operation = enum(boolean, 'operation', ['DIFFERENCE'])
boolean.object = co
boolean.solver = enum(boolean, 'solver', ['EXACT'])
bpy.ops.object.modifier_apply(modifier=boolean.name)
bpy.data.objects.remove(co, do_unlink=True)

bevel = rock.modifiers.new('Soft chisel edges', 'BEVEL')
bevel.width = .021
bevel.segments = 2
bevel.limit_method = enum(bevel, 'limit_method', ['ANGLE'])
bevel.angle_limit = .20
bevel.affect = enum(bevel, 'affect', ['EDGES'])
bpy.ops.object.modifier_apply(modifier=bevel.name)
for face in rock.data.polygons:
    face.use_smooth = True
weighted = rock.modifiers.new('Broad face normals', 'WEIGHTED_NORMAL')
weighted.keep_sharp = True
weighted.weight = 35
bpy.ops.object.modifier_apply(modifier=weighted.name)

# UV0 is a real, padded atlas, so the painted treatment works in the game.
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(55), island_margin=.035)
bpy.ops.object.mode_set(mode='OBJECT')
rock.data.uv_layers.active.name = 'BoulderPaintUV'

mat = bpy.data.materials.new('Boulder_PaintedTerracottaAndSlate')
mat.use_nodes = True
mat.diffuse_color = srgb('ad725d')
nodes = mat.node_tree.nodes
nodes.clear()
links = mat.node_tree.links
output = nodes.new('ShaderNodeOutputMaterial')
shader = nodes.new('ShaderNodeBsdfPrincipled')
shader.inputs['Roughness'].default_value = .92
shader.inputs['Metallic'].default_value = 0
links.new(shader.outputs['BSDF'], output.inputs['Surface'])
texcoord = nodes.new('ShaderNodeTexCoord')
noise = nodes.new('ShaderNodeTexNoise')
noise.inputs['Scale'].default_value = 4.8
noise.inputs['Detail'].default_value = 1.4
noise.inputs['Roughness'].default_value = .55
links.new(texcoord.outputs['Generated'], noise.inputs['Vector'])
separate = nodes.new('ShaderNodeSeparateXYZ')
links.new(texcoord.outputs['Generated'], separate.inputs['Vector'])
mul = nodes.new('ShaderNodeMath'); mul.operation = enum(mul, 'operation', ['MULTIPLY'])
mul.inputs[1].default_value = .23
links.new(noise.outputs['Fac'], mul.inputs[0])
add = nodes.new('ShaderNodeMath'); add.operation = enum(add, 'operation', ['ADD'])
links.new(separate.outputs['Z'], add.inputs[0]); links.new(mul.outputs[0], add.inputs[1])
ramp = nodes.new('ShaderNodeValToRGB')
colors = [(0.0,'283751'),(.27,'34486e'),(.43,'465b81'),(.60,'63556a'),(.72,'9e6257'),(.94,'bc7861'),(1.15,'d59472')]
cr = ramp.color_ramp
cr.elements.remove(cr.elements[1])
cr.elements[0].position = colors[0][0]
cr.elements[0].color = srgb(colors[0][1])
for pos, color in colors[1:]:
    e = cr.elements.new(min(pos/1.15, 1)); e.color = srgb(color)
scale = nodes.new('ShaderNodeMath'); scale.operation = enum(scale,'operation',['DIVIDE'])
scale.inputs[1].default_value = 1.15
links.new(add.outputs[0], scale.inputs[0]); links.new(scale.outputs[0], ramp.inputs['Fac'])
cr.interpolation = enum(cr,'interpolation',['EASE'])
cloud_ramp = nodes.new('ShaderNodeValToRGB')
cloud_ramp.color_ramp.elements[0].position = .24
cloud_ramp.color_ramp.elements[0].color = (.62,.62,.62,1)
cloud_ramp.color_ramp.elements[1].position = .72
cloud_ramp.color_ramp.elements[1].color = (1,1,1,1)
links.new(noise.outputs['Fac'], cloud_ramp.inputs['Fac'])
mix = nodes.new('ShaderNodeMixRGB'); mix.blend_type = enum(mix,'blend_type',['MULTIPLY']); mix.inputs[0].default_value = .38
links.new(ramp.outputs['Color'], mix.inputs[1]); links.new(cloud_ramp.outputs['Color'], mix.inputs[2])
patches = nodes.new('ShaderNodeTexVoronoi')
patches.inputs['Scale'].default_value = 4.2
patches.inputs['Randomness'].default_value = .82
links.new(texcoord.outputs['Generated'], patches.inputs['Vector'])
patch_gray = nodes.new('ShaderNodeRGBToBW'); links.new(patches.outputs['Color'],patch_gray.inputs[0])
patch_ramp = nodes.new('ShaderNodeValToRGB')
patch_ramp.color_ramp.elements[0].position=.24
patch_ramp.color_ramp.elements[0].color=(.60,.58,.62,1)
patch_ramp.color_ramp.elements[1].position=.72
patch_ramp.color_ramp.elements[1].color=(1.16,1.10,1.05,1)
links.new(patch_gray.outputs[0],patch_ramp.inputs['Fac'])
paint=nodes.new('ShaderNodeMixRGB'); paint.blend_type=enum(paint,'blend_type',['MULTIPLY'])
paint.inputs[0].default_value=.30
links.new(mix.outputs[0],paint.inputs[1]); links.new(patch_ramp.outputs[0],paint.inputs[2])
links.new(paint.outputs[0], shader.inputs['Base Color'])
rock.data.materials.append(mat)

image = bpy.data.images.new('Boulder_Albedo_1024', width=1024, height=1024, alpha=False)
image.filepath_raw = str(ART / 'boulder_albedo.png')
image.file_format = enum(image,'file_format',['PNG'])
image.colorspace_settings.name = 'sRGB'
target = nodes.new('ShaderNodeTexImage'); target.image = image
target.label = 'BAKED GAME ALBEDO — UV0'
nodes.active = target
emission = nodes.new('ShaderNodeEmission')
links.new(paint.outputs[0], emission.inputs['Color'])
links.new(emission.outputs[0], output.inputs['Surface'])
try:
    scene.render.engine = 'CYCLES'
except TypeError as error:
    raise RuntimeError(str(error))
scene.cycles.samples = 1
scene.render.bake.margin = 16
bake_type = next(i.identifier for i in bpy.ops.object.bake.get_rna_type().properties['type'].enum_items if i.name == 'Emission')
bpy.ops.object.bake(type=bake_type)
image.save()
image.pack()
links.new(shader.outputs['BSDF'], output.inputs['Surface'])
links.new(target.outputs['Color'], shader.inputs['Base Color'])
# Leave the palette group visible for source editing, disconnected from runtime.
for node in nodes:
    if node not in (output, shader, target):
        node.label = 'AUTHORING | ' + node.bl_label
target.location = (-300,200); shader.location=(0,200); output.location=(330,200)
rock['asset_notes'] = 'One chunky mass; warm terracotta crown, cool slate lower faces; matte painted surface.'
rock['export_notes'] = 'Origin centered at the flat base. Z up. One selected mesh, applied transforms; stage excluded.'

def simple_mat(name, color, roughness=1):
    m = bpy.data.materials.new(name); m.use_nodes=True; m.diffuse_color=srgb(color)
    s=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    s.inputs['Base Color'].default_value=srgb(color); s.inputs['Roughness'].default_value=roughness
    return m

def move_to_collection(ob, collection):
    for c in list(ob.users_collection): c.objects.unlink(ob)
    collection.objects.link(ob)

bpy.ops.mesh.primitive_plane_add(size=200, location=(0,0,-.008))
ground=bpy.context.object; ground.name='Studio_SandBackdrop'; move_to_collection(ground, studio)
ground.data.materials.append(simple_mat('Studio_WarmSand','d8c4a3'))

world = bpy.data.worlds.new('BoulderStudio_World'); world.use_nodes=True; scene.world=world
background=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
background.inputs['Color'].default_value=srgb('929fbc'); background.inputs['Strength'].default_value=.45

def area(name, location, power, size, color):
    data=bpy.data.lights.new(name,enum(bpy.types.Light,'type',['AREA']))
    data.energy=power; data.shape=enum(data,'shape',['DISK']); data.size=size
    data.color=srgb(color)[:3]
    ob=bpy.data.objects.new(name,data); studio.objects.link(ob); ob.location=location
    ob.rotation_euler=(Vector((0,0,.45))-ob.location).to_track_quat('-Z','Y').to_euler()
    return ob
area('Studio_PeachKey',(-3,-4,6),420,4,'ffe1c1')
area('Studio_CoolFill',(4,-1,3),160,3,'bccfff')
area('Studio_SoftRim',(1,4,5),280,3,'ffddb4')
camera_data=bpy.data.cameras.new('Studio_Camera')
camera=bpy.data.objects.new('Studio_Camera',camera_data); studio.objects.link(camera)
camera.location=(2.3,-3.5,2.65)
camera.rotation_euler=(Vector((0,0,.46))-camera.location).to_track_quat('-Z','Y').to_euler()
camera_data.type=enum(camera_data,'type',['ORTHO']); camera_data.ortho_scale=1.70
scene.camera=camera
scene.render.resolution_x=1000; scene.render.resolution_y=1000; scene.render.resolution_percentage=100
scene.cycles.samples=48
scene.cycles.use_denoising=True
scene.render.image_settings.file_format=enum(scene.render.image_settings,'file_format',['PNG'])
scene.render.film_transparent=False
try: scene.view_settings.view_transform='AgX'
except TypeError: pass
scene.render.filepath=str(ART/'preview.png')

for ob in bpy.context.selected_objects: ob.select_set(False)
rock.select_set(True); bpy.context.view_layer.objects.active=rock
triangulate=rock.modifiers.new('Game triangles','TRIANGULATE')
triangulate.keep_custom_normals=True
bpy.ops.object.modifier_apply(modifier=triangulate.name)
cleanup=bmesh.new(); cleanup.from_mesh(rock.data)
bmesh.ops.dissolve_degenerate(cleanup,dist=1e-6,edges=list(cleanup.edges))
bmesh.ops.remove_doubles(cleanup,verts=list(cleanup.verts),dist=1e-7)
bmesh.ops.triangulate(cleanup,faces=list(cleanup.faces))
bad=[f for f in cleanup.faces if f.calc_area()<1e-12]
if bad: bmesh.ops.delete(cleanup,geom=bad,context='FACES_ONLY')
bmesh.ops.recalc_face_normals(cleanup,faces=list(cleanup.faces))
cleanup.to_mesh(rock.data); cleanup.free(); rock.data.update()
final_normals=rock.modifiers.new('Clean export normals','WEIGHTED_NORMAL')
final_normals.weight=35; final_normals.keep_sharp=True
bpy.ops.object.modifier_apply(modifier=final_normals.name)
for screen in bpy.data.screens:
    for ar in screen.areas:
        if ar.type=='VIEW_3D':
            ar.spaces.active.region_3d.view_perspective=enum(ar.spaces.active.region_3d,'view_perspective',['CAMERA'])
            ar.spaces.active.shading.type=enum(ar.spaces.active.shading,'type',['MATERIAL'])

# Query operator enums instead of assuming version-specific identifiers.
op = bpy.ops.export_scene.gltf.get_rna_type()
materials = next(i.identifier for i in op.properties['export_materials'].enum_items if i.identifier=='EXPORT')
bpy.ops.export_scene.gltf(filepath=str(OUT), use_selection=True, use_active_scene=True,
    export_apply=True, export_yup=True, export_normals=True, export_texcoords=True,
    export_tangents=False, export_materials=materials, export_animations=False)
SOURCE.parent.mkdir(parents=True,exist_ok=True)
bpy.data.libraries.write(str(SOURCE),{scene},fake_user=True,compress=True)
print(json.dumps({'source':str(SOURCE),'export':str(OUT),'preview':scene.render.filepath,
 'vertices':len(rock.data.vertices),'faces':len(rock.data.polygons),'dimensions':list(rock.dimensions)},indent=2))

# Promote the isolated scene library to an ordinary editable .blend, without
# pulling the previously open ground-module project into this asset's source.
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene = bpy.data.scenes['ChunkyBoulder_Studio']
bpy.context.window.scene = scene
rock = bpy.data.objects['ChunkyBoulder']
bpy.context.view_layer.objects.active = rock
for ob in scene.objects: ob.select_set(False)
rock.select_set(True)
for datablocks in (bpy.data.collections, bpy.data.meshes, bpy.data.materials,
                   bpy.data.images, bpy.data.worlds):
    for data in datablocks:
        if data.name.rsplit('.', 1)[-1].isdigit():
            data.name = data.name.rsplit('.', 1)[0]
for screen in bpy.data.screens:
    for ar in screen.areas:
        if ar.type == 'VIEW_3D':
            space = ar.spaces.active
            space.region_3d.view_perspective = enum(space.region_3d, 'view_perspective', ['CAMERA'])
            space.shading.type = enum(space.shading, 'type', ['MATERIAL'])
            space.shading.use_scene_lights = True
            space.shading.use_scene_world = True
            space.overlay.show_overlays = False
old_save_version = bpy.context.preferences.filepaths.save_version
try:
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE))
finally:
    bpy.context.preferences.filepaths.save_version = old_save_version
