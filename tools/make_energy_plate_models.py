#!/usr/bin/env python3
"""Deterministically author the energy plates, pedestal button and lever.

Coordinates use the engine's unit tile convention: x east, y south, z up.
The asset manifest must load these with preserveSourceScale. At the default
surface scale (.72, .72, .08), the end's spherical lens rises to .216 units;
the pressure plate remains a low, recessed pad. Positive-emissive materials
are white so the plate shader can apply its link/activation color without
tinting the grey housing. UV0 is the engine x/y projection used by the energy
animation. All emission uses valid KHR_materials_emissive_strength factors.

The pulse button uses unit scale on every axis. Its raised pedestal sits near
the north tile edge, with the round press face tilted 45 degrees toward the
tile center (+y). Quarter turns around (.5, .5) supply the other three edges.
The lever has shallow top-quarter steel caps around an open floor slot.
Its wooden shaft throws along the tile edge (+/-x for north), about a low
transverse y-axis hinge, with a short link-colored grip and off/on poses.
Complete models remain available for editor previews. Separate base and moving
assembly models share their original coordinates for rigid gameplay animation.

Only Python's standard library is required. Re-running produces identical GLBs.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

from make_rotator_models import Primitive, _cross, _dot, _normalized, _sub, _to_gltf

OUTPUT_DIRECTORY = Path(__file__).resolve().parent.parent / "assets/custom/models"
CENTER = (.5, .5)
SEGMENTS = 96


def material(name, color, metallic=.0, roughness=.5, emission=.0):
    result = {
        "name": name,
        "pbrMetallicRoughness": {
            "baseColorFactor": [*color, 1.0],
            "metallicFactor": metallic,
            "roughnessFactor": roughness,
        },
    }
    if emission:
        result["emissiveFactor"] = [1.0, 1.0, 1.0]
        result["extensions"] = {
            "KHR_materials_emissive_strength": {"emissiveStrength": emission}}
    return result


MATERIALS = {
    "Housing": material("Housing", (.255, .278, .310), .55, .46),
    "BeveledSteel": material("BeveledSteel", (.425, .448, .480), .62, .40),
    "Recess": material("Recess", (.055, .063, .078), .30, .58),
    "Engraving": material("Engraving", (.170, .192, .225), .40, .50),
    "EnergyPanel": material("EnergyPanel", (.65, .65, .65), .05, .52, 2.5),
    "EnergyLens": material("EnergyLens", (.70, .70, .70), .10, .33, 3.0),
    "EnergyInlay": material("EnergyInlay", (.82, .82, .82), .15, .40, 5.0),
    "LeverWood": material("LeverWood", (.30, .16, .075), .0, .72),
    "LeverGrip": material("LeverGrip", (.80, .80, .80), .05, .55, .25),
    "LeverHousing": material("LeverHousing", (.255, .278, .310), .50, .58),
    "LeverSteel": material("LeverSteel", (.425, .448, .480), .55, .58),
}


class SmoothPrimitive(Primitive):
    def smooth_triangle(self, vertices, normals):
        a, b, c = vertices
        face = _cross(_sub(b, a), _sub(c, a))
        if _dot(face, face) < 1e-18:
            return
        if _dot(face, tuple(sum(n[i] for n in normals) for i in range(3))) < 0:
            vertices = (vertices[0], vertices[2], vertices[1])
            normals = (normals[0], normals[2], normals[1])
        self.positions.extend(vertices)
        self.normals.extend(_normalized(n) for n in normals)


def pillow_outline(radius):
    """Rounded square with softly bowed sides and fuller, rounded corners."""
    outline = []
    for index in range(SEGMENTS):
        angle = 2 * math.pi * index / SEGMENTS
        c, s = math.cos(angle), math.sin(angle)
        bow = 1.0 - .055 * math.cos(2 * angle) ** 4
        x = math.copysign(abs(c) ** .32, c) * radius * bow
        y = math.copysign(abs(s) ** .32, s) * radius * bow
        outline.append((.5 + x, .5 + y))
    return outline


def circle_outline(radius):
    return [(.5 + radius * math.cos(2 * math.pi * i / SEGMENTS),
             .5 + radius * math.sin(2 * math.pi * i / SEGMENTS))
            for i in range(SEGMENTS)]


def band(mesh, outer, inner, outer_height, inner_height, up=True):
    """Connect equal-sample closed outlines, including sloping bevels."""
    for i in range(len(outer)):
        j = (i + 1) % len(outer)
        mesh.quad((*outer[i], outer_height), (*outer[j], outer_height),
                  (*inner[j], inner_height), (*inner[i], inner_height), (0, 0, 1 if up else -1))


def square_band(mesh, outer_radius, inner_radius, z):
    band(mesh, pillow_outline(outer_radius), pillow_outline(inner_radius), z, z)


def ribbon(mesh, path, width, z):
    """Flat ornamental strokes, with continuous miters at each path corner."""
    pairs = []
    for index, p in enumerate(path):
        previous = path[max(0, index - 1)]
        following = path[min(len(path) - 1, index + 1)]
        dx, dy = following[0] - previous[0], following[1] - previous[1]
        length = math.hypot(dx, dy)
        ox, oy = -dy / length * width * .5, dx / length * width * .5
        pairs.append(((p[0] + ox, p[1] + oy, z),
                      (p[0] - ox, p[1] - oy, z)))
    for a, b in zip(pairs, pairs[1:]):
        mesh.quad(a[0], a[1], b[1], b[0], (0, 0, 1))


def rotated(point, angle):
    x, y = point[0] - .5, point[1] - .5
    c, s = math.cos(angle), math.sin(angle)
    return (.5 + x * c - y * s, .5 + x * s + y * c)


def prism(mesh, outline, z0, z1):
    center = tuple(sum(p[i] for p in outline) / len(outline) for i in range(2))
    mesh.fan(center, outline, z0, False)
    mesh.fan(center, outline, z1, True)
    mesh.prism_walls(outline, z0, z1)


def arc_strip(mesh, inner_radius, outer_radius, start, end, z0, z1, segments=16):
    outline = []
    for radius, indices in ((outer_radius, range(segments + 1)),
                            (inner_radius, range(segments, -1, -1))):
        for i in indices:
            theta = start + (end - start) * i / segments
            outline.append((.5 + radius * math.cos(theta), .5 + radius * math.sin(theta)))
    # Arc strips are concave: make top and bottom explicitly rather than fanning.
    for i in range(segments):
        j = 2 * segments + 1 - i
        p, q, r, s = outline[i], outline[i + 1], outline[j - 1], outline[j]
        mesh.quad((*p, z1), (*q, z1), (*r, z1), (*s, z1), (0, 0, 1))
        mesh.quad((*p, z0), (*q, z0), (*r, z0), (*s, z0), (0, 0, -1))
    mesh.prism_walls(outline, z0, z1)


def pressure_geometry():
    meshes = {name: Primitive() for name in
              ("Housing", "BeveledSteel", "Recess", "EnergyPanel", "Engraving", "EnergyInlay")}
    base, shoulder, lip, inner = (pillow_outline(r) for r in (.480, .462, .407, .388))
    meshes["Housing"].fan(CENTER, base, .0, False)
    meshes["Housing"].prism_walls(base, .0, .48)
    band(meshes["BeveledSteel"], base, shoulder, .48, 1.02)
    band(meshes["Housing"], shoulder, lip, 1.02, 1.02)
    band(meshes["BeveledSteel"], lip, pillow_outline(.400), 1.02, .96)
    band(meshes["Recess"], pillow_outline(.400), inner, .96, .73)
    meshes["EnergyPanel"].fan(CENTER, inner, .73, True)
    # A fine square inlay and dark engraved key-line surround the main colored field.
    square_band(meshes["EnergyInlay"], .356, .346, .742)
    square_band(meshes["Engraving"], .306, .296, .746)
    for quarter in range(4):
        angle = quarter * math.pi * .5
        # Four stepped corner ornaments repeat the silhouette at a smaller scale.
        corner = [(.661, .704), (.704, .704), (.704, .661)]
        ribbon(meshes["Engraving"], [rotated(p, angle) for p in corner], .015, .749)
        # Subtle cardinal diamonds connect the pattern to the perimeter line.
        diamond = [(.490, .184), (.5, .172), (.510, .184), (.5, .196)]
        diamond = [rotated(p, angle) for p in diamond]
        meshes["Engraving"].fan(tuple(rotated((.5, .184), angle)), diamond, .751, True)
        # Metal corner retainers sit above the steel rim, clear of the luminous panel.
        tab = [(.806, .821), (.821, .806), (.854, .839), (.839, .854)]
        prism(meshes["BeveledSteel"], [rotated(p, angle) for p in tab], 1.02, 1.105)
    return meshes


def inward_walls(mesh, outline, z0, z1):
    """A cavity's walls face its interior, unlike an outside prism surface."""
    for i, p in enumerate(outline):
        q = outline[(i + 1) % len(outline)]
        mesh.quad((*p, z0), (*q, z0), (*q, z1), (*p, z1),
                  (p[1] - q[1], q[0] - p[0], 0))


