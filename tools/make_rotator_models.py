#!/usr/bin/env python3
"""Generate the rotator plate models.

Produces binary glTF files in `assets/custom/models/`:

- `rotator_clockwise.glb` / `rotator_counter_clockwise.glb` - the complete
  plate (cogwheel plus direction icon). These are the tile models, so the
  editor, the tile thumbnails and any static view show the whole plate.
- `rotator_gear.glb` - the cogwheel alone. Gameplay draws it separately so it
  can spin a quarter turn when the rotator fires; twelve teeth make a quarter
  turn land on an identical silhouette, so nothing snaps afterwards.
- `rotator_icon_clockwise.glb` / `rotator_icon_counter_clockwise.glb` - the
  direction icon alone, drawn on top of the spinning gear and kept still.

The icon is a circular arrow (an open ring ending in an arrowhead). The
counter-clockwise plate is the mirror image of the clockwise one.

Colors: every model is untextured and white except the icon, whose material
base color is a mid grey. The engine multiplies a draw's color by the material
base color, so tinting a rotator with its link color makes the plate that color
and the icon a darker shade of the same color. Change ICON_SHADE to make the
icon lighter or darker.

Coordinates: the manifest loads these with `preserveSourceScale`, so the model
lives in the tile's unit box: x and y in [0, 1] across the plate, z in [0, 1]
through its height (the engine maps glTF (X, Y, Z) to (x, z, -y)). The icon
sits on top of the plate, above z = 1. Clockwise means clockwise seen from
above in game: from east towards south.

Only the standard library is used, and the output is deterministic, so
re-running the script reproduces the same bytes.
"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path

# ---------------------------------------------------------------- CONSTANTS
TOOTH_COUNT = 12              # multiple of 4, so a quarter turn is seamless
BODY_RADIUS = 0.40            # radius of the disc between teeth
TIP_RADIUS = 0.50             # radius of the tooth tips
TOOTH_ROOT_FRACTION = 0.60    # tooth width at the root, as a share of pitch
TOOTH_TIP_FRACTION = 0.40     # tooth width at the tip, as a share of pitch
HUB_RADIUS = 0.33             # raised centre disc the icon sits on
HUB_HEIGHT = 1.0              # top of the hub (plate top)
RIM_HEIGHT = 0.72             # top of the teeth ring, below the hub
HUB_SEGMENTS = 48

ICON_BASE = 1.0               # icon bottom (plate top)
ICON_TOP = 1.45               # icon top
ICON_INNER_RADIUS = 0.150
ICON_OUTER_RADIUS = 0.255
ICON_ARC_START_DEGREES = 60.0   # ring starts here (0 = east, 90 = south)
ICON_ARC_END_DEGREES = 318.0    # arrowhead base
ICON_HEAD_TIP_DEGREES = 356.0   # arrowhead point
ICON_HEAD_INNER_RADIUS = 0.095
ICON_HEAD_OUTER_RADIUS = 0.310
ICON_ARC_SEGMENTS = 40

PLATE_COLOR = [1.0, 1.0, 1.0, 1.0]
ICON_SHADE = 0.38
PLATE_ROUGHNESS = 0.55
PLATE_METALLIC = 0.25

OUTPUT_DIRECTORY = Path(__file__).resolve().parent.parent / "assets" / "custom" / "models"
CENTER = (0.5, 0.5)


# ------------------------------------------------------------------ GEOMETRY
def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _normalized(v):
    length = math.sqrt(_dot(v, v))
    return (v[0] / length, v[1] / length, v[2] / length)


class Primitive:
    """Flat-shaded triangles in engine coordinates (x east, y south, z up)."""

    def __init__(self) -> None:
        self.positions: list[tuple[float, float, float]] = []
        self.normals: list[tuple[float, float, float]] = []

    def triangle(self, a, b, c, outward) -> None:
        normal = _cross(_sub(b, a), _sub(c, a))
        if _dot(normal, outward) < 0.0:
            b, c = c, b
            normal = (-normal[0], -normal[1], -normal[2])
        if _dot(normal, normal) < 1e-18:
            return
        normal = _normalized(normal)
        for vertex in (a, b, c):
            self.positions.append(vertex)
            self.normals.append(normal)

    def quad(self, a, b, c, d, outward) -> None:
        self.triangle(a, b, c, outward)
        self.triangle(a, c, d, outward)

    def prism_walls(self, outline, z0, z1) -> None:
        """Side walls of an outline given counter-clockwise (math sense)."""
        count = len(outline)
        for index in range(count):
            p = outline[index]
            q = outline[(index + 1) % count]
            outward = (q[1] - p[1], -(q[0] - p[0]), 0.0)
            self.quad(
                (p[0], p[1], z0), (q[0], q[1], z0),
                (q[0], q[1], z1), (p[0], p[1], z1),
                outward,
            )

    def fan(self, center, outline, z, up: bool) -> None:
        count = len(outline)
        outward = (0.0, 0.0, 1.0 if up else -1.0)
        c = (center[0], center[1], z)
        for index in range(count):
            p = outline[index]
            q = outline[(index + 1) % count]
            self.triangle(c, (p[0], p[1], z), (q[0], q[1], z), outward)


def _polar(radius: float, angle: float):
    return (CENTER[0] + radius * math.cos(angle), CENTER[1] + radius * math.sin(angle))


def gear_outline():
    pitch = 2.0 * math.pi / TOOTH_COUNT
    root_half = pitch * TOOTH_ROOT_FRACTION * 0.5
    tip_half = pitch * TOOTH_TIP_FRACTION * 0.5
    outline = []
    for tooth in range(TOOTH_COUNT):
        middle = tooth * pitch
        outline.append(_polar(BODY_RADIUS, middle - root_half))
        outline.append(_polar(TIP_RADIUS, middle - tip_half))
        outline.append(_polar(TIP_RADIUS, middle + tip_half))
        outline.append(_polar(BODY_RADIUS, middle + root_half))
        valley_start = middle + root_half
        valley_end = middle + pitch - root_half
        for step in range(1, 3):
            angle = valley_start + (valley_end - valley_start) * step / 3.0
            outline.append(_polar(BODY_RADIUS, angle))
    return outline


def circle_outline(radius: float, segments: int):
    return [
        _polar(radius, 2.0 * math.pi * index / segments)
        for index in range(segments)
    ]


def add_gear(primitive: Primitive) -> None:
    teeth = gear_outline()
    primitive.fan(CENTER, teeth, 0.0, up=False)
    primitive.fan(CENTER, teeth, RIM_HEIGHT, up=True)
    primitive.prism_walls(teeth, 0.0, RIM_HEIGHT)
    hub = circle_outline(HUB_RADIUS, HUB_SEGMENTS)
    primitive.fan(CENTER, hub, HUB_HEIGHT, up=True)
    primitive.prism_walls(hub, RIM_HEIGHT, HUB_HEIGHT)


def add_icon(primitive: Primitive, direction: int) -> None:
    """`direction` is +1 for clockwise in game and -1 for counter-clockwise."""

    def angle(degrees: float) -> float:
        return direction * math.radians(degrees)

    z0, z1 = ICON_BASE, ICON_TOP
    up = (0.0, 0.0, 1.0)
    down = (0.0, 0.0, -1.0)
    start = ICON_ARC_START_DEGREES
    end = ICON_ARC_END_DEGREES
    angles = [
        angle(start + (end - start) * index / ICON_ARC_SEGMENTS)
        for index in range(ICON_ARC_SEGMENTS + 1)
    ]

    def point(radius, theta, z):
        x, y = _polar(radius, theta)
        return (x, y, z)

    for a, b in zip(angles, angles[1:]):
        inner_a0, inner_b0 = point(ICON_INNER_RADIUS, a, z0), point(ICON_INNER_RADIUS, b, z0)
        outer_a0, outer_b0 = point(ICON_OUTER_RADIUS, a, z0), point(ICON_OUTER_RADIUS, b, z0)
        inner_a1, inner_b1 = point(ICON_INNER_RADIUS, a, z1), point(ICON_INNER_RADIUS, b, z1)
        outer_a1, outer_b1 = point(ICON_OUTER_RADIUS, a, z1), point(ICON_OUTER_RADIUS, b, z1)
        middle = (a + b) * 0.5
        radial = (math.cos(middle), math.sin(middle), 0.0)
        inward = (-radial[0], -radial[1], 0.0)
        primitive.quad(inner_a1, outer_a1, outer_b1, inner_b1, up)
        primitive.quad(inner_a0, outer_a0, outer_b0, inner_b0, down)
        primitive.quad(outer_a0, outer_b0, outer_b1, outer_a1, radial)
        primitive.quad(inner_a0, inner_b0, inner_b1, inner_a1, inward)

    # Tail cap of the ring, facing back along the arc.
    tail = angles[0]
    backwards = (
        direction * math.sin(tail), -direction * math.cos(tail), 0.0,
    )
    primitive.quad(
        point(ICON_INNER_RADIUS, tail, z0), point(ICON_OUTER_RADIUS, tail, z0),
        point(ICON_OUTER_RADIUS, tail, z1), point(ICON_INNER_RADIUS, tail, z1),
        backwards,
    )

    # Arrowhead: a triangle whose base spans the ring at the arc's end and
    # whose point continues along the direction of travel.
    base = angles[-1]
    tip_radius = (ICON_INNER_RADIUS + ICON_OUTER_RADIUS) * 0.5
    head = [
        _polar(ICON_HEAD_INNER_RADIUS, base),
        _polar(ICON_HEAD_OUTER_RADIUS, base),
        _polar(tip_radius, angle(ICON_HEAD_TIP_DEGREES)),
    ]
    centroid = (
        sum(p[0] for p in head) / 3.0,
        sum(p[1] for p in head) / 3.0,
    )
    primitive.triangle(*[(p[0], p[1], z1) for p in head], up)
    primitive.triangle(*[(p[0], p[1], z0) for p in head], down)
    for index in range(3):
        p = head[index]
        q = head[(index + 1) % 3]
        middle = ((p[0] + q[0]) * 0.5, (p[1] + q[1]) * 0.5)
        outward = (middle[0] - centroid[0], middle[1] - centroid[1], 0.0)
        primitive.quad(
            (p[0], p[1], z0), (q[0], q[1], z0),
            (q[0], q[1], z1), (p[0], p[1], z1),
            outward,
        )


# ---------------------------------------------------------------- GLB WRITER
def _to_gltf(vector):
    # Engine (x, y, z) -> glTF (X, Y, Z) = (x, z, -y); a proper rotation, so
    # triangle winding is preserved.
    return (vector[0], vector[2], -vector[1])


def write_glb(path: Path, primitives: list[tuple[str, Primitive]]) -> None:
    materials = {
        "Plate": {
            "name": "Plate",
            "pbrMetallicRoughness": {
                "baseColorFactor": PLATE_COLOR,
                "metallicFactor": PLATE_METALLIC,
                "roughnessFactor": PLATE_ROUGHNESS,
            },
        },
        "Icon": {
            "name": "Icon",
            "pbrMetallicRoughness": {
                "baseColorFactor": [ICON_SHADE, ICON_SHADE, ICON_SHADE, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.8,
            },
        },
    }
    used_materials: list[str] = []
    binary = bytearray()
    buffer_views = []
    accessors = []
    gltf_primitives = []

    def add_view(data: bytes, target: int) -> int:
        while len(binary) % 4:
            binary.append(0)
        buffer_views.append({
            "buffer": 0,
            "byteOffset": len(binary),
            "byteLength": len(data),
            "target": target,
        })
        binary.extend(data)
        return len(buffer_views) - 1

    for material_name, primitive in primitives:
        if material_name not in used_materials:
            used_materials.append(material_name)
        positions = [_to_gltf(p) for p in primitive.positions]
        normals = [_to_gltf(n) for n in primitive.normals]
        # The engine requires TEXCOORD_0 even for untextured materials; a
        # top-down projection keeps it meaningful should a texture be added.
        uvs = [(p[0], p[1]) for p in primitive.positions]
        count = len(positions)
        position_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
        normal_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
        uv_bytes = b"".join(struct.pack("<2f", *uv) for uv in uvs)
        index_bytes = b"".join(struct.pack("<I", i) for i in range(count))
        minimum = [min(p[axis] for p in positions) for axis in range(3)]
        maximum = [max(p[axis] for p in positions) for axis in range(3)]
        accessors.append({
            "bufferView": add_view(position_bytes, 34962),
            "componentType": 5126,
            "count": count,
            "type": "VEC3",
            "min": minimum,
            "max": maximum,
        })
        position_accessor = len(accessors) - 1
        accessors.append({
            "bufferView": add_view(normal_bytes, 34962),
            "componentType": 5126,
            "count": count,
            "type": "VEC3",
        })
        normal_accessor = len(accessors) - 1
        accessors.append({
            "bufferView": add_view(uv_bytes, 34962),
            "componentType": 5126,
            "count": count,
            "type": "VEC2",
        })
        uv_accessor = len(accessors) - 1
        accessors.append({
            "bufferView": add_view(index_bytes, 34963),
            "componentType": 5125,
            "count": count,
            "type": "SCALAR",
        })
        gltf_primitives.append({
            "attributes": {
                "POSITION": position_accessor,
                "NORMAL": normal_accessor,
                "TEXCOORD_0": uv_accessor,
            },
            "indices": len(accessors) - 1,
            "material": used_materials.index(material_name),
            "mode": 4,
        })

    while len(binary) % 4:
        binary.append(0)
    document = {
        "asset": {"version": "2.0", "generator": "tools/make_rotator_models.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": path.stem}],
        "meshes": [{"name": path.stem, "primitives": gltf_primitives}],
        "materials": [materials[name] for name in used_materials],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }
    json_bytes = json.dumps(document, separators=(",", ":"), sort_keys=True).encode("utf-8")
    while len(json_bytes) % 4:
        json_bytes += b" "
    total = 12 + 8 + len(json_bytes) + 8 + len(binary)
    with path.open("wb") as file:
        file.write(struct.pack("<III", 0x46546C67, 2, total))
        file.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
        file.write(json_bytes)
        file.write(struct.pack("<II", len(binary), 0x004E4942))
        file.write(bytes(binary))


def main() -> None:
    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)

    gear = Primitive()
    add_gear(gear)
    icons = {}
    for suffix, direction in (("clockwise", 1), ("counter_clockwise", -1)):
        icon = Primitive()
        add_icon(icon, direction)
        icons[suffix] = icon

    write_glb(OUTPUT_DIRECTORY / "rotator_gear.glb", [("Plate", gear)])
    for suffix, icon in icons.items():
        write_glb(OUTPUT_DIRECTORY / f"rotator_icon_{suffix}.glb", [("Icon", icon)])
        write_glb(
            OUTPUT_DIRECTORY / f"rotator_{suffix}.glb",
            [("Plate", gear), ("Icon", icon)],
        )
    print(f"Wrote rotator models to {OUTPUT_DIRECTORY}")


if __name__ == "__main__":
    main()
