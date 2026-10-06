#!/usr/bin/env python3
"""Build a KayKit-style oak ladder section, repeatable every one world unit.

Engine coordinates: x across the ladder, y away from the north wall, z up.
Rails and their grain have identical cross-sections at z=0 and z=1. Three
rungs are spaced at 1/3-unit intervals, including across section boundaries.
No feet, end bevels or projecting caps interrupt vertical repetition. The
manifest must preserve source scale; instances only translate and rotate.
Standard library only; output is deterministic and has no external textures.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

from make_rotator_models import Primitive, _to_gltf

OUTPUT = Path(__file__).resolve().parent.parent / "assets/custom/models/ladder.glb"
RUNG_CENTERS = (1 / 6, 1 / 2, 5 / 6)
COLORS = {
    "Oak": (0.42, 0.235, 0.105, 1.0),
    "Oak bevel": (0.52, 0.305, 0.145, 1.0),
    "Rung wood": (0.49, 0.275, 0.115, 1.0),
    "Carved grain": (0.31, 0.16, 0.065, 1.0),
    "Forged iron": (0.075, 0.095, 0.11, 1.0),
    "Iron pins": (0.28, 0.32, 0.34, 1.0),
}


def chamfered_rectangle(a0, a1, b0, b1, bevel):
    return [(a0 + bevel, b0), (a1 - bevel, b0),
            (a1, b0 + bevel), (a1, b1 - bevel),
            (a1 - bevel, b1), (a0 + bevel, b1),
            (a0, b1 - bevel), (a0, b0 + bevel)]


def timber(mesh, edge_mesh, outline, start, end, axis):
    """Extrude an octagonal timber, with separate light-catching bevels."""
    def point(a, b, distance):
        return (a, b, distance) if axis == 2 else (distance, a, b)

    for i, p in enumerate(outline):
        q = outline[(i + 1) % len(outline)]
        outward = (q[1] - p[1], p[0] - q[0])
        normal = (*outward, 0) if axis == 2 else (0, *outward)
        material = edge_mesh if i % 2 else mesh
        material.quad(point(*p, start), point(*q, start),
                      point(*q, end), point(*p, end), normal)
    center = tuple(sum(p[i] for p in outline) / len(outline) for i in range(2))
    for distance, direction in ((start, -1), (end, 1)):
        normal = (0, 0, direction) if axis == 2 else (direction, 0, 0)
        for i, p in enumerate(outline):
            q = outline[(i + 1) % len(outline)]
            mesh.triangle(point(*center, distance), point(*p, distance),
                          point(*q, distance), normal)


def pin(mesh, x, z):
    """Small, six-sided iron peg heads on the visible face of the rails."""
    outline = [(x + 0.015 * math.cos(i * math.tau / 6),
                z + 0.015 * math.sin(i * math.tau / 6)) for i in range(6)]
    for i, p in enumerate(outline):
        q = outline[(i + 1) % len(outline)]
        mesh.triangle((x, 0.207, z), (p[0], 0.203, p[1]),
                      (q[0], 0.203, q[1]), (0, 1, 0))
        mesh.quad((p[0], 0.179, p[1]), (q[0], 0.179, q[1]),
                  (q[0], 0.203, q[1]), (p[0], 0.203, p[1]),
                  (q[1] - p[1], 0, p[0] - q[0]))


def geometry():
    meshes = {name: Primitive() for name in COLORS}
    for x in (0.205, 0.795):
        outline = chamfered_rectangle(x - 0.055, x + 0.055, 0.04, 0.18, 0.014)
        timber(meshes["Oak"], meshes["Oak bevel"], outline, 0.0, 1.0, axis=2)
        # Restrained carved grain, periodic at both ends of the section.
        for offset, phase in ((-0.022, 0.0), (0.017, math.pi)):
            for i in range(12):
                z0, z1 = i / 12, (i + 1) / 12
                x0 = x + offset + 0.004 * math.sin(z0 * math.tau + phase)
                x1 = x + offset + 0.004 * math.sin(z1 * math.tau + phase)
                meshes["Carved grain"].quad(
                    (x0 - 0.002, 0.1804, z0), (x1 - 0.002, 0.1804, z1),
                    (x1 + 0.002, 0.1804, z1), (x0 + 0.002, 0.1804, z0),
                    (0, 1, 0))
        # One restrained collar per section; its ends stay inside the tile.
        collar = chamfered_rectangle(x - 0.059, x + 0.059, 0.036, 0.187, 0.015)
        timber(meshes["Forged iron"], meshes["Forged iron"],
               collar, 0.477, 0.523, axis=2)
        for z in RUNG_CENTERS:
            pin(meshes["Iron pins"], x, z)

    for z in RUNG_CENTERS:
        outline = chamfered_rectangle(0.065, 0.193, z - 0.044, z + 0.044, 0.012)
        timber(meshes["Rung wood"], meshes["Oak bevel"],
               outline, 0.21, 0.79, axis=0)
    return meshes


def validate_geometry(meshes):
    """Reject broken module dimensions or nonmatching rail ends before export."""
    positions = [p for mesh in meshes.values() for p in mesh.positions]
    assert min(p[2] for p in positions) == 0.0
    assert max(p[2] for p in positions) == 1.0
    assert all(0 <= p[0] <= 1 and 0 <= p[1] <= 1 for p in positions)
    for name in ("Oak", "Oak bevel", "Carved grain"):
        vertices = meshes[name].positions
        lower = {(round(x, 7), round(y, 7)) for x, y, z in vertices if z == 0}
        upper = {(round(x, 7), round(y, 7)) for x, y, z in vertices if z == 1}
        assert lower == upper, f"{name}: mismatched vertical seam"
    for a, b in zip(RUNG_CENTERS, (*RUNG_CENTERS[1:], RUNG_CENTERS[0] + 1)):
        assert math.isclose(b - a, 1 / 3), "Irregular rung spacing at a join"
    for mesh in meshes.values():
        assert mesh.positions and len(mesh.positions) == len(mesh.normals)
        assert all(math.isclose(sum(v * v for v in n), 1.0) for n in mesh.normals)


def write_glb(meshes):
    binary = bytearray()
    views, accessors, primitives = [], [], []

    def accessor(data, count, kind, low=None, high=None, component=5126, target=34962):
        binary.extend(bytes(-len(binary) % 4))
        offset = len(binary)
        binary.extend(data)
        views.append({"buffer": 0, "byteOffset": offset,
                      "byteLength": len(data), "target": target})
        record = {"bufferView": len(views) - 1, "componentType": component,
                  "count": count, "type": kind}
        if low is not None:
            record.update(min=low, max=high)
        accessors.append(record)
        return len(accessors) - 1

    for material, mesh in enumerate(meshes.values()):
        positions = [_to_gltf(p) for p in mesh.positions]
        normals = [_to_gltf(n) for n in mesh.normals]
        count = len(positions)
        low = [min(p[i] for p in positions) for i in range(3)]
        high = [max(p[i] for p in positions) for i in range(3)]
        pack = lambda values: b"".join(struct.pack("<3f", *v) for v in values)
        position = accessor(pack(positions), count, "VEC3", low, high)
        normal = accessor(pack(normals), count, "VEC3")
        uv = accessor(bytes(count * 8), count, "VEC2")
        indices = accessor(struct.pack(f"<{count}I", *range(count)), count, "SCALAR",
                           component=5125, target=34963)
        primitives.append({"attributes": {"POSITION": position, "NORMAL": normal,
                                           "TEXCOORD_0": uv},
                           "indices": indices, "material": material, "mode": 4})
    document = {
        "asset": {"version": "2.0", "generator": "tools/make_ladder_model.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Ladder section"}],
        "meshes": [{"name": "Ladder section", "primitives": primitives}],
        "materials": [{"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": color,
            "metallicFactor": 0.35 if "iron" in name.lower() else 0.0,
            "roughnessFactor": 0.72 if "iron" in name.lower() else 0.85}}
            for name, color in COLORS.items()],
        "buffers": [{"byteLength": len(binary)}], "bufferViews": views,
        "accessors": accessors,
    }
    data = json.dumps(document, separators=(",", ":"), sort_keys=True).encode()
    data += b" " * (-len(data) % 4)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(struct.pack("<III", 0x46546C67, 2, 28 + len(data) + len(binary)) +
                       struct.pack("<II", len(data), 0x4E4F534A) + data +
                       struct.pack("<II", len(binary), 0x004E4942) + binary)


if __name__ == "__main__":
    meshes = geometry()
    validate_geometry(meshes)
    write_glb(meshes)
    print(f"{OUTPUT}: {sum(len(m.positions) // 3 for m in meshes.values())} triangles")