def pressure_components():
    """The fixed rim and pad, with .30 units of downward source-Z travel."""
    meshes = pressure_geometry()
    moving = ("EnergyPanel", "Engraving", "EnergyInlay")
    base = {name: mesh for name, mesh in meshes.items() if name not in moving}
    pad = {name: mesh for name, mesh in meshes.items() if name in moving}

    # Continue the existing recess below the resting pad. The skirt tapers
    # inward so its faces never coincide with the fixed cavity walls. At full
    # press the pad occupies z=.32..451, clear of the floor at z=.30.
    opening, skirt = pillow_outline(.388), pillow_outline(.386)
    inward_walls(base["Recess"], opening, .30, .73)
    base["Recess"].fan(CENTER, opening, .30, True)
    band(pad["EnergyPanel"], opening, skirt, .73, .62, up=False)
    pad["EnergyPanel"].fan(CENTER, skirt, .62, False)
    return base, pad


def spherical_lens(mesh, radius=.315, base=.40, rise=2.30, rings=18):
    """Smooth spherical-cap profile after the default tile height/width scaling."""
    max_phi = math.radians(75)
    radial_scale = radius / math.sin(max_phi)
    vertical_scale = rise / (1 - math.cos(max_phi))

    def vertex(ring, segment):
        phi, angle = max_phi * ring / rings, 2 * math.pi * segment / SEGMENTS
        s, c = math.sin(phi), math.cos(phi)
        position = (.5 + radial_scale * s * math.cos(angle),
                    .5 + radial_scale * s * math.sin(angle),
                    base + vertical_scale * (c - math.cos(max_phi)))
        normal = (s * math.cos(angle) / radial_scale,
                  s * math.sin(angle) / radial_scale, c / vertical_scale)
        return position, normal

    for ring in range(rings):
        for segment in range(SEGMENTS):
            a, an = vertex(ring, segment)
            b, bn = vertex(ring + 1, segment)
            c, cn = vertex(ring + 1, segment + 1)
            d, dn = vertex(ring, segment + 1)
            mesh.smooth_triangle((a, b, c), (an, bn, cn))
            if ring:
                mesh.smooth_triangle((a, c, d), (an, cn, dn))
    mesh.fan(CENTER, circle_outline(radius), base, False)


