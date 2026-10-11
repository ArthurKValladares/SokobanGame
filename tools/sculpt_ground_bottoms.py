"""Native Blender sculpting helpers for broad, interlocking island boulders.

The editable meshes use coarse convex rock masses with clipped hidden joins.
No procedural modifier or external asset is needed at runtime.
"""
from __future__ import annotations

import math
import random
from itertools import combinations

import bmesh
from mathutils import Euler, Vector
from mathutils.geometry import closest_point_on_tri

MAX_OVERHANG = .12
FINAL_OVERHANG = .04


def tighten_rock_point(point, mask, old_margin=MAX_OVERHANG, new_margin=FINAL_OVERHANG):
    """Narrow the accepted rock form while fixing every hidden join plane."""
    result = Vector(point)
    for axis, lower_side, upper_side in ((0, 3, 1), (1, 0, 2)):
        lower, upper = bool(mask & (1 << lower_side)), bool(mask & (1 << upper_side))
        old_min, old_max = -old_margin if lower else 0, 1+old_margin if upper else 1
        new_min, new_max = -new_margin if lower else 0, 1+new_margin if upper else 1
        result[axis] = new_min + (result[axis]-old_min)*(new_max-new_min)/(old_max-old_min)
    return result


def exact_hull_faces(points):
    """Resolve rare near-coplanar Blender hull quads with true support planes."""
    planes = {}
    for a, b, c in combinations(range(len(points)), 3):
        normal = (points[b]-points[a]).cross(points[c]-points[a])
        if normal.length < 1e-9:
            continue
        normal.normalize()
        distances = [(p-points[a]).dot(normal) for p in points]
        if min(distances) >= -1e-7:
            normal.negate()
            distances = [-d for d in distances]
        if max(distances) > 1e-7:
            continue
        members = tuple(i for i,d in enumerate(distances) if abs(d) <= 1e-7)
        planes[members] = normal
    faces = []
    for members, normal in planes.items():
        center = sum((points[i] for i in members), Vector())/len(members)
        u = (points[members[0]]-center).normalized()
        v = normal.cross(u)
        ordered = sorted(members, key=lambda i:math.atan2((points[i]-center).dot(v), (points[i]-center).dot(u)))
        for i in range(1,len(ordered)-1):
            triangle = (ordered[0],ordered[i],ordered[i+1])
            if (points[triangle[1]]-points[triangle[0]]).cross(points[triangle[2]]-points[triangle[0]]).length > 1e-9:
                faces.append(triangle)
    return faces


def rock_hull(center, radii, rng, mask, z_bounds=(-.995, .777)):
    """A skewed, chamfered block: broad faces, blunt corners, unequal shoulders."""
    rotation = Euler((rng.choice((-1, 1))*rng.uniform(.18, .42), rng.choice((-1, 1))*rng.uniform(.20, .48),
                      rng.uniform(-.58, .58))).to_matrix()
    points = []
    skew = Vector((rng.uniform(-.15, .15), rng.uniform(-.15, .15), 0))
    for axis in range(3):
        others = [i for i in range(3) if i != axis]
        for sign in (-1, 1):
            face_scale = rng.uniform(.91, 1.09)
            for a in (-1, 1):
                for b in (-1, 1):
                    p = Vector((0, 0, 0))
                    p[axis] = sign*face_scale
                    p[others[0]] = a*rng.uniform(.34, .71)
                    p[others[1]] = b*rng.uniform(.34, .71)
                    # Low-frequency distortion keeps large facets rather than a
                    # uniformly triangulated sphere or a row of pointed cones.
                    p += skew*p.z
                    p = center + rotation @ Vector([p[i]*radii[i] for i in range(3)])
                    bounds = ((-MAX_OVERHANG if mask & 8 else 0, 1+MAX_OVERHANG if mask & 2 else 1),
                              (-MAX_OVERHANG if mask & 1 else 0, 1+MAX_OVERHANG if mask & 4 else 1))
                    for i in (0, 1):
                        p[i] = max(bounds[i][0], min(bounds[i][1], p[i]))
                    p.z = max(z_bounds[0], min(z_bounds[1], p.z))
                    points.append(p)
    rock = bmesh.new()
    for point in points:
        rock.verts.new(point)
    hull = bmesh.ops.convex_hull(rock, input=list(rock.verts), use_existing_faces=False)
    unused = [item for key in ("geom_interior", "geom_unused") for item in hull.get(key, [])
              if isinstance(item, bmesh.types.BMVert) and item.is_valid]
    if unused:
        bmesh.ops.delete(rock, geom=list(set(unused)), context="VERTS")
    bmesh.ops.recalc_face_normals(rock, faces=list(rock.faces))
    bmesh.ops.triangulate(rock, faces=list(rock.faces))
    rock.verts.ensure_lookup_table()
    rock.verts.index_update()
    output = ([v.co.copy() for v in rock.verts],
              [[v.index for v in face.verts] for face in rock.faces])
    rock.free()
    points, faces = output
    violation = max(max((p-points[f[0]]).dot((points[f[1]]-points[f[0]]).cross(points[f[2]]-points[f[0]]).normalized())
                        for p in points) for f in faces)
    if violation > 1e-5:
        output = points, exact_hull_faces(points)
    return output


