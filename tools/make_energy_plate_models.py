#!/usr/bin/env python3
"""Deterministically author the pressure and end plates from simple geometry.

Coordinates use the engine's unit tile convention: x east, y south, z up.
The asset manifest must load these with preserveSourceScale. At the default
surface scale (.72, .72, .08), the end's spherical lens rises to .216 units;
the pressure plate remains a low, recessed pad. Positive-emissive materials
are white so the plate shader can apply its link/activation color without
tinting the grey housing. UV0 is the engine x/y projection used by the energy
animation. All emission uses valid KHR_materials_emissive_strength factors.

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


if __name__ == "__main__":
    main()