def end_geometry():
    meshes = {name: SmoothPrimitive() for name in
              ("Housing", "BeveledSteel", "Recess", "EnergyLens", "EnergyInlay")}
    housing = meshes["Housing"]
    outer = circle_outline(.451)
    # Closed underside slopes into the lens seat, with downward-facing normals.
    band(housing, outer, circle_outline(.315), .18, .40, up=False)
    housing.prism_walls(outer, .18, .48)
    band(meshes["BeveledSteel"], circle_outline(.451), circle_outline(.435), .48, 1.12)
    band(housing, circle_outline(.435), circle_outline(.348), 1.12, 1.12)
    band(meshes["BeveledSteel"], circle_outline(.348), circle_outline(.327), 1.12, .70)
    band(meshes["Recess"], circle_outline(.327), circle_outline(.315), .70, .40)
    spherical_lens(meshes["EnergyLens"])
    # Eight interrupted inlays form a bright circular energy track in the 3D ring.
    for segment in range(8):
        start, end = math.radians(segment * 45 + 6), math.radians(segment * 45 + 39)
        arc_strip(meshes["Recess"], .382, .408, start, end, 1.119, 1.122)
        arc_strip(meshes["EnergyInlay"], .387, .402, start, end, 1.124, 1.147)
    for quarter in range(4):
        angle = quarter * math.pi * .5
        clamp = [(.904, .458), (.972, .458), (.988, .478), (.988, .522),
                 (.972, .542), (.904, .542)]
        clamp = [rotated(p, angle) for p in clamp]
        prism(housing, clamp, .18, 1.32)
        top = [(.915, .470), (.961, .470), (.974, .484), (.974, .516),
               (.961, .530), (.915, .530)]
        top = [rotated(p, angle) for p in top]
        prism(meshes["BeveledSteel"], top, 1.32, 1.40)
        light = [(.935, .484), (.955, .484), (.955, .516), (.935, .516)]
        prism(meshes["EnergyInlay"], [rotated(p, angle) for p in light], 1.401, 1.425)
    return meshes


