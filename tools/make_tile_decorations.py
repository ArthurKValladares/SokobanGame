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
    'pebbles': [(.24,.27,.23), (.38,.40,.30), (.46,.50,.47), (.56,.53,.42),
                (.65,.57,.40), (.68,.71,.63), (.80,.75,.59), (.88,.86,.74)],
    'grass': [(.31,.46,.075), (.42,.58,.10), (.53,.65,.15), (.65,.73,.24),
              (.24,.43,.065), (.37,.55,.08), (.59,.69,.20), (.73,.78,.31)],
    'moss': [(.25,.40,.055), (.34,.48,.075), (.43,.56,.105), (.50,.61,.14),
             (.38,.52,.08), (.57,.66,.18), (.85,.82,.36), (.96,.91,.54)]}


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
    n = rng.choice([6,7,8,9]) if shape in ('rounded','river') else rng.choice([5,6,7])
    elongation = rng.uniform(.48,.74) if shape=='river' else rng.uniform(.70,1.23)
    rotation = rng.uniform(0, math.tau)
    radial = [rng.uniform(.76,1.20) for _ in range(n)]
    if shape=='angular':
        radial[rng.randrange(n)]*=.65
    shoulder={'rounded':(.92,.58),'slab':(.97,.81),'wedge':(.83,.47),
              'angular':(.90,.67),'river':(.95,.63)}[shape]
    top_offset=(rng.uniform(-.28,.28)*radius,rng.uniform(-.24,.24)*radius)
    slope=rng.uniform(-.20,.20) if shape=='wedge' else rng.uniform(-.07,.07)
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
            swatches.append(min(7,swatch+1) if ring==2 and rng.random()<.18 else swatch)
    for j in range(n):
        faces.append((3*n+j, 3*n+(j+1)%n, 4*n))
        swatches.append(min(7,swatch+1) if rng.random()<.20 else swatch)
    builder.island(verts, faces, swatches)


def blade(builder, x, y, height, width, angle, lean, swatch,
          *, bend=2.0, twist=0.0, taper=.58, base=.30):
    """A solid folded blade with independently shaped bend and taper."""
    forward = Vector((math.cos(angle), math.sin(angle), 0))
    verts = []
    # Closed diamond sections preserve thickness even on the narrow reeds.
    # Rotating each section gently changes the folded silhouette as it rises.
    for t, profile in ((0, base), (.38, 1), (.74, taper)):
        direction=angle+twist*t
        section_forward=Vector((math.cos(direction),math.sin(direction),0))
        side=Vector((-math.sin(direction),math.cos(direction),0))
        center = Vector((x,y,-.006+(height+.006)*t)) + forward*(lean*t**bend)
        halfwidth = width*profile*.5
        ridge = max(.0012, width*.11)*profile
        verts.extend([tuple(center-side*halfwidth), tuple(center+section_forward*ridge),
                      tuple(center+side*halfwidth), tuple(center-section_forward*ridge)])
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


def leaf(builder, x, y, length, width, angle, swatch,
         *, lift=.035, fullness=.60, cup=.15):
    """Closed oval-to-lance leaf with a raised fold and a solid underside."""
    local = [(0,0,.003), (.22*length,-.44*width,.003+.31*lift),
             (fullness*length,-.5*width,.003+.57*lift), (length,0,.003+.90*lift),
             (fullness*length,.5*width,.003+.57*lift), (.22*length,.44*width,.003+.31*lift),
             (.50*length,0,.003+(.82+cup)*lift), (.48*length,0,.0045)]
    verts = [(x+u*math.cos(angle)-v*math.sin(angle),
              y+u*math.sin(angle)+v*math.cos(angle),z) for u,v,z in local]
    faces = [(j,(j+1)%6,6) for j in range(6)] + [((j+1)%6,j,7) for j in range(6)]
    builder.island(verts,faces,[min(7,swatch+(j%3==0)) for j in range(6)]+[min(7,swatch)]*6)


def make_pebbles(builder, rng):
    """One stone per site; variation changes the stone, never the density."""
    radius=rng.uniform(.023,.039)
    height=rng.uniform(.013,.027) if rng.random()<.40 else rng.uniform(.028,.052)
    pebble(builder,0,0,radius,height,rng,rng.randrange(8))


