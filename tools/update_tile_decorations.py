"""Refresh the existing native kit from its current Blender authoring functions.

Run in the open tile_decorations.blend. Keeps object names, asset metadata,
presentation, transforms and shared close-up meshes; updates geometry/exports.
"""
import ast
import contextlib
import io
import json
import math
from pathlib import Path
import random
import struct
import bpy
import bmesh
from mathutils import Vector

ROOT=Path(__file__).resolve().parent.parent
KIT=ROOT/'assets/custom/source/tile_decorations'
SCENE=bpy.data.scenes.get('TileDecorations')
if SCENE is None:
    raise RuntimeError('Open assets/custom/source/tile_decorations.blend first.')
bpy.context.window.scene=SCENE
assets_collection=SCENE.collection.children['Decoration assets | 32 meshes']

# Reuse the actual authoring definitions without running the new-scene setup.
source_path=ROOT/'tools/make_tile_decorations.py'
source=ast.parse(source_path.read_text(encoding='utf-8'))
definition_names={'MeshBuilder','edge_point','pebble','blade','leaf',
                  'decoration_focus','trail_lengths','trail_point','density_weight',
                  'trail_samples','inward_angle','make_pebbles','make_grass','make_decoration'}
module=ast.Module(body=[node for node in source.body
                       if isinstance(node,(ast.ClassDef,ast.FunctionDef))
                       and node.name in definition_names],type_ignores=[])
exec(compile(module,str(source_path),'exec'),globals())

catalog_path=KIT/'catalog.json'
catalog=json.loads(catalog_path.read_text(encoding='utf-8'))
configurations=['edge','corner','strip','end']
for entry in catalog['assets']:
    obj=bpy.data.objects[entry['id']]
    style_index=('pebbles','grass').index(entry['style'])
    row=configurations.index(entry['configuration'])
    variation=entry['variant']-1
    rng=random.Random(44071+style_index*17003+row*713+variation*101)
    builder=MeshBuilder()
    entry['focal_point']=make_decoration(builder,entry['style'],entry['configuration'],variation,rng)
    fresh=builder.object('__updated_'+obj.name,obj.data.materials[0])
    old_mesh=obj.data
    old_name=old_mesh.name
    new_mesh=fresh.data
    # Native close-up instances share the same mesh datablocks.
    for linked in bpy.data.objects:
        if linked.type=='MESH' and linked.data==old_mesh:
            linked.data=new_mesh
    bpy.data.objects.remove(fresh,do_unlink=True)
    if old_mesh.users==0:
        bpy.data.meshes.remove(old_mesh)
    new_mesh.name=old_name
    new_mesh.calc_loop_triangles()
    coords=[v.co for v in new_mesh.vertices]
    entry['triangles']=len(new_mesh.loop_triangles)
    entry['source_vertex_count']=len(coords)
    entry['bounds']={'min':[min(p[i] for p in coords) for i in range(3)],
                     'max':[max(p[i] for p in coords) for i in range(3)]}
    assert all(-.5<=v.co.x<=.5 and -.5<=v.co.y<=.5 for v in new_mesh.vertices),obj.name
    bm=bmesh.new()
    bm.from_mesh(new_mesh)
    assert all(e.is_manifold for e in bm.edges),obj.name
    assert all(v.link_edges for v in bm.verts),obj.name
    bm.free()

with contextlib.redirect_stdout(io.StringIO()):
    for entry in catalog['assets']:
        obj=bpy.data.objects[entry['id']]
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active=obj
        offset=obj.location.copy()
        path=ROOT/entry['model']
        try:
            obj.location=(0,0,0)
            bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',
                                     use_selection=True,use_active_scene=True,
                                     export_animations=False,export_normals=True,
                                     export_texcoords=True,export_yup=True,
                                     export_materials='EXPORT')
        finally:
            obj.location=offset
        payload=path.read_bytes()
        length=struct.unpack_from('<I',payload,12)[0]
        exported=json.loads(payload[20:20+length])
        assert len(exported['meshes'])==len(exported['nodes'])==1,obj.name
        entry['vertex_count']=sum(exported['accessors'][p['attributes']['POSITION']]['count']
                                  for m in exported['meshes'] for p in m['primitives'])

catalog['revision']='Single focal cluster with softer taper and open center'
catalog_path.write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8')
bpy.ops.object.select_all(action='DESELECT')
bpy.context.view_layer.objects.active=bpy.data.objects[catalog['assets'][0]['id']]
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/custom/source/tile_decorations.blend'))
print(json.dumps({'updated_assets':len(catalog['assets']),
                  'min_triangles':min(a['triangles'] for a in catalog['assets']),
                  'max_triangles':max(a['triangles'] for a in catalog['assets'])}))