def control_pedestal_geometry():
    """The hip-height, north-edge pedestal for the pulse button."""
    names = ("Housing", "BeveledSteel", "Recess", "EnergyPanel", "Engraving", "EnergyInlay")
    meshes = {name: Primitive() for name in names}
    # Position the middle of the press face, rather than the pedestal edge,
    # at .60 units above the floor for the 1.1-unit player character.
    face_center_height = .60
    pedestal_rise = face_center_height - (.26 + .041 * math.sqrt(.5))

    def pedestal_outline(width, depth):
        return [(.5 + (x - .5) * width, .19 + (y - .5) * depth)
                for x, y in pillow_outline(.48)]

    base = pedestal_outline(.46, .33)
    shoulder = pedestal_outline(.42, .29)
    meshes["Housing"].fan((.5, .19), base, 0, False)
    meshes["Housing"].prism_walls(base, 0, .069)
    band(meshes["BeveledSteel"], base, shoulder, .069, .102)
    meshes["Housing"].fan((.5, .19), shoulder, .102, True)
    # A recessed seam separates the wide foot from the narrower sloping mount.
    band(meshes["Engraving"], pedestal_outline(.461, .331),
         pedestal_outline(.459, .329), .029, .036)

    # The support's sloping top follows the back of the 45-degree control face.
    support = [(.352, .059), (.648, .059), (.668, .079), (.668, .273),
               (.648, .293), (.352, .293), (.332, .273), (.332, .079)]
    bottom = .102
    height = lambda y: .303 + pedestal_rise - (y - .059) * .72
    center = (.5, .176, height(.176))
    meshes["Housing"].fan((.5, .176), support, bottom, False)
    for i, p in enumerate(support):
        q = support[(i + 1) % len(support)]
        outward = (q[1] - p[1], p[0] - q[0], 0)
        meshes["Housing"].quad((*p, bottom), (*q, bottom),
                               (*q, height(q[1])), (*p, height(p[1])), outward)
        meshes["BeveledSteel"].triangle(center, (*p, height(p[1])),
                                       (*q, height(q[1])), (0, .72, 1))
    # Rear vents and a front status inlay give the pedestal a mechanical body.
    for x in (.401, .433, .465, .497, .529, .561, .593):
        meshes["Recess"].quad((x, .0585, .148 + pedestal_rise),
                              (x + .013, .0585, .148 + pedestal_rise),
                              (x + .013, .0585, .219 + pedestal_rise),
                              (x, .0585, .219 + pedestal_rise), (0, -1, 0))
    meshes["Recess"].quad((.442, .294, .111 + pedestal_rise),
                          (.558, .294, .111 + pedestal_rise),
                          (.558, .294, .128 + pedestal_rise),
                          (.442, .294, .128 + pedestal_rise), (0, 1, 0))
    meshes["EnergyInlay"].quad((.451, .2945, .116 + pedestal_rise),
                               (.549, .2945, .116 + pedestal_rise),
                               (.549, .2945, .123 + pedestal_rise),
                               (.451, .2945, .123 + pedestal_rise), (0, 1, 0))
    return meshes, pedestal_rise