def make_grass(builder, rng):
    """Mix upright, spreading, reed, windswept, and broad-leaf silhouettes."""
    # Blade/leaf counts, tallest height, width, lean, fan, bend, root spread.
    # Heights remain independent of the later horizontal footprint fitting.
    profiles=(
        ((2,4),(0,1),(.082,.138),(.013,.024),(.012,.036),.90,(1.8,2.8),.007),
        ((4,7),(0,2),(.044,.088),(.017,.029),(.042,.080),2.60,(1.15,1.8),.015),
        ((3,6),(0,1),(.150,.190),(.0105,.016),(.008,.027),.66,(2.0,3.1),.006),
        ((3,6),(0,2),(.096,.165),(.014,.025),(.055,.100),.64,(1.25,2.2),.011),
        ((2,4),(2,4),(.060,.106),(.021,.034),(.030,.062),2.10,(1.4,2.4),.018),
    )
    kind=rng.randrange(len(profiles))
    counts,leaf_counts,heights,widths,leans,fan,bends,root_spread=profiles[kind]
    count=rng.randint(*counts)
    leaf_count=rng.randint(*leaf_counts)
    tallest=rng.uniform(*heights)
    leader=rng.randrange(count)
    heading=math.pi/2+rng.uniform(-.42,.42)
    if kind==3:
        heading+=rng.uniform(-.45,.45)
    for j in range(count):
        root_angle=rng.uniform(0,math.tau)
        root_radius=root_spread*math.sqrt(rng.random())
        x=math.cos(root_angle)*root_radius
        y=math.sin(root_angle)*root_radius*.48
        height=tallest if j==leader else max(.040,tallest*rng.uniform(.55,.92))
        angle=heading+(j/(count-1)-.5)*fan+rng.uniform(-.16,.16)
        taper=rng.uniform(.31,.49) if kind==2 else rng.uniform(.43,.73)
        blade(builder,x,y,height,rng.uniform(*widths),angle,rng.uniform(*leans),
              rng.choice([0,1,2,4]),bend=rng.uniform(*bends),
              twist=rng.uniform(-.27,.27),taper=taper,base=rng.uniform(.24,.43))
    for j in range(leaf_count):
        if kind==4:
            length,width=rng.uniform(.055,.100),rng.uniform(.033,.064)
            lift=rng.uniform(.020,.050)
            angle=heading+(j/max(1,leaf_count-1)-.5)*3.8+rng.uniform(-.24,.24)
        elif kind==2:
            length,width=rng.uniform(.035,.055),rng.uniform(.015,.025)
            lift=rng.uniform(.015,.030)
            angle=heading+rng.uniform(-1.0,1.0)
        else:
            length,width=rng.uniform(.040,.075),rng.uniform(.020,.040)
            lift=rng.uniform(.012,.030)
            angle=heading+rng.uniform(-.60,.60) if kind==3 else heading+rng.uniform(-1.5,1.5)
        leaf(builder,rng.uniform(-root_spread,root_spread),rng.uniform(-.006,.006),
             length,width,angle,rng.choice([0,1,4,5]),lift=lift,
             fullness=rng.uniform(.42,.70),cup=rng.uniform(.05,.27))


def decoration_distribution(style):
    """Fixed scatter bands with smaller, independently sized pieces inside."""
    return {
        'pebbles':(.110,(.025,.065),(.024,.064)),
        'grass':(.145,(.033,.071),(.038,.086)),
        'moss':(.125,(.036,.079),(.036,.084)),
    }[style]