def triangle_distance(p, triangle):
    return (p-closest_point_on_tri(p, *triangle)).length


def hull_clearance(a, b):
    """Conservative vertex/face gap after a separating-plane test."""
    pa, fa = a
    pb, fb = b
    separated = False
    for points, faces, other in ((pa, fa, pb), (pb, fb, pa)):
        for face in faces:
            p, q, r = [points[i] for i in face]
            normal = (q-p).cross(r-p).normalized()
            if min((v-p).dot(normal) for v in other) > .045:
                separated = True
                break
    if not separated:
        return 0
    return min(min(triangle_distance(p, [pb[i] for i in f]) for p in pa for f in fb),
               min(triangle_distance(p, [pa[i] for i in f]) for p in pb for f in fa))


def add_rock_masses(body, triangle, variant, mask):
    """Embed unequal side boulders into the recessed native cliff core."""
    rng = random.Random(78019+variant*997+mask*31)
    masses = []
    corners = ((0, 0, 0, 3), (1, 0, 0, 1), (1, 1, 2, 1), (0, 1, 2, 3))
    wrapping = set()
    for x, y, a, b in corners:
        if mask & (1 << a) and mask & (1 << b):
            wrapping.add((x, y))
            center = Vector((.19 if not x else .81, .19 if not y else .81,
                             rng.uniform(.31, .41)))
            broad = (x+y+variant) % 2 == 0
            radii = Vector((rng.uniform(.43, .49) if broad else rng.uniform(.31, .36),
                            rng.uniform(.33, .42), rng.uniform(.29, .34) if broad else rng.uniform(.38, .44)))
            masses.append(rock_hull(center, radii, rng, mask))
    for side in range(4):
        if not mask & (1 << side):
            continue
        ends = ((0, 0), (1, 0)) if side == 0 else ((1, 0), (1, 1)) if side == 1 else ((1, 1), (0, 1)) if side == 2 else ((0, 1), (0, 0))
        occupied = [end in wrapping for end in ends]
        if all(occupied):
            continue
        anchors = [.24, .74] if not any(occupied) else [.72 if occupied[0] else .28]
        for along in anchors:
            # Alternate widths/heights and rotate the seam directions. Adjacent
            # grid cells retain common hidden planes but their rocks differ.
            along += rng.uniform(-.04, .04)
            center = Vector(((along, .19, rng.uniform(.29, .39)),
                             (.81, along, rng.uniform(.29, .39)),
                             (1-along, .81, rng.uniform(.29, .39)),
                             (.19, 1-along, rng.uniform(.29, .39)))[side])
            radii = Vector((rng.uniform(.36, .45), rng.uniform(.31, .37), rng.uniform(.36, .44)))
            if side % 2:
                radii.x, radii.y = radii.y, radii.x
            masses.append(rock_hull(center, radii, rng, mask))
    # Broad staggered lower masses give the island a belly, rather than ending
    # every vertical side in a regular row of tips.
    under_count = 3 if mask == 0 else 1 + int(mask in (1, 3) or variant % 3 == 0)
    for i in range(under_count):
        center = Vector((.39 + .20*(i % 2)+rng.uniform(-.07, .07),
                         .40 + .18*(i // 2)+rng.uniform(-.07, .07), rng.uniform(-.31, -.24)))
        radii = Vector((rng.uniform(.32, .43), rng.uniform(.32, .43), rng.uniform(.37, .47)))
        masses.append(rock_hull(center, radii, rng, mask))
    for index, (points, faces) in enumerate(masses):
        group = body.vertex_groups.new(name=f"BoulderMass{index+1:02d}").index
        for face in faces:
            triangle([tighten_rock_point(points[i], mask) for i in face], group)

    sides = [side for side in range(4) if mask & (1 << side)]
    floaters = []
    desired = 1 + variant % 3 if sides else 0
    for index in range(desired):
        for attempt in range(100):
            side = sides[(variant+index+attempt) % len(sides)]
            along = rng.uniform(.15, .85)
            normal = -.063 if side in (0, 3) else 1.063
            center = Vector(((along, normal, -.87), (normal, along, -.87),
                             (1-along, normal, -.87), (normal, 1-along, -.87))[side])
            center.z += rng.uniform(-.025, .04)
            tangential = rng.uniform(.080, .115) if index == 0 else rng.uniform(.035, .075)
            radii = Vector((tangential, .038, rng.uniform(.055, .09)))
            if side % 2:
                radii.x, radii.y = radii.y, radii.x
            candidate = rock_hull(center, radii, rng, mask)
            if max(p.z for p in candidate[0]) > -.69:
                continue
            if all(hull_clearance(candidate, other) > .045 for other in masses+floaters):
                floaters.append(candidate)
                break
        else:
            raise AssertionError((body.name, "No separated floating boulder position", index))
    for index, (points, faces) in enumerate(floaters):
        group = body.vertex_groups.new(name=f"FloatingRock{index+1:02d}").index
        for face in faces:
            triangle([tighten_rock_point(points[i], mask) for i in face], group)
    return len(masses), len(floaters)