def pulse_button_geometry(components=False):
    """The complete button, or its exact fixed/pressable triangle partition."""
    meshes, pedestal_rise = control_pedestal_geometry()
    names = tuple(meshes)

    # Author the round control in a local XY plane, then tilt it toward +y.
    face = {name: Primitive() for name in names}
    outer = circle_outline(.175)
    face["Housing"].fan(CENTER, outer, -.068, False)
    face["Housing"].prism_walls(outer, -.068, -.020)
    band(face["BeveledSteel"], outer, circle_outline(.159), -.020, .013)
    band(face["Housing"], circle_outline(.159), circle_outline(.143), .013, .013)
    band(face["BeveledSteel"], circle_outline(.143), circle_outline(.134), .013, .003)
    band(face["Recess"], circle_outline(.134), circle_outline(.121), .003, -.011)
    face["Recess"].prism_walls(circle_outline(.121), -.011, .018)
    # The luminous press cap has a short sidewall and a softly beveled face.
    face["EnergyPanel"].prism_walls(circle_outline(.120), .008, .025)
    band(face["EnergyPanel"], circle_outline(.120), circle_outline(.109), .025, .041)
    face["EnergyPanel"].fan(CENTER, circle_outline(.109), .041, True)
    band(face["Engraving"], circle_outline(.085), circle_outline(.080), .042, .042)
    # Three restrained grooves keep the face tactile and button-shaped.
    for offset in (-.018, 0, .018):
        ribbon(face["Engraving"], [(.471, .5 + offset), (.529, .5 + offset)], .004, .043)
    # Capture the cap before adding the stationary bezel's engraved screws.
    # Material alone cannot identify the moving grooves: the pedestal and
    # bezel deliberately use the same engraving material.
    cap_counts = {name: len(face[name].positions)
                  for name in ("EnergyPanel", "Engraving")} if components else {}
    for sector in range(8):
        start = math.radians(sector * 45 + 7)
        end = math.radians(sector * 45 + 38)
        arc_strip(face["Recess"], .147, .157, start, end, .0125, .0145)
        arc_strip(face["EnergyInlay"], .149, .155, start, end, .0145, .017)

    def small_circle(x, y, radius, segments=24):
        return [(x + radius * math.cos(2 * math.pi * i / segments),
                 y + radius * math.sin(2 * math.pi * i / segments))
                for i in range(segments)]

    # Four slotted bezel screws sit on the outer steel shoulder, clear of inlays.
    for quarter in range(4):
        angle = math.radians(quarter * 90 + 45)
        x, y = .5 + .166 * math.cos(angle), .5 + .166 * math.sin(angle)
        head = small_circle(x, y, .009)
        face["Recess"].fan((x, y), small_circle(x, y, .011), -.0055, True)
        face["BeveledSteel"].prism_walls(head, -.006, .005)
        band(face["BeveledSteel"], head, small_circle(x, y, .007), .005, .008)
        face["BeveledSteel"].fan((x, y), small_circle(x, y, .007), .008, True)
        ribbon(face["Engraving"], [(x - .005, y), (x + .005, y)], .0025, .0085)

    tilt = math.sqrt(.5)
    cap_offsets = {}
    for name, local in face.items():
        mesh = meshes[name]
        if name in cap_counts:
            cap_offsets[name] = len(mesh.positions)
        mesh.positions.extend((x, .20 + (y - .5) * tilt + z * tilt,
                               .26 + pedestal_rise - (y - .5) * tilt + z * tilt)
                              for x, y, z in local.positions)
        mesh.normals.extend((x, y * tilt + z * tilt, -y * tilt + z * tilt)
                            for x, y, z in local.normals)
    # Two mounting bolts in the visible corners of the pedestal's foot.
    for x in (.317, .683):
        y = .29
        bolt = small_circle(x, y, .008)
        prism(meshes["BeveledSteel"], bolt, .104, .112)
        ribbon(meshes["Engraving"], [(x - .005, y), (x + .005, y)], .0025, .113)
    if components:
        cap = {name: Primitive() for name in cap_counts}
        for name, count in cap_counts.items():
            start = cap_offsets[name]
            cap[name].positions = meshes[name].positions[start:start + count]
            cap[name].normals = meshes[name].normals[start:start + count]
            del meshes[name].positions[start:start + count]
            del meshes[name].normals[start:start + count]
        return {name: mesh for name, mesh in meshes.items() if mesh.positions}, cap
    return meshes


