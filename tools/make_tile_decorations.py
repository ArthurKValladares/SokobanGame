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
    # Eight ramps a style. A ramp runs from the colour at a root, a skirt or
    # an underside to the colour at a tip or a crown: a face picks its ramp,
    # and each of its corners picks how far along the ramp it sits. A style
    # keeps to a few close ramps, so that it reads as one colour: grey
    # stones, light green grass, bright green moss with paler flecks.
    'pebbles': [((.34,.33,.30), (.60,.59,.54)), ((.40,.39,.36), (.68,.67,.62)),
                ((.29,.29,.27), (.52,.52,.48)), ((.37,.35,.31), (.64,.62,.55)),
                ((.34,.33,.30), (.60,.59,.54)), ((.40,.39,.36), (.68,.67,.62)),
                ((.29,.29,.27), (.52,.52,.48)), ((.37,.35,.31), (.64,.62,.55))],
    'grass': [((.36,.58,.12), (.66,.86,.26)), ((.41,.62,.12), (.73,.90,.30)),
              ((.31,.53,.12), (.58,.81,.22)), ((.36,.58,.12), (.66,.86,.26)),
              ((.41,.62,.12), (.73,.90,.30)), ((.31,.53,.12), (.58,.81,.22)),
              ((.36,.58,.12), (.66,.86,.26)), ((.41,.62,.12), (.73,.90,.30))],
    'moss': [((.28,.54,.09), (.50,.79,.15)), ((.32,.58,.09), (.56,.83,.17)),
             ((.25,.50,.09), (.45,.74,.14)), ((.60,.80,.22), (.84,.94,.42)),
             ((.28,.54,.09), (.50,.79,.15)), ((.32,.58,.09), (.56,.83,.17)),
             ((.25,.50,.09), (.45,.74,.14)), ((.60,.80,.22), (.84,.94,.42))]}
BANDS = {'pebbles': .16, 'grass': .21, 'moss': .26}
# How close two pieces may come, as a share of their sizes: stones and leaves
# keep clear ground between them, and clumps of moss nearly touch.
CROWDING = {'pebbles': 1.15, 'grass': 1, 'moss': .9}