def scatter_edge_sites(edge, indices, band, widths, depths, rng):
    """Jitter length strata and shuffle depth strata instead of making rows."""
    count=len(indices)
    near=depths[1]/2+.002
    far=band-near
    for attempt in range(500):
        insets=[near+(rank+rng.uniform(.12,.88))*(far-near)/count
                for rank in range(count)]
        rng.shuffle(insets)
        sites=[]
        for index,inset in zip(indices,insets):
            width=rng.uniform(*widths)
            jitter=.008 if index in (0,11) else .020
            if index in (0,11):
                jitter=min(jitter,1/24-width/2-.001)
            sites.append({'edge':edge,'corner':False,
                          't':(index+.5)/12-.5+rng.uniform(-jitter,jitter),
                          'inset':inset,'width':width,
                          'depth':rng.uniform(*depths)})
        # One randomly positioned outer sample reaches the border. The other
        # pieces are free to occupy the full band rather than hugging a line.
        outer=min(sites,key=lambda site:site['inset'])
        outer['inset']=outer['depth']/2
        gaps=[b['t']-a['t'] for a,b in zip(sites,sites[1:])]
        if any(not .052<=gap<=.116 for gap in gaps):
            continue
        if any(math.hypot(a['t']-b['t'],a['inset']-b['inset'])<.055
               for i,a in enumerate(sites) for b in sites[i+1:]):
            continue
        # Avoid accidental local rows or long diagonal progressions.
        if any(max(s['inset'] for s in sites[i:i+4])-
               min(s['inset'] for s in sites[i:i+4])<.38*(far-near)
               for i in range(count-3)):
            continue
        mean_t=sum(s['t'] for s in sites)/count
        mean_d=sum(s['inset'] for s in sites)/count
        covariance=sum((s['t']-mean_t)*(s['inset']-mean_d) for s in sites)
        variance_t=sum((s['t']-mean_t)**2 for s in sites)
        variance_d=sum((s['inset']-mean_d)**2 for s in sites)
        if abs(covariance)/math.sqrt(variance_t*variance_d)>.55:
            continue
        return sites
    raise RuntimeError('Could not distribute edge sites evenly')


def perimeter_sites(style, config, rng):
    """Balanced 2D scatter, with one owner for each shared corner."""
    edges={'edge':(0,),'corner':(0,1),'strip':(0,2),'end':(0,1,3)}[config]
    band,widths,depths=decoration_distribution(style)
    for attempt in range(500):
        sites=[]
        for edge in edges:
            indices=[i for i in range(12)
                     if not (i==0 and (edge-1)%4 in edges)
                     and not (i==11 and (edge+1)%4 in edges)]
            sites.extend(scatter_edge_sites(edge,indices,band,widths,depths,rng))
            if (edge+1)%4 in edges:
                width,depth=rng.uniform(*widths),rng.uniform(*depths)
                sites.append({'edge':edge,'corner':True,'width':width,'depth':depth,
                              'center':(.5-rng.uniform(width/2+.003,.052),
                                        -.5+rng.uniform(depth/2+.003,.052))})
        centers=[]
        for site in sites:
            if site['corner']:
                x,y=site['center']
                centers.append(((x,y),(-y,x),(-x,-y),(y,-x))[site['edge']])
            else:
                centers.append(edge_point(site['edge'],site['t'],site['inset']))
        if all(math.dist(a,b)>=.052 for i,a in enumerate(centers) for b in centers[i+1:]):
            return sites
    raise RuntimeError('Could not keep the shared corners evenly spaced')


def place_perimeter_patch(builder, patch, site):
    """Place a whole patch inside its sampled footprint without clipping."""
    edge,corner=site['edge'],site['corner']
    width,depth=site['width'],site['depth']
    points=patch.vertices
    if corner:
        # North/east corner: point the plant inward along the diagonal.
        points=[((x-y)/math.sqrt(2),(x+y)/math.sqrt(2),z) for x,y,z in points]
    low=[min(p[i] for p in points) for i in (0,1)]
    high=[max(p[i] for p in points) for i in (0,1)]
    placed=[]
    for x,y,z in points:
        u=(x-low[0])/(high[0]-low[0])
        v=(y-low[1])/(high[1]-low[1])
        if corner:
            px=site['center'][0]+(u-.5)*width
            py=site['center'][1]+(v-.5)*depth
            # Rotate the shared NE site to the other three corners.
            px,py=((px,py),(-py,px),(-px,-py),(py,-px))[edge]
        else:
            px,py=edge_point(edge,site['t']+(u-.5)*width,
                            site['inset']+(v-.5)*depth)
        placed.append((px,py,z))
    builder.island(placed,patch.faces,patch.swatches)


def make_uniform_decoration(builder, style, config, variation, rng):
    band,_,_=decoration_distribution(style)
    maker={'pebbles':make_pebbles,'grass':make_grass,'moss':make_moss}[style]
    for site in perimeter_sites(style,config,rng):
        patch=MeshBuilder()
        maker(patch,rng)
        if style=='moss':
            # A small cluster should keep soft bulb proportions when fitted
            # to its footprint, rather than becoming a row of tall spikes.
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