def pulse_button_components():
    """Button parts; cap travel is .024 along engine (0,-sqrt(.5),-sqrt(.5))."""
    base, cap = pulse_button_geometry(components=True)
    hidden_base, hidden_cap = Primitive(), Primitive()
    # The original seat ends at local depth -.011. Continue it far enough
    # behind the face to cover the .024-unit press without an open hole.
    inward_walls(hidden_base, circle_outline(.121), -.035, -.011)
    hidden_base.fan(CENTER, circle_outline(.121), -.035, True)
    # Complete the cap behind its original sidewall. Its slight inward taper
    # clears the seat, and its pressed underside stays above depth -.035.
    band(hidden_cap, circle_outline(.120), circle_outline(.119), .008, -.004, up=False)
    hidden_cap.fan(CENTER, circle_outline(.119), -.004, False)
    face_origin_z = .60 - .041 * math.sqrt(.5)
    tilt = math.sqrt(.5)
    for target, local in ((base["Recess"], hidden_base),
                          (cap["EnergyPanel"], hidden_cap)):
        target.positions.extend((x, .20 + (y - .5) * tilt + z * tilt,
                                 face_origin_z - (y - .5) * tilt + z * tilt)
                                for x, y, z in local.positions)
        target.normals.extend((x, y * tilt + z * tilt, -y * tilt + z * tilt)
                              for x, y, z in local.normals)
    return base, cap


def axial_profile(mesh, origin, axis, profile, segments=32, radial_axis=None):
    """Closed metal shafts and rounded grips, swept about an arbitrary axis."""
    axis = _normalized(axis)
    reference = (0, 0, 1) if abs(axis[2]) < .9 else (0, 1, 0)
    u = _normalized(radial_axis if radial_axis is not None else _cross(axis, reference))
    v = _cross(axis, u)

    def ring_point(distance, radius, segment):
        angle = 2 * math.pi * segment / segments
        return tuple(origin[i] + axis[i] * distance +
                     radius * (u[i] * math.cos(angle) + v[i] * math.sin(angle))
                     for i in range(3))

    for (a, ra), (b, rb) in zip(profile, profile[1:]):
        for segment in range(segments):
            angle = 2 * math.pi * (segment + .5) / segments
            outward = tuple(u[i] * math.cos(angle) + v[i] * math.sin(angle) for i in range(3))
            mesh.quad(ring_point(a, ra, segment), ring_point(a, ra, segment + 1),
                      ring_point(b, rb, segment + 1), ring_point(b, rb, segment), outward)
    for (distance, radius), sign in ((profile[0], -1), (profile[-1], 1)):
        center = tuple(origin[i] + axis[i] * distance for i in range(3))
        for segment in range(segments):
            mesh.triangle(center, ring_point(distance, radius, segment),
                          ring_point(distance, radius, segment + 1),
                          tuple(component * sign for component in axis))


