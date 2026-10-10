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
assets_collection=next((collection for collection in SCENE.collection.children
                        if collection.name.startswith('Decoration assets |')),None)
if assets_collection is None:
    raise RuntimeError('The native tile-decoration asset collection is missing.')
assets_collection.name='Decoration assets | 48 meshes'

# Reuse the actual authoring definitions without running the new-scene setup.
source_path=ROOT/'tools/make_tile_decorations.py'
source=ast.parse(source_path.read_text(encoding='utf-8'))
definition_names={'solid_material','palette_material','MeshBuilder','edge_point','pebble','blade','leaf',
                  'decoration_focus','trail_lengths','trail_point','density_weight',
                  'trail_samples','inward_angle','make_pebbles','make_grass','moss_clump','make_moss',
                  'align_decoration_edges','make_opposite_edge_decoration','make_decoration'}
module=ast.Module(body=[node for node in source.body
                       if ((isinstance(node,(ast.ClassDef,ast.FunctionDef))
                            and node.name in definition_names)
                           or (isinstance(node,ast.Assign)
                               and any(isinstance(target,ast.Name) and target.id=='PALETTES'
                                       for target in node.targets)))],type_ignores=[])
exec(compile(module,str(source_path),'exec'),globals())

catalog_path=KIT/'catalog.json'
catalog=json.loads(catalog_path.read_text(encoding='utf-8'))
configurations=['edge','corner','strip','end']
styles=('pebbles','grass','moss')
existing_ids={entry['id'] for entry in catalog['assets']}
new_ids=[]
for style in styles:
    for config in configurations:
        for variant in range(1,5):
            name=f'tile_{style}_{config}_{variant:02d}'
            if name not in existing_ids:
                catalog['assets'].append({'id':name,'style':style,'configuration':config,
                                          'variant':variant,
                                          'edge_mask':catalog['configurations'][config]['edge_mask'],
                                          'model':f'assets/custom/models/tile_decorations/{name}.glb'})
                new_ids.append(name)
materials={style:bpy.data.materials.get('TileDetails_'+style) for style in styles}
for style,material in tuple(materials.items()):
    if material is None:
        materials[style]=palette_material(style)

for entry in catalog['assets']:
    obj=bpy.data.objects.get(entry['id'])
    style_index=styles.index(entry['style'])
    row=configurations.index(entry['configuration'])
    variation=entry['variant']-1
    rng=random.Random(44071+style_index*17003+row*713+variation*101)
    builder=MeshBuilder()
    entry['focal_point']=make_decoration(builder,entry['style'],entry['configuration'],variation,rng)
    entry['focal_points']=builder.focal_points
    if obj is None:
        obj=builder.object(entry['id'],materials[entry['style']])
        obj.location=(style_index*6+variation*1.30,(3-row)*1.57+.55,0)
        obj['style']=entry['style']
        obj['configuration']=entry['configuration']
        obj['edge_mask']=entry['edge_mask']
        obj['variation']=entry['variant']
        obj['placement']='Origin at tile center on supporting top; preserve source scale.'
        obj.asset_mark()
        obj.asset_data.description=(f'{entry["style"].title()} / {entry["configuration"]} / '
                                    f'variation {entry["variant"]}. 1x1 tile, Z=0 base.')
        for tag in ('Tile decorations',entry['style'],entry['configuration'],'Sokoban'):
            obj.asset_data.tags.new(tag)
        new_mesh=obj.data
    else:
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

catalog['revision']='Pebbles, grass and moss perimeter clusters with an open opposite-edge center'
catalog_path.write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8')
bpy.ops.object.select_all(action='DESELECT')
bpy.context.view_layer.objects.active=bpy.data.objects[catalog['assets'][0]['id']]
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/custom/source/tile_decorations.blend'))
print(json.dumps({'updated_assets':len(catalog['assets']),'new_assets':new_ids,
                  'min_triangles':min(a['triangles'] for a in catalog['assets']),
                  'max_triangles':max(a['triangles'] for a in catalog['assets'])}))
