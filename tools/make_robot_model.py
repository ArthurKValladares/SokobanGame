#!/usr/bin/env python3
"""Generate the robot's editable, deterministic source mesh and optional preview.

Run `python tools/make_robot_model.py`; add --preview for a PNG contact sheet.
The GLB uses native PBR colors, with no external texture dependencies. Geometry
is authored in engine coordinates (X east, Y south, Z up), then converted to
glTF (X, Z, -Y). The engine loads it with preserveSourceScale. Its leading end
faces south, matching facingQuarterTurns; runtime rotations swap its bright
honey lanterns and dim ember tail lamps when the motor reverses. The level's
walkable oak deck is Z=1. Copper pipes, gears and riveted bronze armor give it
a rustic steam-powered appearance without obstructing its rider.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

from make_rotator_models import Primitive, _to_gltf

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "assets/custom/models/robot.glb"
SOURCE = ROOT / "assets/custom/source/robot"
DRIVE_DURATION = 2.0
SHOES_PER_SIDE = 24
TRACK_RADIUS = 1.0 / math.tau


def rotate_x(vector, angle):
    x, y, z = vector
    c, s = math.cos(angle), math.sin(angle)
    return (x, y * c - z * s, y * s + z * c)


def track_pose(distance):
    """Two-unit capsule loop. Positive phase drives the lower shoes toward -Y."""
    phase = distance % DRIVE_DURATION
    if phase < 0.5:
        return (0.75 - phase, 0.23 - TRACK_RADIUS, math.pi)
    if phase < 1.0:
        angle = -(phase - 0.5) / TRACK_RADIUS
        phi = -math.pi / 2 + angle
        return (0.25 + TRACK_RADIUS * math.cos(phi),
                0.23 + TRACK_RADIUS * math.sin(phi), math.pi + angle)
    if phase < 1.5:
        return (0.25 + phase - 1.0, 0.23 + TRACK_RADIUS, 0.0)
    angle = -(phase - 1.5) / TRACK_RADIUS
    phi = math.pi / 2 + angle
    return (0.75 + TRACK_RADIUS * math.cos(phi),
            0.23 + TRACK_RADIUS * math.sin(phi), angle)


class Rig:
    def __init__(self):
        self.bones = [{"name": "RobotBody", "pivot": (0, 0, 0),
                       "angle": 0.0, "kind": "body"}]
        self.current = 0
        self.local = False

    def bone(self, name, pivot, angle=0.0, kind="wheel", phase=0.0):
        self.bones.append({"name": name, "pivot": pivot, "angle": angle,
                           "kind": kind, "phase": phase})
        return len(self.bones) - 1

    def use(self, joint=0, local=False):
        self.current, self.local = joint, local


class RigidPrimitive(Primitive):
    def __init__(self, rig):
        super().__init__()
        self.rig = rig
        self.joints = []

    def triangle(self, a, b, c, outward):
        if self.rig.local:
            bone = self.rig.bones[self.rig.current]
            pivot, angle = bone["pivot"], bone["angle"]
            a, b, c = [tuple(v + p for v, p in zip(rotate_x(point, angle), pivot))
                       for point in (a, b, c)]
            outward = rotate_x(outward, angle)
        count = len(self.positions)
        super().triangle(a, b, c, outward)
        self.joints.extend([self.rig.current] * (len(self.positions) - count))


def material(name, color, metallic=0.0, roughness=0.5, emissive=None):
    result = {"name": name, "pbrMetallicRoughness": {
        "baseColorFactor": [*color, 1.0],
        "metallicFactor": metallic, "roughnessFactor": roughness}}
    if emissive:
        result["emissiveFactor"] = emissive
    return result


MATERIALS = [
    material("Aged bronze boiler armor", (0.30, 0.145, 0.065), 0.68, 0.68),
    material("Worn brass deck rim", (0.48, 0.295, 0.105), 0.75, 0.58),
    material("Forged iron track links", (0.095, 0.09, 0.075), 0.72, 0.77),
    material("Blackened wrought iron straps", (0.055, 0.048, 0.038), 0.7, 0.64),
    material("Weathered cream paint", (0.81, 0.735, 0.52), 0.0, 0.86),
    material("Leading honey lantern glass", (1.0, 0.58, 0.13), 0.0, 0.25,
             (1.0, 0.40, 0.045)),
    material("Trailing ember lantern glass", (0.43, 0.065, 0.014), 0.0, 0.35,
             (0.21, 0.025, 0.003)),
    material("Tarnished brass rivets and collars", (0.57, 0.375, 0.135), 0.8, 0.49),
    material("Weathered oak planks", (0.245, 0.135, 0.052), 0.0, 0.92),
    material("Dark oak grain and alternate planks", (0.16, 0.077, 0.025), 0.0, 0.97),
    material("Hammered copper pipes", (0.49, 0.20, 0.078), 0.78, 0.54),
    material("Old russet oxidation", (0.24, 0.073, 0.025), 0.25, 0.94),
]


def outline(x0, y0, x1, y1, cut):
    return [(x0 + cut, y0), (x1 - cut, y0), (x1, y0 + cut),
            (x1, y1 - cut), (x1 - cut, y1), (x0 + cut, y1),
            (x0, y1 - cut), (x0, y0 + cut)]


def prism(primitive, points, bottom, top):
    primitive.prism_walls(points, bottom, top)
    center = (sum(p[0] for p in points) / len(points),
              sum(p[1] for p in points) / len(points))
    primitive.fan(center, points, bottom, False)
    primitive.fan(center, points, top, True)


def box(primitive, x0, y0, z0, x1, y1, z1):
    prism(primitive, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1)


def loft(primitive, rings):
    for (lower, z0), (upper, z1) in zip(rings, rings[1:]):
        for i, p in enumerate(lower):
            nxt = (i + 1) % len(lower)
            q = lower[nxt]
            outward = (q[1] - p[1], p[0] - q[0], 0.0)
            primitive.quad((*p, z0), (*q, z0), (*upper[nxt], z1),
                           (*upper[i], z1), outward)
    primitive.fan((0.5, 0.5), rings[0][0], rings[0][1], False)
    primitive.fan((0.5, 0.5), rings[-1][0], rings[-1][1], True)


def plane_point(center, axis, u, v, depth):
    if axis == "x":
        return (center[0] + depth, center[1] + u, center[2] + v)
    if axis == "y":
        return (center[0] + u, center[1] + depth, center[2] + v)
    return (center[0] + u, center[1] + v, center[2] + depth)


def axis_normal(axis, sign):
    return {"x": (sign, 0, 0), "y": (0, sign, 0), "z": (0, 0, sign)}[axis]


def extrude(primitive, center, axis, points, length):
    """Extrude a circle or star-shaped polygon in a named axial plane."""
    bottom, top = -length / 2, length / 2
    for i, (u, v) in enumerate(points):
        nu, nv = points[(i + 1) % len(points)]
        a = plane_point(center, axis, u, v, bottom)
        b = plane_point(center, axis, nu, nv, bottom)
        c = plane_point(center, axis, nu, nv, top)
        d = plane_point(center, axis, u, v, top)
        outward = plane_point((0, 0, 0), axis, nv - v, u - nu, 0)
        primitive.quad(a, b, c, d, outward)
        primitive.triangle(plane_point(center, axis, 0, 0, bottom), b, a,
                           axis_normal(axis, -1))
        primitive.triangle(plane_point(center, axis, 0, 0, top), d, c,
                           axis_normal(axis, 1))


def circle(radius, segments=16):
    return [(radius * math.cos(i * math.tau / segments),
             radius * math.sin(i * math.tau / segments)) for i in range(segments)]


def cylinder(primitive, center, axis, radius, length, segments=16):
    extrude(primitive, center, axis, circle(radius, segments), length)


def ring(primitive, center, axis, outer, inner, length):
    """Extruded annulus, also used for the elongated iron track chains."""
    for i, (u, v) in enumerate(outer):
        nxt = (i + 1) % len(outer)
        nu, nv = outer[nxt]
        iu, iv = inner[i]
        niu, niv = inner[nxt]
        outward = plane_point((0, 0, 0), axis, nv - v, u - nu, 0)
        inward = plane_point((0, 0, 0), axis, iv - niv, niu - iu, 0)
        for depth, sign in ((-length / 2, -1), (length / 2, 1)):
            primitive.quad(plane_point(center, axis, u, v, depth),
                           plane_point(center, axis, nu, nv, depth),
                           plane_point(center, axis, niu, niv, depth),
                           plane_point(center, axis, iu, iv, depth), axis_normal(axis, sign))
        primitive.quad(plane_point(center, axis, u, v, -length / 2),
                       plane_point(center, axis, nu, nv, -length / 2),
                       plane_point(center, axis, nu, nv, length / 2),
                       plane_point(center, axis, u, v, length / 2), outward)
        primitive.quad(plane_point(center, axis, iu, iv, -length / 2),
                       plane_point(center, axis, niu, niv, -length / 2),
                       plane_point(center, axis, niu, niv, length / 2),
                       plane_point(center, axis, iu, iv, length / 2), inward)


def cog(primitive, center, radius, length):
    points = []
    for tooth in range(12):
        for offset, factor in ((0.0, 0.78), (0.18, 1.0), (0.70, 1.0), (0.88, 0.78)):
            angle = (tooth + offset) * math.tau / 12
            points.append((radius * factor * math.cos(angle),
                           radius * factor * math.sin(angle)))
    extrude(primitive, center, "x", points, length)


def clipped_strip(points, low, high):
    for edge, keep_greater in ((low, True), (high, False)):
        output = []
        for index, b in enumerate(points):
            a = points[index - 1]
            a_inside = a[0] >= edge if keep_greater else a[0] <= edge
            b_inside = b[0] >= edge if keep_greater else b[0] <= edge
            if a_inside != b_inside:
                t = (edge - a[0]) / (b[0] - a[0])
                output.append((edge, a[1] + t * (b[1] - a[1])))
            if b_inside:
                output.append(b)
        points = output
    return points


def robot():
    rig = Rig()
    p = [RigidPrimitive(rig) for _ in MATERIALS]
    # Open linked-iron tracks expose their rollers and axles, rather than a
    # solid rubber shell. Raised shoes have oxidized edges and brass hub caps.
    def chain_outline(radius):
        return [(center + radius * math.cos(angle), radius * math.sin(angle))
                for center, start in ((0.25, -math.pi / 2), (-0.25, math.pi / 2))
                for angle in (start + i * math.pi / 12 for i in range(13))]
    chain_outer, chain_inner = chain_outline(0.147), chain_outline(0.115)
    for side_name, x, outward in (("L", 0.14, -1), ("R", 0.86, 1)):
        ring(p[2], (x, 0.5, 0.23), "x", chain_outer, chain_inner, 0.20)
        for shoe in range(SHOES_PER_SIDE):
            phase = shoe * DRIVE_DURATION / SHOES_PER_SIDE
            y, z, angle = track_pose(phase)
            joint = rig.bone(f"RobotShoe{side_name}{shoe:02}", (x, y, z),
                             angle, "shoe", phase)
            rig.use(joint, local=True)
            box(p[2], -0.108, -0.035, -0.015, 0.108, 0.035, 0.015)
            box(p[11], -0.110, -0.033, 0.010, 0.110, -0.016, 0.020)
            box(p[3], -0.111, -0.008, 0.013, 0.111, 0.008, 0.023)
        rig.use()
        for roller, y in enumerate((0.24, 0.50, 0.76)):
            rig.use(rig.bone(f"RobotRoller{side_name}{roller}", (x, y, 0.23)))
            cylinder(p[11], (x, y, 0.23), "x", 0.137, 0.16)
            cylinder(p[3], (x + outward * 0.085, y, 0.23), "x", 0.103, 0.024)
            cylinder(p[7], (x + outward * 0.10, y, 0.23), "x", 0.037, 0.024, 10)
            cylinder(p[3], (x + outward * 0.114, y, 0.23), "x", 0.015, 0.008, 6)
            for angle in (0.0, math.tau / 3, math.tau * 2 / 3):
                cylinder(p[7], (x + outward * 0.100,
                               y + math.cos(angle) * 0.069,
                               0.23 + math.sin(angle) * 0.069), "x", 0.009, 0.020, 8)
        rig.use()
    box(p[3], 0.19, 0.20, 0.24, 0.81, 0.80, 0.50)
    # Riveted boiler armor and a broad, battered brass shoulder.
    body = outline(0.055, 0.04, 0.945, 0.96, 0.16)
    shoulder = outline(0.035, 0.025, 0.965, 0.975, 0.16)
    top = outline(0.08, 0.075, 0.92, 0.925, 0.14)
    loft(p[0], [(outline(0.11, 0.09, 0.89, 0.91, 0.14), 0.40),
                (body, 0.48), (body, 0.84)])
    loft(p[1], [(body, 0.83), (shoulder, 0.88), (top, 0.99)])
    prism(p[3], outline(0.105, 0.10, 0.895, 0.90, 0.14), 0.985, 0.998)
    # Six oak boards are individually clipped to the octagonal deck. Narrow
    # seams, grain cuts and dark nail heads retain a flat standing surface.
    deck = outline(0.13, 0.125, 0.87, 0.875, 0.115)
    for plank in range(6):
        low = 0.13 + plank * 0.74 / 6 + 0.002
        high = 0.13 + (plank + 1) * 0.74 / 6 - 0.002
        points = clipped_strip(deck, low, high)
        prism(p[8 if plank % 3 else 9], points, 0.994, 1.0)
        center_x = (low + high) / 2
        min_y = min(point[1] for point in points)
        max_y = max(point[1] for point in points)
        for offset in (-0.027, 0.022):
            for segment in range(5):
                y0 = 0.25 + segment * 0.10
                y1 = y0 + 0.10
                x0 = center_x + offset + math.sin(plank * 2 + segment) * 0.006
                x1 = center_x + offset + math.sin(plank * 2 + segment + 1) * 0.006
                p[9].quad((x0, y0, 1.0005), (x0 + 0.0025, y0, 1.0005),
                          (x1 + 0.0025, y1, 1.0005), (x1, y1, 1.0005), (0, 0, 1))
        for y in (min_y + 0.045, max_y - 0.045):
            cylinder(p[3], (center_x, y, 1.0005), "z", 0.009, 0.002, 8)
    # Painted opposing cream arrows indicate the patrol axis. Directional
    # lanterns provide the active heading, leaving both deck arrows rustic.
    for sign, x in ((1, 0.39), (-1, 0.61)):
        arrow = [(x - 0.035, -0.14), (x + 0.035, -0.14),
                 (x + 0.035, 0.075), (x + 0.085, 0.075),
                 (x, 0.23), (x - 0.085, 0.075), (x - 0.035, 0.075)]
        points = [(a, 0.5 + b * sign) for a, b in arrow]
        if sign < 0:
            points.reverse()
        prism(p[4], points, 1.002, 1.004)
    # Round miner's lanterns in tarnished brass flanges replace modern sockets.
    for sign, slot in ((-1, 6), (1, 5)):
        body_y = 0.5 + sign * 0.44
        lens_y = 0.5 + sign * 0.491
        for x in (0.32, 0.68):
            cylinder(p[3], (x, body_y, 0.675), "y", 0.108, 0.09)
            cylinder(p[7], (x, 0.5 + sign * 0.48, 0.675), "y", 0.096, 0.026)
            cylinder(p[slot], (x, lens_y, 0.675), "y", 0.069, 0.018, 20)
            # Four visible screw heads on each brass lantern flange.
            for angle in (0, math.pi / 2, math.pi, math.pi * 1.5):
                cylinder(p[3], (x + math.cos(angle) * 0.084,
                               0.5 + sign * 0.493,
                               0.675 + math.sin(angle) * 0.084), "y", 0.008, 0.008, 6)
        # Vertical iron boiler strap and two rows of dome-headed rivets.
        y0, y1 = (0.034, 0.052) if sign < 0 else (0.948, 0.966)
        box(p[3], 0.465, y0, 0.48, 0.535, y1, 0.84)
        for z in (0.52, 0.61, 0.73, 0.81):
            cylinder(p[7], (0.5, 0.5 + sign * 0.47, z), "y", 0.013, 0.016, 8)
        for x in (0.25, 0.375, 0.625, 0.75):
            cylinder(p[7], (x, 0.5 + sign * 0.473, 0.505), "y", 0.011, 0.012, 8)
    # Black iron hoops, brass bolt heads and exposed side transmission gears.
    for side_name, side, outward in (("L", 0.055, -1), ("R", 0.945, 1)):
        for y in (0.25, 0.75):
            box(p[3], side - 0.012, y - 0.027, 0.47,
                side + 0.012, y + 0.027, 0.85)
            for z in (0.50, 0.79):
                cylinder(p[7], (side + outward * 0.019, y, z), "x", 0.012, 0.014, 8)
        rig.use(rig.bone(f"RobotCog{side_name}",
                        (side + outward * 0.025, 0.53, 0.65), kind="cog"))
        cog(p[11], (side + outward * 0.025, 0.53, 0.65), 0.135, 0.042)
        cylinder(p[3], (side + outward * 0.040, 0.53, 0.65), "x", 0.095, 0.010)
        for angle in range(6):
            a = angle * math.tau / 6
            cylinder(p[7], (side + outward * 0.047,
                           0.53 + math.cos(a) * 0.075,
                           0.65 + math.sin(a) * 0.075), "x", 0.012, 0.008, 8)
        cylinder(p[7], (side + outward * 0.038, 0.53, 0.65), "x", 0.043, 0.026, 12)
        cylinder(p[3], (side + outward * 0.043, 0.53, 0.65), "x", 0.018, 0.022, 6)
        rig.use()
        # Copper return pipe, elbow and brass compression collars.
        pipe_x = side + outward * 0.028
        cylinder(p[10], (pipe_x, 0.52, 0.815), "y", 0.018, 0.37, 12)
        cylinder(p[10], (pipe_x, 0.705, 0.735), "z", 0.018, 0.16, 12)
        cylinder(p[7], (pipe_x, 0.705, 0.815), "z", 0.026, 0.045, 12)
        for y in (0.36, 0.67):
            cylinder(p[7], (pipe_x, y, 0.815), "y", 0.026, 0.030, 12)
    # A cream pressure gauge and rust-red handwheel sit below the deck.
    cylinder(p[7], (0.019, 0.31, 0.69), "x", 0.074, 0.030)
    cylinder(p[4], (0.006, 0.31, 0.69), "x", 0.056, 0.010)
    p[3].quad((0.0005, 0.306, 0.687), (0.0005, 0.314, 0.692),
              (0.0005, 0.287, 0.729), (0.0005, 0.283, 0.724), (-1, 0, 0))
    cylinder(p[3], (0.002, 0.31, 0.69), "x", 0.009, 0.003, 8)
    ring(p[11], (0.985, 0.31, 0.69), "x", circle(0.065), circle(0.050), 0.024)
    for angle in range(4):
        a = angle * math.pi / 2
        dy, dz = math.cos(a) * 0.055, math.sin(a) * 0.055
        py, pz = -math.sin(a) * 0.006, math.cos(a) * 0.006
        p[11].quad((0.998, 0.31 - py, 0.69 - pz),
                   (0.998, 0.31 + py, 0.69 + pz),
                   (0.998, 0.31 + dy + py, 0.69 + dz + pz),
                   (0.998, 0.31 + dy - py, 0.69 + dz - pz), (1, 0, 0))
    cylinder(p[7], (0.985, 0.31, 0.69), "x", 0.018, 0.028, 8)
    # Broad corner plates and small rivets retain the reference's flat roof.
    for x in (0.205, 0.795):
        for y in (0.20, 0.80):
            cylinder(p[7], (x, y, 0.999), "z", 0.023, 0.006, 10)
    return p


def write_glb(primitives):
    binary, views, accessors, meshes = bytearray(), [], [], []
    rig = primitives[0].rig

    def accessor(values, kind, components, target, bounds=False):
        while len(binary) % 4:
            binary.append(0)
        data = b"".join(struct.pack("<" + {5126: "f", 5125: "I", 5123: "H"}[kind] * components,
                                    *value) for value in values)
        views.append({"buffer": 0, "byteOffset": len(binary),
                      "byteLength": len(data)})
        if target is not None:
            views[-1]["target"] = target
        binary.extend(data)
        record = {"bufferView": len(views) - 1, "componentType": kind,
                  "count": len(values), "type": {
                      1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4", 16: "MAT4"}[components]}
        if bounds:
            record["min"] = [min(v[i] for v in values) for i in range(components)]
            record["max"] = [max(v[i] for v in values) for i in range(components)]
        accessors.append(record)
        return len(accessors) - 1

    for index, primitive in enumerate(primitives):
        positions = [_to_gltf(v) for v in primitive.positions]
        normals = [_to_gltf(v) for v in primitive.normals]
        meshes.append({"attributes": {
            "POSITION": accessor(positions, 5126, 3, 34962, True),
            "NORMAL": accessor(normals, 5126, 3, 34962),
            "TEXCOORD_0": accessor([(v[0], v[1]) for v in primitive.positions],
                                    5126, 2, 34962),
            "JOINTS_0": accessor([(joint, 0, 0, 0) for joint in primitive.joints],
                                 5123, 4, 34962),
            "WEIGHTS_0": accessor([(1.0, 0.0, 0.0, 0.0)] * len(positions),
                                  5126, 4, 34962)},
            "indices": accessor([(i,) for i in range(len(positions))], 5125, 1, 34963),
            "material": index, "mode": 4})
    nodes = [{"mesh": 0, "skin": 0, "name": "Robot", "children": [1]}]
    inverse_binds = []
    for bone in rig.bones:
        translation = _to_gltf(bone["pivot"])
        angle = bone["angle"]
        nodes.append({"name": bone["name"], "translation": translation,
                      "rotation": (math.sin(angle / 2), 0, 0, math.cos(angle / 2))})
        c, s = math.cos(angle), math.sin(angle)
        x, y, z = translation
        inverse_binds.append((1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0,
                              -x, -c * y - s * z, s * y - c * z, 1))
    nodes[1]["children"] = list(range(2, len(nodes)))
    inverse_accessor = accessor(inverse_binds, 5126, 16, None)
    times = [DRIVE_DURATION * i / 128 for i in range(129)]
    time_accessor = accessor([(time,) for time in times], 5126, 1, None, True)
    samplers, channels = [], []
    for index, bone in enumerate(rig.bones):
        if bone["kind"] == "body":
            continue
        states = [bone_pose(bone, time) for time in times]
        if bone["kind"] == "shoe":
            samplers.append({"input": time_accessor, "interpolation": "LINEAR",
                             "output": accessor([_to_gltf(pivot) for pivot, _ in states],
                                                5126, 3, None)})
            channels.append({"sampler": len(samplers) - 1,
                             "target": {"node": index + 1, "path": "translation"}})
        quaternions = []
        for _, angle in states:
            q = (math.sin(angle / 2), 0, 0, math.cos(angle / 2))
            if quaternions and sum(a * b for a, b in zip(q, quaternions[-1])) < 0:
                q = tuple(-component for component in q)
            quaternions.append(q)
        samplers.append({"input": time_accessor, "interpolation": "LINEAR",
                         "output": accessor(quaternions, 5126, 4, None)})
        channels.append({"sampler": len(samplers) - 1,
                         "target": {"node": index + 1, "path": "rotation"}})
    document = {"asset": {"version": "2.0", "generator": "tools/make_robot_model.py"},
                "scene": 0, "scenes": [{"nodes": [0]}],
                "nodes": nodes,
                "skins": [{"name": "RobotDriveRig", "skeleton": 1,
                           "joints": list(range(1, len(nodes))),
                           "inverseBindMatrices": inverse_accessor}],
                "animations": [{"name": "Drive", "samplers": samplers,
                                "channels": channels,
                                "extras": {"secondsPerTile": 1.0,
                                           "trackLoopLength": DRIVE_DURATION}}],
                "meshes": [{"name": "Robot", "primitives": meshes}],
                "materials": MATERIALS, "buffers": [{"byteLength": len(binary)}],
                "bufferViews": views, "accessors": accessors}
    data = json.dumps(document, sort_keys=True, separators=(",", ":")).encode()
    data += b" " * (-len(data) % 4)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(struct.pack("<III", 0x46546C67, 2, 28 + len(data) + len(binary)) +
                       struct.pack("<II", len(data), 0x4E4F534A) + data +
                       struct.pack("<II", len(binary), 0x004E4942) + binary)


def bone_pose(bone, time):
    if bone["kind"] == "shoe":
        y, z, angle = track_pose(bone["phase"] + time)
        return (bone["pivot"][0], y, z), angle
    rate = -math.tau if bone["kind"] == "wheel" else math.tau
    return bone["pivot"], bone["angle"] + (rate * time if bone["kind"] != "body" else 0)


def posed_primitives(primitives, time):
    rig = primitives[0].rig
    states = [bone_pose(bone, time) for bone in rig.bones]
    posed = []
    for source in primitives:
        target = Primitive()
        for vertex, normal, joint in zip(source.positions, source.normals, source.joints):
            bone = rig.bones[joint]
            pivot, angle = states[joint]
            local = rotate_x(tuple(v - p for v, p in zip(vertex, bone["pivot"])), -bone["angle"])
            target.positions.append(tuple(v + p for v, p in zip(rotate_x(local, angle), pivot)))
            target.normals.append(rotate_x(normal, angle - bone["angle"]))
        posed.append(target)
    return posed


def validate_rig(primitives):
    """Conservative radial bounds also cover every interpolated drive pose."""
    rig = primitives[0].rig
    if len(rig.bones) + 1 > 128:
        raise ValueError("Robot exceeds the GPU skeleton node palette")
    radii = [0.0 for _ in rig.bones]
    x_bounds = [[1.0, 0.0] for _ in rig.bones]
    for primitive in primitives:
        if not primitive.positions or len(primitive.positions) != len(primitive.joints):
            raise ValueError("Robot primitive is empty or has missing rigid weights")
        for vertex, joint in zip(primitive.positions, primitive.joints):
            bone = rig.bones[joint]
            x, y, z = vertex
            if not (0 <= x <= 1 and 0 <= y <= 1 and 0 <= z <= 1.005):
                raise ValueError(f"Robot bind vertex exceeds its unit cell: {vertex}")
            dy, dz = y - bone["pivot"][1], z - bone["pivot"][2]
            radii[joint] = max(radii[joint], math.hypot(dy, dz))
            x_bounds[joint][0] = min(x_bounds[joint][0], x)
            x_bounds[joint][1] = max(x_bounds[joint][1], x)
    for joint, bone in enumerate(rig.bones):
        if bone["kind"] == "body":
            continue
        radius = radii[joint]
        if bone["kind"] == "shoe":
            y0, y1 = 0.25 - TRACK_RADIUS, 0.75 + TRACK_RADIUS
            z0, z1 = 0.23 - TRACK_RADIUS, 0.23 + TRACK_RADIUS
        else:
            y0 = y1 = bone["pivot"][1]
            z0 = z1 = bone["pivot"][2]
        # Rotations preserve local radius; LINEAR translation keys stay
        # inside the same center bounds, including between authored samples.
        if y0 - radius < 0 or y1 + radius > 1 or z0 - radius < 0 or z1 + radius > 1.005:
            raise ValueError(f"Moving robot joint exceeds its cell: {bone['name']}")
        if x_bounds[joint][0] < 0 or x_bounds[joint][1] > 1:
            raise ValueError(f"Moving robot joint exceeds its width: {bone['name']}")


def preview(primitives):
    """Optional authoring preview; the game itself bakes its palette thumbnails."""
    import numpy as np
    from PIL import Image

    size, scale = 900, 490
    panels = []
    light = np.array((0.2, 0.5, 0.84))
    for sign in (1, -1):
        view = np.array((0.8 * sign, 1.0 * sign, 0.92))
        view /= np.linalg.norm(view)
        right = np.cross((0, 0, 1), view)
        right /= np.linalg.norm(right)
        up = np.cross(view, right)
        pixels = np.empty((size, size, 3), dtype=np.uint8)
        pixels[:] = (32, 39, 45)
        depth = np.full((size, size), -np.inf)
        for slot, primitive in enumerate(primitives):
            base = np.array(MATERIALS[slot]["pbrMetallicRoughness"]["baseColorFactor"][:3])
            for i in range(0, len(primitive.positions), 3):
                normal = np.array(primitive.normals[i])
                if np.dot(normal, view) <= 0:
                    continue
                shade = 0.38 + 0.62 * max(np.dot(normal, light), 0)
                if slot in (5, 6):
                    shade = 1.0
                color = np.clip((base * shade) ** (1 / 2.2) * 255, 0, 255).astype(np.uint8)
                vertices = np.array(primitive.positions[i:i + 3]) - (0.5, 0.5, 0.5)
                screen = np.column_stack((size / 2 + vertices @ right * scale,
                                          size / 2 - vertices @ up * scale))
                zs = vertices @ view
                low = np.maximum(np.floor(screen.min(axis=0)).astype(int), 0)
                high = np.minimum(np.ceil(screen.max(axis=0)).astype(int), size - 1)
                if np.any(high < low):
                    continue
                a, b, c = screen
                denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                if abs(denominator) < 1e-8:
                    continue
                yy, xx = np.mgrid[low[1]:high[1] + 1, low[0]:high[0] + 1]
                xx, yy = xx + 0.5, yy + 0.5
                w0 = ((b[1] - c[1]) * (xx - c[0]) + (c[0] - b[0]) * (yy - c[1])) / denominator
                w1 = ((c[1] - a[1]) * (xx - c[0]) + (a[0] - c[0]) * (yy - c[1])) / denominator
                w2 = 1 - w0 - w1
                z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2]
                region = np.s_[low[1]:high[1] + 1, low[0]:high[0] + 1]
                mask = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6) & (z > depth[region])
                depth[region][mask] = z[mask]
                pixels[region][mask] = color
        panels.append(pixels)
    SOURCE.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.concatenate(panels, axis=1)).resize((1200, 600), Image.Resampling.LANCZOS).save(
        SOURCE / "preview.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", action="store_true")
    parser.add_argument("--preview-phase", type=float, default=0.0)
    options = parser.parse_args()
    primitives = robot()
    validate_rig(primitives)
    write_glb(primitives)
    if options.preview:
        preview(posed_primitives(primitives, options.preview_phase))
    print(f"Wrote {OUTPUT} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