def lever_geometry(active=False):
    """A wooden floor lever between the symmetric top quarters of a circle.

    The north housing runs east/west, parallel to its tile edge. Only the
    handle rotates: off leans west, on leans east around the y-axis hinge.
    """
    names = ("LeverHousing", "LeverSteel", "Recess", "LeverWood", "LeverGrip")
    meshes = {name: Primitive() for name in names}
    housing, steel = meshes["LeverHousing"], meshes["LeverSteel"]

    def foot_outline(width, depth):
        return [(.5 + (x - .5) * width, .20 + (y - .5) * depth)
                for x, y in pillow_outline(.48)]

    outer, shoulder = foot_outline(.56, .29), foot_outline(.52, .25)
    housing.fan((.5, .20), outer, 0, False)
    housing.prism_walls(outer, 0, .043)
    band(steel, outer, shoulder, .043, .063)

    # Match the angular samples of the foot to a rectangular opening. The
    # top is an annulus, so it leaves a physical hole down to the dark floor.
    channel = []
    for step in range(SEGMENTS):
        angle = 2 * math.pi * step / SEGMENTS
        c, s = math.cos(angle), math.sin(angle)
        radius = min(.221 / max(abs(c), 1e-12), .050 / max(abs(s), 1e-12))
        channel.append((.5 + radius * c, .20 + radius * s))
    band(housing, shoulder, channel, .063, .063)
    meshes["Recess"].fan((.5, .20), channel, .012, True)
    for i, p in enumerate(channel):
        q = channel[(i + 1) % len(channel)]
        meshes["Recess"].quad((*p, .012), (*q, .012), (*q, .063), (*p, .063),
                              (p[1] - q[1], q[0] - p[0], 0))

    # Keep only the upper quarter of a complete circle's height. A symmetric
    # 30..150-degree arc closes with a horizontal chord directly on the foot.
    # The circle itself is centered below the foot; only this shallow cap exists.
    arch = []
    for step in range(49):
        angle = math.pi / 6 + math.pi * 2 / 3 * step / 48
        height = .063 if step in (0, 48) else -.067 + .26 * math.sin(angle)
        arch.append((.5 + .26 * math.cos(angle), height))
    inset = [(.5 + (x - .5) * .96, .11 + (z - .11) * .96) for x, z in arch]

    def arch_cheek(y0, y1, rear_material, front_material):
        center = tuple(sum(p[i] for p in inset) / len(inset) for i in range(2))
        for i, p in enumerate(arch):
            j = (i + 1) % len(arch)
            q, ip, iq = arch[j], inset[i], inset[j]
            outward = (q[1] - p[1], 0, p[0] - q[0])
            housing.quad((p[0], y0 + .006, p[1]),
                         (q[0], y0 + .006, q[1]),
                         (q[0], y1 - .006, q[1]),
                         (p[0], y1 - .006, p[1]), outward)
            for y, rim_y, sign, material_name in (
                    (y0, y0 + .006, -1, rear_material),
                    (y1, y1 - .006, 1, front_material)):
                normal = (0, sign, 0)
                steel.quad((ip[0], y, ip[1]), (iq[0], y, iq[1]),
                           (q[0], rim_y, q[1]), (p[0], rim_y, p[1]), normal)
                meshes[material_name].triangle((center[0], y, center[1]),
                                               (ip[0], y, ip[1]), (iq[0], y, iq[1]), normal)

    arch_cheek(.112, .150, "LeverSteel", "Recess")
    arch_cheek(.250, .288, "Recess", "LeverSteel")

    def small_circle(x, y, radius, segments=24):
        return [(x + radius * math.cos(2 * math.pi * i / segments),
                 y + radius * math.sin(2 * math.pi * i / segments))
                for i in range(segments)]

    # The wood passes directly between the caps with no cross-slot metal tube.
    pivot = (.5, .20, .135)

    angle = math.radians(20)
    direction = ((1 if active else -1) * math.sin(angle), 0, math.cos(angle))
    # The mostly wooden shaft continues through the hinge and into the slit,
    # below its floor. The upper grip overlaps the wood so no join floats.
    axial_profile(meshes["LeverWood"], pivot, direction,
                  [(-.135, .020), (-.110, .026), (.645, .026), (.675, .025)],
                  radial_axis=(0, 1, 0))
    axial_profile(meshes["LeverGrip"], pivot, direction,
                  [(.650, .036), (.665, .056), (.810, .056), (.825, .041), (.835, 0)],
                  radial_axis=(0, 1, 0))

    # Just two round fasteners secure the simple foot to the ground.
    for x in (.269, .731):
        prism(steel, small_circle(x, .20, .010), .063, .077)
    return meshes


def lever_components():
    """The fixed housing and original off handle, hinged at (.5,.20,.135).

    Rotating the handle +40 degrees around engine Y reaches the on pose.
    The wood and colored grip move as one solid assembly.
    """
    meshes = lever_geometry(False)
    moving = ("LeverWood", "LeverGrip")
    return ({name: mesh for name, mesh in meshes.items() if name not in moving},
            {name: mesh for name, mesh in meshes.items() if name in moving})


