#!/usr/bin/env python3
"""Deterministically build a flat-shaded wooden lectern with an open book.

Uses engine coordinates (x east, y south, z up) in a single tile, transformed
to glTF by the same convention as the other custom models. Standard library only.
"""
from __future__ import annotations

import json
import struct
from pathlib import Path

from make_rotator_models import Primitive, _to_gltf

OUTPUT = Path(__file__).resolve().parent.parent / "assets/custom/models/lectern.glb"
COLORS = {
    "Oak": (0.38, 0.19, 0.075, 1.0),
    "Endgrain": (0.56, 0.30, 0.12, 1.0),
    "Brass": (0.84, 0.57, 0.20, 1.0),
    "Cover": (0.055, 0.24, 0.23, 1.0),
    "Paper": (0.94, 0.85, 0.65, 1.0),
    "Ink": (0.30, 0.25, 0.18, 1.0),
    "Ribbon": (0.68, 0.13, 0.10, 1.0),
}


def slab(mesh, x0, x1, y0, y1, bottom, top):
    """A four-sided slab with height functions, allowing sloped book pages."""
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    low = [(x, y, bottom(x, y)) for x, y in corners]
    high = [(x, y, top(x, y)) for x, y in corners]
    mesh.quad(*high, (0, 0, 1))
    mesh.quad(*low, (0, 0, -1))
    for i, outward in enumerate(((0, -1, 0), (1, 0, 0), (0, 1, 0), (-1, 0, 0))):
        j = (i + 1) % 4
        mesh.quad(low[i], low[j], high[j], high[i], outward)


def box(mesh, x0, x1, y0, y1, z0, z1):
    slab(mesh, x0, x1, y0, y1, lambda x, y: z0, lambda x, y: z1)


def bevel_base(mesh, radius_x, radius_y, z0, z1):
    corners = [(-radius_x + .045, -radius_y), (radius_x - .045, -radius_y),
        (radius_x, -radius_y + .045), (radius_x, radius_y - .045),
        (radius_x - .045, radius_y), (-radius_x + .045, radius_y),
        (-radius_x, radius_y - .045), (-radius_x, -radius_y + .045)]
    outline = [(x + .5, y + .5) for x, y in corners]
    mesh.prism_walls(outline, z0, z1)
    mesh.fan((.5, .5), outline, z0, False)
    mesh.fan((.5, .5), outline, z1, True)


def geometry():
    meshes = {name: Primitive() for name in COLORS}
    bevel_base(meshes["Oak"], .33, .28, .0, .10)
    bevel_base(meshes["Endgrain"], .29, .24, .10, .15)
    box(meshes["Oak"], .39, .61, .39, .61, .15, .77)
    box(meshes["Endgrain"], .41, .59, .375, .39, .23, .65)
    for z in (.18, .65):
        box(meshes["Brass"], .375, .625, .375, .625, z, z + .035)
    # A broad desk, tilted towards the reader on its south side.
    desk = lambda x, y: .76 + (.80 - y) * .36
    slab(meshes["Oak"], .09, .91, .19, .82,
        lambda x, y: desk(x, y) - .085, desk)
    slab(meshes["Endgrain"], .09, .91, .19, .82, desk,
        lambda x, y: desk(x, y) + .018)
    # Raised lip keeps the book in place.
    slab(meshes["Brass"], .09, .91, .79, .825,
        lambda x, y: desk(x, y) + .018, lambda x, y: desk(x, y) + .06)
    slab(meshes["Cover"], .16, .84, .24, .74,
        lambda x, y: desk(x, y) + .022, lambda x, y: desk(x, y) + .045)
    # Two separate page stacks meet at a shallow V-shaped gutter.
    for x0, x1, side in ((.18, .485, -1), (.515, .82, 1)):
        paper = lambda x, y, side=side: desk(x, y) + .071 + side * (x - .5) * .065
        slab(meshes["Paper"], x0, x1, .265, .715,
            lambda x, y: desk(x, y) + .046, paper)
        for i, length in enumerate((.21, .24, .19, .23, .16)):
            y = .35 + i * .052
            slab(meshes["Ink"], x0 + .03, x0 + .03 + length, y, y + .009,
                lambda x, y: paper(x, y) + .001, lambda x, y: paper(x, y) + .003)
    slab(meshes["Ribbon"], .491, .509, .26, .775,
        lambda x, y: desk(x, y) + .05, lambda x, y: desk(x, y) + .054)
    # Small brass feet and cover corners give the stand a readable silhouette.
    for x in (.13, .81):
        for y in (.22, .76):
            slab(meshes["Brass"], x, x + .06, y, y + .04,
                lambda x, y: desk(x, y) + .019, lambda x, y: desk(x, y) + .026)
    return meshes


def write_glb(meshes):
    binary = bytearray()
    views, accessors, primitives = [], [], []

    def accessor(data, count, kind, low=None, high=None, component=5126, target=34962):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        binary.extend(data)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        record = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
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
        primitives.append({"attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv},
            "indices": indices, "material": material, "mode": 4})
    document = {
        "asset": {"version": "2.0", "generator": "tools/make_lectern_model.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": "Lectern"}],
        "meshes": [{"name": "Lectern", "primitives": primitives}],
        "materials": [{"name": name, "pbrMetallicRoughness": {"baseColorFactor": color,
            "metallicFactor": .35 if name == "Brass" else 0.0, "roughnessFactor": .8}}
            for name, color in COLORS.items()],
        "buffers": [{"byteLength": len(binary)}], "bufferViews": views, "accessors": accessors,
    }
    data = json.dumps(document, separators=(",", ":"), sort_keys=True).encode()
    data += b" " * (-len(data) % 4)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(struct.pack("<III", 0x46546C67, 2, 28 + len(data) + len(binary)) +
        struct.pack("<II", len(data), 0x4E4F534A) + data +
        struct.pack("<II", len(binary), 0x004E4942) + binary)


if __name__ == "__main__":
    write_glb(geometry())
    print(OUTPUT)