def palette_image(style):
    """Paint, save and embed a style's palette as a new image.

    Always a new one: repainting an image that is already packed leaves the
    packed copy, which is the one the exporter writes, as it was.
    """
    name = 'TileDetails_' + style + '_palette'
    stale = bpy.data.images.get(name)
    if stale:
        stale.name = name + '_stale'
    image = bpy.data.images.new(name, width=128, height=16, alpha=False)
    ramps = PALETTES[style]
    pixels = []
    for y in range(16):
        # Row 0 is the bottom of the image and of UV0: the foot of a ramp.
        t = y / 15
        for x in range(128):
            low, high = ramps[x // 16]
            pixels.extend([a + (b - a) * t for a, b in zip(low, high)] + [1])
    image.pixels.foreach_set(pixels)
    image.update()
    image.filepath_raw = str(KIT / (style + '_palette.png'))
    image.file_format = 'PNG'
    image.save()
    image.pack()
    if stale:
        stale.user_remap(image)
        bpy.data.images.remove(stale)
    return image


def palette_material(style):
    image = palette_image(style)
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
        # Per vertex: how far along its ramp, how far the wind moves it, and
        # how far its normal leans toward straight up. Per face: whether it
        # shades as part of a rounded surface or as a facet.
        self.shades, self.winds, self.lifts, self.smooth = [], [], [], []
        self.focal_points = []

    def island(self, verts, faces, swatches, shades=None, winds=None, *, smooth=False, lift=0):
        offset = len(self.vertices)
        self.vertices.extend(verts)
        self.faces.extend(tuple(i + offset for i in face) for face in faces)
        self.swatches.extend(swatches)
        self.shades.extend(shades if shades is not None else [.5] * len(verts))
        self.winds.extend(winds if winds is not None else [0] * len(verts))
        self.lifts.extend([lift] * len(verts))
        self.smooth.extend([smooth] * len(faces))

    def add(self, patch, place):
        """Append another builder's islands, each vertex moved by place(x, y, z)."""
        offset = len(self.vertices)
        self.vertices.extend(place(*vertex) for vertex in patch.vertices)
        self.faces.extend(tuple(i + offset for i in face) for face in patch.faces)
        for name in ('swatches', 'shades', 'winds', 'lifts', 'smooth'):
            getattr(self, name).extend(getattr(patch, name))

    def bounds(self):
        return ([min(v[i] for v in self.vertices) for i in range(3)],
                [max(v[i] for v in self.vertices) for i in range(3)])

    def object(self, name, material):
        mesh = bpy.data.meshes.new(name + '_mesh')
        mesh.from_pydata(self.vertices, [], self.faces)
        mesh.materials.append(material)
        mesh.update()
        # Recalculate outward normals on every disconnected closed island.
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(mesh)
        bm.free()
        mesh.update()
        uv = mesh.uv_layers.new(name='PaletteUV')
        up = Vector((0, 0, 1))
        normals = [up] * len(mesh.loops)
        for face, swatch, smooth in zip(mesh.polygons, self.swatches, self.smooth):
            for loop_index in face.loop_indices:
                vertex = mesh.loops[loop_index].vertex_index
                # The column picks the ramp and the row the place along it.
                # Staying on the column's centre keeps a face off its
                # neighbours' colours however the texture is filtered.
                uv.data[loop_index].uv = ((swatch + .5) / 8, .08 + .84 * self.shades[vertex])
                normal = Vector(mesh.vertices[vertex].normal if smooth else face.normal)
                lift = self.lifts[vertex]
                normal = normal * (1 - lift) + up * lift
                normals[loop_index] = normal.normalized() if normal.length > 1e-6 else up
        # Every normal is authored: a facet repeats its face normal, a rounded
        # surface its vertex normal, and foliage leans both toward the sky so
        # a tuft takes the light of the ground it stands on.
        for polygon in mesh.polygons:
            polygon.use_smooth = True
        mesh.normals_split_custom_set(normals)
        if any(self.winds):
            # _WIND: 0 at a root that stays put, 1 at a tip that sways most.
            wind = mesh.attributes.new('_WIND', 'FLOAT', 'POINT')
            wind.data.foreach_set('value', self.winds)
        obj = bpy.data.objects.new(name, mesh)
        assets_collection.objects.link(obj)
        return obj


def edge_point(edge, t, inset):
    """Clockwise edge parameter; north is -Y. All geometry stays in the cell."""
    return [(t, -.5 + inset), (.5 - inset, t),
            (-t, .5 - inset), (-.5 + inset, -t)][edge]


def edge_placement(edge, t, inset):
    """Move a patch authored with X along a border and Y pointing into the tile."""
    def place(x, y, z):
        px, py = edge_point(edge, t + x, inset + y)
        return (px, py, z)
    return place


def mound(builder, x, y, radius, depth, height, rng, swatch, *, sides=8, uneven=.08,
          rotation=None, base=0, lift=.4, wind=0):
    """One smooth rounded lump with an uneven outline: a pebble, or a lump of moss.

    A skirt, a shoulder, a brow and a crown, from `base` up. `wind` is the
    weight its crown sways by; its skirt holds still.
    """
    if rotation is None:
        rotation = rng.uniform(0, math.tau)
    phase = rng.uniform(0, math.tau)
    lobes = rng.choice((2, 3))
    radial = [1 + uneven * (math.cos(lobes * j * math.tau / sides + phase) + rng.uniform(-.5, .5))
              for j in range(sides)]
    crown = (rng.uniform(-.15, .15) * radius, rng.uniform(-.15, .15) * depth)
    cos_r, sin_r = math.cos(rotation), math.sin(rotation)
    verts, shades, winds = [], [], []
    for scale, rise, drift in ((.86, 0, 0), (1, .36, .15), (.68, .84, .6)):
        for j in range(sides):
            # Half a step round, so the long axis ends in an edge, not a point.
            angle = (j + .5) * math.tau / sides
            px = math.cos(angle) * radius * radial[j] * scale + crown[0] * drift
            py = math.sin(angle) * depth * radial[j] * scale + crown[1] * drift
            verts.append((x + px * cos_r - py * sin_r, y + px * sin_r + py * cos_r,
                          base + (rise * height if rise else -.008)))
            shades.append(rise)
            winds.append(wind * rise)
    verts.append((x + crown[0] * cos_r - crown[1] * sin_r,
                  y + crown[0] * sin_r + crown[1] * cos_r, base + height))
    shades.append(1)
    winds.append(wind)
    faces = [tuple(reversed(range(sides)))]
    for ring in range(2):
        for j in range(sides):
            faces.append((ring*sides+j, ring*sides+(j+1)%sides,
                          (ring+1)*sides+(j+1)%sides, (ring+1)*sides+j))
    for j in range(sides):
        faces.append((2*sides+j, 2*sides+(j+1)%sides, 3*sides))
    builder.island(verts, faces, [swatch] * len(faces), shades, winds, smooth=True, lift=lift)


def mound_height(height, share):
    """How high a mound's surface is, `share` of the way from its crown to its rim."""
    if share <= .68:
        return height * (1 - .16 * share / .68)
    return height * (.84 - .48 * (min(share, 1) - .68) / .32)


def pebble(builder, rng, size=None):
    """One small smooth stone."""
    radius = size or rng.uniform(.03, .05)
    # Normals well toward the sky: a stone this small reads as one grey shape,
    # not as a lit half and a shaded half.
    mound(builder, 0, 0, radius, radius * rng.uniform(.7, .95), radius * rng.uniform(.5, .68),
          rng, rng.choice((0, 0, 1, 2, 3)), sides=9 if radius > .03 else 7, uneven=.1, lift=.55)


def small_pebble(builder, rng):
    """A stray stone, half the size of the ones in the line."""
    pebble(builder, rng, rng.uniform(.014, .024))


def leaf(builder, x, y, heading, height, width, lean, swatch, *, rings=(0, .34, .7),
         profile=(.7, 1, .62), thickness=.26, arch=.55, wind=1, lift=.62, shade=(0, 1)):
    """One closed blade: a diamond section that arches away from its root.

    `heading` is the way it leans, `lean` how far its tip ends up from its
    root. The section is thick for its width so a blade still reads when the
    camera looks down its length.
    """
    forward = Vector((math.cos(heading), math.sin(heading), 0))
    side = Vector((-forward.y, forward.x, 0))
    foot = -.006

    def spine(t):
        out = lean * t ** 1.8
        rise = foot + (height - foot) * (t * (1 - arch) + (1 - (1 - t) ** 2) * arch)
        return out, rise

    verts, shades, winds = [], [], []
    for t, scale in zip(rings, profile):
        out, rise = spine(t)
        ahead = spine(t + .02)
        tangent = Vector((ahead[0] - out, ahead[1] - rise)).normalized()
        # The face that ends up on top once the blade has bent over.
        upper = forward * -tangent.y + Vector((0, 0, tangent.x))
        center = Vector((x, y, 0)) + forward * out + Vector((0, 0, rise))
        half = width * scale * .5
        ridge = half * thickness
        verts.extend(tuple(p) for p in (center - side * half, center + upper * ridge,
                                         center + side * half, center - upper * ridge))
        along = shade[0] + (shade[1] - shade[0]) * t
        shades.extend((along, min(1, along + .1), along, max(0, along - .06)))
        winds.extend([wind * t] * 4)
    out, rise = spine(1)
    verts.append((x + forward.x * out, y + forward.y * out, rise))
    shades.append(shade[1])
    winds.append(wind)
    count = len(rings)
    faces = [(3, 2, 1, 0)]
    for ring in range(count - 1):
        for j in range(4):
            faces.append((ring*4+j, ring*4+(j+1)%4, (ring+1)*4+(j+1)%4, (ring+1)*4+j))
    for j in range(4):
        faces.append(((count-1)*4+j, (count-1)*4+(j+1)%4, count*4))
    builder.island(verts, faces, [swatch] * len(faces), shades, winds, smooth=True, lift=lift)


def grass_leaf(builder, rng):
    """One short, plump leaf lying well over, so that it shows its face to the camera."""
    heading = rng.uniform(0, math.tau)
    leaf(builder, 0, 0, heading, rng.uniform(.026, .045), rng.uniform(.03, .04),
         rng.uniform(.06, .09), rng.choice((0, 0, 1, 2)), rings=(0, .35, .7),
         profile=(.5, 1, .82), thickness=.3, arch=.6, wind=.9)


def grass_sprig(builder, rng):
    """Three or four upright blades from one root: the tall accent of a border."""
    turn = rng.uniform(0, math.tau)
    count = rng.choice((3, 3, 4))
    swatch = rng.choice((0, 1, 2))
    for i in range(count):
        heading = turn + (i + rng.uniform(-.25, .25)) * math.tau / count
        height = rng.uniform(.09, .16)
        leaf(builder, .006 * math.cos(heading), .006 * math.sin(heading), heading, height,
             rng.uniform(.022, .028), rng.uniform(.025, .055), swatch, rings=(0, .45),
             profile=(.7, 1), thickness=.3, arch=.5, wind=1)


def moss_clump(builder, rng):
    """A few lumps of moss grown into one another, running along the border."""
    lumps = []
    for index in range(rng.choice((3, 3, 4, 5, 6, 7))):
        radius = rng.uniform(.034, .054) if index else rng.uniform(.044, .06)
        if lumps:
            # Each new lump leans on one already there, more often to its
            # side than in front of it or behind.
            ox, oy, other = rng.choice(lumps)
            angle = rng.choice((0, math.pi)) + rng.uniform(-1.25, 1.25)
            reach = (other + radius) * rng.uniform(.58, .76)
            x, y = ox + math.cos(angle) * reach, oy + math.sin(angle) * reach * .8
        else:
            x = y = 0
        lumps.append((x, y, radius))
    swatch = rng.choice((0, 0, 1, 2))
    flecks = rng.choice((1, 2, 2, 3))
    for x, y, radius in lumps:
        depth = radius * rng.uniform(.8, 1)
        height = radius * rng.uniform(.55, .72)
        rotation = rng.uniform(0, math.tau)
        # Moss does not blow about, but its crowns give a little in a gust.
        wind = .5 * min(1, radius / .04)
        mound(builder, x, y, radius, depth, height, rng, swatch, sides=10, uneven=.07,
              rotation=rotation, lift=.5, wind=wind)
        if flecks and rng.random() < .6:
            # A paler fleck on the lump, riding wherever its surface goes.
            flecks -= 1
            share = rng.uniform(.1, .55)
            angle = rng.uniform(0, math.tau)
            px, py = math.cos(angle) * radius * share, math.sin(angle) * depth * share
            surface = mound_height(height, share)
            fleck = rng.uniform(.007, .011)
            mound(builder, x + px * math.cos(rotation) - py * math.sin(rotation),
                  y + px * math.sin(rotation) + py * math.cos(rotation), fleck, fleck,
                  fleck * .5, rng, 3, sides=5, uneven=0, base=surface - .002, lift=.6,
                  wind=wind * surface / height)


def moss_dot(builder, rng):
    """A lone small lump, the start of a clump."""
    radius = rng.uniform(.016, .028)
    mound(builder, 0, 0, radius, radius * rng.uniform(.8, 1), radius * rng.uniform(.55, .7),
          rng, rng.choice((0, 1, 2)), sides=7, uneven=.07, lift=.5, wind=.5 * radius / .04)


def make_patch(maker, rng):
    patch = MeshBuilder()
    maker(patch, rng)
    return patch


def style_pieces(style, rng):
    """What lies along one decorated edge: an even run first, then the strays."""
    if style == 'grass':
        return ([grass_leaf] * rng.choice((9, 10, 11, 12)),
                [grass_leaf] * rng.choice((3, 4, 5)) + [grass_sprig] * rng.choice((1, 1, 2)))
    if style == 'moss':
        return [moss_clump] * rng.choice((2, 3, 3)), [moss_dot] * rng.choice((1, 2, 3))
    return [pebble] * rng.choice((5, 6, 7)), [small_pebble] * rng.choice((1, 2, 3))


def decoration_distribution(style):
    """The depth of a style's border band, and how tightly its pieces pack."""
    return BANDS[style], CROWDING[style]


def plant_corner(builder, style, edge, rng, placed, reserved):
    """One piece in the corner two decorated borders share, reaching both."""
    large, _ = style_pieces(style, rng)
    patch = make_patch(large[0], rng)
    low, high = patch.bounds()
    t, inset = .5 - high[0], -low[1]
    builder.add(patch, edge_placement(edge, t, inset))
    placed.append((edge_point(edge, t, inset), max(high[0] - low[0], high[1] - low[1]) / 2))
    # Neither border spreads its own run over the corner.
    reserved[edge][1] = high[0] - low[0]
    reserved[(edge + 1) % 4][0] = high[1] - low[1]


def scatter_edge(builder, style, edge, edges, rng, placed, reserved):
    """Lay one border out: an even run of pieces along it, then a few strays."""
    band, crowding = decoration_distribution(style)
    large, small = style_pieces(style, rng)
    before, after = reserved[edge]
    # A corner piece already reaches this border and stands in for one piece.
    touching = before > 0 or after > 0
    large = large[(before > 0) + (after > 0):]

    def fits(center, radius):
        x, y = center
        distances = (y + .5, .5 - x, .5 - y, x + .5)
        # Each decorated border owns the nearer half of a shared corner.
        if any(distances[other] < distances[edge] for other in edges if other != edge):
            return False
        return all(math.dist(center, other) >= crowding * (radius + other_radius)
                   for other, other_radius in placed)

    def grow(makers, stratified, depth_bias):
        nonlocal touching
        count = len(makers)
        for index, maker in enumerate(makers):
            for attempt in range(400):
                patch = make_patch(maker, rng)
                low, high = patch.bounds()
                radius = max(high[0] - low[0], high[1] - low[1]) / 2
                t_low, t_high = -.5 - low[0], .5 - high[0]
                if stratified:
                    t_low, t_high = t_low + before * .8, t_high - after * .8
                    width = (t_high - t_low) / count
                    t = t_low + (index + rng.uniform(.18, .82)) * width
                else:
                    t = rng.uniform(t_low, t_high)
                near, far = -low[1], band - high[1]
                if far < near:
                    continue
                inset = near + (far - near) * rng.random() ** depth_bias
                if not touching:
                    # The first piece of a border without a corner reaches it.
                    inset = near
                center = edge_point(edge, t, inset)
                # Later attempts settle for a tighter fit rather than a gap.
                if not fits(center, radius * (1 if attempt < 200 else .6)):
                    continue
                builder.add(patch, edge_placement(edge, t, inset))
                placed.append((center, radius))
                touching = True
                break

    grow(large, True, 1.3)
    grow(small, False, 1)


def make_uniform_decoration(builder, style, config, variation, rng):
    band, _ = decoration_distribution(style)
    edges = {'edge':(0,),'corner':(0,1),'strip':(0,2),'end':(0,1,3)}[config]
    placed = []
    reserved = {edge: [0, 0] for edge in range(4)}
    for edge in edges:
        if (edge + 1) % 4 in edges:
            plant_corner(builder, style, edge, rng, placed, reserved)
    for edge in edges:
        scatter_edge(builder, style, edge, edges, rng, placed, reserved)
    low, high = builder.bounds()
    reach = (-low[1], high[0], high[1], -low[0])
    assert max(reach) <= .5 and all(reach[edge] == .5 for edge in edges), \
        'Every decorated border is reached, and none is crossed'
    # Retain compatible representative points for existing catalog readers.
    focus=[0,-.5+band/2,0]
    if config=='corner':
        focus=[.5-band/2,-.5+band/2,0]
    builder.focal_points=[focus]
    if config=='strip':
        builder.focal_points.append([0,.5-band/2,0])
    return focus


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
                             export_attributes=True,export_materials='EXPORT')
    obj.location=display_offset
    payload=export_path.read_bytes()
    json_length=struct.unpack_from('<I',payload,12)[0]
    exported=json.loads(payload[20:20+json_length])
    record=next(a for a in catalog if a['id']==obj.name)
    record['source_vertex_count']=record['vertex_count']
    record['vertex_count']=sum(exported['accessors'][p['attributes']['POSITION']]['count']
                               for m in exported['meshes'] for p in m['primitives'])

manifest={'version':1,'revision':'Dotted pebbles, leaf trails and moss lumps; wind weights on foliage',
          'tile_size':1,'coordinates':'Blender X/Y horizontal, Z up; origin at tile center and surface',
          'north':'-Y','placement_z':0,'preserveSourceScale':True,
          'configurations':{name:{'edge_mask':mask,'edges':[('N','E','S','W')[i] for i in edges]}
                            for name,mask,edges,title in CONFIGS},'assets':catalog}
# LF on every platform, as .gitattributes asks of the working tree.
(KIT/'catalog.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
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