def validate_geometry(meshes):
    """Reject degeneracy, non-unit normals, invalid winding and tile overhangs."""
    for name, mesh in meshes.items():
        assert mesh.positions and len(mesh.positions) == len(mesh.normals), name
        for position, normal in zip(mesh.positions, mesh.normals):
            assert all(math.isfinite(value) for value in (*position, *normal)), name
            assert 0 <= position[0] <= 1 and 0 <= position[1] <= 1 and position[2] >= 0, (name, position)
            assert abs(_dot(normal, normal) - 1) < 1e-6, (name, normal)
        for i in range(0, len(mesh.positions), 3):
            a, b, c = mesh.positions[i:i + 3]
            face = _cross(_sub(b, a), _sub(c, a))
            assert _dot(face, face) > 1e-18, (name, i)
            normals = mesh.normals[i:i + 3]
            assert _dot(face, tuple(sum(n[j] for n in normals) for j in range(3))) > 0, (name, i)


def write_glb(path, meshes):
    validate_geometry(meshes)
    binary = bytearray()
    views, accessors, primitives = [], [], []

    def accessor(data, count, kind, low=None, high=None, component=5126, target=34962):
        binary.extend(b"\0" * (-len(binary) % 4))
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data), "target": target})
        binary.extend(data)
        record = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
        if low is not None:
            record.update(min=low, max=high)
        accessors.append(record)
        return len(accessors) - 1

    for material_index, mesh in enumerate(meshes.values()):
        positions = [_to_gltf(p) for p in mesh.positions]
        normals = [_to_gltf(n) for n in mesh.normals]
        count = len(positions)
        low = [min(p[i] for p in positions) for i in range(3)]
        high = [max(p[i] for p in positions) for i in range(3)]
        pack = lambda vectors: b"".join(struct.pack("<3f", *v) for v in vectors)
        position = accessor(pack(positions), count, "VEC3", low, high)
        normal = accessor(pack(normals), count, "VEC3")
        uv = accessor(b"".join(struct.pack("<2f", p[0], p[1]) for p in mesh.positions), count, "VEC2")
        indices = accessor(struct.pack(f"<{count}I", *range(count)), count, "SCALAR", component=5125, target=34963)
        primitives.append({"attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv},
                           "indices": indices, "material": material_index, "mode": 4})
    document = {
        "asset": {"version": "2.0", "generator": "tools/make_energy_plate_models.py"},
        "extensionsUsed": ["KHR_materials_emissive_strength"],
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": path.stem}],
        "meshes": [{"name": path.stem, "primitives": primitives}],
        "materials": [MATERIALS[name] for name in meshes],
        "buffers": [{"byteLength": len(binary)}], "bufferViews": views, "accessors": accessors,
    }
    payload = json.dumps(document, sort_keys=True, separators=(",", ":")).encode("utf-8")
    payload += b" " * (-len(payload) % 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 28 + len(payload) + len(binary)) +
                    struct.pack("<II", len(payload), 0x4E4F534A) + payload +
                    struct.pack("<II", len(binary), 0x004E4942) + binary)
    positions = [p for mesh in meshes.values() for p in mesh.positions]
    low = [round(min(p[i] for p in positions), 5) for i in range(3)]
    high = [round(max(p[i] for p in positions), 5) for i in range(3)]
    triangles = len(positions) // 3
    print(f"{path.name}: {triangles} triangles, engine bounds {low} .. {high}, {len(meshes)} materials")


def main():
    write_glb(OUTPUT_DIRECTORY / "pressure_plate.glb", pressure_geometry())
    write_glb(OUTPUT_DIRECTORY / "end_plate.glb", end_geometry())
    write_glb(OUTPUT_DIRECTORY / "pulse_button.glb", pulse_button_geometry())
    write_glb(OUTPUT_DIRECTORY / "lever_off.glb", lever_geometry(False))
    write_glb(OUTPUT_DIRECTORY / "lever_on.glb", lever_geometry(True))
    for prefix, moving_name, components in (
            ("pressure_plate", "pad", pressure_components()),
            ("pulse_button", "cap", pulse_button_components()),
            ("lever", "handle", lever_components())):
        base, moving = components
        write_glb(OUTPUT_DIRECTORY / f"{prefix}_base.glb", base)
        write_glb(OUTPUT_DIRECTORY / f"{prefix}_{moving_name}.glb", moving)


if __name__ == "__main__":
    main()