def moss_clump(builder, x, y, radius, height, rng, swatch, *, fleck=False, detail=True):
    """A closed rounded lobe, with fewer sections for small satellites."""
    n=7 if detail else 5
    rotation=rng.uniform(0,math.tau)
    elongation=rng.uniform(.75,1.18)
    radial=[rng.uniform(.91,1.09) for _ in range(n)]
    verts=[]
    rings=(((.63,-.006),(1,.27*height),(.94,.60*height),(.60,.88*height))
           if n==7 else ((.63,-.006),(1,.29*height),(.68,.82*height)))
    for ring,(scale,z) in enumerate(rings):
        for j in range(n):
            angle=j*math.tau/n+rotation
            jitter=rng.uniform(-.018,.018)*height if ring else 0
            verts.append((x+math.cos(angle)*radius*radial[j]*scale,
                          y+math.sin(angle)*radius*elongation*radial[j]*scale,z+jitter))
    verts.append((x+.04*radius,y-.03*radius,height))
    faces=[tuple(reversed(range(n)))]
    swatches=[max(0,swatch-1)]
    for ring in range(len(rings)-1):
        for j in range(n):
            faces.append((ring*n+j,ring*n+(j+1)%n,(ring+1)*n+(j+1)%n,(ring+1)*n+j))
            swatches.append(swatch if ring<len(rings)-2 else min(5,swatch+1))
    shoulder=(len(rings)-1)*n
    tip_index=len(rings)*n
    for j in range(n):
        faces.append((shoulder+j,shoulder+(j+1)%n,tip_index))
        swatches.append(min(5,swatch+1))
    builder.island(verts,faces,swatches)
    # Sample an actual top triangle, then inset a small solid fleck into it.
    # The caller permits at most two flecks across the whole patch.
    if fleck:
        j=rng.randrange(n)
        tip=verts[tip_index]
        left,right=verts[shoulder+j],verts[shoulder+(j+1)%n]
        tip_weight=rng.uniform(.52,.84)
        split=rng.random()
        left_weight=(1-tip_weight)*split
        right_weight=(1-tip_weight)*(1-split)
        px,py,pz=(tip_weight*tip[i]+left_weight*left[i]+right_weight*right[i]
                  for i in range(3))
        size=radius*rng.uniform(.045,.070)
        base=pz-.65*size
        fleck=[(px-size,py-size*.65,base),(px+size,py-size*.65,base),
               (px,py+size,base),(px,py,pz+.60*size)]
        builder.island(fleck,[(0,2,1),(0,1,3),(1,2,3),(2,0,3)],
                       [6,7,7,6])


def make_moss(builder, rng):
    """Mix compact bulb triads with cushions bearing lopsided satellites."""
    radius=rng.uniform(.027,.038)
    compact=rng.random()<.50
    height=radius*rng.uniform(*((1.15,1.45) if compact else (1.25,1.65)))
    swatch=rng.choice([1,2,3,4])
    satellites=2 if compact else rng.choice((2,3))
    direction=rng.uniform(0,math.tau)
    spread=rng.uniform(*((1.65,2.35) if compact else (2.4,4.7)))
    fleck_count=rng.choices((0,1,2),weights=(2,5,3))[0]
    fleck_lobes=set(rng.sample(range(satellites+1),fleck_count))
    moss_clump(builder,0,0,radius,height,rng,swatch,fleck=0 in fleck_lobes)
    for i in range(satellites):
        angle=direction+(i/(satellites-1)-.5)*spread+rng.uniform(-.18,.18)
        distance=radius*rng.uniform(*((.90,1.18) if compact else (.68,.98)))
        satellite_radius=radius*rng.uniform(*((.72,.85) if compact else (.50,.76)))
        satellite_height=satellite_radius*rng.uniform(*((1.25,1.50) if compact else (1.10,1.40)))
        moss_clump(builder,math.cos(angle)*distance,
                   math.sin(angle)*distance*rng.uniform(.76,1.04),
                   satellite_radius,satellite_height,rng,
                   max(0,min(5,swatch+rng.choice((-1,0,0,1)))),
                   fleck=i+1 in fleck_lobes,detail=False)


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

manifest={'version':1,'revision':'Varied pebbles and grass tufts with multi-lobe moss in balanced edge scatter',
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
